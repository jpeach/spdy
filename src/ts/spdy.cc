/*
 *  Copyright (c) 2011 James Peach
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <ts/ts.h>
#include <stdlib.h>
#include <spdy/spdy.h>
#include <base/logging.h>

#include "io.h"
#include "protocol.h"

static int spdy_vconn_io(TSCont, TSEvent, void *);

static void
spdy_rst_stream(
        const spdy::message_header& header,
        spdy_io_control *           io,
        const uint8_t __restrict *  ptr)
{
    spdy::rst_stream_message rst;

    rst = spdy::rst_stream_message::parse(ptr, header.datalen);

    debug_protocol("received %s frame stream=%u status_code=%s (%u)",
            cstringof(header.control.type), rst.stream_id,
            cstringof((spdy::error)rst.status_code), rst.status_code);

    io->destroy_stream(rst.stream_id);
}

static void
spdy_syn_stream(
        const spdy::message_header& header,
        spdy_io_control *           io,
        const uint8_t __restrict *  ptr)
{
    spdy::syn_stream_message    syn;
    spdy_io_stream *            stream;

    debug_protocol("received %s frame stream=%u associated=%u priority=%u headers=%zu",
            cstringof(header.control.type), syn.stream_id,
            syn.associated_id, syn.priority, kvblock.size());

    syn = spdy::syn_stream_message::parse(ptr, header.datalen);
    if (!io->valid_client_stream_id(syn.stream_id)) {
        debug_protocol("invalid stream-id %u", syn.stream_id);
        spdy_send_reset_stream(io, syn.stream_id, spdy::PROTOCOL_ERROR);
        return;
    }

    switch (header.control.version) {
    case spdy::PROTOCOL_VERSION_2: // fallthru
    case spdy::PROTOCOL_VERSION_3: break;
    default:
        debug_protocol("bad protocol version %d", header.control.version);
        spdy_send_reset_stream(io, syn.stream_id, spdy::PROTOCOL_ERROR);
        return;
    }

    spdy::key_value_block kvblock(
            spdy::key_value_block::parse(
                    (spdy::protocol_version)header.control.version,
                    io->decompressor,
                    ptr + spdy::syn_stream_message::size,
                    header.datalen - spdy::syn_stream_message::size)
    );

    if (!kvblock.url().is_complete()) {
        debug_protocol("incomplete URL");
        // XXX missing URL, protocol error
        // 3.2.1 400 Bad Request
    }

    if ((stream = io->create_stream(syn.stream_id)) == 0) {
        debug_protocol("failed to create stream %u", syn.stream_id);
        spdy_send_reset_stream(io, syn.stream_id, spdy::INVALID_STREAM);
        return;
    }

    stream->io = io;
    stream->version = (spdy::protocol_version)header.control.version;
    stream->open(kvblock);
}

static void
dispatch_spdy_control_frame(
        const spdy::message_header& header,
        spdy_io_control *           io,
        const uint8_t __restrict *  ptr)
{
    switch (header.control.type) {
    case spdy::CONTROL_SYN_STREAM:
        spdy_syn_stream(header, io, ptr);
        break;
    case spdy::CONTROL_SYN_REPLY:
    case spdy::CONTROL_RST_STREAM:
        spdy_rst_stream(header, io, ptr);
        break;
    case spdy::CONTROL_SETTINGS:
    case spdy::CONTROL_PING:
    case spdy::CONTROL_GOAWAY:
    case spdy::CONTROL_HEADERS:
    case spdy::CONTROL_WINDOW_UPDATE:
        debug_protocol("SPDY control frame, version=%u type=%s flags=0x%x, %zu bytes",
            header.control.version, cstringof(header.control.type),
            header.flags, header.datalen);
        break;
    default:
        // SPDY 2.2.1 - MUST ignore unrecognized control frames
        TSError("ignoring invalid control frame type %u", header.control.type);
    }

    io->reenable();
}

static void
consume_spdy_frame(spdy_io_control * io)
{
    spdy::message_header    header;
    TSIOBufferBlock         blk;
    const uint8_t *         ptr;
    int64_t                 nbytes;

next_frame:

    blk = TSIOBufferStart(io->input.buffer);
    ptr = (const uint8_t *)TSIOBufferBlockReadStart(blk, io->input.reader, &nbytes);
    TSReleaseAssert(nbytes >= spdy::message_header::size);

    header = spdy::message_header::parse(ptr, (size_t)nbytes);
    TSAssert(header.datalen > 0); // XXX

    if (header.is_control) {
        if (header.control.version != spdy::PROTOCOL_VERSION) {
            TSError("[spdy] client is version %u, but we implement version %u",
                header.control.version, spdy::PROTOCOL_VERSION);
        }
    } else {
        debug_protocol("SPDY data frame, stream=%u flags=0x%x, %zu bytes",
            header.data.stream_id, header.flags, header.datalen);
    }

    if (header.datalen >= spdy::MAX_FRAME_LENGTH) {
        // XXX puke
    }

    if (header.datalen <= (nbytes - spdy::message_header::size)) {
        // We have all the data in-hand ... parse it.
        io->input.consume(spdy::message_header::size);
        io->input.consume(header.datalen);

        ptr += spdy::message_header::size;

        if (header.is_control) {
            dispatch_spdy_control_frame(header, io, ptr);
        } else {
            TSError("[spdy] no data frame support yet");
        }

        if (TSIOBufferReaderAvail(io->input.reader) >= spdy::message_header::size) {
            goto next_frame;
        }
    }

    // Push the high water mark to the end of the frame so that we don't get
    // called back until we have the whole thing.
    io->input.watermark(spdy::message_header::size + header.datalen);
}

static int
spdy_vconn_io(TSCont contp, TSEvent ev, void * edata)
{
    TSVIO               vio = (TSVIO)edata;
    int                 nbytes;
    spdy_io_control *   io;

    (void)vio;

    // Experimentally, we recieve the read or write TSVIO pointer as the
    // callback data.
    //debug_plugin("received IO event %s, VIO=%p", cstringof(ev), vio);

    switch (ev) {
    case TS_EVENT_VCONN_READ_READY:
    case TS_EVENT_VCONN_READ_COMPLETE:
        io = spdy_io_control::get(contp);
        nbytes = TSIOBufferReaderAvail(io->input.reader);
        debug_plugin("received %d bytes", nbytes);
        if ((unsigned)nbytes >= spdy::message_header::size) {
            consume_spdy_frame(io);
        }

        // XXX frame parsing can throw. If it does, best to catch it, log it
        // and drop the connection.
        break;
    case TS_EVENT_VCONN_WRITE_READY:
    case TS_EVENT_VCONN_WRITE_COMPLETE:
        // No need to handle write events. We have already pushed all the data
        // we have into the write buffer.
        break;
    case TS_EVENT_VCONN_EOS: // fallthru
    default:
        if (ev != TS_EVENT_VCONN_EOS) {
            debug_plugin("unexpected accept event %s", cstringof(ev));
        }
        io = spdy_io_control::get(contp);
        TSVConnClose(io->vconn);
        delete io;
    }

    return TS_EVENT_NONE;
}

static int
spdy_accept_io(TSCont contp, TSEvent ev, void * edata)
{
    TSVConn vconn;
    spdy_io_control * io;

    TSVIO read_vio, write_vio;

    switch (ev) {
    case TS_EVENT_NET_ACCEPT:
        debug_protocol("accepting new SPDY session");
        vconn = (TSVConn)edata;
        io = new spdy_io_control(vconn);
        io->input.watermark(spdy::message_header::size);
        io->output.watermark(spdy::message_header::size);
        // XXX is contp leaked here?
        contp = TSContCreate(spdy_vconn_io, TSMutexCreate());
        TSContDataSet(contp, io);
        read_vio = TSVConnRead(vconn, contp, io->input.buffer, INT64_MAX);
        write_vio = TSVConnWrite(vconn, contp, io->output.reader, INT64_MAX);
        break;
    default:
        debug_plugin("unexpected accept event %s", cstringof(ev));
    }

    return TS_EVENT_NONE;
}

static void
spdy_initialize(uint16_t port)
{
    TSCont    contp;
    TSAction  action;

    contp = TSContCreate(spdy_accept_io, TSMutexCreate());
    action = TSNetAccept(contp, port, -1 /* domain */, 1 /* accept threads */);
    if (TSActionDone(action)) {
        debug_plugin("accept action done?");
    }
}

void
TSPluginInit(int argc, const char *argv[])
{
    int port;
    TSPluginRegistrationInfo info;

    info.plugin_name = (char *)"spdy";
    info.vendor_name = (char *)"James Peach";
    info.support_email = (char *)"jamespeach@me.com";

    if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
        TSError("[%s] Plugin registration failed", __func__);
    }

    debug_plugin("initializing");

    if (argc != 2) {
        TSError("[%s] Usage: spdy.so PORT", __func__);
        return;
    }

    port = atoi(argv[1]);
    if (port <= 1 || port > UINT16_MAX) {
        TSError("[%s] invalid port number: %s", __func__, argv[1]);
        return;
    }

    spdy_initialize((uint16_t)port);
}

/* vim: set sw=4 tw=79 ts=4 et ai : */
