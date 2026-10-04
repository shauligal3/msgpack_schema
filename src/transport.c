/* SPDX-License-Identifier: MIT */
/**
 * @file transport.c
 * @brief Framed messages over stream sockets.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#include "internal.h"

/** Upper bound on read() calls per mpschema_pipe_process() invocation. */
#define MPS_PIPE_MAX_READS 10

/* ------------------------------------------------------------------------ */
/* Sending                                                                  */
/* ------------------------------------------------------------------------ */

/* Writes all iovecs, resuming after short writes and EINTR. */
static int writev_all(int fd, struct iovec *iov, int iovcnt)
{
    while (iovcnt > 0) {
        ssize_t n = writev(fd, iov, iovcnt);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return mpschema_io_error;
        }
        /* skip fully written vectors, then advance into the partial one */
        while (iovcnt > 0 && (size_t)n >= iov->iov_len) {
            n -= (ssize_t)iov->iov_len;
            iov++;
            iovcnt--;
        }
        if (iovcnt > 0) {
            iov->iov_base = (char *)iov->iov_base + n;
            iov->iov_len -= (size_t)n;
        }
    }
    return mpschema_ok;
}

int mpschema_xmit_msg(int sock, uint16_t msgtype, const char *msg, size_t msglen)
{
    char hdr[16];
    size_t hdrlen = sizeof(hdr);
    struct iovec iov[2];
    int rc;

    if (msg == NULL || msglen == 0 || msglen > UINT16_MAX) {
        return mpschema_invalid_param;
    }
    rc = mpschema_encode_header(msgtype, (uint16_t)msglen, hdr, &hdrlen);
    if (rc != mpschema_ok) {
        return rc;
    }
    if (hdrlen + msglen > MPSCHEMA_MAX_MSG_SZ) {
        return mpschema_invalid_param;
    }

    iov[0].iov_base = hdr;
    iov[0].iov_len = hdrlen;
    iov[1].iov_base = mps_deconst(msg); /* writev() does not modify the data */
    iov[1].iov_len = msglen;

    rc = writev_all(sock, iov, 2);
    if (rc != mpschema_ok) {
        MPS_WARNING("[%s] msgtype %u len %zu sock %d: write failed: %s\n", __func__,
                    (unsigned)msgtype, msglen, sock, strerror(errno));
    }
    return rc;
}

int mpschema_xmit_obj(int sock, const void *obj, const mpschema_t *schema)
{
    char buf[MPSCHEMA_MAX_MSG_SZ];
    size_t sz = sizeof(buf);
    int rc;

    if (schema == NULL) {
        return mpschema_invalid_param;
    }
    rc = mpschema_encode_obj(obj, schema, buf, &sz);
    if (rc != mpschema_ok) {
        return rc;
    }
    return mpschema_xmit_msg(sock, schema->mps_obj_type, buf, sz);
}

/* ------------------------------------------------------------------------ */
/* Receiving                                                                */
/* ------------------------------------------------------------------------ */

struct mpschema_pipe_ {
    int sock;
    void *ctxt;
    size_t used;                   /* bytes buffered in buf */
    char buf[MPSCHEMA_MAX_MSG_SZ]; /* holds at most one partial frame at rest */
};

mpschema_pipe_t *mpschema_pipe_alloc(int sock, void *ctxt)
{
    mpschema_pipe_t *pipe = calloc(1, sizeof(*pipe));

    if (pipe != NULL) {
        pipe->sock = sock;
        pipe->ctxt = ctxt;
    }
    return pipe;
}

void mpschema_pipe_free(mpschema_pipe_t *pipe)
{
    free(pipe);
}

/* Hands every complete buffered frame to @p handler and compacts the buffer. */
static int dispatch_buffered(mpschema_pipe_t *pipe, mpschema_message_handler_t handler)
{
    while (pipe->used != 0) {
        uint16_t type = 0;
        uint16_t len = 0;
        size_t off = 0;
        size_t frame;
        int rc = mpschema_decode_header(&type, &len, &off, pipe->buf, pipe->used);

        if (rc == mpschema_incomplete) {
            return mpschema_ok;
        }
        if (rc != mpschema_ok) {
            MPS_DEBUG("[%s] sock %d: corrupt frame header\n", __func__, pipe->sock);
            return mpschema_malformed;
        }
        frame = off + len;
        if (frame > sizeof(pipe->buf)) {
            MPS_DEBUG("[%s] sock %d: frame of %zu bytes exceeds the %d byte limit\n", __func__,
                      pipe->sock, frame, MPSCHEMA_MAX_MSG_SZ);
            return mpschema_malformed;
        }
        if (pipe->used < frame) {
            return mpschema_ok; /* wait for the rest of the payload */
        }

        rc = handler(pipe->ctxt, type, pipe->buf + off, len);

        pipe->used -= frame;
        memmove(pipe->buf, pipe->buf + frame, pipe->used);

        if (rc != 0) {
            MPS_WARNING("[%s] sock %d: handler failed with %d for type %u len %u\n", __func__,
                        pipe->sock, rc, (unsigned)type, (unsigned)len);
            return rc;
        }
    }
    return mpschema_ok;
}

int mpschema_pipe_process(mpschema_pipe_t *pipe, mpschema_message_handler_t handler)
{
    int reads;
    int rc;

    if (pipe == NULL || handler == NULL) {
        return mpschema_invalid_param;
    }

    for (reads = 0; reads < MPS_PIPE_MAX_READS; reads++) {
        ssize_t n;

        rc = dispatch_buffered(pipe, handler);
        if (rc != mpschema_ok) {
            return rc;
        }

        n = read(pipe->sock, pipe->buf + pipe->used, sizeof(pipe->buf) - pipe->used);
        if (n == 0) {
            MPS_DEBUG("[%s] sock %d: connection closed by peer\n", __func__, pipe->sock);
            return mpschema_closed;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return mpschema_ok;
            }
            MPS_DEBUG("[%s] sock %d: read failed: %s\n", __func__, pipe->sock, strerror(errno));
            return mpschema_io_error;
        }
        pipe->used += (size_t)n;
    }
    return dispatch_buffered(pipe, handler);
}
