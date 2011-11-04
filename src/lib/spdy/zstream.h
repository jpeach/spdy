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

    unsigned    &avail_out() { return stream.avail_out; }
    uint8_t *      &next_out() { return stream.next_out; }

    template <typename T, typename N>
    void input(T * ptr, N nbytes) {
        stream.next_in = (uint8_t *)ptr;
        stream.avail_in = nbytes;
    }

    template <typename T, typename N>
    int consume(T * ptr, N nbytes) {
        stream.next_out = (uint8_t *)ptr;
        stream.avail_out = nbytes;
        return ZlibMechanism::transact(&stream, Z_FINISH);
        // return is Z_STREAM_END if all input processed
    }

    ~zstream() {
        ZlibMechanism::destroy(&stream);
    }

private:
    z_stream stream;
};

struct decompress
{
    int init(z_stream * zstr) {
        return inflateInit(zstr);
    }

    int transact(z_stream * zstr, int flush) {
        return inflate(zstr, flush);
    }

    int destroy(z_stream * zstr) {
        return inflateEnd(zstr);
    }
};

struct compress
{
    int init(z_stream * zstr) {
        return deflateInit(zstr, Z_DEFAULT_COMPRESSION);
    }

    int transact(z_stream * zstr, int flush) {
        return deflate(zstr, flush);
    }

    int destroy(z_stream * zstr) {
        return deflateEnd(zstr);
    }
};

static const char dictionary[] =
"optionsgetheadpostputdeletetraceacceptaccept-charsetaccept-encodingaccept-"
"languageauthorizationexpectfromhostif-modified-sinceif-matchif-none-matchi"
"f-rangeif-unmodifiedsincemax-forwardsproxy-authorizationrangerefererteuser"
"-agent10010120020120220320420520630030130230330430530630740040140240340440"
"5406407408409410411412413414415416417500501502503504505accept-rangesageeta"
"glocationproxy-authenticatepublicretry-afterservervarywarningwww-authentic"
"ateallowcontent-basecontent-encodingcache-controlconnectiondatetrailertran"
"sfer-encodingupgradeviawarningcontent-languagecontent-lengthcontent-locati"
"oncontent-md5content-rangecontent-typeetagexpireslast-modifiedset-cookieMo"
"ndayTuesdayWednesdayThursdayFridaySaturdaySundayJanFebMarAprMayJunJulAugSe"
"pOctNovDecchunkedtext/htmlimage/pngimage/jpgimage/gifapplication/xmlapplic"
"ation/xhtmltext/plainpublicmax-agecharset=iso-8859-1utf-8gzipdeflateHTTP/1"
".1statusversionurl";

} // namespace spdy
/* vim: set sw=4 ts=4 tw=79 et : */
