/* SPDX-License-Identifier: MIT */
/**
 * @file buffer.c
 * @brief Bump-pointer buffer / arena.
 */
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#include "internal.h"

/** Alignment guaranteed by mpschema_buffer_calloc(). */
#define MPS_ARENA_ALIGN alignof(max_align_t)

static size_t buffer_avail(const mpschema_buffer_t *b)
{
    return b->alloced > b->used ? b->alloced - b->used : 0;
}

int mpschema_buffer_write(void *data, const char *buf, size_t len)
{
    mpschema_buffer_t *b = (mpschema_buffer_t *)data;
    int rc = mpschema_ok;
    size_t avail;

    if (b == NULL || (len != 0 && (buf == NULL || b->data == NULL))) {
        return mpschema_invalid_param;
    }
    avail = buffer_avail(b);
    if (len > avail) {
        len = avail;
        rc = mpschema_no_space;
    }
    if (len != 0) {
        memcpy(b->data + b->used, buf, len);
        b->used += len;
    }
    return rc;
}

char *mpschema_buffer_strdup(mpschema_buffer_t *arena, const char *buf, size_t len)
{
    char *copy;

    if (arena == NULL || arena->data == NULL || (buf == NULL && len != 0) ||
        len >= buffer_avail(arena)) {
        return NULL;
    }
    copy = arena->data + arena->used;
    if (len != 0) {
        memcpy(copy, buf, len);
    }
    copy[len] = '\0';
    arena->used += len + 1;
    return copy;
}

void *mpschema_buffer_calloc(mpschema_buffer_t *arena, size_t nmemb, size_t size)
{
    uintptr_t addr;
    size_t total;
    size_t padding;
    void *mem;

    if (arena == NULL || arena->data == NULL) {
        return NULL;
    }
    if (size != 0 && nmemb > SIZE_MAX / size) {
        return NULL;
    }
    total = nmemb * size;

    addr = (uintptr_t)(arena->data + arena->used);
    padding = (size_t)((MPS_ARENA_ALIGN - (addr % MPS_ARENA_ALIGN)) % MPS_ARENA_ALIGN);
    if (padding > buffer_avail(arena) || total > buffer_avail(arena) - padding) {
        return NULL;
    }

    mem = arena->data + arena->used + padding;
    memset(mem, 0, total);
    arena->used += padding + total;
    return mem;
}
