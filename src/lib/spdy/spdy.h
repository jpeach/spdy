/*
 * Copyright (c) 2011 James Peach <jamespeach@me.com>
 *
 */

#ifndef SPDY_H_57211D6A_F320_42E3_8205_89E651B4A5DB
#define SPDY_H_57211D6A_F320_42E3_8205_89E651B4A5DB

#include <inttypes.h>
#include <stddef.h>
#include <stdexcept>
#include <string>
#include <map>

#include "zstream.h"

namespace spdy {

    enum : unsigned {
        PROTOCOL_VERSION = 3,
        MAX_FRAME_LENGTH = (1u << 24)
    };

    struct protocol_error : public std::runtime_error {
        explicit protocol_error(const std::string& msg)
            : std::runtime_error(msg) {
        }
    };

    enum control_frame_type : unsigned {
        CONTROL_SYN_STREAM      = 1,
        CONTROL_SYN_REPLY       = 2,
        CONTROL_RST_STREAM      = 3,
        CONTROL_SETTINGS        = 4,
        CONTROL_PING            = 6,
        CONTROL_GOAWAY          = 7,
        CONTROL_HEADERS         = 8,
        CONTROL_WINDOW_UPDATE   = 9
    };

    enum error : unsigned {
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
                unsigned version;
                control_frame_type type;
            } control;
            struct {
                unsigned stream_id;
            } data;
        };

        bool        is_control;
        uint8_t     flags;
        uint32_t    datalen;

        static message_header parse(const uint8_t *, size_t);
        static size_t marshall(const message_header&, uint8_t *, size_t);
        static const unsigned size = 8; /* bytes */
    };

    // SYN_STREAM frame:
    //
    // +------------------------------------+
    // |1|    version    |         1        |
    // +------------------------------------+
    // |  Flags (8)  |  Length (24 bits)    |
    // +------------------------------------+
    // |X|           Stream-ID (31bits)     |
    // +------------------------------------+
    // |X| Associated-To-Stream-ID (31bits) |
    // +------------------------------------+
    // |  Pri | Unused | Header Count(int16)|
    // +------------------------------------+   <+
    // |     Length of name (int32)         |    | This section is the "Name/Value
    // +------------------------------------+    | Header Block", and is compressed.
    // |           Name (string)            |    |
    // +------------------------------------+    |
    // |     Length of value  (int32)       |    |
    // +------------------------------------+    |
    // |          Value   (string)          |    |
    // +------------------------------------+    |
    // |           (repeats)                |   <+

    struct syn_stream_message
    {
        unsigned stream_id;
        unsigned associated_id;
        unsigned priority;
        unsigned header_count;

        static syn_stream_message parse(const uint8_t *, size_t);
        static const unsigned size = 10; /* bytes */
    };

    // GOAWAY frame:
    //
    // +----------------------------------+
    // |1|   version    |         7       |
    // +----------------------------------+
    // | 0 (flags) |     8 (length)       |
    // +----------------------------------|
    // |X|  Last-good-stream-ID (31 bits) |
    // +----------------------------------+
    // |          Status code             |
    // +----------------------------------+

    struct goaway_message
    {
        unsigned last_stream_id;
        unsigned status_code;

        static goaway_message parse(const uint8_t *, size_t);
        static const unsigned size = 8; /* bytes */
    };

    struct rst_stream_message
    {
        unsigned stream_id;
        unsigned status_code;

        static rst_stream_message parse(const uint8_t *, size_t);
        static size_t marshall(const rst_stream_message&, uint8_t *, size_t);
        static const unsigned size = 8; /* bytes */
    };

    struct url_components
    {
        std::string method;
        std::string scheme;
        std::string hostport;
        std::string path;
        std::string version;

        bool is_complete() const {
            return !(method.empty() && scheme.empty() && hostport.empty() &&
                    path.empty() && version.empty());
        }
    };

    struct key_value_block
    {
        typedef std::map<std::string, std::string> map_type;
        typedef map_type::const_iterator const_iterator;
        typedef map_type::iterator iterator;

        map_type::size_type size() const {
            return headers.size();
        }

        bool exists(const std::string& key) const {
            return headers.find(key) != headers.end();
        }

        std::string& operator[] (const std::string& key) {
            return headers[key];
        }

        const std::string& operator[] (const std::string& key) const {
            return headers[key];
        }

        const_iterator begin() const { return headers.begin(); }
        const_iterator end() const { return headers.end(); }

        url_components& url() { return components; }
        const url_components& url() const { return components; }

        url_components components;
        mutable /* XXX */ map_type headers;

        static key_value_block parse(unsigned, zstream<decompress>&, const uint8_t *, size_t);
    };


} // namespace spdy

template <typename T> std::string stringof(const T&);

template<> std::string
stringof<spdy::control_frame_type>(const spdy::control_frame_type&);

template<> std::string
stringof<spdy::error>(const spdy::error&);

#endif /* SPDY_H_57211D6A_F320_42E3_8205_89E651B4A5DB */
/* vim: set sw=4 ts=4 tw=79 et : */
