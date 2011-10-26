/*
 * Copyright (c) 2011 James Peach <jamespeach@me.com>
 *
 */

#ifndef SPDY_H_57211D6A_F320_42E3_8205_89E651B4A5DB
#define SPDY_H_57211D6A_F320_42E3_8205_89E651B4A5DB

#include <inttypes.h>
#include <stddef.h>

#define SPDY_MAX_FRAME_LENGTH  (1u << 24)

namespace spdy {

    enum frame_type
    {
        syn_frame = 1,
        syn_reply,
        rst_stream
    };

    enum error : uint32_t
    {
        PROTOCOL_ERROR        = 1,
        INVALID_STREAM        = 2,
        REFUSED_STREAM        = 3,
        UNSUPPORTED_VERSION   = 4,
        CANCEL                = 5,
        FLOW_CONTROL_ERROR    = 6,
        STREAM_IN_USE         = 7,
        STREAM_ALREADY_CLOSED = 8
    };

    // Control frame header:
    // +----------------------------------+
    // |C| Version(15bits) | Type(16bits) |
    // +----------------------------------+
    // | Flags (8)  |  Length (24 bits)   |
    // +----------------------------------+
    // |               Data               |
    // +----------------------------------+
    //
    // Data frame header:
    // +----------------------------------+
    // |C|       Stream-ID (31bits)       |
    // +----------------------------------+
    // | Flags (8)  |  Length (24 bits)   |
    // +----------------------------------+
    // |               Data               |
    // +----------------------------------+

    struct message_header
    {
        union {
            struct {
                uint16_t    version;
                uint16_t    type;
            } control;
            struct {
                uint32_t    stream_id;
            } data;
        };

        bool        is_control;
        uint8_t     flags;
        uint32_t    datalen;

        static message_header parse(const uint8_t *, size_t);
        static const unsigned size = 8; /* bytes */
    };

} // namespace spdy

#endif /* SPDY_H_57211D6A_F320_42E3_8205_89E651B4A5DB */
/* vim: set sw=4 ts=4 tw=79 et : */
