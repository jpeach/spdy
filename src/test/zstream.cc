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
#include <spdy/spdy.h>
#include <assert.h>
#include <string.h>
#include <vector>
#include <array>
#include <random>
#include <algorithm>

#define CHUNKSIZE 128

// Test initial state invariants.
void initstate()
{
    spdy::zstream<spdy::compress> zin;
    spdy::zstream<spdy::decompress> zout;

//    assert(zin.drained());
//    assert(zout.drained());
}

// Test basic compress/decompress cycle.
void roundtrip()
{
    ssize_t ret;
    char text[CHUNKSIZE];
    char inbuf[CHUNKSIZE];
    char outbuf[CHUNKSIZE];

    spdy::zstream<spdy::compress> zin;
    spdy::zstream<spdy::decompress> zout;

    memset(text, 0xaaaaaaaa, sizeof(text));
    memset(inbuf, 0xaaaaaaaa, sizeof(inbuf));

    memset(outbuf, 0, sizeof(outbuf));
    zin.input(inbuf, sizeof(inbuf));
    ret = zin.consume(outbuf, sizeof(outbuf));
    assert(ret > 0); // no error

    memset(inbuf, 0, sizeof(inbuf));
    zout.input(outbuf, ret);
    ret = zout.consume(inbuf, sizeof(inbuf));
    assert(ret > 0); // no error

    assert(memcmp(text, inbuf, sizeof(inbuf)) == 0);
}

void shortbuf()
{
    ssize_t ret;
    char outbuf[8];
    std::minstd_rand0 rand0;
    spdy::zstream<spdy::compress> zin;
    std::array<std::minstd_rand0::result_type, CHUNKSIZE> text;

    // random fill so it doesn't compress well
    std::for_each(text.begin(), text.end(),
           [&rand0](decltype(text)::value_type& v) { v = rand0(); } );

    zin.input(text.data(), text.size() * sizeof(decltype(text)::value_type));
    do {
        ret = zin.consume(outbuf, sizeof(outbuf));
    } while (ret != 0);
}

void compress_kvblock()
{
    spdy::key_value_block           kvblock;
    std::vector<uint8_t>            hdrs;
    std::vector<uint8_t>            check;
    spdy::zstream<spdy::compress>   compress;
    spdy::zstream<spdy::decompress> expand;
    ssize_t nbytes, ret;

    kvblock["key1"] = "value1";
    kvblock["key2"] = "value2";
    kvblock["key3"] = "value3";
    kvblock["key4"] = "value4";

    hdrs.resize(kvblock.nbytes(spdy::PROTOCOL_VERSION_2));
    nbytes = spdy::key_value_block::marshall(spdy::PROTOCOL_VERSION_2,
            compress, kvblock, &hdrs[0], hdrs.capacity());
    hdrs.resize(nbytes);

    nbytes = 0;
    check.resize(kvblock.nbytes(spdy::PROTOCOL_VERSION_2));
    expand.input(&hdrs[0], hdrs.size());
    do {
        ret = expand.consume(&check[nbytes], check.size() - nbytes);
        nbytes += ret;
    } while (ret > 0);

    assert(ret == 0);
}

void spdy_decompress()
{
    const uint8_t pkt[] =
    {
        /* SYN_REPLY header
        0x80, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0xd8,
        0x00, 0x00, 0x00, 0x01,
        */

                    0x78, 0x9c, 0x34, 0xcf, 0x41, 0x6b,
        0xc2, 0x40, 0x10, 0x05, 0xe0, 0x01, 0xd3, 0xe2,
        0xa1, 0x56, 0xe8, 0xa9, 0x17, 0x61, 0x7f, 0x40,
        0x37, 0xee, 0x64, 0x89, 0x36, 0x11, 0x0f, 0xc1,
        0x56, 0x2f, 0xea, 0xa1, 0x49, 0xed, 0x79, 0x93,
        0x8c, 0x89, 0xa0, 0x1b, 0x49, 0x46, 0x69, 0xfe,
        0x7d, 0xa5, 0xea, 0xe9, 0xc1, 0xe3, 0xe3, 0xc1,
        0x83, 0x2e, 0xf4, 0xa2, 0x2c, 0xa3, 0x23, 0xcb,
        0x2f, 0x63, 0x0b, 0x6a, 0xe0, 0x21, 0x6d, 0xf9,
        0x12, 0x9d, 0xa8, 0x20, 0xe8, 0x20, 0x6a, 0x78,
        0x9e, 0x55, 0x96, 0xc9, 0xb2, 0x5c, 0x92, 0x2d,
        0xb8, 0x04, 0xc7, 0x0b, 0x46, 0x23, 0x78, 0xba,
        0xb7, 0x49, 0x7b, 0x24, 0x78, 0x65, 0xfa, 0xe5,
        0x61, 0xc9, 0x87, 0xfd, 0x44, 0x64, 0xa5, 0xa9,
        0x1b, 0xe2, 0xe9, 0x77, 0x32, 0x97, 0xef, 0xe0,
        0x7c, 0x18, 0x26, 0x18, 0xfc, 0x50, 0xfe, 0x26,
        0xb4, 0x12, 0xeb, 0xea, 0x2c, 0x3c, 0x85, 0x28,
        0x94, 0x1f, 0xaa, 0x20, 0xf4, 0xb5, 0x58, 0xac,
        0x12, 0xe8, 0x2d, 0x4d, 0xc3, 0x72, 0x55, 0xe5,
        0xbb, 0xed, 0x8e, 0xf2, 0x9b, 0x56, 0x81, 0x98,
        0x53, 0x7a, 0xd5, 0x38, 0x0e, 0x51, 0x87, 0xe8,
        0xff, 0xeb, 0xc7, 0x98, 0xea, 0x33, 0xd5, 0xf0,
        0x12, 0x25, 0xf1, 0x50, 0xbb, 0xe8, 0xa2, 0x3c,
        0xd9, 0x86, 0x4d, 0xba, 0x27, 0x70, 0x36, 0xa6,
        0x6e, 0xa1, 0x7f, 0xbb, 0xf4, 0x69, 0xb3, 0xcb,
        0xa6, 0x2d, 0xfe, 0x00, 0x00, 0x00, 0xff, 0xff
    };


    char outbuf[16384];
    ssize_t ret;
    spdy::zstream<spdy::decompress> zout;

    zout.input(pkt, sizeof(pkt));
    do {
        ret = zout.consume(outbuf, sizeof(outbuf));
    } while (ret > 0);

    assert(ret == 0);
}

void spdy_headers()
{

    const uint8_t pkt[] =
    {
        /* SYN_STREAM header
        0x80, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0xde,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x00,
        */
                    0x38, 0xea, 0xdf, 0xa2, 0x51, 0xb2,
        0x62, 0xe0, 0x60, 0xe0, 0x47, 0xcb, 0x5a, 0x0c,
        0x82, 0x20, 0x8d, 0x3a, 0x50, 0x9d, 0x3a, 0xc5,
        0x29, 0xc9, 0x19, 0x0c, 0x7c, 0xa8, 0xc1, 0xcf,
        0xc0, 0x68, 0xc0, 0xc0, 0x02, 0xca, 0x5c, 0x0c,
        0x5c, 0x25, 0x19, 0x89, 0x85, 0x45, 0x15, 0x05,
        0x45, 0x29, 0xf9, 0x0c, 0x6c, 0xb9, 0xc0, 0x0c,
        0x9d, 0x9f, 0xc2, 0xc0, 0xe2, 0xe1, 0xea, 0xe8,
        0xc2, 0xc0, 0x56, 0x0c, 0x4c, 0x04, 0xb9, 0xa9,
        0x40, 0x75, 0x25, 0x25, 0x05, 0x0c, 0xcc, 0x20,
        0xcb, 0x18, 0xf5, 0x19, 0xb8, 0x10, 0x39, 0x84,
        0xa1, 0xd4, 0x37, 0xbf, 0x2a, 0x33, 0x27, 0x27,
        0x51, 0xdf, 0x54, 0xcf, 0x40, 0x41, 0xc3, 0x37,
        0x31, 0x39, 0x33, 0xaf, 0x24, 0xbf, 0x38, 0xc3,
        0x5a, 0xc1, 0x13, 0x68, 0x57, 0x8e, 0x02, 0x50,
        0x40, 0xc1, 0x3f, 0x58, 0x21, 0x42, 0xc1, 0xd0,
        0x20, 0xde, 0x3c, 0xde, 0x48, 0x53, 0xc1, 0x11,
        0x18, 0x1c, 0xa9, 0xe1, 0xa9, 0x49, 0xde, 0x99,
        0x25, 0xfa, 0xa6, 0xc6, 0xa6, 0x7a, 0x46, 0x0a,
        0x00, 0x69, 0x78, 0x7b, 0x84, 0xf8, 0xfa, 0xe8,
        0x28, 0xe4, 0x64, 0x66, 0xa7, 0x2a, 0xb8, 0xa7,
        0x26, 0x67, 0xe7, 0x6b, 0x2a, 0x38, 0x67, 0x00,
        0x33, 0x7e, 0xaa, 0xbe, 0x21, 0xd0, 0x50, 0x3d,
        0x0b, 0x73, 0x13, 0x3d, 0x43, 0x03, 0x33, 0x85,
        0xe0, 0xc4, 0xb4, 0xc4, 0xa2, 0x4c, 0x88, 0x26,
        0x06, 0x76, 0xa8, 0xf7, 0x19, 0x38, 0x60, 0xa1,
        0x02, 0x00, 0x00, 0x00, 0xff, 0xff
    };

    char outbuf[16384];
    ssize_t ret;
    spdy::zstream<spdy::decompress> zout;

    zout.input(pkt, sizeof(pkt));
    do {
        ret = zout.consume(outbuf, sizeof(outbuf));
    } while (ret > 0);

    assert(ret == 0);
}

int main(void)
{
    initstate();
    roundtrip();
    shortbuf();
    compress_kvblock();
    spdy_headers();
    spdy_decompress();
    return 0;
}

/* vim: set sw=4 ts=4 tw=79 et : */
