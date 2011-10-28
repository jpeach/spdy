/*
 * Copyright (c) 2011 James Peach <jamespeach@me.com>
 *
 */

#include "spdy.h"
#include <stdexcept>
#include <string.h>
#include <arpa/inet.h>

template <typename T>
T extract(const uint8_t __restrict * &ptr) {
    T val;
    memcpy(&val, ptr, sizeof(val));
    std::advance(ptr, sizeof(val));
    return val;
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
        header.data.stream_id = ntohl(extract<uint32_t>(ptr)) & 0x80000000u;
        val = ntohl(extract<uint32_t>(ptr));
        header.flags = (val >> 24);
        header.datalen = (val & 0x00ffffffu);
    }

    return header;
}

spdy::syn_stream_message spdy::syn_stream_message::parse(
        const uint8_t __restrict * ptr, size_t len)
{
    syn_stream_message msg;

    if (len < message_header::size) {
        throw protocol_error(std::string("short syn_stream message"));
    }

    msg.stream_id = ntohl(extract<uint32_t>(ptr)) & 0x80000000u;
    msg.associated_id = ntohl(extract<uint32_t>(ptr)) & 0x80000000u;
    msg.priority = ntohl(extract<uint16_t>(ptr)) & 0x0003;
    msg.header_count = ntohl(extract<uint16_t>(ptr));

    return msg;
}

/* vim: set sw=4 ts=4 tw=79 et : */
