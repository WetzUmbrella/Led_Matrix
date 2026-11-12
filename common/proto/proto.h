/**
 * @file    proto.h
 * @brief   Shared wire layout (T-B2-01): one encode/decode pair per message,
 *          explicit shifts and masks, bit 0 is the least significant bit of
 *          byte 0, every multi-byte field little-endian, no wire struct is
 *          ever sent.
 *
 * Implemented by Ahsanul (B4) for Benjamin, task T-B2-01. The field list,
 * offsets and widths are protocol/layout_table.md; this file and
 * pc/proto_layout.py (T-B1-01) follow it, and the cross-language vectors in
 * protocol/vectors/ prove they agree.
 * Implemented: proto_put_bits(), proto_get_bits(), the byte-aligned
 * proto_put_u16_le/u32_le/u64_le and proto_get_*_le helpers, the common
 * header, and an encode/decode pair for each of the 24 message rows (18
 * from T-B2-01, 6 from the whack-a-mole re-plan step 3: TAP, MOLE_ON,
 * FEEDBACK, GAME_CONTROL, WALL_PICTURE, ACK), each with a valid-range check;
 * host tests with the T-B2-01 boundary set.
 * Not yet implemented: the OD-01 citation sentence on where poll-mode
 * callbacks run (layout_table.md section 9 records the gap).
 *
 * PROVISIONAL (OD-07, owner Benjamin): every width not fixed by PostTri
 * (type 5, session_id 8, seq 16, Node ID 4, format_version 2, chunk index 7,
 * NACK reason 2, rows 4, cols 4, failure policy 1; coordinator state 4 bits
 * is derived, ceil(log2 13)), the
 * type codes in T-B2-01 row order, contiguous packing from bit 0, a node_id
 * in every message a tile sends, ref_seq in NACK and BUSY, acked_seq in
 * TELEMETRY_ACK, frame_rate left off the wire (one valid value, 0 bits), and
 * the ASSET_CHUNK payload starting on the next byte boundary.
 * PROVISIONAL (OD-05, owners Jolyn and Jia Jun): nibble order (a), so packed
 * nibble k of a frame is the 4-bit field at bit 4k.
 * PROVISIONAL (plan file names): the plan names the files proto_layout.c/.h;
 * they are proto.c/.h so that the plan's function names (proto_put_bits(),
 * proto_encode_beacon()) carry their module prefix (Barr-C 6.1.i). See
 * docs/proto_DEFENCE.md.
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdbool.h>
#include <stdint.h>

/** Bytes needed for a number of bits. */
#define PROTO_BYTES(bits) (((bits) + 7u) / 8u)

/** Width of the message type field, bits (PROVISIONAL, OD-07). */
#define PROTO_TYPE_BITS 5u

/** Width of the session_id field, bits (PROVISIONAL, OD-07). */
#define PROTO_SESSION_BITS 8u

/** Width of the seq field, bits (PROVISIONAL, OD-07). */
#define PROTO_SEQ_BITS 16u

/** Width of every timestamp, bits (LDC-6: 64-bit time). */
#define PROTO_TIME_BITS 64u

/** Width of a Node ID, bits: 1 to 9, 0 = no node (PROVISIONAL, OD-07). */
#define PROTO_NODE_BITS 4u

/** Width of a CRC32, bits (T-B2-01). */
#define PROTO_CRC_BITS 32u

/** Width of format_version, bits (PROVISIONAL, OD-07). */
#define PROTO_FORMAT_BITS 2u

/** Width of frame_count and every frame index, bits (VB-4b, VC-3). */
#define PROTO_FRAME_BITS 10u

/** Width of the chunk index, bits (PROVISIONAL, OD-07). */
#define PROTO_CHUNK_BITS 7u

/** Width of the NACK reason code, bits (PROVISIONAL, OD-07). */
#define PROTO_REASON_BITS 2u

/** Width of SESSION_CONFIG rows and cols, bits: 1 to 9 each (whack-a-mole
 *  re-plan, step 3; PROVISIONAL, OD-07). */
#define PROTO_GRID_BITS 4u

/** Largest rows and cols of a grid (step 4d, DS-15: up to 9 x 9 cells,
 *  at most 9 of them holding a tile). */
#define PROTO_GRID_MAX 9u

/** Width of a SESSION_CONFIG placed flag, bits (step 4d). */
#define PROTO_PLACED_BITS 1u

/** Bits of one SESSION_CONFIG cell entry: placed, row, col (step 4d). */
#define PROTO_CELL_BITS (PROTO_PLACED_BITS + PROTO_GRID_BITS + PROTO_GRID_BITS)

/** Width of the failure policy, bits (PROVISIONAL, OD-07). */
#define PROTO_POLICY_BITS 1u

/** Width of the readiness and online bitmaps, bits (VC-11b). */
#define PROTO_BITMAP_BITS 9u

/** Width of the coordinator state, bits (PROVISIONAL, OD-07). */
#define PROTO_COORD_STATE_BITS 4u

/** Width of the fault word, bits (LDC-2). */
#define PROTO_FAULT_BITS 32u

/** Width of the orientation, bits (VB-5c). */
#define PROTO_ORIENT_BITS 2u

/** Width of the display state, bits (VB-5c). */
#define PROTO_DISPLAY_STATE_BITS 3u

/** Width of a TAP peak, bits: the tap_detect peak, uint16_t mg. */
#define PROTO_PEAK_BITS 16u

/** Width of MOLE_ON up_ms, bits: 0 to 4,000 ms needs 12 (DS-10, 11 Nov
 *  2026: room for the stretch rotate sprite, up to 3,000 ms). */
#define PROTO_UP_MS_BITS 12u

/** Longest target, ms; 0 means "up until tapped or cleared" (the start
 *  sprite, DS-08). */
#define PROTO_UP_MS_MAX 4000u

/** Width of the MOLE_ON target kind, bits: mole, start, and 3 codes reserved
 *  for the stretch goals (rotate, golden, decoy), 3 spare (DS-10). */
#define PROTO_TARGET_KIND_BITS 3u

/** Width of the GAME_CONTROL level, bits: Easy, Normal, Hard (DS-08). */
#define PROTO_LEVEL_BITS 2u

/** Width of the GAME_CONTROL tap threshold, bits: in 20 mg steps, 0 =
 *  the tile's default, 1 to 250 = 20 to 5,000 mg (A12, DS-10). */
#define PROTO_THRESHOLD_BITS 8u

/** Largest tap threshold code (5,000 mg). */
#define PROTO_THRESHOLD_MAX 250u

/** mg per tap threshold step. */
#define PROTO_THRESHOLD_STEP_MG 20u

/** WALL_PICTURE fields (DS-09): list slot, effect, colour, repeat flag and
 *  duration widths, bits. */
#define PROTO_PIC_SLOT_BITS   2u
#define PROTO_PIC_EFFECT_BITS 2u
#define PROTO_PIC_COLOUR_BITS 3u
#define PROTO_PIC_REPEAT_BITS 1u
#define PROTO_PIC_DUR_BITS    13u

/** STATUS game fields (DS-08, DS-10): round state, level, counts, time
 *  left and the average reaction time, widths, bits. */
#define PROTO_ROUND_STATE_BITS 3u
#define PROTO_COUNT_BITS       7u
#define PROTO_TIME_LEFT_BITS   6u
#define PROTO_REACTION_BITS    11u

/** Largest counts, time left (s) and reaction time (ms) on STATUS. */
#define PROTO_COUNT_MAX     127u
#define PROTO_TIME_LEFT_MAX 63u
#define PROTO_REACTION_MAX  2047u

/** Width of the FEEDBACK result, bits: miss, hit and a code reserved for
 *  the decoy stretch goal, 3 codes and 1 spare (decision of 6 Nov 2026,
 *  see layout_table.md 5.9). */
#define PROTO_RESULT_BITS 2u

/** Width of the GAME_CONTROL command, bits: 4 commands now, 12 codes spare
 *  so commands can be added without a layout change (decision of 6 Nov
 *  2026, see layout_table.md 5.10). */
#define PROTO_GAME_CMD_BITS 4u

/** Width of the game score, bits: -99 to 99 is 199 values, two's
 *  complement. */
#define PROTO_SCORE_BITS 8u

/** Largest game score; the smallest is -PROTO_SCORE_MAX. */
#define PROTO_SCORE_MAX 99

/** Width of a WALL_PICTURE pixel mask, bits: one per pixel of a tile. */
#define PROTO_MASK_BITS 64u

/** Largest Node ID (CL_MAX_NODES 9, T-B2-03). */
#define PROTO_NODE_MAX 9u

/** Largest frame_count and frame index (VC-3: 1023 frames). */
#define PROTO_FRAME_MAX 1023u

/** Frame rate of every asset (NFR1, W6D-1e). Not on the wire. */
#define PROTO_FRAME_RATE 60u

/** Packed frame bytes: 64 pixels x 12 bits (LDC-1, T-B1-03). */
#define PROTO_FRAME_BYTES 96u

/** ASSET_CHUNK payload bytes, except the last chunk (T-B2-01). */
#define PROTO_CHUNK_PAYLOAD_MAX 1024u

/** Largest chunk index: the 600-frame 8-bit DE_PROFILE asset (OD-15) is
 *  115,200 B = 113 chunks of 1,024 B; 1023 packed frames need 96. */
#define PROTO_CHUNK_INDEX_MAX 112u

/** Bits of the common header: type, session_id, seq (29). */
#define PROTO_HEADER_BITS                                                      \
    (PROTO_TYPE_BITS + PROTO_SESSION_BITS + PROTO_SEQ_BITS)

/** Bytes needed to read a header (4). */
#define PROTO_HEADER_BYTES PROTO_BYTES(PROTO_HEADER_BITS)

/* Encoded length of each message, bits and bytes (layout_table.md
 * section 4). */
#define PROTO_REGISTER_BITS                                                    \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_SESSION_BITS + PROTO_CRC_BITS)
#define PROTO_REGISTER_BYTES  PROTO_BYTES(PROTO_REGISTER_BITS)
#define PROTO_HEARTBEAT_BITS  (PROTO_HEADER_BITS + PROTO_NODE_BITS)
#define PROTO_HEARTBEAT_BYTES PROTO_BYTES(PROTO_HEARTBEAT_BITS)
#define PROTO_SESSION_CONFIG_BITS                                              \
    (PROTO_HEADER_BITS + PROTO_GRID_BITS + PROTO_GRID_BITS +                   \
     PROTO_POLICY_BITS + (PROTO_NODE_MAX * PROTO_CELL_BITS) +                  \
     PROTO_FRAME_BITS + (PROTO_NODE_MAX * PROTO_CRC_BITS))
#define PROTO_SESSION_CONFIG_BYTES PROTO_BYTES(PROTO_SESSION_CONFIG_BITS)
#define PROTO_MANIFEST_BITS                                                    \
    (PROTO_HEADER_BITS + PROTO_FORMAT_BITS + PROTO_FRAME_BITS + PROTO_CRC_BITS)
#define PROTO_MANIFEST_BYTES   PROTO_BYTES(PROTO_MANIFEST_BITS)
#define PROTO_CHUNK_HEAD_BYTES PROTO_BYTES(PROTO_HEADER_BITS + PROTO_CHUNK_BITS)
#define PROTO_ASSET_CHUNK_MAX_BYTES                                            \
    (PROTO_CHUNK_HEAD_BYTES + PROTO_CHUNK_PAYLOAD_MAX)
#define PROTO_CHUNK_ACK_BITS                                                   \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_CHUNK_BITS)
#define PROTO_CHUNK_ACK_BYTES PROTO_BYTES(PROTO_CHUNK_ACK_BITS)
#define PROTO_NACK_BITS                                                        \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_REASON_BITS + PROTO_SEQ_BITS)
#define PROTO_NACK_BYTES PROTO_BYTES(PROTO_NACK_BITS)
#define PROTO_BUSY_BITS  (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_SEQ_BITS)
#define PROTO_BUSY_BYTES PROTO_BYTES(PROTO_BUSY_BITS)
#define PROTO_ASSET_READY_BITS                                                 \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_CRC_BITS)
#define PROTO_ASSET_READY_BYTES PROTO_BYTES(PROTO_ASSET_READY_BITS)
#define PROTO_SYNC_READY_BITS   (PROTO_HEADER_BITS + PROTO_NODE_BITS)
#define PROTO_SYNC_READY_BYTES  PROTO_BYTES(PROTO_SYNC_READY_BITS)
#define PROTO_BEACON_BITS       (PROTO_HEADER_BITS + PROTO_TIME_BITS)
#define PROTO_BEACON_BYTES      PROTO_BYTES(PROTO_BEACON_BITS)
#define PROTO_START_AT_BITS     (PROTO_HEADER_BITS + PROTO_TIME_BITS)
#define PROTO_START_AT_BYTES    PROTO_BYTES(PROTO_START_AT_BITS)
#define PROTO_RESUME_AT_BITS                                                   \
    (PROTO_HEADER_BITS + PROTO_TIME_BITS + PROTO_FRAME_BITS)
#define PROTO_RESUME_AT_BYTES PROTO_BYTES(PROTO_RESUME_AT_BITS)
#define PROTO_PAUSE_BYTES     PROTO_BYTES(PROTO_HEADER_BITS)
#define PROTO_STOP_BYTES      PROTO_BYTES(PROTO_HEADER_BITS)
#define PROTO_STATUS_BITS                                                      \
    (PROTO_HEADER_BITS + PROTO_BITMAP_BITS + PROTO_BITMAP_BITS +               \
     PROTO_COORD_STATE_BITS + PROTO_SCORE_BITS + PROTO_ROUND_STATE_BITS +      \
     PROTO_LEVEL_BITS + (3u * PROTO_COUNT_BITS) + PROTO_TIME_LEFT_BITS +       \
     PROTO_REACTION_BITS)
#define PROTO_STATUS_BYTES PROTO_BYTES(PROTO_STATUS_BITS)
#define PROTO_TELEMETRY_BITS                                                   \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_FAULT_BITS +                  \
     PROTO_ORIENT_BITS + PROTO_DISPLAY_STATE_BITS)
#define PROTO_TELEMETRY_BYTES     PROTO_BYTES(PROTO_TELEMETRY_BITS)
#define PROTO_TELEMETRY_ACK_BITS  (PROTO_HEADER_BITS + PROTO_SEQ_BITS)
#define PROTO_TELEMETRY_ACK_BYTES PROTO_BYTES(PROTO_TELEMETRY_ACK_BITS)
#define PROTO_TAP_BITS                                                         \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_PEAK_BITS + PROTO_TIME_BITS)
#define PROTO_TAP_BYTES PROTO_BYTES(PROTO_TAP_BITS)
#define PROTO_MOLE_ON_BITS                                                     \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_TIME_BITS +                   \
     PROTO_UP_MS_BITS + PROTO_TARGET_KIND_BITS)
#define PROTO_MOLE_ON_BYTES PROTO_BYTES(PROTO_MOLE_ON_BITS)
#define PROTO_FEEDBACK_BITS                                                    \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_RESULT_BITS)
#define PROTO_FEEDBACK_BYTES PROTO_BYTES(PROTO_FEEDBACK_BITS)
#define PROTO_GAME_CONTROL_BITS                                                \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_GAME_CMD_BITS +               \
     PROTO_LEVEL_BITS + PROTO_THRESHOLD_BITS)
#define PROTO_GAME_CONTROL_BYTES PROTO_BYTES(PROTO_GAME_CONTROL_BITS)
#define PROTO_WALL_PICTURE_BITS                                                \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_PIC_SLOT_BITS +               \
     PROTO_PIC_EFFECT_BITS + PROTO_PIC_COLOUR_BITS + PROTO_PIC_REPEAT_BITS +   \
     PROTO_TIME_BITS + PROTO_PIC_DUR_BITS + PROTO_MASK_BITS)
#define PROTO_WALL_PICTURE_BYTES PROTO_BYTES(PROTO_WALL_PICTURE_BITS)

/* ACK: node_id and acked_seq after the header. */
#define PROTO_ACK_BITS  (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_SEQ_BITS)
#define PROTO_ACK_BYTES PROTO_BYTES(PROTO_ACK_BITS)

/** Message type codes, in the order of the T-B2-01 message rows
 *  (PROVISIONAL, OD-07). */
typedef enum
{
    PROTO_TYPE_REGISTER = 0,
    PROTO_TYPE_HEARTBEAT,
    PROTO_TYPE_SESSION_CONFIG,
    PROTO_TYPE_MANIFEST,
    PROTO_TYPE_ASSET_CHUNK,
    PROTO_TYPE_CHUNK_ACK,
    PROTO_TYPE_NACK,
    PROTO_TYPE_BUSY,
    PROTO_TYPE_ASSET_READY,
    PROTO_TYPE_SYNC_READY,
    PROTO_TYPE_BEACON,
    PROTO_TYPE_START_AT,
    PROTO_TYPE_RESUME_AT,
    PROTO_TYPE_PAUSE,
    PROTO_TYPE_STOP,
    PROTO_TYPE_STATUS,
    PROTO_TYPE_TELEMETRY,
    PROTO_TYPE_TELEMETRY_ACK,
    PROTO_TYPE_TAP,          /**< 18: whack-a-mole rows from here. */
    PROTO_TYPE_MOLE_ON,      /**< 19. */
    PROTO_TYPE_FEEDBACK,     /**< 20. */
    PROTO_TYPE_GAME_CONTROL, /**< 21. */
    PROTO_TYPE_WALL_PICTURE,   /**< 22. */
    PROTO_TYPE_ACK,          /**< 23. */
    PROTO_TYPE_COUNT
} proto_type_t;

/** FEEDBACK result: the flash the tile shows. */
typedef enum
{
    PROTO_FEEDBACK_MISS = 0, /**< Red flash: a tap on a tile with no mole. */
    PROTO_FEEDBACK_HIT,      /**< Green flash: a tap on the mole. */
    PROTO_FEEDBACK_DECOY,    /**< Reserved for the decoy stretch goal; no
                                  tile acts on it yet. */
    PROTO_FEEDBACK_COUNT
} proto_feedback_t;

/** GAME_CONTROL command. The PC sends the mode switches, ARM, START_NOW,
 *  END (abort) and the tap test to the Pico; the Pico sends START, END, ARM
 *  (with the threshold) and the tap test to each tile (DS-08, DS-10). */
typedef enum
{
    PROTO_GAME_MODE_VIDEO = 0, /**< Switch to video mode (only stopped). */
    PROTO_GAME_MODE_GAME,      /**< Switch to game mode (only stopped). */
    PROTO_GAME_START,          /**< To a tile: the round starts. */
    PROTO_GAME_END,            /**< End or abort the round. */
    PROTO_GAME_ARM,            /**< Arm a round at a level (start sprite). */
    PROTO_GAME_START_NOW,      /**< Skip the start sprite: count down now. */
    PROTO_GAME_TAP_TEST_ON,    /**< Tap test: every tap lights its tile. */
    PROTO_GAME_TAP_TEST_OFF,   /**< Tap test off. */
    PROTO_GAME_CMD_COUNT
} proto_game_cmd_t;

/** Game level (DS-08, B4). */
typedef enum
{
    PROTO_LEVEL_EASY = 0,
    PROTO_LEVEL_NORMAL,
    PROTO_LEVEL_HARD,
    PROTO_LEVEL_COUNT
} proto_level_t;

/** MOLE_ON target kind (DS-10). */
typedef enum
{
    PROTO_TARGET_MOLE = 0, /**< A mole: tap it. */
    PROTO_TARGET_START,    /**< The start sprite: tap it to start (DS-08). */
    PROTO_TARGET_ROTATE,   /**< Reserved: the rotate sprite (stretch). */
    PROTO_TARGET_GOLDEN,   /**< Reserved: the golden mole (stretch). */
    PROTO_TARGET_DECOY,    /**< Reserved: the decoy (stretch). */
    PROTO_TARGET_COUNT
} proto_target_kind_t;

/** WALL_PICTURE effect (DS-09). */
typedef enum
{
    PROTO_PIC_PICTURE = 0, /**< Show the mask in the colour. */
    PROTO_PIC_SPARKLE,     /**< The tile's own twinkle; the mask is unused. */
    PROTO_PIC_CLEAR,       /**< Clear the tile's picture list. */
    PROTO_PIC_EFFECT_COUNT
} proto_pic_effect_t;

/** WALL_PICTURE colour (DS-09), each within channel values 0 to 8. */
typedef enum
{
    PROTO_COLOUR_WHITE = 0,
    PROTO_COLOUR_YELLOW,
    PROTO_COLOUR_GREEN,
    PROTO_COLOUR_RED,
    PROTO_COLOUR_BLUE,
    PROTO_COLOUR_ORANGE,
    PROTO_COLOUR_PURPLE,
    PROTO_COLOUR_CYAN,
    PROTO_COLOUR_COUNT
} proto_colour_t;

/** Round state on STATUS (DS-08). */
typedef enum
{
    PROTO_ROUND_IDLE = 0, /**< GAME IDLE (or not in game mode). */
    PROTO_ROUND_ARMED,    /**< Start sprite up, waiting for the tap. */
    PROTO_ROUND_COUNTDOWN,
    PROTO_ROUND_PLAYING,
    PROTO_ROUND_RESULTS,
    PROTO_ROUND_STATE_COUNT
} proto_round_state_t;

/** Failure policy (FR18, T-B2-04). */
typedef enum
{
    PROTO_POLICY_CONTINUE = 0,
    PROTO_POLICY_PAUSE,
    PROTO_POLICY_COUNT
} proto_policy_t;

/** MANIFEST format_version (VC-5; PROVISIONAL numbering, OD-07 brief). */
typedef enum
{
    PROTO_FORMAT_UNKNOWN_TEST = 0, /**< Known to no firmware (test vector). */
    PROTO_FORMAT_8BIT,             /**< 192 B frames, DE_PROFILE only. */
    PROTO_FORMAT_PACKED,           /**< 96 B frames, 4 bits per channel. */
    PROTO_FORMAT_5BIT,             /**< Reserved (VC-12, deferred). */
    PROTO_FORMAT_COUNT
} proto_format_t;

/** NACK reason codes (PROVISIONAL, OD-07 brief: one per cited reason). */
typedef enum
{
    PROTO_NACK_UNKNOWN_FORMAT = 0, /**< VC-5. */
    PROTO_NACK_WRONG_LENGTH,       /**< VC-5. */
    PROTO_NACK_COMMAND_QUEUE_FULL, /**< VA-3.6, T-B3-03. */
    PROTO_NACK_UPLOAD_QUEUE_FULL,  /**< VA-3.6, T-B3-05. */
    PROTO_NACK_COUNT
} proto_nack_reason_t;

/** Coordinator states, W6 statechart 2 (T-B2-04), as STATUS carries them. */
typedef enum
{
    PROTO_COORD_BOOTING = 0,
    PROTO_COORD_CONNECTING_TO_ROUTER,
    PROTO_COORD_WAITING_FOR_SESSION,
    PROTO_COORD_VALIDATING_SESSION,
    PROTO_COORD_WAITING_FOR_NODES,
    PROTO_COORD_WAITING_FOR_TILE_READINESS,
    PROTO_COORD_SYNCHRONISING,
    PROTO_COORD_READY,
    PROTO_COORD_COORDINATING_PLAYBACK,
    PROTO_COORD_PAUSED,
    PROTO_COORD_RECOVERING,
    PROTO_COORD_FAULT,
    PROTO_COORD_COORDINATING_GAME, /**< 12: whack-a-mole round running. */
    PROTO_COORD_COUNT
} proto_coord_state_t;

/** Display states, W6 statechart 5 (T-B5-03), as TELEMETRY carries them. */
typedef enum
{
    PROTO_DISPLAY_INITIALISING = 0,
    PROTO_DISPLAY_IDLE,
    PROTO_DISPLAY_PREPARING_FRAME,
    PROTO_DISPLAY_TRANSMITTING_PIXELS,
    PROTO_DISPLAY_WAITING_FOR_NEXT_TICK,
    PROTO_DISPLAY_PAUSED,
    PROTO_DISPLAY_FAULT,
    PROTO_DISPLAY_COUNT
} proto_display_state_t;

/** Orientation quadrants (IFM-6a). */
typedef enum
{
    PROTO_ORIENT_0 = 0,
    PROTO_ORIENT_90,
    PROTO_ORIENT_180,
    PROTO_ORIENT_270,
    PROTO_ORIENT_COUNT
} proto_orientation_t;

/** Common header on every message (IFM-2a, W6D-2f). */
typedef struct
{
    uint8_t  type;       /**< proto_type_t code, 5 bits. */
    uint8_t  session_id; /**< 8 bits. */
    uint16_t seq;        /**< 16 bits. */
} proto_header_t;

/** REGISTER, tile to Pico (T-B2-01, T-B3-05). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id;           /**< 1 to 9. */
    uint8_t        cached_session_id; /**< Session of the cached asset. */
    uint32_t       cached_crc32;      /**< CRC32 of the cached asset. */
} proto_register_t;

/** HEARTBEAT, tile to Pico (T-B3-03). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9. */
} proto_heartbeat_t;

/** A tile's cell in the grid (SESSION_CONFIG, step 4d, DS-15). */
typedef struct
{
    uint8_t b_placed; /**< 1 = the tile is in the wall. */
    uint8_t row;      /**< 0 to rows - 1 when placed, otherwise 0. */
    uint8_t col;      /**< 0 to cols - 1 when placed, otherwise 0. */
} proto_cell_t;

/** SESSION_CONFIG, PC to Pico (T-B2-04, T-B1-05). frame_count and crc32[]
 *  are PROVISIONAL (OD-07 item 9, owner Benjamin): the Pico needs each
 *  tile's manifest CRC32 to check ASSET_READY, and the shared frame_count to
 *  compute the saved frame index for RESUME_AT and a rejoin. Step 4d
 *  (DS-15, 12 Nov 2026): a grid of up to 9 x 9 cells and a cell per Node
 *  ID, replacing the 9-position row-major node_map; the codec refuses a
 *  placed cell outside the grid, two tiles in one cell and a cell on a tile
 *  that is not placed. */
typedef struct
{
    proto_header_t header;
    uint8_t        rows;                    /**< 1 to 9. */
    uint8_t        cols;                    /**< 1 to 9. */
    uint8_t        policy;                  /**< proto_policy_t. */
    proto_cell_t   cell[PROTO_NODE_MAX];    /**< Node ID n at cell[n - 1]. */
    uint16_t       frame_count;             /**< 1 to 1023, every tile. */
    uint32_t       crc32[PROTO_NODE_MAX];   /**< Manifest CRC32 of Node ID
                                                 n at crc32[n - 1]; 0 for a
                                                 tile not placed. */
} proto_session_config_t;

/** MANIFEST, PC to tile (VC-5, IFM-1a). Session is header.session_id. */
typedef struct
{
    proto_header_t header;
    uint8_t        format_version; /**< proto_format_t. */
    uint16_t       frame_count;    /**< 1 to 1023. */
    uint32_t       crc32;          /**< zlib CRC32 of this tile's payload. */
} proto_manifest_t;

/** ASSET_CHUNK, PC to tile (T-B2-01, LDC-8). */
typedef struct
{
    proto_header_t  header;
    uint8_t         chunk_index; /**< 0 to 112. */
    uint16_t        payload_len; /**< 1 to 1,024 bytes. */
    uint8_t const * p_payload;   /**< Encode: source; decode: points into
                                      the received buffer. */
} proto_asset_chunk_t;

/** CHUNK_ACK, tile to PC (T-B3-05). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id;     /**< 1 to 9. */
    uint8_t        chunk_index; /**< 0 to 112. */
} proto_chunk_ack_t;

/** NACK, tile to sender (VC-5, VA-3.6). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9. */
    uint8_t        reason;  /**< proto_nack_reason_t. */
    uint16_t       ref_seq; /**< seq of the rejected message. */
} proto_nack_t;

/** BUSY, tile to PC (SRM-9, T-B3-04). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9. */
    uint16_t       ref_seq; /**< seq of the refused chunk or manifest. */
} proto_busy_t;

/** ASSET_READY {session_id, CRC32}, tile to Pico (W6D-7a). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9. */
    uint32_t       crc32;   /**< CRC32 of the validated payload. */
} proto_asset_ready_t;

/** SYNC_READY, tile to Pico (OD-10 rule on the tile). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9. */
} proto_sync_ready_t;

/** BEACON {session_id, seq, t_p} (T-B2-01, T-B2-02). */
typedef struct
{
    proto_header_t header; /**< header.type is PROTO_TYPE_BEACON. */
    uint64_t       t_p;    /**< Pico time_us_64() read just before sending. */
} proto_beacon_t;

/** START_AT {t_start}, Pico to tiles (T-B2-05). */
typedef struct
{
    proto_header_t header;
    uint64_t       t_start; /**< Timeline origin, Pico time, microseconds. */
} proto_start_at_t;

/** RESUME_AT {t_resume, saved frame index}, Pico to tiles (T-B2-05). */
typedef struct
{
    proto_header_t header;
    uint64_t       t_resume;    /**< Pico time, microseconds. */
    uint16_t       frame_index; /**< 0 to 1023, shown at t_resume. */
} proto_resume_at_t;

/** PAUSE or STOP, Pico to tiles (T-B2-05): header only. */
typedef struct
{
    proto_header_t header;
} proto_command_t;

/** STATUS, Pico to PC (T-B2-04). */
typedef struct
{
    proto_header_t header;
    uint16_t       readiness; /**< Bit n-1 = Node ID n ready, 9 bits. */
    uint16_t       online;    /**< Bit n-1 = Node ID n online, 9 bits. */
    uint8_t        state;     /**< proto_coord_state_t. */
    int8_t         score;     /**< Game score, -99 to 99; 0 outside a game. */
    uint8_t        round_state; /**< proto_round_state_t. */
    uint8_t        level;       /**< proto_level_t. */
    uint8_t        missed;      /**< Targets that timed out, 0 to 127. */
    uint8_t        wrong;       /**< Wrong taps, 0 to 127. */
    uint8_t        shown;       /**< Targets shown, 0 to 127. */
    uint8_t        time_left_s; /**< Seconds left in the round, 0 to 63. */
    uint16_t       reaction_ms; /**< Average hit reaction time, 0 to 2,047. */
} proto_status_t;

/** TELEMETRY {seq, fault word, orientation, display state}, tile to PC
 *  (VC-4). The telemetry seq is header.seq. */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id;       /**< 1 to 9. */
    uint32_t       fault_word;    /**< LDC-2. */
    uint8_t        orientation;   /**< proto_orientation_t. */
    uint8_t        display_state; /**< proto_display_state_t. */
} proto_telemetry_t;

/** TELEMETRY_ACK {seq}, PC to tile (VC-4). */
typedef struct
{
    proto_header_t header;
    uint16_t       acked_seq; /**< header.seq of the acknowledged TELEMETRY. */
} proto_telemetry_ack_t;

/** TAP {board number, seq, peak, timestamp}, tile to Pico, unicast, acked.
 *  The seq is header.seq. */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9, the tile's board number. */
    uint16_t       peak;    /**< tap_detect peak, mg; dedupe keeps the
                                 highest within 50 ms. */
    uint64_t       t_tap;   /**< Tap time in the Pico's corrected clock, us. */
} proto_tap_t;

/** MOLE_ON, Pico to one tile, unicast, acked: a solid-fill mole from t_show
 *  until t_show + up_ms x 1,000 us. */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9, the addressed tile. */
    uint64_t       t_show;  /**< Show time, Pico corrected clock, us. */
    uint16_t       up_ms;   /**< 0 to 4,000 ms; the hide time is
                                 t_show + up_ms x 1,000 us; 0 = up until
                                 tapped or cleared. */
    uint8_t        kind;    /**< proto_target_kind_t. */
} proto_mole_on_t;

/** FEEDBACK, Pico to one tile, unicast, acked: flash now. */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9, the addressed tile. */
    uint8_t        result;  /**< proto_feedback_t. */
} proto_feedback_msg_t;

/** GAME_CONTROL, PC to Pico and Pico to one tile, unicast, acked. */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 0 = PC to Pico; 1 to 9 = the addressed
                                 tile. */
    uint8_t        command; /**< proto_game_cmd_t. */
    uint8_t        level;   /**< proto_level_t; used with ARM. */
    uint8_t        threshold; /**< Tap threshold in 20 mg steps, 0 = the
                                   tile's default; used with ARM. */
} proto_game_control_t;

/** WALL_PICTURE, Pico to one tile, unicast, acked (DS-09): one entry of the
 *  tile's timed picture list, the tile's 8 x 8 piece of a picture the Pico
 *  drew across the wall (countdown, score, stars), shown from t_show for
 *  dur_ms on the shared clock so every tile changes together. */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9, the addressed tile. */
    uint8_t        slot;    /**< List entry, 0 to 3. */
    uint8_t        effect;  /**< proto_pic_effect_t. */
    uint8_t        colour;  /**< proto_colour_t. */
    uint8_t        b_repeat; /**< 1 = after this entry, start the list again
                                  from slot 1 (results loop). */
    uint64_t       t_show;  /**< Show time, Pico corrected clock, us. */
    uint16_t       dur_ms;  /**< 0 to 8,191 ms; 0 = until replaced. */
    uint64_t       mask;    /**< Bit n = pixel n lit, pixels in asset frame
                                 order (layout_table.md section 6), before
                                 the tile's rotation. */
} proto_wall_picture_t;

/** ACK, the reply to every TAP, MOLE_ON, FEEDBACK, GAME_CONTROL and
 *  WALL_PICTURE. */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id;   /**< 0 = between PC and Pico; 1 to 9 = the
                                   tile that sends it or is sent it. */
    uint16_t       acked_seq; /**< header.seq of the acknowledged message. */
} proto_ack_t;

/**
 * @brief  Write the low width bits of value at bit offset bit_off. Bit 0 is
 *         the least significant bit of p_buf[0], so a field wider than 8 bits
 *         is little-endian. Other bits of the touched bytes are kept.
 * @param  p_buf    Buffer of at least (bit_off + width + 7) / 8 bytes.
 * @param  bit_off  First bit.
 * @param  width    1 to 64.
 * @param  value    Value; bits above width are ignored.
 */
void proto_put_bits (uint8_t * p_buf, uint32_t bit_off, uint32_t width,
                     uint64_t value);

/**
 * @brief  Read width bits at bit offset bit_off (inverse of proto_put_bits).
 * @param  p_buf    Buffer of at least (bit_off + width + 7) / 8 bytes.
 * @param  bit_off  First bit.
 * @param  width    1 to 64.
 * @return The field value.
 */
uint64_t proto_get_bits (uint8_t const * p_buf, uint32_t bit_off,
                         uint32_t width);

/**
 * @brief  Write a 16-bit value little-endian at a byte position.
 * @param  p_buf  Buffer; p_buf[0] receives the low byte.
 * @param  value  Value.
 */
void proto_put_u16_le (uint8_t * p_buf, uint16_t value);

/**
 * @brief  Write a 32-bit value little-endian at a byte position.
 * @param  p_buf  Buffer; p_buf[0] receives the low byte.
 * @param  value  Value.
 */
void proto_put_u32_le (uint8_t * p_buf, uint32_t value);

/**
 * @brief  Write a 64-bit value little-endian at a byte position.
 * @param  p_buf  Buffer; p_buf[0] receives the low byte.
 * @param  value  Value.
 */
void proto_put_u64_le (uint8_t * p_buf, uint64_t value);

/**
 * @brief  Read a little-endian 16-bit value at a byte position.
 * @param  p_buf  Buffer.
 * @return The value.
 */
uint16_t proto_get_u16_le (uint8_t const * p_buf);

/**
 * @brief  Read a little-endian 32-bit value at a byte position.
 * @param  p_buf  Buffer.
 * @return The value.
 */
uint32_t proto_get_u32_le (uint8_t const * p_buf);

/**
 * @brief  Read a little-endian 64-bit value at a byte position.
 * @param  p_buf  Buffer.
 * @return The value.
 */
uint64_t proto_get_u64_le (uint8_t const * p_buf);

/**
 * @brief  Read the common header, so a receiver can dispatch on type before
 *         calling the matching proto_decode_ function.
 * @param  p_buf     Received bytes.
 * @param  len       Received length, bytes.
 * @param  p_header  Receives the header when the result is true.
 * @return true when len holds a header and the type is a known code.
 */
bool proto_decode_header (uint8_t const * p_buf, uint32_t len,
                          proto_header_t * p_header);

/*
 * Encode and decode, one pair per message row. Every encoder writes the type
 * code of its row whatever p_msg->header.type holds, zeroes the padding bits
 * and returns the bytes written, or 0 when p_buf is too small or a field is
 * outside its valid range. Every decoder returns true only when the length
 * is exactly the row's length (ASSET_CHUNK: header plus 1 to 1,024 payload
 * bytes), the type matches and every field is inside its valid range; the
 * session_id and seq checks belong to the receiver.
 */
uint32_t proto_encode_register (proto_register_t const * p_msg, uint8_t * p_buf,
                                uint32_t buf_len);
bool     proto_decode_register (uint8_t const * p_buf, uint32_t len,
                                proto_register_t * p_msg);
uint32_t proto_encode_heartbeat (proto_heartbeat_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_heartbeat (uint8_t const * p_buf, uint32_t len,
                                 proto_heartbeat_t * p_msg);
uint32_t proto_encode_session_config (proto_session_config_t const * p_msg,
                                      uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_session_config (uint8_t const * p_buf, uint32_t len,
                                      proto_session_config_t * p_msg);
uint32_t proto_encode_manifest (proto_manifest_t const * p_msg, uint8_t * p_buf,
                                uint32_t buf_len);
bool     proto_decode_manifest (uint8_t const * p_buf, uint32_t len,
                                proto_manifest_t * p_msg);
uint32_t proto_encode_asset_chunk (proto_asset_chunk_t const * p_msg,
                                   uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_asset_chunk (uint8_t const * p_buf, uint32_t len,
                                   proto_asset_chunk_t * p_msg);
uint32_t proto_encode_chunk_ack (proto_chunk_ack_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_chunk_ack (uint8_t const * p_buf, uint32_t len,
                                 proto_chunk_ack_t * p_msg);
uint32_t proto_encode_nack (proto_nack_t const * p_msg, uint8_t * p_buf,
                            uint32_t buf_len);
bool     proto_decode_nack (uint8_t const * p_buf, uint32_t len,
                            proto_nack_t * p_msg);
uint32_t proto_encode_busy (proto_busy_t const * p_msg, uint8_t * p_buf,
                            uint32_t buf_len);
bool     proto_decode_busy (uint8_t const * p_buf, uint32_t len,
                            proto_busy_t * p_msg);
uint32_t proto_encode_asset_ready (proto_asset_ready_t const * p_msg,
                                   uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_asset_ready (uint8_t const * p_buf, uint32_t len,
                                   proto_asset_ready_t * p_msg);
uint32_t proto_encode_sync_ready (proto_sync_ready_t const * p_msg,
                                  uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_sync_ready (uint8_t const * p_buf, uint32_t len,
                                  proto_sync_ready_t * p_msg);
uint32_t proto_encode_beacon (proto_beacon_t const * p_msg, uint8_t * p_buf,
                              uint32_t buf_len);
bool     proto_decode_beacon (uint8_t const * p_buf, uint32_t len,
                              proto_beacon_t * p_msg);
uint32_t proto_encode_start_at (proto_start_at_t const * p_msg, uint8_t * p_buf,
                                uint32_t buf_len);
bool     proto_decode_start_at (uint8_t const * p_buf, uint32_t len,
                                proto_start_at_t * p_msg);
uint32_t proto_encode_resume_at (proto_resume_at_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_resume_at (uint8_t const * p_buf, uint32_t len,
                                 proto_resume_at_t * p_msg);
uint32_t proto_encode_pause (proto_command_t const * p_msg, uint8_t * p_buf,
                             uint32_t buf_len);
bool     proto_decode_pause (uint8_t const * p_buf, uint32_t len,
                             proto_command_t * p_msg);
uint32_t proto_encode_stop (proto_command_t const * p_msg, uint8_t * p_buf,
                            uint32_t buf_len);
bool     proto_decode_stop (uint8_t const * p_buf, uint32_t len,
                            proto_command_t * p_msg);
uint32_t proto_encode_status (proto_status_t const * p_msg, uint8_t * p_buf,
                              uint32_t buf_len);
bool     proto_decode_status (uint8_t const * p_buf, uint32_t len,
                              proto_status_t * p_msg);
uint32_t proto_encode_telemetry (proto_telemetry_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_telemetry (uint8_t const * p_buf, uint32_t len,
                                 proto_telemetry_t * p_msg);
uint32_t proto_encode_telemetry_ack (proto_telemetry_ack_t const * p_msg,
                                     uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_telemetry_ack (uint8_t const * p_buf, uint32_t len,
                                     proto_telemetry_ack_t * p_msg);
uint32_t proto_encode_tap (proto_tap_t const * p_msg, uint8_t * p_buf,
                           uint32_t buf_len);
bool     proto_decode_tap (uint8_t const * p_buf, uint32_t len,
                           proto_tap_t * p_msg);
uint32_t proto_encode_mole_on (proto_mole_on_t const * p_msg, uint8_t * p_buf,
                               uint32_t buf_len);
bool     proto_decode_mole_on (uint8_t const * p_buf, uint32_t len,
                               proto_mole_on_t * p_msg);
uint32_t proto_encode_feedback (proto_feedback_msg_t const * p_msg,
                                uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_feedback (uint8_t const * p_buf, uint32_t len,
                                proto_feedback_msg_t * p_msg);
uint32_t proto_encode_game_control (proto_game_control_t const * p_msg,
                                    uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_game_control (uint8_t const * p_buf, uint32_t len,
                                    proto_game_control_t * p_msg);
uint32_t proto_encode_wall_picture (proto_wall_picture_t const * p_msg,
                                  uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_wall_picture (uint8_t const * p_buf, uint32_t len,
                                  proto_wall_picture_t * p_msg);
uint32_t proto_encode_ack (proto_ack_t const * p_msg, uint8_t * p_buf,
                           uint32_t buf_len);
bool     proto_decode_ack (uint8_t const * p_buf, uint32_t len,
                           proto_ack_t * p_msg);

/**
 * @brief  Overwrite only the t_p field of an encoded BEACON, so the sender can
 *         read time_us_64() after everything else is encoded (T-B2-02: t_p is
 *         read immediately before udp_sendto).
 * @param  p_buf  An encoded BEACON of PROTO_BEACON_BYTES bytes.
 * @param  t_p    Pico time, microseconds.
 */
void proto_beacon_set_t_p (uint8_t * p_buf, uint64_t t_p);

#endif /* PROTO_H */

/*** end of file ***/
