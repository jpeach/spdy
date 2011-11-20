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
#include <platform/logging.h>
#include <netinet/in.h>
#include "io.h"

#define SPDY_EVENT_HTTP_SUCCESS     90000
#define SPDY_EVENT_HTTP_FAILURE     90001
#define SPDY_EVENT_HTTP_TIMEOUT     90002

static int spdy_session_io(TSCont, TSEvent, void *);
static TSMLoc make_ts_http_request(TSMBuffer, const spdy::key_value_block&);
static void print_ts_http_header(TSMBuffer, TSMLoc);

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

struct inet_address
{
    explicit inet_address(const struct sockaddr * addr) {
        memcpy(&sa.storage, addr, addr->sa_len);
    }

    uint16_t& port() {
        switch (sa.storage.ss_family) {
        case AF_INET:
            return sa.in.sin_port;
        case AF_INET6:
            return sa.in6.sin6_port;
        default:
            TSReleaseAssert("invalid inet address type");
            return sa.in.sin_port;
        }
    }

    const sockaddr * saddr() const {
        return &sa.sa;
    }

private:
    union {
        struct sockaddr_in  in;
        struct sockaddr_in6 in6;
        struct sockaddr     sa;
        struct sockaddr_storage storage;
    } sa;
};

template <> std::string
stringof<inet_address>(const inet_address& inaddr) {
    return cstringof(*inaddr.saddr());
}

static void
resolve_host_name(spdy_io_stream * stream, const std::string& hostname)
{
    TSCont contp;

    contp = TSContCreate(spdy_session_io, TSMutexCreate());
    TSContDataSet(contp, stream);

    // XXX split the host and port and stash the port in the resulting sockaddr
    stream->action = TSHostLookup(contp, hostname.c_str(), hostname.size());
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

    print_ts_http_header(buffer.get(), header);

    // Need the kv:version and kv:method to actually make the request. It looks
    // like the most straightforward way is to build the HTTP request by hand
    // and pump it into TSFetchUrl().

    // Apparantly TSFetchUrl may have performance problems,
    // see https://issues.apache.org/jira/browse/TS-912

    // We probably need to manually do the caching, using
    // TSCacheRead/TSCacheWrite.

    // For POST requests which may contain a lot of data, we probably need to
    // do a bunch of work. Looks like the recommended path is
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

    TSFetchUrl(ptr, nbytes, addr, contp, AFTER_BODY, events);
    return true;
}

static int
spdy_session_io(TSCont contp, TSEvent ev, void * edata)
{
    TSHostLookupResult dns = (TSHostLookupResult)edata;
    spdy_io_stream * stream = spdy_io_stream::get(contp);

    switch (ev) {
    case TS_EVENT_HOST_LOOKUP:
        if (dns) {
            inet_address addr(TSHostLookupResultAddrGet(dns));
            debug_http("resolved %s => %s",
                    stream->kvblock.url().hostport.c_str(), cstringof(addr));
            addr.port() = htons(80); // XXX should be parsed from hostport
            initiate_client_request(stream, addr.saddr(), contp);
        } else {
            // XXX
            // Experimentally, if the DNS lookup fails, web proxies return 502
            // Bad Gateway. We should cobble up a HTTP response to tunnel back
            // in a SYN_REPLY.
        }

        stream->action = NULL;
        break;

    case SPDY_EVENT_HTTP_SUCCESS:
        debug_http("HTTP success event");
        break;

    case SPDY_EVENT_HTTP_FAILURE:
        debug_http("HTTP failure event");
        break;

    case SPDY_EVENT_HTTP_TIMEOUT:
        debug_http("HTTP timeout event");
        break;

    default:
        debug_plugin("unexpected accept event %s", cstringof(ev));
    }

    return TS_EVENT_NONE;
}

static void
print_ts_http_header(TSMBuffer buffer, TSMLoc header)
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

    debug_http("http request (%zu of %zu bytes):\n%*.*s",
            nbytes, avail, (int)nbytes, (int)nbytes, ptr);
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

spdy_io_stream::spdy_io_stream(unsigned s)
    : stream_id(s), action(NULL), kvblock()
{
}

spdy_io_stream::~spdy_io_stream()
{
    if (action) {
        TSActionCancel(action);
    }
}

void
spdy_io_stream::start()
{
    resolve_host_name(this, kvblock.url().hostport);
}

/* vim: set sw=4 ts=4 tw=79 et : */
