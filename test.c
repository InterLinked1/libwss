/*
 * libwss -- WebSocket Server Library
 *
 * Copyright (C) 2023, Naveen Albert
 *
 * Naveen Albert <bbs@phreaknet.org>
 *
 * This program is free software, distributed under the terms of
 * the Mozilla Public License Version 2.
 */

/*! \file
 *
 * \brief WebSocket library test
 *
 * \author Naveen Albert <bbs@phreaknet.org>
 */

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <wss.h>

static void ws_log(int level, int len, const char *file, const char *function, int line, const char *buf)
{
	switch (level) {
		case WS_LOG_ERROR:
			fprintf(stderr, "%s:%d %s(): %.*s", file, line, function, len, buf);
			break;
		case WS_LOG_WARNING:
			fprintf(stderr, "%s:%d %s(): %.*s", file, line, function, len, buf);
			break;
		case WS_LOG_DEBUG:
		default: /* Debug consists of multiple levels */
			fprintf(stderr, "%s:%d %s(): %.*s", file, line, function, len, buf);
	}
}

struct custom {
	int rfd;
	int wfd;
};

static ssize_t read_cb(void *data, char *buf, size_t len)
{
	struct custom *cb = data;
	return read(cb->rfd, buf, len);
}

static ssize_t write_cb(void *data, const char *buf, size_t len)
{
	struct custom *cb = data;
	return write(cb->wfd, buf, len);
}

static int test(int io_callbacks)
{
	int res;
	struct wss_client *server, *client;
	struct wss_frame *frame;
	int upstream[2];
	int downstream[2];
	char payload[256];
	size_t payloadlen;
	struct custom serverdata, clientdata;

	wss_set_logger(ws_log);
	wss_set_log_level(WS_LOG_DEBUG + 10);

	assert(!pipe(upstream));
	assert(!pipe(downstream));

	serverdata.rfd = upstream[0];
	serverdata.wfd = downstream[1];
	server = wss_client_new(&serverdata, upstream[0], downstream[1]);
	assert(server != NULL);
	wss_set_client_type(server, WS_SERVER);

	clientdata.rfd = downstream[0];
	clientdata.wfd = upstream[1];
	client = wss_client_new(&clientdata, downstream[0], upstream[1]);
	assert(client != NULL);
	wss_set_client_type(client, WS_CLIENT);

	if (io_callbacks) {
		wss_set_io_callbacks(server, read_cb, write_cb);
		wss_set_io_callbacks(client, read_cb, write_cb);
	}

	/* Server write to client */
	payloadlen = (size_t) snprintf(payload, sizeof(payload), "%s", "{hello there}");
	wss_write(server, WS_OPCODE_TEXT, payload, payloadlen);

	/* Client read from server */
	res = wss_read(client, 250, 0);
	assert(res > 0);
	frame = wss_client_frame(client);
	assert(wss_frame_opcode(frame) == WS_OPCODE_TEXT);
	assert(wss_frame_payload_length(frame) == payloadlen);
	assert(!strcmp(wss_frame_payload(frame), payload));
	wss_frame_destroy(frame);

	/* Client write to server */
	payloadlen = (size_t) snprintf(payload, sizeof(payload), "%s", "{hello world!}");
	wss_write(client, WS_OPCODE_TEXT, payload, payloadlen);

	/* Server read from client */
	res = wss_read(server, 250, 0);
	assert(res > 0);
	frame = wss_client_frame(server);
	assert(wss_frame_opcode(frame) == WS_OPCODE_TEXT);
	assert(wss_frame_payload_length(frame) == payloadlen);
	assert(!strcmp(wss_frame_payload(frame), payload));
	wss_frame_destroy(frame);

	/* Clean up */
	wss_client_destroy(client);
	wss_client_destroy(server);
	close(upstream[0]);
	close(upstream[1]);
	close(downstream[0]);
	close(downstream[1]);
	return 0;
}

int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	fprintf(stderr, "Running WebSocket integration tests\n");
	test(0); /* Tests witout I/O callbacks */
	test(1); /* Tests with I/O callbacks */
	fprintf(stderr, "Tests completed successfully\n");
}
