#  Copyright (c) 2011 James Peach
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

TSROOT := /opt/ats

TSXS := $(TSROOT)/bin/tsxs
SUDO := sudo
CXX := xcrun clang++

CXXFLAGS := -std=c++0x -stdlib=libc++ -g -Wall -Wextra
LDFLAGS := -stdlib=libc++ -g
CPPFLAGS := -I$(TSROOT)/include -Isrc/lib

LinkProgram = $(CXX) $(LDFLAGS) -o $@ $^
LinkBundle = $(CXX) $(LDFLAGS) \
     -bundle -Wl,-bundle_loader,$(TSROOT)/bin/traffic_server -o $@ $^

Spdy_Objects := \
	src/ts/http.o \
	src/ts/io.o \
	src/ts/protocol.o \
	src/ts/spdy.o \
	src/ts/stream.o \
	src/ts/strings.o

LibSpdy_Objects := \
	src/lib/spdy/message.o \
	src/lib/spdy/strings.o \
	src/lib/spdy/zstream.o

LibPlatform_Objects := \
	src/lib/base/logging.o

Zlib_Test_Objects := \
	src/test/stubs.o \
	src/test/zstream.o

OBJECTS := \
	$(Spdy_Objects) \
	$(LibSpdy_Objects) \
	$(LibPlatform_Objects) \
	$(Zlib_Test_Objects)

TARGETS := spdy.so test.zlib

all: $(TARGETS)

install: spdy.so
	$(SUDO) $(TSXS) -i -o $<
#$(SUDO) $(TSROOT)/bin/trafficserver restart

spdy.so: $(Spdy_Objects) $(LibSpdy_Objects) $(LibPlatform_Objects)
	$(LinkBundle) -lz

test.zlib: $(Zlib_Test_Objects) $(LibSpdy_Objects)
	$(LinkProgram) -lz

test: test.zlib
	for t in $^ ; do ./$$t ; done

clean:
	@rm -f $(TARGETS) $(OBJECTS)
	@rm -rf *.dSYM

.PHONY: all install clean test

# vim: set ts=8 noet :
