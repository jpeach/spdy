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
#include <spdy/spdy.h>
#include <platform/logging.h>
#include "io.h"
#include "protocol.h"

#include <algorithm>
#include <vector>
#include <sys/param.h> // MAX

void
spdy_send_reset_stream(
        spdy_io_control *   io,
        unsigned            stream_id,
        spdy::error         status)
{
    spdy::message_header hdr;
    spdy::rst_stream_message rst;

    uint8_t     buffer[spdy::message_header::size + spdy::rst_stream_message::size];
    uint8_t *   ptr = buffer;
    size_t      nbytes = 0;

    hdr.is_control = true;
    hdr.control.version = spdy::PROTOCOL_VERSION;
    hdr.control.type = spdy::CONTROL_RST_STREAM;
    hdr.flags = 0;
    hdr.datalen = spdy::rst_stream_message::size;
    rst.stream_id = stream_id;
    rst.status_code = status;

    nbytes += spdy::message_header::marshall(hdr, ptr, sizeof(buffer));
    nbytes += spdy::rst_stream_message::marshall(rst, ptr, sizeof(buffer) - nbytes);

    debug_protocol("resetting stream %u with error %s",
            stream_id, cstringof(status));
    TSIOBufferWrite(io->output.buffer, buffer, nbytes);
}

void
spdy_send_syn_reply(
        spdy_io_stream * stream,
        const spdy::key_value_block& kvblock)
{
    union {
        spdy::message_header hdr;
        spdy::syn_reply_message syn;
    } msg;

    uint8_t     buffer[
        MAX(spdy::message_header::size, spdy::syn_stream_message::size)];
    size_t      nbytes = 0;

    std::vector<uint8_t> hdrs;

    // Compress the kvblock into a temp buffer before we start. We need to know
    // the size of this so we can fill in the datalen field. Since there's no
    // way to go back and rewrite the data length into the TSIOBuffer, we need
    // to use a temporary copy.
    hdrs.resize(kvblock.nbytes(stream->version));
    nbytes = spdy::key_value_block::marshall(stream->version,
            stream->io->compressor, kvblock, &hdrs[0], hdrs.capacity());
    hdrs.resize(nbytes);
    debug_protocol("hdrs.size()=%zu", hdrs.size());

    msg.hdr.is_control = true;
    msg.hdr.control.version = stream->version;
    msg.hdr.control.type = spdy::CONTROL_SYN_REPLY;
    msg.hdr.flags = 0;
    msg.hdr.datalen = spdy::syn_reply_message::size(stream->version) + hdrs.size();
    nbytes = TSIOBufferWrite(stream->io->output.buffer, buffer,
            spdy::message_header::marshall(msg.hdr, buffer, sizeof(buffer)));
    debug_protocol("nbytes=%u", (unsigned)nbytes);

    msg.syn.stream_id = stream->stream_id;
    nbytes += TSIOBufferWrite(stream->io->output.buffer, buffer,
            spdy::syn_reply_message::marshall(stream->version,
                        msg.syn, buffer, sizeof(buffer)));
    debug_protocol("nbytes=%u", (unsigned)nbytes);

    nbytes += TSIOBufferWrite(stream->io->output.buffer, &hdrs[0], hdrs.size());
    debug_protocol("hdr.datalen=%u nbytes=%u",
            (unsigned)msg.hdr.datalen, (unsigned)nbytes);
}

void
spdy_send_data_frame(
        spdy_io_stream *    stream,
        void *              ptr,
        size_t              nbytes)
{
    spdy::message_header hdr;
    uint8_t     buffer[spdy::message_header::size];

    TSReleaseAssert(nbytes < spdy::MAX_FRAME_LENGTH);

    hdr.is_control = false;
    hdr.flags = spdy::FLAG_FIN;
    hdr.datalen = nbytes;
    hdr.data.stream_id = stream->stream_id;

    spdy::message_header::marshall(hdr, buffer, sizeof(buffer));
    TSIOBufferWrite(stream->io->output.buffer, buffer, spdy::message_header::size);
    TSIOBufferWrite(stream->io->output.buffer, ptr, nbytes);
}

/* vim: set sw=4 tw=79 ts=4 et ai : */