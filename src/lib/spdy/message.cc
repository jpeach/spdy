/*
 * Copyright (c) 2011 James Peach <jamespeach@me.com>
 *
 */

#include "spdy.h"
#include <stdexcept>
#include <string.h>
#include <arpa/inet.h>

// XXX for the insert and extract we assume that the compiler uses an intrinsic
// to inline the memcpy() calls. Need to verify this by examining the
// assembler.

template <typename T>
T extract(const uint8_t __restrict * &ptr) {
    T val;
    memcpy(&val, ptr, sizeof(val));
    std::advance(ptr, sizeof(val));
    return val;
}

template <>
uint8_t extract<uint8_t>(const uint8_t __restrict * &ptr) {
    return *ptr++;
}

template <typename T>
void insert(const T& val, uint8_t __restrict * &ptr) {
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

spdy::message_header spdy::message_header::parse(
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

size_t spdy::message_header::marshall(
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

spdy::syn_stream_message spdy::syn_stream_message::parse(
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
    msg.header_count = ntohs(extract<uint16_t>(ptr));

    return msg;
}

spdy::goaway_message spdy::goaway_message::parse(
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

spdy::rst_stream_message spdy::rst_stream_message::parse(
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

size_t spdy::rst_stream_message::marshall(
        const rst_stream_message& msg, uint8_t __restrict * ptr, size_t len)
{
    if (len < rst_stream_message::size) {
        throw protocol_error(std::string("short rst_stream buffer"));
    }

    insert_stream_id(msg.stream_id, ptr);
    insert<uint32_t>(msg.status_code, ptr);
    return 8;
}

/* vim: set sw=4 ts=4 tw=79 et : */
