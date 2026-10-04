/* SPDX-License-Identifier: MIT */
/**
 * @file transport.h
 * @brief Sending and receiving framed messages over a socket.
 *
 * Messages are framed with the header described in header.h and are limited
 * to ::MPSCHEMA_MAX_MSG_SZ bytes including the header.
 */
#ifndef MPSCHEMA_TRANSPORT_H
#define MPSCHEMA_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Called once for every complete message received by a pipe.
 * @param ctxt     the context given to mpschema_pipe_alloc()
 * @param msgtype  the message type from the header
 * @param msgstart the payload; only valid during the call
 * @param msglen   the payload length
 * @return 0 to continue; a non-zero value stops processing and is returned
 *         from mpschema_pipe_process()
 */
typedef int (*mpschema_message_handler_t)(void *ctxt, uint16_t msgtype, char *msgstart,
                                          size_t msglen);

/**
 * @brief Writes a framed message, retrying on short writes and EINTR.
 * @return ::mpschema_ok, ::mpschema_invalid_param (empty or oversized
 *         message) or ::mpschema_io_error
 */
int mpschema_xmit_msg(int sock, uint16_t msgtype, const char *msg, size_t msglen);

/**
 * @brief Encodes @p obj with mpschema_encode_obj() and sends it with the
 *        schema's object type as message type.
 */
int mpschema_xmit_obj(int sock, const void *obj, const mpschema_t *schema);

/** Opaque receive state for one socket. */
typedef struct mpschema_pipe_ mpschema_pipe_t;

/**
 * @brief Allocates receive state for @p sock.
 * @param sock a connected stream socket, ideally non-blocking
 * @param ctxt passed back to the message handler
 * @return the pipe, or NULL on allocation failure
 */
mpschema_pipe_t *mpschema_pipe_alloc(int sock, void *ctxt);

/**
 * @brief Reads what is available on the socket and dispatches every complete
 *        message to @p handler.
 *
 * Designed for non-blocking sockets driven by an event loop: call it when the
 * socket is readable. Partial messages are kept until the next call. To keep
 * one busy peer from starving others, a call performs a bounded number of
 * reads.
 *
 * @return ::mpschema_ok when the socket has no more data for now,
 *         ::mpschema_closed if the peer closed the connection,
 *         ::mpschema_io_error on socket errors,
 *         ::mpschema_malformed on a corrupt or oversized frame,
 *         or the non-zero value returned by @p handler
 */
int mpschema_pipe_process(mpschema_pipe_t *pipe, mpschema_message_handler_t handler);

/** @brief Frees a pipe (NULL is allowed). Does not close the socket. */
void mpschema_pipe_free(mpschema_pipe_t *pipe);

#ifdef __cplusplus
}
#endif

#endif /* MPSCHEMA_TRANSPORT_H */
