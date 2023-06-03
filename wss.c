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
 * \brief WebSocket Server Library
 *
 * \author Naveen Albert <bbs@phreaknet.org>
 */

#define _GNU_SOURCE 1 /* asprintf */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include <math.h>
#include <assert.h>

#include "wss.h"

/* Leftmost (MSB) to rightmost (LSB) */
#define BIT0 0x80
#define BIT1 0x40
#define BIT2 0x20
#define BIT3 0x10
#define BIT4 0x08
#define BIT5 0x04
#define BIT6 0x02
#define BIT7 0x01

enum wss_parse_state {
	WS_PARSE_INITIAL = 0,
	WS_PARSE_LENGTH,
	WS_PARSE_XLENGTH,
	WS_PARSE_XLENGTH2,
	WS_PARSE_MASK,
	WS_PARSE_PAYLOAD,
};

static const char *parse_state_name(enum wss_parse_state state)
{
	switch (state) {
		case WS_PARSE_INITIAL: return "INITIAL";
		case WS_PARSE_LENGTH: return "LENGTH";
		case WS_PARSE_XLENGTH: return "XLENGTH";
		case WS_PARSE_XLENGTH2: return "XLENGTH2";
		case WS_PARSE_MASK: return "MASK";
		case WS_PARSE_PAYLOAD: return "PAYLOAD";
	}
	return "";
}

struct wss_frame {
	unsigned int fin:1;		/*!< FIN (Final fragment in message) */
	unsigned int rsv1:1;	/*!< RSV1: 0 unless defined by extension */
	unsigned int rsv2:1;	/*!< RSV2: 0 unless defined by extension */
	unsigned int rsv3:1;	/*!< RSV3: 0 unless defined by extension */
	unsigned int opcode:4;	/*!< Opcode */
	unsigned int masked:1;	/*!< Masked? (used for data from client to server, only) */
	unsigned long length;	/*!< Payload length */
	char key[4];			/*!< Masking key (4 bytes) */
	char *data;
	/* Frame parsing */
	char buf[8];
	enum wss_parse_state state;
	unsigned int maxread;
	unsigned int datapos;
};

struct wss_client {
	int rfd;
	int wfd;
	void *data;
	struct wss_frame frame;
	unsigned short int closecode;	/*!< Close code on errors */
};

static const char *opcode_name(int opcode)
{
	switch (opcode) {
		case WS_OPCODE_CONTINUE: return "CONTINUE";
		case WS_OPCODE_TEXT: return "TEXT";
		case WS_OPCODE_BINARY: return "BINARY";
		case WS_OPCODE_CLOSE: return "CLOSE";
		case WS_OPCODE_PING: return "PING";
		case WS_OPCODE_PONG: return "PONG";
	}
	return "Invalid";
}

const char *wss_frame_name(struct wss_frame *frame)
{
	return opcode_name(frame->opcode);
}

static void (*logger_cb)(int level, int bytes, const char *file, const char *function, int line, const char *msg) = NULL;
static int loglevel = WS_LOG_NONE;

#define wss_debug(level, fmt, ...) wss_log(level + WS_LOG_DEBUG, fmt, ## __VA_ARGS__)

#define wss_log(level, fmt, ...) __wss_log(level, __FILE__, __func__, __LINE__, fmt, ## __VA_ARGS__)

static void __attribute__ ((format (printf, 5, 6))) __wss_log(int level, const char *file, const char *function, int line, const char *fmt, ...)
{
	va_list ap;
	int len;
	char *buf;

	if (level > loglevel) {
		return;
	}

	va_start(ap, fmt);
	len = vasprintf(&buf, fmt, ap);
	va_end(ap);

	if (len < 0) {
		return;
	}

	if (logger_cb) {
		logger_cb(level, len, file, function, line, buf);
	} else {
		/* Log to STDERR */
		int res = write(STDERR_FILENO, buf, len);
		(void) res;
	}
	free(buf);
}

void wss_set_logger(void (*logger)(int level, int bytes, const char *file, const char *function, int line, const char *msg))
{
	logger_cb = logger;
}

void wss_set_log_level(int level)
{
	loglevel = level;
}

static void frame_init(struct wss_frame *frame)
{
	memset(frame, 0, sizeof(struct wss_frame));
	frame->state = WS_PARSE_INITIAL;
	frame->maxread = 2; /* Max 2 bytes for the first part of the frame */
	frame->datapos = 0;
}

void wss_frame_destroy(struct wss_frame *frame)
{
	if (frame->data) {
		free(frame->data);
		frame->data = NULL;
	}
}

void wss_client_destroy(struct wss_client *client)
{
	wss_frame_destroy(&client->frame);
	free(client);
}

struct wss_client *wss_client_new(void *data, int rfd, int wfd)
{
	/* Since this is a library, the structure shouldn't be stack allocated, for ABI */
	struct wss_client *client = calloc(1, sizeof(*client));
	if (client) {
		client->rfd = rfd;
		client->wfd = wfd;
		client->data = data;
	}
	return client;
}

#define WS_PARSE_NEXT(consumed, newstate, bytes) \
	pos += consumed; \
	res -= consumed; \
	frame->state = newstate; \
	frame->maxread = bytes; \
	frame->datapos = 0; /* Reset */ \
	if (!res) { \
		break; \
	} \

#define WS_NEED_BYTES(c) \
	frame->datapos += res; \
	if (frame->datapos < c) { \
		/* Incomplete */ \
		frame->maxread = c - frame->datapos; \
		wss_debug(4, "Incomplete read (want %d bytes, have %d so far, want %d more\n", c, frame->datapos, frame->maxread); \
		return 0; \
	}

/*! \note This function is blocking. Precede with poll() if needed */
static int frame_internal_read(struct wss_client *client, struct wss_frame *frame)
{
	int res;
	int pos = 0;

	/* Assume that some amount of data is available and read will at least return immediately. */
	assert(frame->maxread <= sizeof(frame->buf) - frame->datapos); /* or buffer overflow */
	res = read(client->rfd, frame->buf + frame->datapos, frame->maxread);
	if (res <= 0) {
		wss_debug(1, "WebSocket client read returned %d: %s\n", res, strerror(errno));
		return -1;
	}

	wss_debug(5, "WebSocket client parse state now %s, read %d bytes\n", parse_state_name(frame->state), res);

	/* Process the data received. Each read must return at least 1 byte... */
	switch (frame->state) {
		case WS_PARSE_INITIAL:
			frame->fin = (frame->buf[pos] & BIT0) == BIT0;
			frame->rsv1 = (frame->buf[pos] & BIT1) == BIT1;
			frame->rsv2 = (frame->buf[pos] & BIT2) == BIT2;
			frame->rsv3 = (frame->buf[pos] & BIT3) == BIT3;
			if (frame->rsv1 || frame->rsv2 || frame->rsv3) {
				wss_log(WS_LOG_ERROR, "RSV bit(s) must be low\n");
				client->closecode = WS_CLOSE_PROTOCOL_ERROR;
				return 1;
			}
			frame->opcode = frame->buf[pos] & 0x0f; /* Lower half of the byte */
			if (!WS_OPCODE_VALID(frame->opcode)) {
				wss_log(WS_LOG_ERROR, "Invalid opcode received: %02X\n", frame->opcode);
				client->closecode = WS_CLOSE_PROTOCOL_ERROR;
				return -1;
			}
			WS_PARSE_NEXT(1, WS_PARSE_LENGTH, 1);
			/* Fallthrough */
		case WS_PARSE_LENGTH:
			frame->masked = (frame->buf[pos] & BIT0) == BIT0;
			if (!frame->masked) {
				wss_log(WS_LOG_ERROR, "Client data is not masked, aborting\n");
				client->closecode = WS_CLOSE_PROTOCOL_ERROR;
				return -1;
			}
			frame->length = frame->buf[pos] & 0x7f;
			if (frame->length == 127) {
				/* Need 64 more bits (8 bytes) - most significant bit must be 0 */
				WS_PARSE_NEXT(1, WS_PARSE_XLENGTH2, 8);
			} else if (frame->length == 126) {
				/* Need 16 more bits (2 bytes) */
				WS_PARSE_NEXT(1, WS_PARSE_XLENGTH, 2);
			} else {
				/* We're done, that is the length */
				WS_PARSE_NEXT(1, WS_PARSE_MASK, 1);
			}
			/* Fallthrough */
		case WS_PARSE_XLENGTH: /* Extended payload length */
			/* 2 bytes contain the length */
			WS_NEED_BYTES(2);
			frame->length = ntohs(*((unsigned int *) (frame->buf)));
			WS_PARSE_NEXT(2, WS_PARSE_MASK, 4);
			break;
		case WS_PARSE_XLENGTH2: /* Extended payload length continued, if length == 127 */
			/* Same as XLENGTH, but 8 bytes */
			WS_NEED_BYTES(8);
			frame->length = ntohl(*((unsigned int *) (frame->buf)));
			if (frame->length > pow(2, 63) - 1) {
				wss_log(WS_LOG_ERROR, "Frame length %lu is too large\n", frame->length); /* Highest order bit must be 0 */
				return -1;
			}
			WS_PARSE_NEXT(8, WS_PARSE_MASK, 4);
			break;
		case WS_PARSE_MASK:
			/* If we got here, the next 4 bytes MUST be the masking key. We'd have bailed out already otherwise. */
			WS_NEED_BYTES(4);
			memcpy(frame->key, frame->buf, 4);
			WS_PARSE_NEXT(4, WS_PARSE_PAYLOAD, frame->length);
			break;
		case WS_PARSE_PAYLOAD:
			break; /* Never used */
		/* No default */
	}
	return 0;
}

static int read_payload(struct wss_client *client, struct wss_frame *frame)
{
	unsigned long already = 0;
	unsigned long length = frame->length;

	if (frame->opcode == WS_OPCODE_CONTINUE) {
		char *newbuf = realloc(client->frame.data, client->frame.length + length + 1);
		if (!newbuf) {
			free(client->frame.data);
			wss_log(WS_LOG_ERROR, "realloc failed\n");
			client->closecode = WS_CLOSE_UNEXPECTED;
			return -1;
		}
		client->frame.data = newbuf;
		client->frame.length += length;
	} else {
		frame->data = malloc(length + 1); /* If it's text, make it null terminated */
	}
	client->frame.data[client->frame.length] = '\0'; /* For text payloads, null terminate. Don't subtract 1, the buffer is already +1 larger. */
	if (!frame->data) {
		wss_log(WS_LOG_ERROR, "malloc failed\n");
		client->closecode = WS_CLOSE_UNEXPECTED;
		return -1;
	}
	while (length > 0) {
		unsigned long i = already;
		/* XXX poll before? */
		int res = read(client->rfd, frame->data + already, length);
		if (res <= 0) {
			wss_debug(1, "WebSocket client read returned %d: %s\n", res, strerror(errno));
			client->closecode = WS_CLOSE_PROTOCOL_ERROR;
			return -1;
		}
		/* Unmask the data received */
		already += res;
		length -= res;
		for (; i < already; i++) {
			frame->data[i] = frame->data[i] ^ frame->key[i % 4];
		}
	}
	return 0;
}

int wss_read(struct wss_client *client, int pollms, int ready)
{
	int res;
	struct pollfd pfd;
	struct wss_frame *frame, frame2;
	unsigned long full_length = 0;

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = client->rfd;
	pfd.events = POLLIN;

	frame_init(&client->frame);
	frame = &client->frame;

	for (;;) {
		/* Connections must make progress. */
		pfd.revents = 0;
		if (ready) {
			/* If calling application knows data is available on this fd, skip the first poll */
			ready = 0;
		} else {
			res = poll(&pfd, 1, frame->state == WS_PARSE_INITIAL ? pollms : 1000);
			if (res <= 0) {
				wss_debug(1, "WebSocket client poll returned %d (%s)\n", res, strerror(errno));
				if (frame->state != WS_PARSE_INITIAL) {
					wss_log(WS_LOG_ERROR, "Partial WebSocket frame received (discarding)\n"); /* Possible parsing issue? */
					client->closecode = WS_CLOSE_PROTOCOL_ERROR;
				}
				break;
			}
		}
		res = frame_internal_read(client, frame);
		if (res) {
			break;
		} else if (frame->state == WS_PARSE_PAYLOAD) {
			/* End of frame_internal_read loop. Read the payload now. */
			full_length += frame->length;
			if (full_length > WS_MAX_PAYLOAD_LENGTH) {
				wss_log(WS_LOG_ERROR, "Payload length (%lu) exceeds max allowed (%u)\n", full_length, WS_MAX_PAYLOAD_LENGTH);
				client->closecode = WS_CLOSE_LARGE_PAYLOAD;
				return -1;
			}
			if (frame->length) {
				res = read_payload(client, frame);
				if (res < 0) {
					wss_log(WS_LOG_ERROR, "Partial WebSocket frame received? (length supposed to be %lu)\n", frame->length);
					client->closecode = WS_CLOSE_PROTOCOL_ERROR;
					return -1;
				}
			}
			wss_debug(3, "WebSocket %s frame received (length %lu)\n", wss_frame_name(frame), frame->length);
			if (!frame->fin && (frame->opcode == WS_OPCODE_TEXT || frame->opcode == WS_OPCODE_BINARY)) {
				/* The next frame will have more data. Read into the temp frame. */
				frame_init(&frame2);
				frame = &frame2;
				continue;
			}
			res = 1;
			break; /* Return finalized frame to the application */
		}
	}
	return res;
}

struct wss_frame *wss_client_frame(struct wss_client *client)
{
	return &client->frame;
}

int wss_frame_opcode(struct wss_frame *frame)
{
	return frame->opcode;
}

char *wss_frame_payload(struct wss_frame *frame)
{
	return frame->data;
}

unsigned long wss_frame_payload_length(struct wss_frame *frame)
{
	return frame->length;
}

static int full_write(int fd, const char *buf, unsigned int len)
{
	while (len > 0) {
		ssize_t res = write(fd, buf, len);
		if (res > 0) {
			len -= res;
		}
	}
	return len;
}

static int wss_frame_write(struct wss_client *client, int opcode, const char *payload, size_t len, int fin)
{
	char preamble[10]; /* At least 2, maximum of 10 */
	unsigned char payload_len;
	int preamble_bytes = 2;

	if (!WS_OPCODE_VALID(opcode)) {
		wss_log(WS_LOG_ERROR, "Invalid frame opcode: %d\n", opcode);
		return -1;
	}

	/* Zero allocation frame write */

	memset(&preamble, 0, 2); /* Only necessary to zero out the first 2 bytes. */
	if (fin) {
		preamble[0] |= BIT0;
	}
	/* Set the 4-byte opcode (higher order bits of opcode will be 0s) */
	preamble[0] |= opcode & 0xff;
	/* No mask in client's direction */
	/* Payload length */
	payload_len = len > 0xffff ? 127 : len > 125 ? 126 : len;
	preamble[1] = payload_len & 0x7f;
	/* Extended payload */
	if (payload_len == 126) {
		unsigned short int *xlen = (unsigned short int *) (preamble + 2);
		*xlen = htons(len);
		preamble_bytes += 2;
	} else if (payload_len == 127) {
		unsigned int *xlen = (unsigned int *) (preamble + 2);
		*xlen = htonl(len);
		preamble_bytes += 8;
	}
	wss_debug(2, "Sending WebSocket %s frame (length %lu, incl. %d header)\n", opcode_name(opcode), len, preamble_bytes);
	full_write(client->wfd, preamble, preamble_bytes);
	if (payload) {
		full_write(client->wfd, payload, len);
	}
	return 0;
}

int wss_write(struct wss_client *client, int opcode, const char *payload, size_t len)
{
	/* Writing is a lot easier than reading...
	 * First, determine if we're going to send multiple frames or not,
	 * since we need to set FIN accordingly. */
	return wss_frame_write(client, opcode, payload, len, 1);
}

int wss_error_code(struct wss_client *client)
{
	return client->closecode;
}

int wss_close(struct wss_client *client, int code)
{
	char buf[2];
	unsigned short int *status;
	if (code < 1000 && code > 1011 && code != 1015) {
		wss_log(WS_LOG_ERROR, "Invalid WebSocket close status code: %d\n", code);
		return -1;
	}
	status = (unsigned short int *) buf;
	*status = htons(code);
	return wss_write(client, WS_OPCODE_CLOSE, buf, 2);
}

int wss_close_code(struct wss_frame *frame)
{
	unsigned short int code, *data;

	if (frame->opcode != WS_OPCODE_CLOSE) {
		return -1;
	} else if (wss_frame_payload_length(frame) < 2) {
		return 1005; /* RFC 6455 7.1.5 */
	}

	/* RFC 6455 5.5.1: First 2 bytes = 2-byte unsigned int */
	data = (unsigned short int *) wss_frame_payload(frame);
	code = ntohs(*data);
	/* Ignore anything after the first 2 bytes */
	wss_debug(1, "WebSocket close code is %d\n", code);
	return code;
}
