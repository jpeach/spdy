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

#include "zstream.h"

namespace spdy {

const uint8_t dictionary[] =
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

#if NOTYET
unsigned long dictionary_id()
{
    unsigned long id;

    id = adler32(0L, Z_NULL, 0);
    id = adler32(id, dictionary, sizeof(dictionary));
    return id;
}
#endif

int decompress::init(z_stream * zstr)
{
    return inflateInit(zstr);
}

int decompress::transact(z_stream * zstr, int flush)
{
    int ret = inflate(zstr, flush);
    if (ret == Z_NEED_DICT) {
        // The spec says that the trailing NULL is not included in the
        // dictionary, but in practice, Chrome does include it.
        ret = inflateSetDictionary(zstr, dictionary, sizeof(dictionary));
        if (ret == Z_OK) {
            ret = inflate(zstr, flush);
        }
    }

    return ret;
}

int decompress::destroy(z_stream * zstr)
{
    return inflateEnd(zstr);
}

int compress::init(z_stream * zstr)
{
    return deflateInit(zstr, Z_DEFAULT_COMPRESSION);
}

int compress::transact(z_stream * zstr, int flush)
{
    int ret = deflate(zstr, flush);
    if (ret == Z_NEED_DICT) {
        ret = deflateSetDictionary(zstr, dictionary, sizeof(dictionary));
        if (ret == Z_OK) {
            ret = deflate(zstr, flush);
        }
    }

    return ret;
}

int compress::destroy(z_stream * zstr)
{
    return deflateEnd(zstr);
}

} // namespace spdy
/* vim: set sw=4 ts=4 tw=79 et : */
