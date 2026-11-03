/**
 * @file    asset_store.h
 * @brief   Asset Storage Task (T-B3-04, T-B3-05, T-B3-01): receives the
 *          uploaded asset into the frames partition, verifies it, copies it
 *          into one RAM block before ASSET_READY and publishes that block
 *          once for Playback Control and the Display Engine.
 *
 * Owner: Cheston (Buddy 3), tasks T-B3-04, T-B3-05 and T-B3-01. Implemented
 * by Ahsanul (B4) for Cheston; T-B3-01 written 29 Oct 2026 in Ben's (B2)
 * session, T-B3-04 and T-B3-05 the same day in Jia Jun's (B5) session.
 *
 * Implemented:
 * - asset_store_probe_heap() (T-B3-01), called by tile_net once after the
 *   first IP address.
 * - Asset Storage Task, priority 8 on core 0 (tile_tasks.h, W6D-4a). It is
 *   the only reader of the Upload Queue and the only writer of the frames
 *   partition. Replies (CHUNK_ACK, BUSY, NACK, ASSET_READY) go back to
 *   tile_net through the Reply Queue, because only tile_net owns sockets.
 * - Upload phases (the asset part of W6 statechart 3): WAITING FOR ASSET,
 *   RECEIVING ASSET, VERIFYING ASSET, ASSET READY. Flash is erased and
 *   written only in WAITING FOR ASSET and RECEIVING ASSET, and only while
 *   asset_store_flash_allowed() is true; every write goes through one
 *   counting wrapper (T-B3-04 acceptance (2)).
 * - Verify (T-B3-05): format and length at the MANIFEST (NACK reason 0 or
 *   1 and the ASSET_FORMAT condition bit), session per chunk, CRC32 over
 *   the packed bytes read back from flash. A CRC mismatch raises the
 *   CRC_FAIL event bit, drops the upload and sends no ASSET_READY.
 * - RAM copy (T-B3-04, OD-04 rule): one heap_caps_malloc() block,
 *   MALLOC_CAP_INTERNAL when the largest internal block is at least the
 *   payload length plus ASSET_STORE_RAM_MARGIN, otherwise MALLOC_CAP_SPIRAM
 *   (only when PSRAM is enabled). The same check runs at the MANIFEST, before
 *   anything is erased: an asset that cannot fit is refused with NACK reason
 *   1 (wrong payload length) and the ASSET_FORMAT condition bit (R1-3,
 *   PROVISIONAL, owner Cheston: reason 1 reused, no new wire value).
 *   The copy is filled from flash and checked again before ASSET_READY;
 *   published once with release ordering, constant while ARMED, PLAYING,
 *   PAUSED or in holdover.
 * - Cached asset: a 16 B header at the start of the frames partition holds
 *   the manifest (written with the erase, inside RECEIVING ASSET). At boot
 *   a payload whose CRC32 matches it is copied into RAM again, so REGISTER
 *   carries the cached session_id and CRC32 and a matching asset is reused.
 *
 * PROVISIONAL (OD-11, owner Cheston): Upload Queue length 4 (4 chunks of
 * 1,024 B, about 4 KB of internal RAM; reject and NACK reason 3 when full),
 * Reply Queue length 8 (drop-oldest, single producer).
 * PROVISIONAL (owner Cheston): uploads are refused with BUSY while Playback
 * Control is ARMED, PLAYING, PAUSED or in CLOCK HOLDOVER. The plan row names
 * ARMED and PLAYING; PAUSED and holdover are added because RESUME_AT plays
 * the same asset from the saved frame.
 * PROVISIONAL (owner Ahsanul): deviation D-1 (C11 atomics) extended to this
 * module for the published asset pointer and the phase word.
 *
 * DEVIATION D-2 (module-wide): ESP-IDF names and types.
 */
#ifndef ASSET_STORE_H
#define ASSET_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "asset_rx.h"

/** Heap left free beside the asset copy (proposed, RAR-4). */
#define ASSET_STORE_RAM_MARGIN 32768u

/** RAM needed for the packed 600-frame asset copy: 57,600 B payload plus the
 *  margin = 90,368 B (T-B3-01 decision rule for OD-04). Used by the boot
 *  probe; the copy itself checks the real payload length plus the margin
 *  (review round 1 R1-3). */
#define ASSET_STORE_RAM_NEEDED 90368u

/** Upload Queue length (PROVISIONAL, OD-11, owner Cheston). */
#define ASSET_STORE_UPLOAD_QUEUE_LEN 4u

/** Reply Queue length (PROVISIONAL, OD-11, owner Cheston). */
#define ASSET_STORE_REPLY_QUEUE_LEN 8u

/** Upload phase (the asset states of W6 statechart 3). */
typedef enum
{
    ASSET_STORE_WAITING = 0, /**< WAITING FOR ASSET. */
    ASSET_STORE_RECEIVING,   /**< RECEIVING ASSET. */
    ASSET_STORE_VERIFYING,   /**< VERIFYING ASSET. */
    ASSET_STORE_READY        /**< ASSET READY. */
} asset_store_phase_t;

/** Kind of an Upload Queue item. */
typedef enum
{
    ASSET_STORE_UP_MANIFEST = 0,
    ASSET_STORE_UP_CHUNK
} asset_store_up_kind_t;

/** One MANIFEST or ASSET_CHUNK from the PC, posted by tile_net. */
typedef struct
{
    uint8_t  kind;           /**< asset_store_up_kind_t. */
    uint8_t  session_id;     /**< Header session_id. */
    uint16_t seq;            /**< Header seq (for BUSY and NACK ref_seq). */
    uint8_t  format_version; /**< MANIFEST only. */
    uint16_t frame_count;    /**< MANIFEST only. */
    uint32_t crc32;          /**< MANIFEST only. */
    uint8_t  chunk_index;    /**< ASSET_CHUNK only. */
    uint16_t payload_len;    /**< ASSET_CHUNK only, 1 to 1,024. */
    uint8_t  payload[ASSET_RX_CHUNK_BYTES]; /**< ASSET_CHUNK only. */
} asset_store_upload_t;

/** Kind of a Reply Queue item. */
typedef enum
{
    ASSET_STORE_REPLY_CHUNK_ACK = 0, /**< To the PC. */
    ASSET_STORE_REPLY_BUSY,          /**< To the PC. */
    ASSET_STORE_REPLY_NACK,          /**< To the PC. */
    ASSET_STORE_REPLY_ASSET_READY    /**< To the Pico. */
} asset_store_reply_kind_t;

/** One reply for tile_net to send. */
typedef struct
{
    uint8_t  kind;        /**< asset_store_reply_kind_t. */
    uint8_t  session_id;  /**< Session of the upload. */
    uint16_t ref_seq;     /**< BUSY and NACK: seq of the refused message. */
    uint8_t  chunk_index; /**< CHUNK_ACK. */
    uint8_t  reason;      /**< NACK: proto_nack_reason_t. */
    uint32_t crc32;       /**< ASSET_READY. */
} asset_store_reply_t;

/**
 * @brief  Create the Upload and Reply Queues and the Asset Storage Task.
 *         Call once from app_main.
 * @return true when everything was created.
 */
bool asset_store_start (void);

/**
 * @brief  Log the largest free internal block, the free internal heap and
 *         the largest free PSRAM block on one line, with the OD-04 rule's
 *         answer: "HEAP largest_internal=<B> free_internal=<B>
 *         largest_spiram=<B> od04=<internal|psram>".
 */
void asset_store_probe_heap (void);

/**
 * @brief  Post one upload item (Network Task only, single producer). Never
 *         blocks.
 * @param  p_item  The item.
 * @return false when the Upload Queue is full: the caller sends NACK
 *         reason 3 (VA-3.6).
 */
bool asset_store_post_upload (asset_store_upload_t const * p_item);

/**
 * @brief  Take one reply (Network Task only). Never blocks.
 * @param  p_reply  Receives the reply when true.
 * @return true when a reply was waiting.
 */
bool asset_store_take_reply (asset_store_reply_t * p_reply);

/**
 * @brief  The validated RAM asset Playback Control plays.
 * @param  p_session_id   Receives the asset's session_id when true.
 * @param  p_frame_count  Receives its frame_count when true.
 * @return true when a validated asset is in RAM.
 */
bool asset_store_active (uint8_t * p_session_id, uint16_t * p_frame_count);

/**
 * @brief  The published RAM asset for the Display Engine (T-B5-02). The
 *         pointer stays valid while Playback Control is ARMED, PLAYING,
 *         PAUSED or in holdover, because no upload is accepted then.
 * @param  pp_payload     Receives the packed payload (frame n at n x 96).
 * @param  p_len          Receives its length in bytes.
 * @param  p_frame_count  Receives frame_count.
 * @return true when an asset is published.
 */
bool asset_store_get (uint8_t const ** pp_payload, uint32_t * p_len,
                      uint16_t * p_frame_count);

/**
 * @brief  The cached asset REGISTER reports (T-B3-05).
 * @param  p_session_id  Receives its session_id, or 0.
 * @param  p_crc32       Receives its CRC32, or 0.
 */
void asset_store_cached (uint8_t * p_session_id, uint32_t * p_crc32);

/**
 * @brief  Whether flash may be erased or written now: false while Playback
 *         Control is ARMED, PLAYING, PAUSED or in CLOCK HOLDOVER (T-B3-04).
 * @return true when a flash write is allowed.
 */
bool asset_store_flash_allowed (void);

/**
 * @brief  The current upload phase (any task).
 * @return The phase.
 */
asset_store_phase_t asset_store_phase (void);

#endif /* ASSET_STORE_H */

/*** end of file ***/
