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

#ifndef PROTOCOL_H_46E29A3D_9EE6_4C4F_A355_FF42DE19EF18
#define PROTOCOL_H_46E29A3D_9EE6_4C4F_A355_FF42DE19EF18

void
spdy_send_reset_stream(
        spdy_io_control *   io,
        unsigned            stream_id,
        spdy::error         status);

void
spdy_send_syn_reply(
        spdy_io_stream * stream,
        const spdy::key_value_block& kvblock);

void
spdy_send_data_frame(
        spdy_io_stream *    stream,
        void *              ptr,
        size_t              nbytes);

#endif /* PROTOCOL_H_46E29A3D_9EE6_4C4F_A355_FF42DE19EF18 */
/* vim: set sw=4 ts=4 tw=79 et : */