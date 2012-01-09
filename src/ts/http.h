/*
 * Copyright (c) 2012 James Peach
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

#ifndef HTTP_H_E7A06C65_4FCF_46C0_8C97_455BEB9A3DE8
#define HTTP_H_E7A06C65_4FCF_46C0_8C97_455BEB9A3DE8

struct spdy_io_stream;

void http_send_txn_response(spdy_io_stream *, TSHttpTxn);

void http_send_error(spdy_io_stream *, TSHttpStatus);
void http_send_response(spdy_io_stream *, TSMBuffer, TSMLoc);

void debug_http_header(unsigned, TSMBuffer, TSMLoc);

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

struct http_parser
{
    http_parser();
    ~http_parser();

    ssize_t parse(TSIOBufferReader);

    TSHttpParser        parser;
    scoped_mbuffer      mbuffer;
    scoped_http_header  header;
    bool                complete;
};

#endif /* HTTP_H_E7A06C65_4FCF_46C0_8C97_455BEB9A3DE8 */
/* vim: set sw=4 ts=4 tw=79 et : */