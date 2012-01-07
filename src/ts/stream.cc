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

#define SPDY_EVENT_HTTP_SUCCESS     90000
#define SPDY_EVENT_HTTP_FAILURE     90001
#define SPDY_EVENT_HTTP_TIMEOUT     90002

static int spdy_stream_io(TSCont, TSEvent, void *);
static TSMLoc make_ts_http_request(TSMBuffer, const spdy::key_value_block&);
static void print_ts_http_header(unsigned, TSMBuffer, TSMLoc);
static void send_http_txn_result(spdy_io_stream *, TSHttpTxn);
static void send_http_txn_error(spdy_io_stream *, TSHttpStatus);

typedef scoped_ts_object<TSMBuffer, TSMBufferCreate, TSMBufferDestroy> scoped_mbuffer;

struct scoped_http_header
{
    explicit scoped_http_header(TSMBuffer b)
            : header(TS_NULL_MLOC), buffer(b) {
        header = TSHttpHdrCreate(buffer);
    }

    scoped_http_header(TSMBuffer b, TSMLoc h)
            : header(h), buffer(b) {
    }

    ~scoped_http_header() {
        if (header != TS_NULL_MLOC) {
            TSHttpHdrDestroy(buffer, header);
            TSHandleMLocRelease(buffer, TS_NULL_MLOC, header);
        }
    }

    operator bool() const {
        return header != TS_NULL_MLOC;
    }

    operator TSMLoc() const {
        return header;
    }

    TSMLoc get() {
        return header;
    }

    TSMLoc release() {
        TSMLoc tmp = TS_NULL_MLOC;
        std::swap(tmp, header);
        return tmp;
    }

private:
    TSMLoc      header;
    TSMBuffer   buffer;
};

template <> std::string
stringof<inet_address>(const inet_address& inaddr) {
    return cstringof(*inaddr.saddr());
}

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

static void
populate_http_headers(
        TSMBuffer   buffer,
        TSMLoc      header,
        spdy::protocol_version version,
        spdy::key_value_block& kvblock)
{
    char status[128];
    char httpvers[sizeof("HTTP/xx.xx")];

    int vers = TSHttpHdrVersionGet(buffer, header);
    TSHttpStatus code = TSHttpHdrStatusGet(buffer, header);

    snprintf(status, sizeof(status),
            "%u %s", (unsigned)code, TSHttpHdrReasonLookup(code));
    snprintf(httpvers, sizeof(httpvers),
            "HTTP/%2u.%2u", TS_HTTP_MAJOR(vers), TS_HTTP_MINOR(vers));

    if (version == spdy::PROTOCOL_VERSION_2) {
        kvblock["status"] = status;
        kvblock["version"] = httpvers;
    } else {
        kvblock[":status"] = status;
        kvblock[":version"] = httpvers;
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

    print_ts_http_header(stream->stream_id, buffer.get(), header);

    if (!http_method_is_supported(buffer.get(), header.get())) {
        send_http_txn_error(stream, TS_HTTP_STATUS_METHOD_NOT_ALLOWED);
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

static void
send_http_response(
        spdy_io_stream *    stream,
        TSMBuffer           buffer,
        TSMLoc              header)
{
    TSMLoc      field;
    spdy::key_value_block kvblock;

    print_ts_http_header(stream->stream_id, buffer, header);

    field = TSMimeHdrFieldGet(buffer, header, 0);
    while (field) {
        TSMLoc next;
        std::pair<const char *, int> name;
        std::pair<const char *, int> value;

        name.first = TSMimeHdrFieldNameGet(buffer, header, field, &name.second);

        // The Connection, Keep-Alive, Proxy-Connection, and Transfer-Encoding
        // headers are not valid and MUST not be sent.
        if (strcmp(name.first, TS_MIME_FIELD_CONNECTION) == 0 ||
                strcmp(name.first, TS_MIME_FIELD_KEEP_ALIVE) == 0 ||
                strcmp(name.first, TS_MIME_FIELD_PROXY_CONNECTION) == 0 ||
                strcmp(name.first, TS_MIME_FIELD_TRANSFER_ENCODING) == 0) {
            debug_http("[%p/%u] skipping %s header",
                    stream->io, stream->stream_id, name.first);
            goto skip;
        }

        value.first = TSMimeHdrFieldValueStringGet(buffer, header,
                field, 0, &value.second);
        kvblock[std::string(name.first, name.second)] =
                std::string(value.first, value.second);

skip:
       next = TSMimeHdrFieldNext(buffer, header, field);
       TSHandleMLocRelease(buffer, header, field);
       field = next;
    }

    populate_http_headers(buffer, header, stream->version, kvblock);
    spdy_send_syn_reply(stream, kvblock);
}

static void
send_http_txn_error(
        spdy_io_stream  *   stream,
        TSHttpStatus        status)
{
    scoped_mbuffer      buffer;
    scoped_http_header  header(buffer.get());

    TSHttpHdrTypeSet(buffer.get(), header.get(), TS_HTTP_TYPE_RESPONSE);
    TSHttpHdrVersionSet(buffer.get(), header.get(), TS_HTTP_VERSION(1, 1));
    TSHttpHdrStatusSet(buffer.get(), header.get(), status);

    debug_http("[%p/%u] sending a HTTP %d result for %s %s://%s%s",
            stream->io, stream->stream_id, status,
            stream->kvblock.url().method.c_str(),
            stream->kvblock.url().scheme.c_str(),
            stream->kvblock.url().hostport.c_str(),
            stream->kvblock.url().path.c_str());

    send_http_response(stream, buffer.get(), header.get());
    spdy_send_data_frame(stream, spdy::FLAG_FIN, nullptr, 0);
}

static void
send_http_txn_result(
        spdy_io_stream  *   stream,
        TSHttpTxn           txn)
{
    int         len;
    char *      body;
    TSMBuffer   buffer;
    TSMLoc      header;

    if (TSFetchHdrGet(txn, &buffer, &header) != TS_SUCCESS) {
        spdy_send_reset_stream(stream->io, stream->stream_id,
                spdy::PROTOCOL_ERROR);
        return;
    }

    send_http_response(stream, buffer, header);

    body = TSFetchRespGet(txn, &len);
    if (body) {
        debug_http("[%p/%u] body %p is %d bytes", stream->io,
                stream->stream_id, body, len);
        spdy_send_data_frame(
                stream, 0 /*| spdy::FLAG_COMPRESSED*/, body, len);
    }

    spdy_send_data_frame(stream, spdy::FLAG_FIN, nullptr, 0);
}

static void
print_ts_http_header(
        unsigned    stream_id,
        TSMBuffer   buffer,
        TSMLoc      header)
{
    spdy_io_control::buffered_stream iobuf;
    int64_t nbytes;
    int64_t avail;
    const char * ptr;
    TSIOBufferBlock blk;

    TSHttpHdrPrint(buffer, header, iobuf.buffer);
    blk = TSIOBufferStart(iobuf.buffer);
    avail = TSIOBufferBlockReadAvail(blk, iobuf.reader);
    ptr = (const char *)TSIOBufferBlockReadStart(blk, iobuf.reader, &nbytes);

    debug_http("[%u] http request (%zu of %zu bytes):\n%*.*s",
            stream_id, nbytes, avail, (int)nbytes, (int)nbytes, ptr);
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
            send_http_txn_error(stream, TS_HTTP_STATUS_BAD_GATEWAY);
        }

        break;

    case SPDY_EVENT_HTTP_SUCCESS:
        send_http_txn_result(stream, (TSHttpTxn)edata);
        stream->io->reenable();
        stream->close();
        break;

    case SPDY_EVENT_HTTP_FAILURE:
        debug_http("[%p/%u] HTTP failure event", stream->io, stream->stream_id);
        send_http_txn_error(stream, TS_HTTP_STATUS_BAD_GATEWAY);
        stream->io->reenable();
        stream->close();
        break;

    case SPDY_EVENT_HTTP_TIMEOUT:
        debug_http("[%p/%u] HTTP timeout event", stream->io, stream->stream_id);
        send_http_txn_error(stream, TS_HTTP_STATUS_GATEWAY_TIMEOUT);
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
