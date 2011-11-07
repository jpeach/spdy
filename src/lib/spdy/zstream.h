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

#include "spdy.h"
#include <zlib.h>

namespace spdy {

struct zchunk
{
    void * ptr;
    size_t len;
};

template <typename T, typename N>
zchunk make_chunk(T * ptr, N len) {
    zchunk c = { ptr, len};
    return c;
}

template <typename ZlibMechanism>
struct zstream : public ZlibMechanism
{
    zstream() {
        memset(&stream, 0, sizeof(stream));
        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;
        ZlibMechanism::init(&stream);
    }

    template <typename T, typename N>
    void input(T * ptr, N nbytes) {
        stream.next_in = (uint8_t *)ptr;
        stream.avail_in = nbytes;
    }

    // Return the number of output bytes.
    template <typename T, typename N>
    ssize_t consume(T * ptr, N nbytes) {
        int ret;
        stream.next_out = (uint8_t *)ptr;
        stream.avail_out = nbytes;

        ret = ZlibMechanism::transact(&stream, Z_SYNC_FLUSH);
        if (ret == Z_BUF_ERROR) {
            return 0;
        }

        if (ret == Z_OK || ret == Z_STREAM_END) {
            // return the number of bytes produced
            return nbytes - stream.avail_out;
        }

        return ret; // XXX error is ambiguous WRT nbytes
    }

    ~zstream() {
        ZlibMechanism::destroy(&stream);
    }

private:
    z_stream stream;
};

struct decompress
{
    int init(z_stream * zstr);
    int transact(z_stream * zstr, int flush);
    int destroy(z_stream * zstr);
};

struct compress
{
    int init(z_stream * zstr);
    int transact(z_stream * zstr, int flush);
    int destroy(z_stream * zstr);
};

} // namespace spdy
/* vim: set sw=4 ts=4 tw=79 et : */
