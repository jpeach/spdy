/*
 * Copyright (c) 2011 James Peach <jamespeach@me.com>
 *
 */

#include "spdy.h"
#include <stdexcept>
#include <string.h>
#include <arpa/inet.h>

struct control_frame_header
{
    uint16_t    version;
    uint16_t    type;
    uint32_t    flags_and_length;
} __attribute((__packed__));

struct data_frame_header
{
    uint32_t    stream_id;
    uint32_t    flags_and_length;
} __attribute((__packed__));

spdy::message_header spdy::message_header::parse(
        const uint8_t __restrict * ptr, size_t len)
{
    message_header header;

    if (len < message_header::size) {
        throw std::runtime_error(std::string("short header"));
    }

    header.is_control = ((*ptr) & 0x80u) ? true : false;
    if (header.is_control) {
        control_frame_header control;
        memcpy(&control, ptr, sizeof(control));
        header.control.version = ntohs(control.version) & 0x7fffu;
        header.control.type = ntohs(control.type);
        header.flags = ntohl(control.flags_and_length) >> 24;
        header.datalen = ntohl(control.flags_and_length) & 0x00ffffffu;
    } else {
        data_frame_header data;
        memcpy(&data, ptr, sizeof(data));
        header.data.stream_id = ntohl(data.stream_id) & 0x80000000u;
        header.flags = ntohl(data.flags_and_length) >> 24;
        header.datalen = ntohl(data.flags_and_length) & 0x00ffffffu;
    }

    return header;
}

/* vim: set sw=4 ts=4 tw=79 et : */
