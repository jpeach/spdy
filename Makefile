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

TS := /opt/trafficserver
TSXS := $(TS)/bin/tsxs
SUDO := sudo

CXX := clang++

# Don't use libc++ yet; there's weird unexpected linker problems
# CXXFLAGS := -std=c++0x -stdlib=libc++ -g -Wall -Wextra
CXXFLAGS := -std=c++0x -g -Wall -Wextra

CPPFLAGS := \
	-I/opt/trafficserver/include \
	-Isrc/lib

LinkBundle = $(CXX) -bundle -Wl,-bundle_loader,$(TS)/bin/traffic_server -o $@ $^
LinkProgram = $(CXX) -o $@ $^

Spdy_Sources := \
	src/ts/spdy.cc \
	src/ts/strings.cc

LibSpdy_Sources := \
	src/lib/spdy/message.cc \
	src/lib/spdy/strings.cc \
	src/lib/spdy/zstream.cc

Test_Sources := \
	src/test/zstream.cc

Spdy_Objects := $(Spdy_Sources:.cc=.o)
LibSpdy_Objects := $(LibSpdy_Sources:.cc=.o)
Test_Objects := $(Test_Sources:.cc=.o)

OBJECTS := \
	$(Spdy_Objects) \
	$(LibSpdy_Objects) \
	$(Test_Objects)

TARGETS := spdy.so test.zlib

all: $(TARGETS)

install: spdy.so
	$(SUDO) $(TSXS) -i -o $<
	$(SUDO) $(TS)/bin/trafficserver restart

spdy.so: $(Spdy_Objects) $(LibSpdy_Objects)
	$(LinkBundle) -lz

test.zlib: src/test/zstream.o $(LibSpdy_Objects)
	$(LinkProgram) -lz

test: test.zlib
	for t in $^ ; do ./$$t ; done

clean:
	rm -f $(TARGETS) $(OBJECTS)
	rm -rf *.dSYM

.PHONY: all install clean test

# vim: set ts=8 noet :
