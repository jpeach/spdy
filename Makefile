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
CXXFLAGS := -std=c++0x -stdlib=libc++ -g -Wall -Wextra

CPPFLAGS := \
	-I/opt/trafficserver/include \
	-Isrc/lib

LinkBundle = $(CXX) -bundle -Wl,-bundle_loader,$(TS)/bin/traffic_server -o $@ $<

SOURCES := \
	src/ts/spdy.cc

OBJECTS := $(SOURCES:.cc=.o)
TARGETS := spdy.so

all: $(TARGETS)

install: spdy.so
	$(SUDO) $(TSXS) -i -o $<

spdy.so: $(OBJECTS)
	$(LinkBundle)

clean:
	rm -f $(TARGETS) $(OBJECTS)
	rm -rf *.dSYM

.PHONY: all install clean

# vim: set ts=8 noet :
