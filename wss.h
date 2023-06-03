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

struct wss_client;
struct wss_frame;

#ifndef WS_MAX_PAYLOAD_LENGTH /* Allow applications to override this */
	/* Max 25 MB */
#define WS_MAX_PAYLOAD_LENGTH 25 * 1024 * 1024
#endif

/* Library log levels */
#define WS_LOG_NONE 0
#define WS_LOG_ERROR 1
#define WS_LOG_WARNING 2
#define WS_LOG_DEBUG 5

/* Frame opcodes */
#define WS_OPCODE_CONTINUE	0x0
#define WS_OPCODE_TEXT		0x1
#define WS_OPCODE_BINARY	0x2
#define WS_OPCODE_CLOSE		0x8
#define WS_OPCODE_PING		0x9
#define WS_OPCODE_PONG		0xA

#define WS_OPCODE_VALID(x) (x <= WS_OPCODE_TEXT || (x >= WS_OPCODE_CLOSE && x <= WS_OPCODE_PONG))

/* Close status codes */
#define WS_CLOSE_NORMAL				1000
#define WS_CLOSE_GOING_AWAY			1001
#define WS_CLOSE_PROTOCOL_ERROR		1002
#define WS_CLOSE_UNACCEPTABLE_TYPE	1003
#define WS_CLOSE_RESERVED			1004 /* Do not send */
#define WS_CLOSE_RESERVED_NONE		1005 /* Do not send */
#define WS_CLOSE_RESERVED_ABNORMAL	1006 /* Do not send */
#define WS_CLOSE_DATA_INCONSISTENT	1007
#define WS_CLOSE_POLICY_VIOLATION	1008
#define WS_CLOSE_LARGE_PAYLOAD		1009
#define WS_CLOSE_EXTENSIONS			1010 /* Only clients can send this */
#define WS_CLOSE_UNEXPECTED			1011
#define WS_CLOSE_RESERVED_TLS		1015 /* Do not send */

/*! \brief Get the name of a WebSocket frame's opcode */
const char *wss_frame_name(struct wss_frame *frame);

/*! \brief Set a logging callback. If not set, output will go to STDERR (call wss_set_log_level(WS_LOG_NONE) to disable logging) */
void wss_set_logger(void (*logger)(int level, int bytes, const char *file, const char *function, int line, const char *msg));

/*! \brief Set the maximum log level to be logged */
void wss_set_log_level(int level);

/*! \brief Free a frame's payload */
void wss_frame_destroy(struct wss_frame *frame);

/*! \brief Clean up a client previously created using wss_client_new. client will no longer be valid after this call. */
void wss_client_destroy(struct wss_client *client);

/*!
 * \brief Create a new WebSocket client
 * \param data Custom user data, that will provided in callback functions
 * \param rfd File descriptor for reading data from the WebSocket client (e.g. a socket)
 * \param wfd File descriptor for writing data to the WebSocket client (may be the same as rfd)
 * \return NULL on failure
 * \return client on success, which should be cleaned up using client
 */
struct wss_client *wss_client_new(void *data, int rfd, int wfd);

/*!
 * \brief Read a WebSocket frame from the client
 * \param client
 * \param pollms Maximum time (in ms) to wait for activity (for a frame to begin being received)
 * \param ready 1 if you are sure that data is already pending from the client, 0 otherwise
 * \retval 0 on no frames received, -1 on failure, 1 if frame(s) successfully received and parsed
 */
int wss_read(struct wss_client *client, int pollms, int ready);

/*!
 * \brief Retrieves the current frame for a client
 * \note This function only returns a valid frame if wss_frame_read returned 1
 * \warning This structure is reused for subsequent frames received by the client.
 *          You must be finished with this memory before further frames are processed.
 *          wss_frame_destroy should be called on the frame before further processing.
 *          If you do not, then you are responsible for freeing that frame's payload using free().
 */
struct wss_frame *wss_client_frame(struct wss_client *client);

/*! \brief Get the opcode of a frame */
int wss_frame_opcode(struct wss_frame *frame);

/*!
 * \brief Get the payload of a frame
 * \return payload. If present, wss_frame_destroy should be called before receiving further frames, to avoid a memory leak.
 *                  Alternately, you can "steal" the reference to the payload and free it yourself later using free().
 * \return NULL, if no payload
 * \note Payloads include a NUL terminating character at the end for convenience of use of text payloads.
 *       This is NOT included in the payload length and should not be considered to be part of the payload.
 */
char *wss_frame_payload(struct wss_frame *frame);

/*! \brief Get the length of a frame's payload */
unsigned long wss_frame_payload_length(struct wss_frame *frame);

/*!
 * \brief Write websocket data
 * \param client
 * \param opcode Frame opcode
 * \param payload Optional payload (NULL if none)
 * \param len Length in octets of the payload
 * \retval 0 on success, -1 on failure
 */
int wss_write(struct wss_client *client, int opcode, const char *payload, size_t len);

/*!
 * \brief Get the status code associated with an error which occured when reading a frame
 * \retval 0 if no error
 * \retval Positive status code that can be passed directly to wss_close if an error occured
 */
int wss_error_code(struct wss_client *client);

/*!
 * \brief Send a CLOSE frame to the client with a particular status code
 * \param client
 * \param code Valid RFC 6455 close status code (1000-1011 or 1015)
 * \retval -1 on failure, 0 on success
 */
int wss_close(struct wss_client *client, int code);

/*!
 * \brief Get WebSocket close code sent by client
 * \param frame A close frame
 * \retval -1 if not a close frame, RFC 6455 close code on success
 */
int wss_close_code(struct wss_frame *frame);
