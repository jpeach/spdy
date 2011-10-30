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

#include "logging.h"

struct spdy_io_control
{
    spdy_io_control(TSVConn);
    ~spdy_io_control();

    TSVConn     vconn;
    TSIOBuffer      iobuf;
    TSIOBufferReader    reader;

    static spdy_io_control * get(TSCont contp) {
    return (spdy_io_control *)TSContDataGet(contp);
    }
};

spdy_io_control::spdy_io_control(TSVConn v) : vconn(v)
{
    iobuf = TSIOBufferCreate();
    reader = TSIOBufferReaderAlloc(iobuf);
    TSIOBufferWaterMarkSet(iobuf, spdy::message_header::size);
}

spdy_io_control::~spdy_io_control()
{
    TSVConnClose(vconn);
    TSIOBufferReaderFree(reader);
    TSIOBufferDestroy(iobuf);
}

static void
dispatch_spdy_control_frame(
        const spdy::message_header& header,
        spdy_io_control *           /*io*/,
        const uint8_t __restrict *  ptr,
        size_t                      nbytes)
{
    union {
        spdy::syn_stream_message stream;
    } msg;

    switch (header.control.type) {
    case spdy::CONTROL_SYN_STREAM:
        msg.stream = spdy::syn_stream_message::parse(ptr, nbytes);
        debug_protocol("%s frame stream=%u associated=%u priority=%u headers=%u",
                cstringof(header.control.type), msg.stream.stream_id,
                msg.stream.associated_id, msg.stream.priority,
                msg.stream.header_count);
        break;
    case spdy::CONTROL_SYN_REPLY:
    case spdy::CONTROL_RST_STREAM:
    case spdy::CONTROL_SETTINGS:
    case spdy::CONTROL_PING:
    case spdy::CONTROL_GOAWAY:
    case spdy::CONTROL_HEADERS:
    case spdy::CONTROL_WINDOW_UPDATE:
        debug_protocol("control frame type %s not implemented yet",
                cstringof(header.control.type));
        break;
    default:
        // SPDY 2.2.1 - MUST ignore unrecognized control frames
        TSError("ignoring invalid control frame type %u", header.control.type);
    }
}

static void
consume_spdy_frame(spdy_io_control * io)
{
    spdy::message_header    header;
    TSIOBufferBlock         blk;
    const uint8_t *         ptr;
    int64_t                 nbytes;

next_frame:

    blk = TSIOBufferStart(io->iobuf);
    ptr = (const uint8_t *)TSIOBufferBlockReadStart(blk, io->reader, &nbytes);
    TSReleaseAssert(nbytes >= spdy::message_header::size);

    header = spdy::message_header::parse(ptr, (size_t)nbytes);
    TSAssert(header.datalen > 0); // XXX

    if (header.is_control) {
        if (header.control.version != spdy::PROTOCOL_VERSION) {
            TSError("[spdy] client is version %u, but we implement version %u",
                header.control.version, spdy::PROTOCOL_VERSION);
        }

        debug_protocol("SPDY control frame, version=%u type=%s flags=0x%x, %zu bytes",
            header.control.version, cstringof(header.control.type),
            header.flags, header.datalen);
    } else {
        debug_protocol("SPDY data frame, stream=%u flags=0x%x, %zu bytes",
            header.data.stream_id, header.flags, header.datalen);
    }

    if (header.datalen >= spdy::MAX_FRAME_LENGTH) {
        // XXX
    }

    if (header.datalen <= (nbytes - spdy::message_header::size)) {
        // We have all the data in-hand ... parse it.
        TSIOBufferReaderConsume(io->reader, spdy::message_header::size);
        TSIOBufferReaderConsume(io->reader, header.datalen);

        ptr += spdy::message_header::size;

        if (header.is_control) {
            dispatch_spdy_control_frame(header, io, ptr, nbytes);
        } else {
            TSError("[spdy] no data frame support yet");
        }

        if (TSIOBufferReaderAvail(io->reader) >= spdy::message_header::size) {
            goto next_frame;
        }
    }

    // Push the high water mark to the end of the frame so that we don't get
    // called back until we have the whole thing.
    TSIOBufferWaterMarkSet(io->iobuf, spdy::message_header::size + header.datalen);
}

static int
spdy_read(TSCont contp, TSEvent ev, void * edata)
{
    int     nbytes;
    spdy_io_control * io;

    switch (ev) {
    case TS_EVENT_VCONN_READ_READY:
    case TS_EVENT_VCONN_READ_COMPLETE:
        io = spdy_io_control::get(contp);
        // what is edata at this point?
        nbytes = TSIOBufferReaderAvail(io->reader);
        debug_plugin("received %d bytes", nbytes);
        if ((unsigned)nbytes >= spdy::message_header::size) {
            consume_spdy_frame(io);
        } else {
            TSIOBufferReaderConsume(io->reader, nbytes);
        }

        // XXX frame parsing can throw. If it does, best to catch it, log it
        // and drop the connection.
        break;
    case TS_EVENT_VCONN_EOS: // fallthru
    default:
        io = spdy_io_control::get(contp);
        debug_plugin("unexpected accept event %s", cstringof(ev));
        TSVConnClose(io->vconn);
        delete io;
    }

    return TS_EVENT_NONE;
}

static int
spdy_accept(TSCont contp, TSEvent ev, void * edata)
{
    TSVConn vconn;
    spdy_io_control * io;

    TSAssert(contp == nullptr);

    switch (ev) {
    case TS_EVENT_NET_ACCEPT:
        debug_protocol("setting up SPDY session on new connection");
        vconn = (TSVConn)edata;
        io = new spdy_io_control(vconn);
        contp = TSContCreate(spdy_read, TSMutexCreate());
        TSContDataSet(contp, io);
        TSVConnRead(vconn, contp, io->iobuf, INT64_MAX);
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

    contp = TSContCreate(spdy_accept, TSMutexCreate());
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
