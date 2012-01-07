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

#include <base/atomic.h>

struct spdy_io_stream;
struct spdy_io_control;

struct spdy_io_stream : public countable
{
    enum stream_state : unsigned {
        inactive_state, open_state, closed_state
    };

    explicit spdy_io_stream(unsigned);
    virtual ~spdy_io_stream();

    // Move kv into the stream and start processing it. Return true if the
    // stream transitions to open state.
    bool open(spdy::key_value_block& kv);
    void close();

    bool is_closed() const  { return state == closed_state; }
    bool is_open() const  { return state == open_state; }

    unsigned                stream_id;
    stream_state            state;
    spdy::protocol_version  version;
    TSAction                action;
    spdy::key_value_block   kvblock;

    spdy_io_control *       io;

    static spdy_io_stream * get(TSCont contp) {
        return (spdy_io_stream *)TSContDataGet(contp);
    }
};

struct spdy_io_control : public countable
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

    // TSVIOReenable() the associated TSVConnection.
    void reenable();

    bool                valid_client_stream_id(unsigned stream_id) const;
    spdy_io_stream *    create_stream(unsigned stream_id);
    void                destroy_stream(unsigned stream_id);

    typedef std::map<unsigned, spdy_io_stream *> stream_map_type;

    TSVConn             vconn;
    buffered_stream     input;
    buffered_stream     output;
    stream_map_type     streams;
    unsigned            last_stream_id;

    spdy::zstream<spdy::compress>   compressor;
    spdy::zstream<spdy::decompress> decompressor;

    static spdy_io_control * get(TSCont contp) {
        return (spdy_io_control *)TSContDataGet(contp);
    }
};

template <typename T, T (*Alloc)(void), TSReturnCode (*Destroy)(T)>
struct scoped_ts_object
{
    scoped_ts_object() : ts(Alloc()) {
    }

    ~scoped_ts_object() {
        Destroy(ts);
    }

    T get() const {
        return ts;
    }

    T release() {
        T tmp(nullptr);
        std::swap(ts, tmp);
        return tmp;
    }

private:
    T ts;
};

template<> std::string stringof<TSEvent>(const TSEvent&);

#endif /* IO_H_C3455D48_1D3C_49C0_BB81_844F4C7946A5 */
/* vim: set sw=4 ts=4 tw=79 et : */
