/* SPDX-License-Identifier: MIT */
/**
 * @file header.h
 * @brief The message framing header used by the stream helpers.
 *
 * A framed message is `[msgtype][msglen][payload]` where `msgtype` and
 * `msglen` are MessagePack unsigned integers (1 to 3 bytes each) and the
 * payload is `msglen` bytes of encoded object.
 */
#ifndef MPSCHEMA_HEADER_H
#define MPSCHEMA_HEADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Writes a framing header.
 * @param msgtype message type (non-zero)
 * @param msglen  payload length (non-zero)
 * @param buf     output buffer
 * @param sz      in: capacity of @p buf; out: header size
 * @return ::mpschema_ok, ::mpschema_invalid_param or ::mpschema_no_space
 */
int mpschema_encode_header(uint16_t msgtype, uint16_t msglen, char *buf, size_t *sz);

/**
 * @brief Parses a framing header at the start of @p buf.
 * @param msgtype out: message type
 * @param msglen  out: payload length
 * @param off     out: header size, i.e. the payload offset
 * @param buf     input bytes
 * @param sz      number of input bytes
 * @return ::mpschema_ok, ::mpschema_incomplete if more bytes are needed, or
 *         ::mpschema_malformed
 */
int mpschema_decode_header(uint16_t *msgtype, uint16_t *msglen, size_t *off, const char *buf,
                           size_t sz);

/** @brief The largest possible header size in bytes. */
size_t mpschema_get_header_max_size(void);

#ifdef __cplusplus
}
#endif

#endif /* MPSCHEMA_HEADER_H */
