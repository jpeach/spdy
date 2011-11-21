/*
 * Copyright (c) 2011 James Peach <jamespeach@me.com>
 *
 */

#include "spdy.h"
#include "zstream.h"
#include <platform/logging.h>

#include <stdexcept>
#include <vector>
#include <map>
#include <algorithm>
#include <string.h>
#include <arpa/inet.h>

// XXX for the insert and extract we assume that the compiler uses an intrinsic
// to inline the memcpy() calls. Need to verify this by examining the
// assembler.

template <typename T> T
extract(const uint8_t __restrict * &ptr) {
    T val;
    memcpy(&val, ptr, sizeof(val));
    std::advance(ptr, sizeof(val));
    return val;
}

template <> uint8_t
extract<uint8_t>(const uint8_t __restrict * &ptr) {
    return *ptr++;
}

template <typename T> void
insert(const T& val, uint8_t __restrict * &ptr) {
    memcpy(ptr, &val, sizeof(val));
    std::advance(ptr, sizeof(val));
}

static inline uint32_t
extract_stream_id(const uint8_t __restrict * &ptr)
{
    return ntohl(extract<uint32_t>(ptr)) & 0x7fffffffu;
}

static inline void
insert_stream_id(uint32_t stream_id, uint8_t __restrict * &ptr)
{
    insert<uint32_t>(htonl(stream_id & 0x7fffffffu), ptr);
}

// XXX 16 bits in SPDYv2, but 32bits in SPDYv3
static inline uint16_t
extract_string_length(const uint8_t __restrict * &ptr)
{
    return ntohs(extract<uint16_t>(ptr));
}

spdy::message_header
spdy::message_header::parse(
        const uint8_t __restrict * ptr, size_t len)
{
    message_header header;

    if (len < message_header::size) {
        throw protocol_error(std::string("short frame header"));
    }

    header.is_control = ((*ptr) & 0x80u) ? true : false;
    if (header.is_control) {
        uint32_t val;

        header.control.version = ntohs(extract<uint16_t>(ptr)) & 0x7fffu;
        header.control.type = (control_frame_type)ntohs(extract<uint16_t>(ptr));
        val = ntohl(extract<uint32_t>(ptr));
        header.flags = (val >> 24);
        header.datalen = (val & 0x00ffffffu);
    } else {
        uint32_t val;
        header.data.stream_id = extract_stream_id(ptr);
        val = ntohl(extract<uint32_t>(ptr));
        header.flags = (val >> 24);
        header.datalen = (val & 0x00ffffffu);
    }

    return header;
}

size_t
spdy::message_header::marshall(
        const message_header& msg, uint8_t __restrict * ptr, size_t len)
{
    if (len < message_header::size) {
        throw protocol_error(std::string("short message_header buffer"));
    }

    if (msg.is_control) {
        insert<uint16_t>(htonl(0x80000000u | spdy::PROTOCOL_VERSION), ptr);
        insert<uint16_t>(msg.control.type, ptr);
        insert<uint32_t>(htonl((msg.flags << 24) | (msg.datalen & 0x00ffffffu)), ptr);
    } else {
        insert_stream_id(msg.data.stream_id, ptr);
        insert<uint32_t>(htonl((msg.flags << 24) | (msg.datalen & 0x00ffffffu)), ptr);
    }

    return 0;
}

spdy::syn_stream_message
spdy::syn_stream_message::parse(
        const uint8_t __restrict * ptr, size_t len)
{
    syn_stream_message msg;

    if (len < syn_stream_message::size) {
        throw protocol_error(std::string("short syn_stream message"));
    }

    msg.stream_id = extract_stream_id(ptr);
    msg.associated_id = extract_stream_id(ptr);
    msg.priority = extract<uint8_t>(ptr) >> 5;  // top 3 bits are priority
    (void)extract<uint8_t>(ptr); // skip unused byte
    return msg;
}

spdy::goaway_message
spdy::goaway_message::parse(
        const uint8_t __restrict * ptr, size_t len)
{
    goaway_message msg;

    if (len < goaway_message::size) {
        throw protocol_error(std::string("short goaway_stream message"));
    }

    msg.last_stream_id = extract_stream_id(ptr);
    msg.status_code = extract_stream_id(ptr);
    return msg;
}

spdy::rst_stream_message
spdy::rst_stream_message::parse(
        const uint8_t __restrict * ptr, size_t len)
{
    rst_stream_message msg;

    if (len < rst_stream_message::size) {
        throw protocol_error(std::string("short rst_stream message"));
    }

    msg.stream_id = extract_stream_id(ptr);
    msg.status_code = extract_stream_id(ptr);
    return msg;
}

size_t
spdy::rst_stream_message::marshall(
        const rst_stream_message& msg, uint8_t __restrict * ptr, size_t len)
{
    if (len < rst_stream_message::size) {
        throw protocol_error(std::string("short rst_stream buffer"));
    }

    insert_stream_id(msg.stream_id, ptr);
    insert<uint32_t>(msg.status_code, ptr);
    return 8;
}

// +------------------------------------+
// | Number of Name/Value pairs (int32) |
// +------------------------------------+
// |     Length of name (int32)         |
// +------------------------------------+
// |           Name (string)            |
// +------------------------------------+
// |     Length of value  (int32)       |
// +------------------------------------+
// |          Value   (string)          |
// +------------------------------------+
// |           (repeats)                |

static spdy::zstream_error
decompress_headers(
        spdy::zstream<spdy::decompress>& decompressor,
        std::vector<uint8_t>& bytes)
{
    ssize_t nbytes;

    do {
        size_t avail;
        size_t old = bytes.size();
        bytes.resize(bytes.size() + getpagesize());
        avail = bytes.size() - old;
        nbytes = decompressor.consume(&bytes[old], avail);
        if (nbytes > 0) {
            bytes.resize(old + nbytes);
        } else {
            bytes.resize(old);
        }
    } while (nbytes > 0);

    if (nbytes < 0) {
        return (spdy::zstream_error)(-nbytes);
    }

    return spdy::z_ok;
}

static spdy::key_value_block
parse_name_value_pairs_v2(
        const uint8_t __restrict * ptr, size_t len)
{
    int32_t npairs;
    int32_t namelen;
    const uint8_t __restrict * end = ptr + len;

    spdy::key_value_block kvblock;

    if (len < sizeof(int32_t)) {
        // XXX throw
    }

    // XXX 16 bits in SPDYv2, but 32bits in SPDYv3
    npairs = ntohs(extract<int16_t>(ptr));
    if (npairs < 1) {
        //
    }

    while (npairs--) {
        std::string key;
        std::string val;
        int32_t nbytes;

        if (std::distance(ptr, end) < 8) {
            // XXX
        }

        nbytes = extract_string_length(ptr);
        if (std::distance(ptr, end) < nbytes) {
            // XXX
        }

        key.assign((const char *)ptr, nbytes);
        std::advance(ptr, nbytes);

        nbytes = extract_string_length(ptr);
        if (std::distance(ptr, end) < namelen) {
            // XXX
        }

        val.assign((const char *)ptr, nbytes);
        std::advance(ptr, nbytes);

        debug_protocol("%s => %s", key.c_str(), val.c_str());

        if (key == "host") {
            kvblock.url().hostport = val;
        } else if (key == "scheme") {
            kvblock.url().scheme = val;
        } else if (key == "url") {
            kvblock.url().path = val;
        } else if (key == "method") {
            kvblock.url().method = val;
        } else if (key == "version") {
            kvblock.url().version = val;
        } else {
            kvblock.headers[key] = val;
        }
    }

    return kvblock;
}

spdy::key_value_block
spdy::key_value_block::parse(
        unsigned                    version,
        zstream<decompress>&        decompressor,
        const uint8_t __restrict *  ptr,
        size_t                      len)
{
    std::vector<uint8_t>    bytes;
    key_value_block         kvblock;

    if (version != 2) {
        // XXX support v3 and throw a proper damn error.
        throw std::runtime_error("unsupported version");
    }

    decompressor.input(ptr, len);
    if (decompress_headers(decompressor, bytes) != z_ok) {
        // XXX
    }

    return parse_name_value_pairs_v2(&bytes[0], bytes.size());
}

size_t
spdy::key_value_block::nbytes(unsigned version) const
{
    size_t nbytes = 0;
    // Length fields are 2 bytes in SPDYv2 and 4 in later versions.
    size_t lensz = version < 3 ? 2 : 4;

    nbytes += lensz;
    for (auto ptr(begin()); ptr != end(); ++ptr) {
        nbytes += lensz + ptr->first.size();
        nbytes += lensz + ptr->second.size();
    }

    return nbytes;
}

/* vim: set sw=4 ts=4 tw=79 et : */
