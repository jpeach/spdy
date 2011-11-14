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
#include "io.h"

static int spdy_session_io(TSCont, TSEvent, void *);

static void
resolve_host_name(spdy_io_stream * stream, const std::string& hostname)
{
    TSCont contp;
    
    contp = TSContCreate(spdy_session_io, TSMutexCreate());
    TSContDataSet(contp, stream);

    stream->action = TSHostLookup(contp, hostname.c_str(), hostname.size());
}

static int
spdy_session_io(TSCont contp, TSEvent ev, void * edata)
{
    TSHostLookupResult dns = (TSHostLookupResult)edata;
    spdy_io_stream * stream = spdy_io_stream::get(contp);

    switch (ev) {
    case TS_EVENT_HOST_LOOKUP:
        if (dns) {
            const struct sockaddr * addr;
            addr = TSHostLookupResultAddrGet(dns);
            debug_http("resolved %s => %s",
                    stream->headers["host"].c_str(), cstringof(*addr));
        } else {
            // XXX
            // Experimentally, if the DNS lookup fails, web proxies return 502
            // Bad Gateway. We should cobble up a HTTP response to tunnel back
            // in a SYN_REPLY.
        }

        stream->action = NULL;
        break;

    default:
        debug_plugin("unexpected accept event %s", cstringof(ev));
    }

    return TS_EVENT_NONE;
}

static void
make_ts_url(const spdy::key_value_block& kvblock)
{
    scoped_ts_object<TSMBuffer, TSMBufferCreate, TSMBufferDestroy> buffer;

    TSReturnCode    tstatus;
    TSMLoc          tloc;

    const char * ptr = nullptr;
    size_t len = 0;

    tstatus = TSUrlCreate(buffer.get(), &tloc);
    tstatus = TSUrlSchemeSet(buffer.get(), tloc, ptr, len); // kv:scheme
    tstatus = TSUrlHostSet(buffer.get(), tloc, ptr, len); // kv:host
    tstatus = TSUrlPathSet(buffer.get(), tloc, ptr, len); // kv:path

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

    TSAssert(tstatus == TS_SUCCESS);
}

spdy_io_stream::spdy_io_stream(unsigned s)
    : stream_id(s), action(NULL), headers()
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
    const std::string& host(headers["host"]);
    if (host.empty()) {
        debug_protocol("XXX missing host header, return PROTOCOL_ERROR");
    }

    resolve_host_name(this, host);
}

/* vim: set sw=4 ts=4 tw=79 et : */
