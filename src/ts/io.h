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

#ifndef IO_H_C3455D48_1D3C_49C0_BB81_844F4C7946A5
#define IO_H_C3455D48_1D3C_49C0_BB81_844F4C7946A5

struct spdy_io_control
{
    spdy_io_control(TSVConn);
    ~spdy_io_control();

    struct buffered_stream {
        TSIOBuffer          buffer;
        TSIOBufferReader    reader;

        buffered_stream() {
            buffer = TSIOBufferCreate();
            reader = TSIOBufferReaderAlloc(buffer);
        }

        ~buffered_stream() {
            TSIOBufferReaderFree(reader);
            TSIOBufferDestroy(buffer);
        }

        void consume(size_t nbytes) {
            TSIOBufferReaderConsume(reader, nbytes);
        }

        void watermark(size_t nbytes) {
            TSIOBufferWaterMarkSet(buffer, nbytes);
        }

    };

    TSVConn         vconn;
    buffered_stream input;
    buffered_stream output;

    spdy::zstream<spdy::compress>   compressor;
    spdy::zstream<spdy::decompress> decompressor;

    static spdy_io_control * get(TSCont contp) {
        return (spdy_io_control *)TSContDataGet(contp);
    }
};

spdy_io_control::spdy_io_control(TSVConn v) : vconn(v)
{
}

spdy_io_control::~spdy_io_control()
{
    TSVConnClose(vconn);
}

template<> std::string stringof<TSEvent>(const TSEvent&);

#endif /* IO_H_C3455D48_1D3C_49C0_BB81_844F4C7946A5 */
/* vim: set sw=4 ts=4 tw=79 et : */
