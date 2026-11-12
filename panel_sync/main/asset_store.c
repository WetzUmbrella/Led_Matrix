/**
 * @file    asset_store.c
 * @brief   Asset Storage Task (T-B3-04, T-B3-05, T-B3-01). See asset_store.h
 *          for what is implemented and the PROVISIONAL choices.
 *
 * Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston.
 *
 * Contexts:
 * - the Asset Storage Task is the only reader of the Upload Queue, the only
 *   producer of the Reply Queue, the only user of the frames partition and
 *   the only writer of g_p_active and g_phase;
 * - the Network Task posts uploads and takes replies through the queues;
 * - Playback Control, the Display Engine and tile_net read the published
 *   asset through g_p_active (acquire load, DEVIATION D-1). A descriptor is
 *   never changed while published: two descriptors alternate, and an upload
 *   unpublishes the old one before the RAM block is freed. An upload is
 *   refused while Playback Control can read the block (flash_allowed()).
 *
 * Frames partition layout (plan choice): bytes 0 to 15 hold the manifest
 * header ("SE15", format_version, session_id, frame_count, CRC32 and its
 * complement, little-endian, explicit shifts); the payload starts at
 * AS_PAYLOAD_OFFSET, one 4 KB sector later, so the header has its own erase
 * sector. The partition ends at 0x390000 inside the 4 MB flash (T-B3-02).
 *
 * DEVIATION D-1 (module-wide): C11 stdatomic.h for g_p_active and g_phase.
 * DEVIATION D-2 (module-wide): ESP-IDF names and types (size_t returns of
 * heap_caps_*, MALLOC_CAP_*, esp_partition_t, esp_err_t, QueueHandle_t).
 */
#include "asset_store.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"

#include "fault_word.h"
#include "playback_control.h"
#include "proto.h"
#include "tile_tasks.h"

/** Data subtype of the frames partition (partitions.csv). */
#define AS_PARTITION_SUBTYPE 0x40u

/** Bytes of the manifest header at the start of the partition. */
#define AS_HEADER_BYTES 16u

/** Erase sector size of the ESP32-S3 flash (4 KB). */
#define AS_SECTOR_BYTES 4096u

/** Payload offset in the partition: one sector after the header. */
#define AS_PAYLOAD_OFFSET AS_SECTOR_BYTES

/** Header magic, "SE15" read as a little-endian word. */
#define AS_MAGIC UINT32_C(0x35314553)

/** Byte positions inside the header. */
#define AS_HDR_MAGIC   0u
#define AS_HDR_FORMAT  4u
#define AS_HDR_SESSION 5u
#define AS_HDR_FRAMES  6u
#define AS_HDR_CRC     8u
#define AS_HDR_NOTCRC  12u

/** Counter line every 60 s while no upload arrives (plan choice), so the
 *  T-B3-04 10 min PLAYING check reads the counters from the log. */
#define AS_COUNTER_LOG_MS 60000u

/** Counter line every 60 s while no upload arrives (plan choice), so the
 *  T-B3-04 10 min PLAYING check reads the counters from the log. */
#define AS_COUNTER_LOG_MS 60000u

/** Bits per byte, for the little-endian helpers. */
#define AS_BYTE_BITS 8u

/** Mask of one byte. */
#define AS_BYTE_MASK 0xFFu

/** Bytes in the little-endian helpers' widths. */
#define AS_U16_BYTES 2u
#define AS_U32_BYTES 4u

/** One published asset. Never changed while g_p_active points to it. */
typedef struct
{
    uint8_t const * p_payload;   /**< RAM block. */
    uint32_t        len;         /**< Payload bytes. */
    uint16_t        frame_count; /**< 1 to 1023. */
    uint8_t         session_id;  /**< Manifest session_id. */
    uint32_t        crc32;       /**< Manifest CRC32. */
} as_asset_t;

/* Created once in asset_store_start(), then only read. */
static QueueHandle_t           g_p_q_upload = NULL;
static QueueHandle_t           g_p_q_reply  = NULL;
static esp_partition_t const * g_p_part     = NULL;

/* Shared: written only by the Asset Storage Task (release), read by any
 * task (acquire). */
static _Atomic(as_asset_t const *) g_p_active = NULL;
static _Atomic uint32_t            g_phase    = ASSET_STORE_WAITING;

/* Asset Storage Task only. */
static as_asset_t g_assets[2];
static uint32_t   g_next_asset = 0u;
static uint8_t *  g_p_block    = NULL;
static asset_rx_t g_rx;
static uint32_t   g_flash_writes  = 0u;
static uint32_t   g_flash_erases  = 0u;
static uint32_t   g_flash_refused = 0u;
static uint32_t   g_reply_drops   = 0u;
static uint32_t   g_ignored       = 0u;
static uint32_t   g_busy          = 0u;

static void asset_store_task (void * p_arg);
static void as_handle (asset_store_upload_t const * p_item);
static void as_log_counters (void);
static void as_on_manifest (asset_store_upload_t const * p_item);
static void as_on_chunk (asset_store_upload_t const * p_item);
static void as_verify (void);
static bool asset_store_load_to_ram (uint8_t session_id, uint16_t frame_count,
                                     uint32_t len, uint32_t crc32);
static void as_unpublish (void);
static uint32_t as_ram_caps (uint32_t len);
static void     as_receive (asset_store_upload_t const * p_item);
static bool     as_crc_flash (uint32_t len, uint32_t * p_crc);
static void     as_load_cached (void);
static bool     as_flash_erase (uint32_t offset, uint32_t len);
static bool as_flash_write (uint32_t offset, void const * p_src, uint32_t len);
static bool as_write_allowed (void);
static void as_set_phase (asset_store_phase_t phase);
static void as_reply (asset_store_reply_t const * p_reply);
static void as_reply_simple (uint8_t kind, asset_store_upload_t const * p_item,
                             uint8_t reason);
static void as_put_le (uint8_t * p_buf, uint32_t value, uint32_t bytes);
static uint32_t as_get_le (uint8_t const * p_buf, uint32_t bytes);
static bool     as_allow_8bit (void);

/**
 * @brief  Create the queues and the Asset Storage Task.
 * @return true when everything was created.
 */
bool asset_store_start (void)
{
    bool b_ok = false;

    asset_rx_init(&g_rx);
    g_p_part     = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                            AS_PARTITION_SUBTYPE, "frames");
    g_p_q_upload = xQueueCreate(ASSET_STORE_UPLOAD_QUEUE_LEN,
                                sizeof(asset_store_upload_t));
    g_p_q_reply =
        xQueueCreate(ASSET_STORE_REPLY_QUEUE_LEN, sizeof(asset_store_reply_t));

    if ((NULL != g_p_part) && (NULL != g_p_q_upload) && (NULL != g_p_q_reply))
    {
        b_ok = (pdPASS ==
                xTaskCreatePinnedToCore(asset_store_task, TILE_ASSET_NAME,
                                        TILE_ASSET_STACK, NULL, TILE_ASSET_PRIO,
                                        NULL, TILE_ASSET_CORE));
    }

    if (NULL == g_p_part)
    {
        printf("asset_store: no frames partition\n");
    }

    return b_ok;
}

/**
 * @brief  Log the three heap values and the OD-04 rule (T-B3-01).
 */
void asset_store_probe_heap (void)
{
    /* DEVIATION D-2: heap_caps_* return size_t. Each value is below the
     * 512 KB of internal SRAM or the 2 MB of PSRAM, so it fits uint32_t. */
    uint32_t const largest_internal =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    /* Same range. */
    uint32_t const free_internal =
        (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    /* Same range. */
    uint32_t const largest_spiram =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    printf("HEAP largest_internal=%" PRIu32 " free_internal=%" PRIu32
           " largest_spiram=%" PRIu32 " od04=%s\n",
           largest_internal, free_internal, largest_spiram,
           (largest_internal >= ASSET_STORE_RAM_NEEDED) ? "internal" : "psram");
}

/**
 * @brief  Post one upload item (Network Task, single producer).
 * @param  p_item  The item.
 * @return false when the Upload Queue is full (NACK reason 3).
 */
bool asset_store_post_upload (asset_store_upload_t const * p_item)
{
    bool b_ok = false;

    if (NULL != g_p_q_upload)
    {
        b_ok = (pdTRUE == xQueueSend(g_p_q_upload, p_item, 0));
    }

    return b_ok;
}

/**
 * @brief  Drop every queued upload item (Network Task, single producer).
 */
void asset_store_drop_uploads (void)
{
    if (NULL != g_p_q_upload)
    {
        /* xQueueReset() always returns pdPASS; there is nothing to check. */
        (void)xQueueReset(g_p_q_upload);
    }
}

/**
 * @brief  Take one reply (Network Task).
 * @param  p_reply  Receives the reply when true.
 * @return true when a reply was waiting.
 */
bool asset_store_take_reply (asset_store_reply_t * p_reply)
{
    bool b_ok = false;

    if (NULL != g_p_q_reply)
    {
        b_ok = (pdTRUE == xQueueReceive(g_p_q_reply, p_reply, 0));
    }

    return b_ok;
}

/**
 * @brief  The validated RAM asset.
 * @param  p_session_id   Receives its session_id when true.
 * @param  p_frame_count  Receives its frame_count when true.
 * @return true when a validated asset is in RAM.
 */
bool asset_store_active (uint8_t * p_session_id, uint16_t * p_frame_count)
{
    as_asset_t const * const p_asset =
        atomic_load_explicit(&g_p_active, memory_order_acquire);

    if (NULL != p_asset)
    {
        *p_session_id  = p_asset->session_id;
        *p_frame_count = p_asset->frame_count;
    }

    return (NULL != p_asset);
}

/**
 * @brief  The published RAM asset for the Display Engine.
 * @param  pp_payload     Receives the payload.
 * @param  p_len          Receives its length.
 * @param  p_frame_count  Receives frame_count.
 * @return true when an asset is published.
 */
bool asset_store_get (uint8_t const ** pp_payload, uint32_t * p_len,
                      uint16_t * p_frame_count)
{
    as_asset_t const * const p_asset =
        atomic_load_explicit(&g_p_active, memory_order_acquire);

    if (NULL != p_asset)
    {
        *pp_payload    = p_asset->p_payload;
        *p_len         = p_asset->len;
        *p_frame_count = p_asset->frame_count;
    }

    return (NULL != p_asset);
}

/**
 * @brief  The cached asset REGISTER reports.
 * @param  p_session_id  Receives its session_id, or 0.
 * @param  p_crc32       Receives its CRC32, or 0.
 */
void asset_store_cached (uint8_t * p_session_id, uint32_t * p_crc32)
{
    as_asset_t const * const p_asset =
        atomic_load_explicit(&g_p_active, memory_order_acquire);

    *p_session_id = (NULL != p_asset) ? p_asset->session_id : 0u;
    *p_crc32      = (NULL != p_asset) ? p_asset->crc32 : 0u;
}

/**
 * @brief  Whether flash may be written now (T-B3-04).
 * @return false while Playback Control is ARMED, PLAYING, PAUSED or in
 *         CLOCK HOLDOVER.
 */
bool asset_store_flash_allowed (void)
{
    playback_control_state_t const state = playback_control_state();

    return ((PLAYBACK_ARMED != state) && (PLAYBACK_PLAYING != state) &&
            (PLAYBACK_PAUSED != state) && (PLAYBACK_CLOCK_HOLDOVER != state));
}

/**
 * @brief  The current upload phase.
 * @return The phase.
 */
asset_store_phase_t asset_store_phase (void)
{
    /* g_phase only ever holds an asset_store_phase_t value. */
    return (asset_store_phase_t)atomic_load_explicit(&g_phase,
                                                     memory_order_acquire);
}

/**
 * @brief  Asset Storage Task: reuse a cached asset, then serve the Upload
 *         Queue.
 * @param  p_arg  Unused.
 */
static void asset_store_task (void * p_arg)
{
    static asset_store_upload_t item; /* 1 KB: kept off the task stack. */

    (void)p_arg; /* The task takes no parameter. */

    as_load_cached();

    for (;;)
    {
        if (pdTRUE == xQueueReceive(g_p_q_upload, &item,
                                    pdMS_TO_TICKS(AS_COUNTER_LOG_MS)))
        {
            as_handle(&item);
        }
        else
        {
            as_log_counters();
        }
    }
}

/**
 * @brief  One upload item: BUSY while playback may read the asset,
 *         otherwise the MANIFEST or chunk rules.
 * @param  p_item  The item.
 */
static void as_handle (asset_store_upload_t const * p_item)
{
    if (!asset_store_flash_allowed())
    {
        g_busy++;
        as_reply_simple(ASSET_STORE_REPLY_BUSY, p_item, 0u);
    }
    else if (ASSET_STORE_UP_MANIFEST == p_item->kind)
    {
        as_on_manifest(p_item);
    }
    else
    {
        as_on_chunk(p_item);
    }
}

/**
 * @brief  MANIFEST: check format and length; a repeat of the open upload
 *         changes nothing; one for the published asset writes nothing: the
 *         asset is announced again to the Pico and a held upload is opened,
 *         so the PC's chunks are ACKed (DS-12 B); otherwise unpublish, erase,
 *         write the header and open the upload.
 * @param  p_item  The MANIFEST.
 */
static void as_on_manifest (asset_store_upload_t const * p_item)
{
    asset_rx_manifest_result_t const check = asset_rx_check_manifest(
        p_item->format_version, p_item->frame_count, as_allow_8bit());

    as_asset_t const * const p_asset =
        atomic_load_explicit(&g_p_active, memory_order_relaxed);

    if (ASSET_RX_MANIFEST_OK != check)
    {
        /* NACK reasons 0 and 1 fit uint8_t. */
        uint8_t const reason = (ASSET_RX_BAD_FORMAT == check)
                                   ? (uint8_t)PROTO_NACK_UNKNOWN_FORMAT
                                   : (uint8_t)PROTO_NACK_WRONG_LENGTH;

        as_reply_simple(ASSET_STORE_REPLY_NACK, p_item, reason);
        fault_word_set(FAULT_BIT_ASSET_FORMAT);
        printf("ASSET manifest refused reason=%" PRIu8 "\n", reason);
    }
    else if ((g_rx.b_open) && (!g_rx.b_held) &&
             (p_item->session_id == g_rx.session_id) &&
             (p_item->crc32 == g_rx.crc32))
    {
        /* The PC repeated the manifest of the upload in progress. */
    }
    else if ((NULL != p_asset) && (p_item->session_id == p_asset->session_id) &&
             (p_item->crc32 == p_asset->crc32))
    {
        /* Same asset already in RAM: reuse it (T-B3-05). */
        asset_store_reply_t const ready = {
            .kind       = ASSET_STORE_REPLY_ASSET_READY,
            .session_id = p_asset->session_id,
            .crc32      = p_asset->crc32,
        };

        as_reply(&ready);
        /* The PC hears nothing of ASSET_READY (it goes to the Pico) and
         * waited about 70 s for CHUNK_ACKs (bench 5 Nov 2026): every chunk
         * it now sends is ACKed as a repeat, with no flash write. */
        asset_rx_open_held(&g_rx, p_item->session_id, p_item->format_version,
                           p_item->frame_count, p_item->crc32);
        printf("ASSET manifest session=%" PRIu8 " crc=0x%08" PRIx32
               " already held\n",
               p_item->session_id, p_item->crc32);
    }
    else
    {
        as_unpublish();
        asset_rx_open(&g_rx, p_item->session_id, p_item->format_version,
                      p_item->frame_count, p_item->crc32);

        uint32_t const payload_len = g_rx.payload_len;

        if (0u == as_ram_caps(payload_len))
        {
            /* Refused before any erase (R1-3): the copy could never be
             * made, so the upload would end in CRC_FAIL. */
            asset_rx_init(&g_rx);
            as_set_phase(ASSET_STORE_WAITING);
            /* NACK reason 1 fits uint8_t. */
            as_reply_simple(ASSET_STORE_REPLY_NACK, p_item,
                            (uint8_t)PROTO_NACK_WRONG_LENGTH);
            fault_word_set(FAULT_BIT_ASSET_FORMAT);
            printf("ASSET manifest refused: %" PRIu32 " B does not fit RAM\n",
                   payload_len);
        }
        else
        {
            as_receive(p_item);
        }
    }
}

/**
 * @brief  Start receiving an accepted manifest: erase the frames partition
 *         and write the header (RECEIVING ASSET).
 * @param  p_item  The manifest; g_rx is already open for it.
 */
static void as_receive (asset_store_upload_t const * p_item)
{
    fault_word_clear_condition(FAULT_BIT_ASSET_FORMAT); /* Owner. */
    as_set_phase(ASSET_STORE_RECEIVING);

    uint8_t        header[AS_HEADER_BYTES];
    uint32_t const erase_len =
        (((AS_PAYLOAD_OFFSET + g_rx.payload_len) + AS_SECTOR_BYTES) - 1u) /
        AS_SECTOR_BYTES * AS_SECTOR_BYTES;

    as_put_le(&header[AS_HDR_MAGIC], AS_MAGIC, AS_U32_BYTES);
    header[AS_HDR_FORMAT]  = p_item->format_version;
    header[AS_HDR_SESSION] = p_item->session_id;
    as_put_le(&header[AS_HDR_FRAMES], p_item->frame_count, AS_U16_BYTES);
    as_put_le(&header[AS_HDR_CRC], p_item->crc32, AS_U32_BYTES);
    as_put_le(&header[AS_HDR_NOTCRC], ~p_item->crc32, AS_U32_BYTES);

    bool const b_ok = (as_flash_erase(0u, erase_len)) &&
                      (as_flash_write(0u, header, AS_HEADER_BYTES));

    printf("ASSET manifest session=%" PRIu8 " frames=%" PRIu16
           " crc=0x%08" PRIx32 " chunks=%" PRIu32 " %s\n",
           p_item->session_id, p_item->frame_count, p_item->crc32,
           g_rx.chunk_count, b_ok ? "receiving" : "flash failed");

    if (!b_ok)
    {
        asset_rx_init(&g_rx);
        as_set_phase(ASSET_STORE_WAITING);
    }
}

/**
 * @brief  ASSET_CHUNK: write a new chunk, ACK new and repeated chunks, NACK
 *         a wrong length (ASSET_FORMAT bit, upload dropped), and verify once
 *         every chunk is held.
 * @param  p_item  The chunk.
 */
static void as_on_chunk (asset_store_upload_t const * p_item)
{
    asset_rx_chunk_result_t const result = asset_rx_chunk(
        &g_rx, p_item->session_id, p_item->chunk_index, p_item->payload_len);

    if (ASSET_RX_CHUNK_NEW == result)
    {
        uint32_t const offset =
            AS_PAYLOAD_OFFSET + /* Widened index, unchanged. */
            (((uint32_t)p_item->chunk_index) * ASSET_RX_CHUNK_BYTES);

        if (as_flash_write(offset, p_item->payload, p_item->payload_len))
        {
            as_reply_simple(ASSET_STORE_REPLY_CHUNK_ACK, p_item, 0u);
        }
        else
        {
            /* Not written: drop the upload so the PC starts again. */
            asset_rx_init(&g_rx);
            as_set_phase(ASSET_STORE_WAITING);
        }
    }
    else if (ASSET_RX_CHUNK_REPEAT == result)
    {
        as_reply_simple(ASSET_STORE_REPLY_CHUNK_ACK, p_item, 0u);
    }
    else if (ASSET_RX_CHUNK_BAD_LENGTH == result)
    {
        /* NACK reason 1 fits uint8_t. */
        as_reply_simple(ASSET_STORE_REPLY_NACK, p_item,
                        (uint8_t)PROTO_NACK_WRONG_LENGTH);
        fault_word_set(FAULT_BIT_ASSET_FORMAT);
        asset_rx_init(&g_rx);
        as_set_phase(ASSET_STORE_WAITING);
        printf("ASSET chunk %" PRIu8 " wrong length, upload dropped\n",
               p_item->chunk_index);
    }
    else
    {
        g_ignored++; /* No upload open, or another session. */
    }

    if (asset_rx_complete(&g_rx))
    {
        as_verify();
    }
}

/**
 * @brief  VERIFYING ASSET: CRC32 over the bytes read back from flash, then
 *         the RAM copy and ASSET_READY; a mismatch raises CRC_FAIL.
 */
static void as_verify (void)
{
    uint32_t crc = 0u;

    as_set_phase(ASSET_STORE_VERIFYING);

    bool const b_read = as_crc_flash(g_rx.payload_len, &crc);

    if ((b_read) && (crc == g_rx.crc32) &&
        (asset_store_load_to_ram(g_rx.session_id, g_rx.frame_count,
                                 g_rx.payload_len, g_rx.crc32)))
    {
        asset_store_reply_t const ready = {
            .kind       = ASSET_STORE_REPLY_ASSET_READY,
            .session_id = g_rx.session_id,
            .crc32      = g_rx.crc32,
        };

        as_reply(&ready);
    }
    else
    {
        fault_word_set(FAULT_BIT_CRC_FAIL);
        as_set_phase(ASSET_STORE_WAITING);
        printf("ASSET verify failed crc=0x%08" PRIx32 " want=0x%08" PRIx32 "\n",
               crc, g_rx.crc32);
    }

    as_log_counters();
    asset_rx_init(&g_rx);
}

/**
 * @brief  One counter line: flash writes and erases (only in the upload
 *         states), refused writes, ignored chunks, BUSY replies and reply
 *         drops (T-B3-04 acceptance (1) and (2)).
 */
static void as_log_counters (void)
{
    printf("ASSET_FLASH writes=%" PRIu32 " erases=%" PRIu32 " refused=%" PRIu32
           " ignored=%" PRIu32 " busy=%" PRIu32 " reply_drops=%" PRIu32 "\n",
           g_flash_writes, g_flash_erases, g_flash_refused, g_ignored, g_busy,
           g_reply_drops);
}

/**
 * @brief  Copy the verified payload from flash into one RAM block chosen by
 *         the OD-04 rule, check its CRC32 again and publish it once.
 * @param  session_id   Manifest session_id.
 * @param  frame_count  Manifest frame_count.
 * @param  len          Payload bytes.
 * @param  crc32        Manifest CRC32.
 * @return true when the asset is published.
 */
static bool asset_store_load_to_ram (uint8_t session_id, uint16_t frame_count,
                                     uint32_t len, uint32_t crc32)
{
    bool b_ok = false;

    as_unpublish();

    uint32_t const caps       = as_ram_caps(len);
    bool const     b_internal = (0u != (caps & MALLOC_CAP_INTERNAL));

    if (0u != caps)
    {
        g_p_block = heap_caps_malloc(len, caps);
    }

    if ((NULL != g_p_block) &&
        (ESP_OK ==
         esp_partition_read(g_p_part, AS_PAYLOAD_OFFSET, g_p_block, len)) &&
        (crc32 == asset_rx_crc32(0u, g_p_block, len)))
    {
        as_asset_t * const p_asset = &g_assets[g_next_asset];

        g_next_asset         = (g_next_asset + 1u) % 2u;
        p_asset->p_payload   = g_p_block;
        p_asset->len         = len;
        p_asset->frame_count = frame_count;
        p_asset->session_id  = session_id;
        p_asset->crc32       = crc32;
        atomic_store_explicit(&g_p_active, p_asset, memory_order_release);
        as_set_phase(ASSET_STORE_READY);
        b_ok = true;
        /* The address shows which region the OD-04 rule chose (T-B3-04
         * acceptance (3)). */
        printf("ASSET ready session=%" PRIu8 " frames=%" PRIu16
               " crc=0x%08" PRIx32 " ram=%p region=%s\n",
               session_id, frame_count, crc32, (void *)g_p_block,
               b_internal ? "internal" : "psram");
    }
    else
    {
        printf("ASSET RAM copy failed (%" PRIu32 " B, %s)\n", len,
               (0u == caps) ? "no region"
                            : (b_internal ? "internal" : "psram"));
        heap_caps_free(g_p_block); /* NULL is allowed. */
        g_p_block = NULL;
    }

    return b_ok;
}

/**
 * @brief  OD-04 rule on the real length (R1-3): internal RAM when its
 *         largest free block holds the payload plus ASSET_STORE_RAM_MARGIN,
 *         otherwise PSRAM if it is enabled and holds the payload.
 * @param  len  Payload bytes.
 * @return heap_caps_malloc() capabilities, or 0 when nothing can hold it.
 */
static uint32_t as_ram_caps (uint32_t len)
{
    uint32_t caps = 0u;

    /* DEVIATION D-2: heap_caps_get_largest_free_block returns size_t; the
     * value is below 512 KB, so it fits uint32_t. */
    uint32_t const largest_internal =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    if (largest_internal >= (len + ASSET_STORE_RAM_MARGIN))
    {
        caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    }

#ifdef CONFIG_SPIRAM
    /* Same range rule, for the 2 MB of PSRAM. */
    else if ((uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >=
             len)
    {
        caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    }
#endif
    else
    {
        /* Neither region can hold the copy. */
    }

    return caps;
}

/**
 * @brief  Withdraw the published asset and free its block. Called only
 *         while flash_allowed() is true, so no playback state reads it.
 */
static void as_unpublish (void)
{
    atomic_store_explicit(&g_p_active, NULL, memory_order_release);
    heap_caps_free(g_p_block); /* NULL is allowed. */
    g_p_block = NULL;
}

/**
 * @brief  CRC32 of the payload as stored in flash, read 1 KB at a time.
 * @param  len    Payload bytes.
 * @param  p_crc  Receives the CRC32.
 * @return true when every read succeeded.
 */
static bool as_crc_flash (uint32_t len, uint32_t * p_crc)
{
    static uint8_t buf[ASSET_RX_CHUNK_BYTES]; /* Off the task stack. */

    bool     b_ok = true;
    uint32_t crc  = 0u;

    for (uint32_t done = 0u; (b_ok) && (done < len);
         done += ASSET_RX_CHUNK_BYTES)
    {
        uint32_t const part = ((len - done) < ASSET_RX_CHUNK_BYTES)
                                  ? (len - done)
                                  : ASSET_RX_CHUNK_BYTES;

        b_ok = (ESP_OK == esp_partition_read(g_p_part, AS_PAYLOAD_OFFSET + done,
                                             buf, part));
        crc  = asset_rx_crc32(crc, buf, part);
    }

    *p_crc = crc;

    return b_ok;
}

/**
 * @brief  At boot: a valid header whose payload CRC32 still matches is
 *         copied into RAM again (cached asset, T-B3-05). No flash write.
 */
static void as_load_cached (void)
{
    uint8_t header[AS_HEADER_BYTES];
    bool    b_ready = false;

    if (ESP_OK == esp_partition_read(g_p_part, 0u, header, AS_HEADER_BYTES))
    {
        uint32_t const crc32 = as_get_le(&header[AS_HDR_CRC], AS_U32_BYTES);
        /* A 2-byte read is below 65,536, so it fits uint16_t. */
        uint16_t const frames =
            (uint16_t)as_get_le(&header[AS_HDR_FRAMES], AS_U16_BYTES);
        bool const b_header =
            (AS_MAGIC == as_get_le(&header[AS_HDR_MAGIC], AS_U32_BYTES)) &&
            (crc32 == ~as_get_le(&header[AS_HDR_NOTCRC], AS_U32_BYTES)) &&
            (ASSET_RX_MANIFEST_OK ==
             asset_rx_check_manifest(header[AS_HDR_FORMAT], frames,
                                     as_allow_8bit()));

        if (b_header)
        {
            asset_rx_open(&g_rx, header[AS_HDR_SESSION], header[AS_HDR_FORMAT],
                          frames, crc32);

            uint32_t crc = 0u;

            b_ready = (as_crc_flash(g_rx.payload_len, &crc)) &&
                      (crc == crc32) &&
                      (asset_store_load_to_ram(header[AS_HDR_SESSION], frames,
                                               g_rx.payload_len, crc32));
            asset_rx_init(&g_rx);
        }
    }

    printf("ASSET cached %s\n", b_ready ? "reused" : "none");
}

/**
 * @brief  Counting wrapper for every erase (T-B3-04 acceptance (2)).
 * @param  offset  Partition offset, sector aligned.
 * @param  len     Bytes, a whole number of sectors.
 * @return true when erased.
 */
static bool as_flash_erase (uint32_t offset, uint32_t len)
{
    bool b_ok = false;

    if (as_write_allowed())
    {
        b_ok = (ESP_OK == esp_partition_erase_range(g_p_part, offset, len));
        g_flash_erases++;
    }

    return b_ok;
}

/**
 * @brief  Counting wrapper for every write (T-B3-04 acceptance (2)).
 * @param  offset  Partition offset.
 * @param  p_src   Bytes.
 * @param  len     Their number.
 * @return true when written.
 */
static bool as_flash_write (uint32_t offset, void const * p_src, uint32_t len)
{
    bool b_ok = false;

    if (as_write_allowed())
    {
        b_ok = (ESP_OK == esp_partition_write(g_p_part, offset, p_src, len));
        g_flash_writes++;
    }

    return b_ok;
}

/**
 * @brief  The one gate on flash writes: an upload state (WAITING FOR ASSET
 *         or RECEIVING ASSET) and flash_allowed(); a refusal is counted.
 * @return true when the write may go ahead.
 */
static bool as_write_allowed (void)
{
    asset_store_phase_t const phase = asset_store_phase();

    bool const b_ok =
        ((ASSET_STORE_WAITING == phase) || (ASSET_STORE_RECEIVING == phase)) &&
        (asset_store_flash_allowed());

    if (!b_ok)
    {
        g_flash_refused++;
    }

    return b_ok;
}

/**
 * @brief  Publish a new phase (release).
 * @param  phase  The phase.
 */
static void as_set_phase (asset_store_phase_t phase)
{
    /* An enum value of 0 to 3 fits uint32_t. */
    atomic_store_explicit(&g_phase, (uint32_t)phase, memory_order_release);
}

/**
 * @brief  Post a reply, drop-oldest with a counter (single producer).
 * @param  p_reply  The reply.
 */
static void as_reply (asset_store_reply_t const * p_reply)
{
    if (0u == uxQueueSpacesAvailable(g_p_q_reply))
    {
        asset_store_reply_t old_reply;

        if (pdTRUE == xQueueReceive(g_p_q_reply, &old_reply, 0))
        {
            g_reply_drops++;
        }
    }

    if (pdTRUE != xQueueSend(g_p_q_reply, p_reply, 0))
    {
        g_reply_drops++;
    }
}

/**
 * @brief  Post a CHUNK_ACK, BUSY or NACK about one upload item.
 * @param  kind    asset_store_reply_kind_t.
 * @param  p_item  The item replied to.
 * @param  reason  NACK reason (ignored otherwise).
 */
static void as_reply_simple (uint8_t kind, asset_store_upload_t const * p_item,
                             uint8_t reason)
{
    asset_store_reply_t const reply = {
        .kind        = kind,
        .session_id  = p_item->session_id,
        .ref_seq     = p_item->seq,
        .chunk_index = p_item->chunk_index,
        .reason      = reason,
        .crc32       = 0u,
    };

    as_reply(&reply);
}

/**
 * @brief  Store a value little-endian with explicit shifts.
 * @param  p_buf  Destination.
 * @param  value  Value.
 * @param  bytes  2 or 4.
 */
static void as_put_le (uint8_t * p_buf, uint32_t value, uint32_t bytes)
{
    for (uint32_t idx = 0u; idx < bytes; idx++)
    {
        /* Masked to one byte. */
        p_buf[idx] = (uint8_t)((value >> (idx * AS_BYTE_BITS)) & AS_BYTE_MASK);
    }
}

/**
 * @brief  Read a little-endian value with explicit shifts.
 * @param  p_buf  Source.
 * @param  bytes  2 or 4.
 * @return The value.
 */
static uint32_t as_get_le (uint8_t const * p_buf, uint32_t bytes)
{
    uint32_t value = 0u;

    for (uint32_t idx = 0u; idx < bytes; idx++)
    {
        value |= ((uint32_t)p_buf[idx]) << (idx * AS_BYTE_BITS); /* Widened. */
    }

    return value;
}

/**
 * @brief  Whether the 8-bit format is accepted (DE_PROFILE build, OD-15).
 * @return true only in the DE_PROFILE build.
 */
static bool as_allow_8bit (void)
{
#ifdef DE_PROFILE
    return true;
#else
    return false;
#endif
}

/*** end of file ***/
