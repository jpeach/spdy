Apache Traffic Server SPDY Protocol Plugin
==========================================

A SPDY protocol plugin for [Apache Traffic
Server](http://trafficserver.apache.org). This plugin implements
the SPDY/2 protocol and transforms incoming SPDY client requests
to HTTP/1.1 origin server requests.

Installation and Configuration
==============================

To install, copy spdy.so into the TrafficServer plugins directory and add the
following to plugin.config, where PORT is the port to listen on:

    spdy.so [OPTIONS]

Options:

    *--system-resolver*
    > Use the system's DNS resolver instead of the Traffic Server DNS resolver.
    > This has the advantage of being able to resolve Bonjour names and
    > /etc/hosts entries and the disadvantage of being a blocking API that will
    > hold down a Traffic Server thread.

To enable debug, configure the spdy diagnostig tag by adding the follosing to
recods.config:

    CONFIG proxy.config.diags.debug.tags STRING spdy.*

Valid SPDY debugging tags are:
    spdy.protocol   - SPDY protocol logging
    spdy.plugin     - SPDY plugin lifecycle
    spdy.http       - HTTP client request processing

HTTP Semantics
==============

301 Moved Permanently

Currently, we just return whatever response the server sends us.
This seems like the right thing to do and none of the SPDY
specifications suggest doing otherwise. However, Chrome does not
actually follow redirections that come via the SPDY channel. It's
a pretty atrocious user experience to have to manually deal with
redirections.

SPDY Version Support
====================

The plugin implements SPDY/2. It's unlikely to ever support SPDY/3, but
probably I'll get around to SPDY/4 at some point.

Testing with Google Chrome
==========================

Use the following command-line to have Chrome proxy all requests through the
SPDY proxy at localhost:9999:

    chrome --use-spdy=no-ssl --host-resolver-rules="MAP * localhost:9999"

Resources
=========

    * http://mbelshe.github.com/SPDY-Specification/draft-mbelshe-spdy-00.xml
    * http://www.chromium.org/spdy/spdy-protocol/spdy-protocol-draft2
    * http://www.chromium.org/spdy/spdy-tools-and-debugging
    * http://technotes.googlecode.com/git/nextprotoneg.html

To Do
=====

* A raw memory container that we can decompress bytes straight into.
  TSIOBuffer() is too coupled to the ATS IO implementation to be
  nice and generic, and std::vector zeros when you resize so you
  can't combine capacity() and size() nicely.

* SPDY protocol versioning. SPDY/2 and SPDY/3 are incompatible in
  a number of ways (eg. SPDY/3 widens some fields to 32bits).

* Err, protocol error handling. That would help.

* Implement request caching using TSCacheRead() and TSCacheWrite().

* Support host:port specification by parsing the port from the SPDY
  host header.
