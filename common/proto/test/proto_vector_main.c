/**
 * @file    proto_vector_main.c
 * @brief   Host tool for the cross-language vectors (T-B1-01): runs the C
 *          codec on bytes written by pc/proto_layout.py and on fields given
 *          on the command line, so pc/tests/test_cross_lang.py can compare
 *          C and Python byte for byte.
 *
 * Implemented by Ahsanul (B4) for Jolyn, task T-B1-01, on the T-B2-01 codec
 * (owner Benjamin). Every message goes through a proto_encode_ or
 * proto_decode_ function; the tool itself knows no offsets.
 *
 * Usage (built by make -C common/proto/test vector_tool):
 *   proto_vector decode FILE         one "name=value" line per field
 *   proto_vector encode TYPE k=v ... the encoded bytes as lowercase hex
 *   proto_vector nibbles FILE        the 192 nibbles of a 96 B frame
 * Output format: "type=<NAME>", "session_id=", "seq=", then the body fields
 * in layout_table.md order, decimal; node_map as 9 comma-separated values;
 * payload as lowercase hex. Exit status 0 on success, 1 on any error.
 */
#include "proto_vector_main.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proto.h"

#define FILE_MAX (PROTO_ASSET_CHUNK_MAX_BYTES + 1u)
/* Large enough for the 115,200 B VC-5 payload vector, so the codec's own
 * length rule, not the file reader, refuses it. */
#define READ_MAX       (131072u)
#define NIBBLE_BITS    4u
#define FRAME_NIBBLES  192u
#define ARG_MODE       1
#define ARG_OPERAND    2
#define ARG_FIRST_PAIR 3
#define HEX_BASE       16
#define DEC_BASE       10
#define NAME_MAX_LEN   32u
#define HEX_PAIR       2u

/** Command-line pairs for encode mode. */
typedef struct
{
    int32_t              count;
    char const * const * pp_args;
} vector_args_t;

/** Message names in type-code order (layout_table.md section 5.1). */
static char const * const g_names[PROTO_TYPE_COUNT] = {
    "REGISTER",  "HEARTBEAT",    "SESSION_CONFIG", "MANIFEST",    "ASSET_CHUNK",
    "CHUNK_ACK", "NACK",         "BUSY",           "ASSET_READY", "SYNC_READY",
    "BEACON",    "START_AT",     "RESUME_AT",      "PAUSE",       "STOP",
    "STATUS",    "TELEMETRY",    "TELEMETRY_ACK",  "TAP",         "MOLE_ON",
    "FEEDBACK",  "GAME_CONTROL", "WALL_PICTURE",     "ACK"};

static uint8_t g_in[READ_MAX];
static uint8_t g_out[FILE_MAX];
static uint8_t g_payload[PROTO_CHUNK_PAYLOAD_MAX];

static bool         read_file (char const * p_path, uint32_t * p_len);
static char const * find_arg (vector_args_t const * p_args, char const * p_key);
static uint64_t     arg_u64 (vector_args_t const * p_args, char const * p_key);
static void         arg_map (vector_args_t const * p_args, uint8_t * p_map);
static void     arg_crc_list (vector_args_t const * p_args, uint32_t * p_crc);
static uint16_t arg_payload (vector_args_t const * p_args);
static proto_header_t arg_header (vector_args_t const * p_args);
static void           print_header (proto_header_t const * p_header);
static void           print_u64 (char const * p_name, uint64_t value);
static void           print_s64 (char const * p_name, int64_t value);
static void           print_hex (uint8_t const * p_bytes, uint32_t len);
static bool           decode_any (uint8_t const * p_buf, uint32_t len);
static bool     decode_tile (uint8_t const * p_buf, uint32_t len, uint8_t type);
static bool     decode_tile_upload (uint8_t const * p_buf, uint32_t len,
                                    uint8_t type);
static bool     decode_coordinator (uint8_t const * p_buf, uint32_t len,
                                    uint8_t type);
static bool     decode_pc (uint8_t const * p_buf, uint32_t len, uint8_t type);
static bool     decode_game (uint8_t const * p_buf, uint32_t len, uint8_t type);
static bool     decode_game_pico (uint8_t const * p_buf, uint32_t len,
                                  uint8_t type);
static uint32_t encode_game (uint32_t type, vector_args_t const * p_args);
static uint32_t encode_any (char const * p_type, vector_args_t const * p_args);
static uint32_t encode_tile (uint32_t type, vector_args_t const * p_args);
static uint32_t encode_coordinator (uint32_t              type,
                                    vector_args_t const * p_args);
static uint32_t encode_pc (uint32_t type, vector_args_t const * p_args);
static bool     print_nibbles (uint32_t len);

/**
 * @brief  Run one mode.
 * @param  argc  Argument count.
 * @param  argv  Arguments.
 * @return 0 on success, 1 on any error.
 */
/* DEVIATION D-2: the C standard fixes int main (int, char **). */
int main (int argc, char ** pp_argv)
{
    bool     b_ok = false;
    uint32_t len  = 0u;

    if (argc > ARG_OPERAND)
    {
        char const * const p_mode = pp_argv[ARG_MODE];

        if (0 == strcmp(p_mode, "decode"))
        {
            b_ok = (read_file(pp_argv[ARG_OPERAND], &len)) &&
                   (decode_any(g_in, len));
        }
        else if (0 == strcmp(p_mode, "nibbles"))
        {
            b_ok =
                (read_file(pp_argv[ARG_OPERAND], &len)) && (print_nibbles(len));
        }
        else if (0 == strcmp(p_mode, "encode"))
        {
            /* DEVIATION D-2: argc is the int main receives. */
            vector_args_t const args = {
                (int32_t)(argc - ARG_FIRST_PAIR), /* argc > 2, small. */
                (char const * const *)&pp_argv
                    [ARG_FIRST_PAIR]}; /* Read only. */
            uint32_t const written = encode_any(pp_argv[ARG_OPERAND], &args);

            if (written > 0u)
            {
                print_hex(g_out, written);
                b_ok = true;
            }
        }
        else
        {
            b_ok = false;
        }
    }

    if (!b_ok)
    {
        fprintf(stderr, "proto_vector: failed\n");
    }

    return b_ok ? 0 : 1; /* DEVIATION D-2: exit status is an int. */
}

/**
 * @brief  Read a whole file into g_in.
 * @param  p_path  File name.
 * @param  p_len   Receives the length.
 * @return false when the file is missing or longer than g_in.
 */
static bool read_file (char const * p_path, uint32_t * p_len)
{
    bool         b_ok   = false;
    FILE * const p_file = fopen(p_path, "rb");

    if (NULL != p_file)
    {
        /* DEVIATION D-2: fread returns the C standard size_t. */
        size_t const got = fread(g_in, 1u, READ_MAX, p_file);

        /* got is at most READ_MAX, which fits uint32_t. */
        *p_len = (uint32_t)got;
        b_ok   = (got < READ_MAX) && (0 == ferror(p_file));
        (void)fclose(p_file); /* Read only, nothing to flush. */
    }

    return b_ok;
}

/**
 * @brief  Value text of "key=value" among the encode arguments.
 * @param  p_args  Arguments.
 * @param  p_key   Key.
 * @return The text after '=', or NULL.
 */
static char const * find_arg (vector_args_t const * p_args, char const * p_key)
{
    char const * p_value = NULL;
    /* DEVIATION D-2: strlen returns the C standard size_t. */
    size_t const key_len = strlen(p_key);

    for (int32_t idx = 0; (NULL == p_value) && (idx < p_args->count); idx++)
    {
        char const * const p_arg = p_args->pp_args[idx];

        if ((0 == strncmp(p_arg, p_key, key_len)) && ('=' == p_arg[key_len]))
        {
            p_value = &p_arg[key_len + 1u];
        }
    }

    return p_value;
}

/**
 * @brief  Decimal value of a key, 0 when absent (the encoder then refuses
 *         any field whose range excludes 0).
 * @param  p_args  Arguments.
 * @param  p_key   Key.
 * @return The value.
 */
static uint64_t arg_u64 (vector_args_t const * p_args, char const * p_key)
{
    char const * const p_value = find_arg(p_args, p_key);
    uint64_t           value   = 0u;

    if (NULL != p_value)
    {
        value = strtoull(p_value, NULL, DEC_BASE); /* Widens to 64 bits. */
    }

    return value;
}

/**
 * @brief  Value of a key, masked to 8 bits. The encoder's range check
 *         refuses a masked value that is still out of range.
 * @param  p_args  Arguments.
 * @param  p_key   Key.
 * @return The value.
 */
static uint8_t arg_u8 (vector_args_t const * p_args, char const * p_key)
{
    /* Masked to 8 bits, so it fits uint8_t. */
    return (uint8_t)(arg_u64(p_args, p_key) & UINT8_MAX);
}

/**
 * @brief  Value of a key, masked to 16 bits.
 * @param  p_args  Arguments.
 * @param  p_key   Key.
 * @return The value.
 */
static uint16_t arg_u16 (vector_args_t const * p_args, char const * p_key)
{
    /* Masked to 16 bits, so it fits uint16_t. */
    return (uint16_t)(arg_u64(p_args, p_key) & UINT16_MAX);
}

/**
 * @brief  Signed decimal value of a key, 0 when absent (the encoder then
 *         refuses any field whose range excludes 0).
 * @param  p_args  Arguments.
 * @param  p_key   Key.
 * @return The value, clamped to INT8_MAX when out of int8_t range.
 */
static int8_t arg_s8 (vector_args_t const * p_args, char const * p_key)
{
    char const * const p_value = find_arg(p_args, p_key);
    int64_t            value   = 0;

    if (NULL != p_value)
    {
        value = strtoll(p_value, NULL, DEC_BASE); /* Widens to 64 bits. */
    }

    if ((value < INT8_MIN) || (value > INT8_MAX))
    {
        value = INT8_MAX; /* Out of int8_t: the encoder refuses 127. */
    }

    /* value is -128 to 127 here, so it fits int8_t. */
    return (int8_t)value;
}

/**
 * @brief  Value of a key, masked to 32 bits.
 * @param  p_args  Arguments.
 * @param  p_key   Key.
 * @return The value.
 */
static uint32_t arg_u32 (vector_args_t const * p_args, char const * p_key)
{
    /* Masked to 32 bits, so it fits uint32_t. */
    return (uint32_t)(arg_u64(p_args, p_key) & UINT32_MAX);
}

/**
 * @brief  node_map=a,b,... into PROTO_MAP_SLOTS entries.
 * @param  p_args  Arguments.
 * @param  p_map   Receives the map.
 */
static void arg_map (vector_args_t const * p_args, uint8_t * p_map)
{
    char const * p_text = find_arg(p_args, "node_map");

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        p_map[slot] = 0u;

        if (NULL != p_text)
        {
            char * p_end = NULL;

            /* A map entry is at most 15 after the encoder's range check,
             * and strtoul's result is masked to 8 bits here. */
            p_map[slot] =
                (uint8_t)(strtoul(p_text, &p_end, DEC_BASE) & UINT8_MAX);
            p_text = (',' == *p_end) ? (p_end + 1) : NULL;
        }
    }
}

/**
 * @brief  crc32=<9 comma-separated decimal values> into p_crc; missing
 *         entries are 0.
 * @param  p_args  Arguments.
 * @param  p_crc   Receives PROTO_MAP_SLOTS values.
 */
static void arg_crc_list (vector_args_t const * p_args, uint32_t * p_crc)
{
    char const * p_text = find_arg(p_args, "crc32");

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        p_crc[slot] = 0u;

        if (NULL != p_text)
        {
            char * p_end = NULL;

            /* A CRC32 is at most 2^32 - 1; strtoull's result is masked to
             * 32 bits here. */
            p_crc[slot] =
                (uint32_t)(strtoull(p_text, &p_end, DEC_BASE) & UINT32_MAX);
            p_text = (',' == *p_end) ? (p_end + 1) : NULL;
        }
    }
}

/**
 * @brief  payload=<hex> into g_payload.
 * @param  p_args  Arguments.
 * @return Payload bytes, 0 when absent or malformed.
 */
static uint16_t arg_payload (vector_args_t const * p_args)
{
    char const * const p_hex = find_arg(p_args, "payload");
    uint16_t           count = 0u;

    if (NULL != p_hex)
    {
        /* DEVIATION D-2: strlen returns the C standard size_t. */
        size_t const digits = strlen(p_hex);

        if ((0u == (digits % HEX_PAIR)) &&
            ((digits / HEX_PAIR) <= PROTO_CHUNK_PAYLOAD_MAX))
        {
            /* digits / 2 is at most 1,024 here, so it fits uint32_t. */
            uint32_t const count_bytes = (uint32_t)(digits / HEX_PAIR);

            for (uint32_t idx = 0u; idx < count_bytes; idx++)
            {
                char pair[HEX_PAIR + 1u] = {p_hex[HEX_PAIR * idx],
                                            p_hex[(HEX_PAIR * idx) + 1u], '\0'};

                /* Two hex digits are at most 255. */
                g_payload[idx] = (uint8_t)strtoul(pair, NULL, HEX_BASE);
            }

            /* At most 1,024 bytes, which fits uint16_t. */
            count = (uint16_t)count_bytes;
        }
    }

    return count;
}

/**
 * @brief  session_id and seq from the arguments.
 * @param  p_args  Arguments.
 * @return The header (type is set by the encoder).
 */
static proto_header_t arg_header (vector_args_t const * p_args)
{
    proto_header_t header;

    header.type = 0u;
    /* Masked to the field width; an out-of-range test value is the
     * Python side's error, caught by its own encoder. */
    header.session_id = arg_u8(p_args, "session_id");
    header.seq        = arg_u16(p_args, "seq");

    return header;
}

/**
 * @brief  Print type, session_id and seq.
 * @param  p_header  Header.
 */
static void print_header (proto_header_t const * p_header)
{
    printf("type=%s\n", g_names[p_header->type]);
    print_u64("session_id", p_header->session_id);
    print_u64("seq", p_header->seq);
}

/**
 * @brief  Print one decimal field.
 * @param  p_name  Field name.
 * @param  value   Value.
 */
static void print_u64 (char const * p_name, uint64_t value)
{
    printf("%s=%" PRIu64 "\n", p_name, value);
}

/**
 * @brief  Print one signed decimal field.
 * @param  p_name  Field name.
 * @param  value   Value.
 */
static void print_s64 (char const * p_name, int64_t value)
{
    printf("%s=%" PRId64 "\n", p_name, value);
}

/**
 * @brief  Print bytes as lowercase hex and a newline.
 * @param  p_bytes  Bytes.
 * @param  len      Count.
 */
static void print_hex (uint8_t const * p_bytes, uint32_t len)
{
    for (uint32_t idx = 0u; idx < len; idx++)
    {
        printf("%02" PRIx8, p_bytes[idx]);
    }

    printf("\n");
}

/**
 * @brief  Decode any message and print it.
 * @param  p_buf  Bytes.
 * @param  len    Length.
 * @return false when the decoder rejects the bytes.
 */
static bool decode_any (uint8_t const * p_buf, uint32_t len)
{
    proto_header_t header;
    bool           b_ok = proto_decode_header(p_buf, len, &header);

    if (b_ok)
    {
        b_ok = (decode_tile(p_buf, len, header.type)) ||
               (decode_tile_upload(p_buf, len, header.type)) ||
               (decode_coordinator(p_buf, len, header.type)) ||
               (decode_pc(p_buf, len, header.type)) ||
               (decode_game(p_buf, len, header.type)) ||
               (decode_game_pico(p_buf, len, header.type));
    }

    return b_ok;
}

/**
 * @brief  Decode and print a message a tile sends.
 * @param  p_buf  Bytes.
 * @param  len    Length.
 * @param  type   Header type code.
 * @return true when the type is a tile message and it decoded.
 */
static bool decode_tile (uint8_t const * p_buf, uint32_t len, uint8_t type)
{
    bool b_ok = false;

    switch (type)
    {
        case PROTO_TYPE_REGISTER:
        {
            proto_register_t msg;

            b_ok = proto_decode_register(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("cached_session_id", msg.cached_session_id);
                print_u64("cached_crc32", msg.cached_crc32);
            }
        }
        break;

        case PROTO_TYPE_HEARTBEAT:
        {
            proto_heartbeat_t msg;

            b_ok = proto_decode_heartbeat(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
            }
        }
        break;

        case PROTO_TYPE_SYNC_READY:
        {
            proto_sync_ready_t msg;

            b_ok = proto_decode_sync_ready(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
            }
        }
        break;

        case PROTO_TYPE_TELEMETRY:
        {
            proto_telemetry_t msg;

            b_ok = proto_decode_telemetry(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("fault_word", msg.fault_word);
                print_u64("orientation", msg.orientation);
                print_u64("display_state", msg.display_state);
            }
        }
        break;

        default:
            b_ok = false;
            break;
    }

    return b_ok;
}

/**
 * @brief  Decode and print a whack-a-mole row (TAP, ACK, MOLE_ON).
 * @param  p_buf  Bytes.
 * @param  len    Length.
 * @param  type   Header type code.
 * @return true when the type is one of these and it decoded.
 */
static bool decode_game (uint8_t const * p_buf, uint32_t len, uint8_t type)
{
    bool b_ok = false;

    switch (type)
    {
        case PROTO_TYPE_TAP:
        {
            proto_tap_t msg;

            b_ok = proto_decode_tap(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("peak", msg.peak);
                print_u64("t_tap", msg.t_tap);
            }
        }
        break;

        case PROTO_TYPE_ACK:
        {
            proto_ack_t msg;

            b_ok = proto_decode_ack(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("acked_seq", msg.acked_seq);
            }
        }
        break;

        case PROTO_TYPE_MOLE_ON:
        {
            proto_mole_on_t msg;

            b_ok = proto_decode_mole_on(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("t_show", msg.t_show);
                print_u64("up_ms", msg.up_ms);
                print_u64("kind", msg.kind);
            }
        }
        break;

        default:
            b_ok = false;
            break;
    }

    return b_ok;
}

/**
 * @brief  Decode and print a whack-a-mole row the Pico sends to a tile
 *         (FEEDBACK, GAME_CONTROL, WALL_PICTURE).
 * @param  p_buf  Bytes.
 * @param  len    Length.
 * @param  type   Header type code.
 * @return true when the type is one of these and it decoded.
 */
static bool decode_game_pico (uint8_t const * p_buf, uint32_t len, uint8_t type)
{
    bool b_ok = false;

    switch (type)
    {
        case PROTO_TYPE_FEEDBACK:
        {
            proto_feedback_msg_t msg;

            b_ok = proto_decode_feedback(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("result", msg.result);
            }
        }
        break;

        case PROTO_TYPE_GAME_CONTROL:
        {
            proto_game_control_t msg;

            b_ok = proto_decode_game_control(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("command", msg.command);
                print_u64("level", msg.level);
                print_u64("threshold", msg.threshold);
            }
        }
        break;

        case PROTO_TYPE_WALL_PICTURE:
        {
            proto_wall_picture_t msg;

            b_ok = proto_decode_wall_picture(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("slot", msg.slot);
                print_u64("effect", msg.effect);
                print_u64("colour", msg.colour);
                print_u64("repeat", msg.b_repeat);
                print_u64("t_show", msg.t_show);
                print_u64("dur_ms", msg.dur_ms);
                print_u64("mask", msg.mask);
            }
        }
        break;

        default:
            b_ok = false;
            break;
    }

    return b_ok;
}

/**
 * @brief  Decode and print a tile's reply to an upload or command
 *         (CHUNK_ACK, NACK, BUSY, ASSET_READY).
 * @param  p_buf  Bytes.
 * @param  len    Length.
 * @param  type   Header type code.
 * @return true when the type is one of these and it decoded.
 */
static bool decode_tile_upload (uint8_t const * p_buf, uint32_t len,
                                uint8_t type)
{
    bool b_ok = false;

    switch (type)
    {
        case PROTO_TYPE_CHUNK_ACK:
        {
            proto_chunk_ack_t msg;

            b_ok = proto_decode_chunk_ack(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("chunk_index", msg.chunk_index);
            }
        }
        break;

        case PROTO_TYPE_NACK:
        {
            proto_nack_t msg;

            b_ok = proto_decode_nack(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("reason", msg.reason);
                print_u64("ref_seq", msg.ref_seq);
            }
        }
        break;

        case PROTO_TYPE_BUSY:
        {
            proto_busy_t msg;

            b_ok = proto_decode_busy(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("ref_seq", msg.ref_seq);
            }
        }
        break;

        case PROTO_TYPE_ASSET_READY:
        {
            proto_asset_ready_t msg;

            b_ok = proto_decode_asset_ready(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("node_id", msg.node_id);
                print_u64("crc32", msg.crc32);
            }
        }
        break;

        default:
            b_ok = false;
            break;
    }

    return b_ok;
}

/**
 * @brief  Decode and print a message the Pico sends.
 * @param  p_buf  Bytes.
 * @param  len    Length.
 * @param  type   Header type code.
 * @return true when the type is a Pico message and it decoded.
 */
static bool decode_coordinator (uint8_t const * p_buf, uint32_t len,
                                uint8_t type)
{
    bool b_ok = false;

    switch (type)
    {
        case PROTO_TYPE_BEACON:
        {
            proto_beacon_t msg;

            b_ok = proto_decode_beacon(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("t_p", msg.t_p);
            }
        }
        break;

        case PROTO_TYPE_START_AT:
        {
            proto_start_at_t msg;

            b_ok = proto_decode_start_at(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("t_start", msg.t_start);
            }
        }
        break;

        case PROTO_TYPE_RESUME_AT:
        {
            proto_resume_at_t msg;

            b_ok = proto_decode_resume_at(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("t_resume", msg.t_resume);
                print_u64("frame_index", msg.frame_index);
            }
        }
        break;

        case PROTO_TYPE_PAUSE:
        case PROTO_TYPE_STOP:
        {
            proto_command_t msg;

            b_ok = (PROTO_TYPE_PAUSE == type)
                       ? proto_decode_pause(p_buf, len, &msg)
                       : proto_decode_stop(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
            }
        }
        break;

        case PROTO_TYPE_STATUS:
        {
            proto_status_t msg;

            b_ok = proto_decode_status(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("readiness", msg.readiness);
                print_u64("online", msg.online);
                print_u64("state", msg.state);
                print_s64("score", msg.score);
                print_u64("round_state", msg.round_state);
                print_u64("level", msg.level);
                print_u64("missed", msg.missed);
                print_u64("wrong", msg.wrong);
                print_u64("shown", msg.shown);
                print_u64("time_left_s", msg.time_left_s);
                print_u64("reaction_ms", msg.reaction_ms);
            }
        }
        break;

        default:
            b_ok = false;
            break;
    }

    return b_ok;
}

/**
 * @brief  Decode and print a message the PC sends.
 * @param  p_buf  Bytes.
 * @param  len    Length.
 * @param  type   Header type code.
 * @return true when the type is a PC message and it decoded.
 */
static bool decode_pc (uint8_t const * p_buf, uint32_t len, uint8_t type)
{
    bool b_ok = false;

    switch (type)
    {
        case PROTO_TYPE_SESSION_CONFIG:
        {
            proto_session_config_t msg;

            b_ok = proto_decode_session_config(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("rows", msg.rows);
                print_u64("cols", msg.cols);
                print_u64("policy", msg.policy);
                printf("node_map=");

                for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
                {
                    printf("%s%" PRIu8, (0u == slot) ? "" : ",",
                           msg.node_map[slot]);
                }

                printf("\n");
                print_u64("frame_count", msg.frame_count);
                printf("crc32=");

                for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
                {
                    printf("%s%" PRIu32, (0u == slot) ? "" : ",",
                           msg.crc32[slot]);
                }

                printf("\n");
            }
        }
        break;

        case PROTO_TYPE_MANIFEST:
        {
            proto_manifest_t msg;

            b_ok = proto_decode_manifest(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("format_version", msg.format_version);
                print_u64("frame_count", msg.frame_count);
                print_u64("crc32", msg.crc32);
            }
        }
        break;

        case PROTO_TYPE_ASSET_CHUNK:
        {
            proto_asset_chunk_t msg;

            b_ok = proto_decode_asset_chunk(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("chunk_index", msg.chunk_index);
                printf("payload=");
                print_hex(msg.p_payload, msg.payload_len);
            }
        }
        break;

        case PROTO_TYPE_TELEMETRY_ACK:
        {
            proto_telemetry_ack_t msg;

            b_ok = proto_decode_telemetry_ack(p_buf, len, &msg);

            if (b_ok)
            {
                print_header(&msg.header);
                print_u64("acked_seq", msg.acked_seq);
            }
        }
        break;

        default:
            b_ok = false;
            break;
    }

    return b_ok;
}

/**
 * @brief  Encode the named message from the arguments into g_out.
 * @param  p_type  Message name.
 * @param  p_args  Arguments.
 * @return Bytes written, or 0.
 */
static uint32_t encode_any (char const * p_type, vector_args_t const * p_args)
{
    uint32_t type    = PROTO_TYPE_COUNT;
    uint32_t written = 0u;

    for (uint32_t idx = 0u; idx < PROTO_TYPE_COUNT; idx++)
    {
        if (0 == strncmp(p_type, g_names[idx], NAME_MAX_LEN))
        {
            type = idx;
        }
    }

    if (type < PROTO_TYPE_COUNT)
    {
        written = encode_tile(type, p_args);

        if (0u == written)
        {
            written = encode_coordinator(type, p_args);
        }

        if (0u == written)
        {
            written = encode_pc(type, p_args);
        }

        if (0u == written)
        {
            written = encode_game(type, p_args);
        }
    }

    return written;
}

/**
 * @brief  Encode a message a tile sends.
 * @param  type    Type code.
 * @param  p_args  Arguments.
 * @return Bytes written, or 0.
 */
static uint32_t encode_tile (uint32_t type, vector_args_t const * p_args)
{
    proto_header_t const header  = arg_header(p_args);
    uint32_t             written = 0u;

    uint8_t const node = arg_u8(p_args, "node_id");

    switch (type)
    {
        case PROTO_TYPE_REGISTER:
        {
            proto_register_t const msg = {header, node,
                                          arg_u8(p_args, "cached_session_id"),
                                          arg_u32(p_args, "cached_crc32")};

            written = proto_encode_register(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_HEARTBEAT:
        {
            proto_heartbeat_t const msg = {header, node};

            written = proto_encode_heartbeat(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_SYNC_READY:
        {
            proto_sync_ready_t const msg = {header, node};

            written = proto_encode_sync_ready(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_CHUNK_ACK:
        {
            proto_chunk_ack_t const msg = {header, node,
                                           arg_u8(p_args, "chunk_index")};

            written = proto_encode_chunk_ack(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_NACK:
        {
            proto_nack_t const msg = {header, node, arg_u8(p_args, "reason"),
                                      arg_u16(p_args, "ref_seq")};

            written = proto_encode_nack(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_BUSY:
        {
            proto_busy_t const msg = {header, node, arg_u16(p_args, "ref_seq")};

            written = proto_encode_busy(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_ASSET_READY:
        {
            proto_asset_ready_t const msg = {header, node,
                                             arg_u32(p_args, "crc32")};

            written = proto_encode_asset_ready(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_TELEMETRY:
        {
            proto_telemetry_t const msg = {
                header, node, arg_u32(p_args, "fault_word"),
                arg_u8(p_args, "orientation"), arg_u8(p_args, "display_state")};

            written = proto_encode_telemetry(&msg, g_out, FILE_MAX);
        }
        break;

        default:
            written = 0u;
            break;
    }

    return written;
}

/**
 * @brief  Encode a message the Pico sends.
 * @param  type    Type code.
 * @param  p_args  Arguments.
 * @return Bytes written, or 0.
 */
static uint32_t encode_coordinator (uint32_t type, vector_args_t const * p_args)
{
    proto_header_t const header  = arg_header(p_args);
    uint32_t             written = 0u;

    switch (type)
    {
        case PROTO_TYPE_BEACON:
        {
            proto_beacon_t const msg = {header, arg_u64(p_args, "t_p")};

            written = proto_encode_beacon(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_START_AT:
        {
            proto_start_at_t const msg = {header, arg_u64(p_args, "t_start")};

            written = proto_encode_start_at(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_RESUME_AT:
        {
            /* Masked to 16 bits; the encoder refuses above 1023. */
            proto_resume_at_t const msg = {header, arg_u64(p_args, "t_resume"),
                                           arg_u16(p_args, "frame_index")};

            written = proto_encode_resume_at(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_PAUSE:
        {
            proto_command_t const msg = {header};

            written = proto_encode_pause(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_STOP:
        {
            proto_command_t const msg = {header};

            written = proto_encode_stop(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_STATUS:
        {
            /* Masked to the member widths; the encoder checks the range. */
            proto_status_t const msg = {
                header, arg_u16(p_args, "readiness"), arg_u16(p_args, "online"),
                arg_u8(p_args, "state"), arg_s8(p_args, "score"),
                arg_u8(p_args, "round_state"), arg_u8(p_args, "level"),
                arg_u8(p_args, "missed"), arg_u8(p_args, "wrong"),
                arg_u8(p_args, "shown"), arg_u8(p_args, "time_left_s"),
                arg_u16(p_args, "reaction_ms")};

            written = proto_encode_status(&msg, g_out, FILE_MAX);
        }
        break;

        default:
            written = 0u;
            break;
    }

    return written;
}

/**
 * @brief  Encode a whack-a-mole row (TAP, MOLE_ON, FEEDBACK, GAME_CONTROL,
 *         WALL_PICTURE, ACK).
 * @param  type    Type code.
 * @param  p_args  Arguments.
 * @return Bytes written, or 0.
 */
static uint32_t encode_game (uint32_t type, vector_args_t const * p_args)
{
    proto_header_t const header  = arg_header(p_args);
    uint8_t const        node    = arg_u8(p_args, "node_id");
    uint32_t             written = 0u;

    switch (type)
    {
        case PROTO_TYPE_TAP:
        {
            proto_tap_t const msg = {header, node, arg_u16(p_args, "peak"),
                                     arg_u64(p_args, "t_tap")};

            written = proto_encode_tap(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_MOLE_ON:
        {
            proto_mole_on_t const msg = {header, node,
                                         arg_u64(p_args, "t_show"),
                                         arg_u16(p_args, "up_ms"),
                                         arg_u8(p_args, "kind")};

            written = proto_encode_mole_on(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_FEEDBACK:
        {
            proto_feedback_msg_t const msg = {header, node,
                                              arg_u8(p_args, "result")};

            written = proto_encode_feedback(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_GAME_CONTROL:
        {
            proto_game_control_t const msg = {header, node,
                                              arg_u8(p_args, "command"),
                                              arg_u8(p_args, "level"),
                                              arg_u8(p_args, "threshold")};

            written = proto_encode_game_control(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_WALL_PICTURE:
        {
            proto_wall_picture_t const msg = {
                header,
                node,
                arg_u8(p_args, "slot"),
                arg_u8(p_args, "effect"),
                arg_u8(p_args, "colour"),
                arg_u8(p_args, "repeat"),
                arg_u64(p_args, "t_show"),
                arg_u16(p_args, "dur_ms"),
                arg_u64(p_args, "mask")};

            written = proto_encode_wall_picture(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_ACK:
        {
            proto_ack_t const msg = {header, node,
                                     arg_u16(p_args, "acked_seq")};

            written = proto_encode_ack(&msg, g_out, FILE_MAX);
        }
        break;

        default:
            written = 0u;
            break;
    }

    return written;
}

/**
 * @brief  Encode a message the PC sends.
 * @param  type    Type code.
 * @param  p_args  Arguments.
 * @return Bytes written, or 0.
 */
static uint32_t encode_pc (uint32_t type, vector_args_t const * p_args)
{
    proto_header_t const header  = arg_header(p_args);
    uint32_t             written = 0u;

    switch (type)
    {
        case PROTO_TYPE_SESSION_CONFIG:
        {
            /* Masked to 8 bits; the encoder checks the range. */
            proto_session_config_t msg = {header,
                                          arg_u8(p_args, "rows"),
                                          arg_u8(p_args, "cols"),
                                          arg_u8(p_args, "policy"),
                                          {0u},
                                          arg_u16(p_args, "frame_count"),
                                          {0u}};

            arg_map(p_args, msg.node_map);
            arg_crc_list(p_args, msg.crc32);
            written = proto_encode_session_config(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_MANIFEST:
        {
            /* Masked to the member widths; the encoder checks the range. */
            proto_manifest_t const msg = {
                header, arg_u8(p_args, "format_version"),
                arg_u16(p_args, "frame_count"), arg_u32(p_args, "crc32")};

            written = proto_encode_manifest(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_ASSET_CHUNK:
        {
            uint16_t const            payload_len = arg_payload(p_args);
            proto_asset_chunk_t const msg         = {
                header, arg_u8(p_args, "chunk_index"), /* Masked. */
                payload_len, g_payload};

            written = proto_encode_asset_chunk(&msg, g_out, FILE_MAX);
        }
        break;

        case PROTO_TYPE_TELEMETRY_ACK:
        {
            /* Masked to 16 bits. */
            proto_telemetry_ack_t const msg = {header,
                                               arg_u16(p_args, "acked_seq")};

            written = proto_encode_telemetry_ack(&msg, g_out, FILE_MAX);
        }
        break;

        default:
            written = 0u;
            break;
    }

    return written;
}

/**
 * @brief  Print the 192 nibbles of a packed frame read with
 *         proto_get_bits(frame, 4k, 4) (OD-05 option (a)).
 * @param  len  Bytes in g_in.
 * @return false unless len is PROTO_FRAME_BYTES.
 */
static bool print_nibbles (uint32_t len)
{
    bool const b_ok = (PROTO_FRAME_BYTES == len);

    if (b_ok)
    {
        for (uint32_t idx = 0u; idx < FRAME_NIBBLES; idx++)
        {
            printf("%s%" PRIu64, (0u == idx) ? "" : ",",
                   proto_get_bits(g_in, NIBBLE_BITS * idx, NIBBLE_BITS));
        }

        printf("\n");
    }

    return b_ok;
}

/*** end of file ***/
