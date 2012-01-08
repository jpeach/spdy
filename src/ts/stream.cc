/*
 * Copyright (c) 2011 James Peach
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ts/ts.h>
#include <spdy/spdy.h>
#include <base/logging.h>
#include <base/inet.h>
#include "io.h"
#include "protocol.h"
#include "http.h"

#define SPDY_EVENT_HTTP_SUCCESS     90000
#define SPDY_EVENT_HTTP_FAILURE     90001
#define SPDY_EVENT_HTTP_TIMEOUT     90002

static int spdy_stream_io(TSCont, TSEvent, void *);
static TSMLoc make_ts_http_request(TSMBuffer, const spdy::key_value_block&);

static void
resolve_host_name(spdy_io_stream * stream, const std::string& hostname)
{
    TSCont contp;

    contp = TSContCreate(spdy_stream_io, TSMutexCreate());
    TSContDataSet(contp, stream);
    retain(stream);

    // XXX split the host and port and stash the port in the resulting sockaddr
    stream->action = TSHostLookup(contp, hostname.c_str(), hostname.size());
    if (TSActionDone(stream->action)) {
        stream->action = NULL;
    }
}

static bool
http_method_is_supported(
        TSMBuffer   buffer,
        TSMLoc      header)
{
    int len;
    const char * method;

    method = TSHttpHdrMethodGet(buffer, header, &len);
    if (method && strncmp(method, TS_HTTP_METHOD_GET, len) == 0) {
        return true;
    }

    return false;
}

static void
make_ts_http_url(
        TSMBuffer   buffer,
        TSMLoc      header,
        const spdy::key_value_block& kvblock)
{
    TSReturnCode    tstatus;
    TSMLoc          url;

    tstatus = TSHttpHdrUrlGet(buffer, header, &url);
    if (tstatus == TS_ERROR) {
        tstatus = TSUrlCreate(buffer, &url);
    }

    TSUrlSchemeSet(buffer, url,
            kvblock.url().scheme.data(), kvblock.url().scheme.size());
    TSUrlHostSet(buffer, url,
            kvblock.url().hostport.data(), kvblock.url().hostport.size());
    TSUrlPathSet(buffer, url,
            kvblock.url().path.data(), kvblock.url().path.size());
    TSHttpHdrMethodSet(buffer, header,
            kvblock.url().method.data(), kvblock.url().method.size());

    TSHttpHdrUrlSet(buffer, header, url);

    TSAssert(tstatus == TS_SUCCESS);
}

static TSMLoc
make_ts_http_request(
        TSMBuffer buffer,
        const spdy::key_value_block& kvblock)
{
    scoped_http_header header(buffer);

    TSHttpHdrTypeSet(buffer, header, TS_HTTP_TYPE_REQUEST);

    // XXX extract the real HTTP version header from kvblock.url()
    TSHttpHdrVersionSet(buffer, header, TS_HTTP_VERSION(1, 1));
    make_ts_http_url(buffer, header, kvblock);

    // Duplicate the header fields into the MIME header for the HTTP request we
    // are building.
    for (auto ptr(kvblock.begin()); ptr != kvblock.end(); ++ptr) {
        if (ptr->first[0] != ':') {
            TSMLoc field;

            // XXX Need special handling for duplicate headers; we should
            // append them as a multi-value

            TSMimeHdrFieldCreateNamed(buffer, header,
                    ptr->first.c_str(), -1, &field);
            TSMimeHdrFieldValueStringInsert(buffer, header, field,
                    -1, ptr->second.c_str(), -1);
            TSMimeHdrFieldAppend(buffer, header, field);
        }
    }

    return header.release();
}

static bool
initiate_client_request(
        spdy_io_stream *        stream,
        const struct sockaddr * addr,
        TSCont                  contp)

{
    scoped_mbuffer      buffer;
    spdy_io_control::buffered_stream iobuf;
    int64_t nbytes;
    const char * ptr;
    TSIOBufferBlock blk;

    scoped_http_header header(
            buffer.get(), make_ts_http_request(buffer.get(), stream->kvblock));
    if (!header) {
        return false;
    }

    debug_http_header(stream->stream_id, buffer.get(), header);

    if (!http_method_is_supported(buffer.get(), header.get())) {
        http_send_txn_error(stream, TS_HTTP_STATUS_METHOD_NOT_ALLOWED);
        return true;
    }

    // For POST requests which may contain a lot of data, we probably need to
    // do a bunch of work. Looks like the recommended path is:
    //      TSHttpConnect()
    //      TSVConnWrite() to send an HTTP request.
    //      TSVConnRead() to get the HTTP response.
    //      TSHttpParser to parse the response (if needed).

    TSFetchEvent events = {
        /* success_event_id */ SPDY_EVENT_HTTP_SUCCESS,
        /* failure_event_id */ SPDY_EVENT_HTTP_FAILURE,
        /* timeout_event_id */ SPDY_EVENT_HTTP_TIMEOUT
    };

    TSHttpHdrPrint(buffer.get(), header, iobuf.buffer);
    blk = TSIOBufferStart(iobuf.buffer);
    ptr = (const char *)TSIOBufferBlockReadStart(blk, iobuf.reader, &nbytes);

    // Keep a refcount on the SPDY stream in case the TCP connection drops
    // while the request is in-flight.
    TSFetchUrl(ptr, nbytes, addr, contp, AFTER_BODY, events);
    return true;
}

static int
spdy_stream_io(TSCont contp, TSEvent ev, void * edata)
{
    TSHostLookupResult dns = (TSHostLookupResult)edata;
    spdy_io_stream * stream = spdy_io_stream::get(contp);

    if (!stream->is_open()) {
        debug_protocol("[%p/%u] received %s on closed stream",
                stream->io, stream->stream_id, cstringof(ev));
        goto done;
    }

    switch (ev) {
    case TS_EVENT_HOST_LOOKUP:
        stream->action = nullptr;

        if (dns) {
            inet_address addr(TSHostLookupResultAddrGet(dns));
            debug_http("[%p/%u] resolved %s => %s",
                    stream->io, stream->stream_id,
                    stream->kvblock.url().hostport.c_str(), cstringof(addr));
            addr.port() = htons(80); // XXX should be parsed from hostport
            if (initiate_client_request(stream, addr.saddr(), contp)) {
                retain(stream);
                retain(stream->io);
            }

        } else {
            // Experimentally, if the DNS lookup fails, web proxies return 502
            // Bad Gateway.
            http_send_txn_error(stream, TS_HTTP_STATUS_BAD_GATEWAY);
        }

        break;

    case SPDY_EVENT_HTTP_SUCCESS:
        http_send_txn_response(stream, (TSHttpTxn)edata);
        stream->io->reenable();
        stream->close();
        break;

    case SPDY_EVENT_HTTP_FAILURE:
        debug_http("[%p/%u] HTTP failure event", stream->io, stream->stream_id);
        http_send_txn_error(stream, TS_HTTP_STATUS_BAD_GATEWAY);
        stream->io->reenable();
        stream->close();
        break;

    case SPDY_EVENT_HTTP_TIMEOUT:
        debug_http("[%p/%u] HTTP timeout event", stream->io, stream->stream_id);
        http_send_txn_error(stream, TS_HTTP_STATUS_GATEWAY_TIMEOUT);
        stream->io->reenable();
        stream->close();
        break;

    default:
        debug_plugin("unexpected stream event %s", cstringof(ev));
    }

done:
    release(stream->io);
    release(stream);
    return TS_EVENT_NONE;
}

spdy_io_stream::spdy_io_stream(unsigned s)
    : stream_id(s), state(inactive_state), action(nullptr), kvblock()
{
}

spdy_io_stream::~spdy_io_stream()
{
    if (action) {
        TSActionCancel(action);
    }
}

void
spdy_io_stream::close()
{
    if (action) {
        TSActionCancel(action);
        action = nullptr;
    }

    state = closed_state;
}

bool
spdy_io_stream::open(spdy::key_value_block& kv)
{
    if (state == inactive_state) {
        // Make sure we keep a refcount on our enclosing control block so that
        // it stays live as long as we do.
        retain(io);
        kvblock = kv;
        state = open_state;
        resolve_host_name(this, kvblock.url().hostport);
        return true;
    }

    return false;
}

/* vim: set sw=4 ts=4 tw=79 et : */
