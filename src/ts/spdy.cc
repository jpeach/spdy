/*
 *  Copyright (c) 2011 James Peach
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <ts/ts.h>
#include <stdlib.h>

struct spdy_io_control
{
    spdy_io_control(TSVConn);
    ~spdy_io_control();

    TSVConn		vconn;
    TSIOBuffer		iobuf;
    TSIOBufferReader	reader;

    static spdy_io_control * get(TSCont contp) {
	return (spdy_io_control *)TSContDataGet(contp);
    }
};

spdy_io_control::spdy_io_control(TSVConn v) : vconn(v)
{
    iobuf = TSIOBufferCreate();
    reader = TSIOBufferReaderAlloc(iobuf);
}

spdy_io_control::~spdy_io_control()
{
    TSVConnClose(vconn);
    TSIOBufferReaderFree(reader);
    TSIOBufferDestroy(iobuf);
}

static int
spdy_read(TSCont contp, TSEvent event, void * edata)
{
    TSVConn	vconn;
    int		nbytes;
    spdy_io_control * io;

    switch (event) {
    case TS_EVENT_NET_ACCEPT:
	TSAssert(contp == NULL);
	vconn = (TSVConn)edata;
	io = new spdy_io_control(vconn);
	contp = TSContCreate(spdy_read, TSMutexCreate());
	TSContDataSet(contp, io);
	TSVConnRead(vconn, contp, io->iobuf, INT64_MAX);
	break;
    case TS_EVENT_VCONN_READ_READY:
    case TS_EVENT_VCONN_READ_COMPLETE:
	io = spdy_io_control::get(contp);
	// what is edata at this point?
	nbytes = TSIOBufferReaderAvail(io->reader);
	TSIOBufferReaderConsume(io->reader, nbytes);
	TSError("received %d bytes", nbytes);
	break;
    case TS_EVENT_VCONN_EOS: // fallthru
    default:
	io = spdy_io_control::get(contp);
	TSError("unexpected accept event %d", (int)event);
	TSVConnClose(io->vconn);
	delete io;
    }

    return TS_EVENT_NONE;
}

static int
spdy_accept(TSCont /* contp */, TSEvent event, void * edata)
{
    switch (event) {
    case TS_EVENT_NET_ACCEPT:
	TSError("accepting connection, go set up a session");
	return spdy_read(NULL, event, edata);
    default:
	TSError("unexpected accept event %d", (int)event);
    }

    return TS_EVENT_NONE;
}

static void
spdy_initialize(uint16_t port)
{
  TSCont    contp;
  TSAction  action;

  contp = TSContCreate(spdy_accept, TSMutexCreate());
  action = TSNetAccept(contp, port, -1 /* domain */, 1 /* accept threads */);
  if (TSActionDone(action)) {
      TSError("accept action done?");
  }
}

void
TSPluginInit(int argc, const char *argv[])
{
    int port;
    TSPluginRegistrationInfo info;

    info.plugin_name = (char *)"spdy";
    info.vendor_name = (char *)"James Peach";
    info.support_email = (char *)"jamespeach@me.com";

    if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
	TSError("[%s] Plugin registration failed", __func__);
    }

    TSError("[spdy] initializing SPDY plugin...");

    if (argc != 2) {
	TSError("[%s] Usage: spdy.so PORT", __func__);
	return;
    }

    port = atoi(argv[1]);
    if (port <= 1 || port > UINT16_MAX) {
	TSError("[%s] invalid port number: %s", __func__, argv[1]);
	return;
    }

    spdy_initialize((uint16_t)port);
}

/* vim: set tw=79 ts=4 et ai */
