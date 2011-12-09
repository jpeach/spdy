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

#include <spdy/spdy.h>
#include <base/logging.h>

template<> std::string
stringof<spdy::control_frame_type>(const spdy::control_frame_type& ev)
{
    static const detail::named_value<unsigned> control_names[] =
    {
        { "CONTROL_SYN_STREAM", 1 },
        { "CONTROL_SYN_REPLY", 2 },
        { "CONTROL_RST_STREAM", 3 },
        { "CONTROL_SETTINGS", 4 },
        { "CONTROL_PING", 6 },
        { "CONTROL_GOAWAY", 7 },
        { "CONTROL_HEADERS", 8 },
        { "CONTROL_WINDOW_UPDATE", 9}
    };

    return detail::match(control_names, (unsigned)ev);
}

template<> std::string
stringof<spdy::error>(const spdy::error& e)
{
    static const detail::named_value<unsigned> error_names[] =
    {
        { "PROTOCOL_ERROR", 1 },
        { "INVALID_STREAM", 2 },
        { "REFUSED_STREAM", 3 },
        { "UNSUPPORTED_VERSION", 4 },
        { "CANCEL", 5 },
        { "FLOW_CONTROL_ERROR", 6 },
        { "STREAM_IN_USE", 7 },
        { "STREAM_ALREADY_CLOSED", 8 }
    };

    return detail::match(error_names, (unsigned)e);
}

/* vim: set sw=4 ts=4 tw=79 et : */
