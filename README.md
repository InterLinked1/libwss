# libwss

This is `libwss`, yet another WebSocket library (Y.A.W.L.). I wrote it because, in looking for a C websocket library for a project, I didn't really find any that were quite what I was looking for, so I wrote my own.

## Caveats

A few things to keep in mind:

This is a simple and relatively low-level *WebSocket server library*, **not** a *WebSocket server*. The library can be used to build a WebSocket server, but it is not itself a server. This contrasts with many other libraries that themselves can operate a server for you. This library does not do that, since it's intended for use in custom WebSocket server applications. The library implements the WebSocket protocol, and doesn't do anything else.

Because this is not a server, it does not parse HTTP requests or handle the HTTP to WebSocket upgrade for you. It is expected you do this, if needed, in your WebSocket server application *before* calling `wss_client_new`. Likewise, listening on a socket, accepting clients, and closing sockets is your responsibility.

This library does not keep track of clients for you, or manipulate them for you, in any way. Your server or application is responsible for that. For example, if you want to broadcast data received from one client to all the other ones, you could store a linked list of clients and iterate over them and write to each one.

While the type of client handling is not strictly dictated by this library, it is more geared towards multithreaded programs. However, the library by itself does not do any locking. Your application should surround calls to `wss_write` with a mutex as needed to ensure writes are properly serialized.

A common paradigm in WebSocket libraries is to provide a set of callbacks, such as `on_open`, `on_close`, and `on_message` to WebSocket applications. The library itself does not do this, but this can be implemented as a thin abstraction on top of the library. The interface is up to the application.

## Documentation

All public functions are documented in `wss.h`.

For an example of building a WebSocket server using `libwss`, you can refer to the `ws_handler` function in the [net_ws](https://github.com/InterLinked1/lbbs/blob/master/nets/net_ws.c) LBBS module, for which this library was originally written. (Do keep in mind though that while this library is licensed under the MPL, `net_ws` is licensed under GPLv2.)

## Building

This library is quite simple and has *no* external dependencies. To build, simply run:

`make`

To install the shared library, `libwss.so`, run:

`make install`

You can then link with the library in your project with `-lwss`, as you would expect.
