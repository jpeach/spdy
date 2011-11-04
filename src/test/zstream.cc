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

#include <spdy/zstream.h>
#include <assert.h>

#define CHUNKSIZE 128

int main(void)
{
    int ret = 0;
    char text[CHUNKSIZE];
    char inbuf[CHUNKSIZE];
    char outbuf[CHUNKSIZE];

    memset(text, 0xaaaaaaaa, sizeof(text));
    memset(inbuf, 0xaaaaaaaa, sizeof(inbuf));

    spdy::zstream<spdy::compress> zin;
    spdy::zstream<spdy::decompress> zout;

    memset(outbuf, 0, sizeof(outbuf));
    zin.input(inbuf, sizeof(inbuf));
    ret = zin.consume(outbuf, sizeof(outbuf));
    assert(ret == Z_STREAM_END);

    memset(inbuf, 0, sizeof(inbuf));
    zout.input(outbuf, 12); // XXX
    ret = zout.consume(inbuf, sizeof(inbuf));
    assert(ret == Z_STREAM_END);

    assert(memcmp(text, inbuf, sizeof(inbuf)) == 0);

    return 0;
}

/* vim: set sw=4 ts=4 tw=79 et : */
