/* SPDX-License-Identifier: MIT */
/**
 * @file inspect.c
 * @brief Iteration, debug printing and JSON rendering of objects.
 */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "internal.h"

/* ------------------------------------------------------------------------ */
/* Value formatting shared by the printers                                  */
/* ------------------------------------------------------------------------ */

/* Returns the string stored in a STRING / VARLEN_STRING element and its length. */
static const char *elem_string(const mpschema_member_t *m, const void *elem, size_t *len)
{
    const char *s;

    if (m->sm_type == SCHEMA_TAG_STRING) {
        s = (const char *)elem;
        *len = m->sm_elem_size != 0 ? strnlen(s, m->sm_elem_size) : 0;
        return s;
    }
    memcpy((void *)&s, elem, sizeof(s));
    *len = s != NULL ? strlen(s) : 0;
    return s != NULL ? s : "";
}

/* Formats a numeric or boolean element into @p out (always NUL-terminated). */
static void format_number(const mpschema_member_t *m, const void *elem, char *out, size_t cap)
{
    switch (m->sm_type) {
    case SCHEMA_TAG_BOOL: {
        uint8_t v;
        memcpy(&v, elem, sizeof(v));
        (void)snprintf(out, cap, "%s", v ? "true" : "false");
        break;
    }
    case SCHEMA_TAG_UINT8: {
        uint8_t v;
        memcpy(&v, elem, sizeof(v));
        (void)snprintf(out, cap, "%u", (unsigned)v);
        break;
    }
    case SCHEMA_TAG_UINT16: {
        uint16_t v;
        memcpy(&v, elem, sizeof(v));
        (void)snprintf(out, cap, "%u", (unsigned)v);
        break;
    }
    case SCHEMA_TAG_UINT32: {
        uint32_t v;
        memcpy(&v, elem, sizeof(v));
        (void)snprintf(out, cap, "%" PRIu32, v);
        break;
    }
    case SCHEMA_TAG_UINT64: {
        uint64_t v;
        memcpy(&v, elem, sizeof(v));
        (void)snprintf(out, cap, "%" PRIu64, v);
        break;
    }
    case SCHEMA_TAG_DOUBLE: {
        double v;
        memcpy(&v, elem, sizeof(v));
        if (isfinite(v)) {
            (void)snprintf(out, cap, "%.17g", v);
        } else {
            (void)snprintf(out, cap, "null"); /* JSON has no inf / nan */
        }
        break;
    }
    default:
        (void)snprintf(out, cap, "?");
        break;
    }
}

static int is_nested(const mpschema_member_t *m)
{
    return m->sm_type == SCHEMA_TAG_NESTED || m->sm_type == SCHEMA_TAG_REFERS;
}

static int is_string(const mpschema_member_t *m)
{
    return m->sm_type == SCHEMA_TAG_STRING || m->sm_type == SCHEMA_TAG_VARLEN_STRING;
}

/* ------------------------------------------------------------------------ */
/* mpschema_foreach                                                         */
/* ------------------------------------------------------------------------ */

int mpschema_foreach(const mpschema_t *schema, const void *obj, void *context,
                     mpschema_foreach_cb_t *cb, int skip_noexist)
{
    const mpschema_member_t *m;

    if (schema == NULL || obj == NULL || cb == NULL) {
        return mpschema_invalid_param;
    }
    for (m = schema->mps_members; !is_mpschema_eof(m); m++) {
        const size_t count = mps_member_elem_count(m, obj);
        size_t j;

        if (is_nested(m)) {
            continue;
        }
        for (j = 0; j < count; j++) {
            int exists = 0;
            const void *elem = mps_member_elem(m, obj, j, &exists);
            int rv;

            if (elem == NULL || (!exists && skip_noexist)) {
                continue;
            }
            rv = cb(schema, m->sm_name, m->sm_type, elem, context);
            if (rv != 0) {
                return rv;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* mpschema_print                                                           */
/* ------------------------------------------------------------------------ */

static const char *type_name(mpschema_tag_type_t type)
{
    switch (type) {
    case SCHEMA_TAG_BOOL:
        return "bool";
    case SCHEMA_TAG_UINT8:
        return "uint8";
    case SCHEMA_TAG_UINT16:
        return "uint16";
    case SCHEMA_TAG_UINT32:
        return "uint32";
    case SCHEMA_TAG_UINT64:
        return "uint64";
    case SCHEMA_TAG_DOUBLE:
        return "double";
    case SCHEMA_TAG_STRING:
        return "string";
    case SCHEMA_TAG_VARLEN_STRING:
        return "var-string";
    case SCHEMA_TAG_NESTED:
    case SCHEMA_TAG_REFERS:
        return "nested";
    default:
        return "?";
    }
}

static void print_object(const mpschema_t *schema, const void *obj, int depth);

static void print_elem(const mpschema_member_t *m, const void *elem, size_t idx, int depth)
{
    const int indent = depth * 2;
    char value[64];

    if (is_nested(m)) {
        MPS_DEBUG("%*s%s %s[%zu]:\n", indent, "", type_name(m->sm_type), m->sm_name, idx);
        print_object(m->sm_nested, elem, depth + 1);
    } else if (is_string(m)) {
        size_t len;
        const char *s = elem_string(m, elem, &len);
        MPS_DEBUG("%*s%s %s[%zu]: %.*s\n", indent, "", type_name(m->sm_type), m->sm_name, idx,
                  (int)MPS_MIN(len, (size_t)INT32_MAX), s);
    } else {
        format_number(m, elem, value, sizeof(value));
        MPS_DEBUG("%*s%s %s[%zu]: %s\n", indent, "", type_name(m->sm_type), m->sm_name, idx, value);
    }
}

static void print_object(const mpschema_t *schema, const void *obj, int depth)
{
    const mpschema_member_t *m;

    for (m = schema->mps_members; !is_mpschema_eof(m); m++) {
        const size_t count = mps_member_elem_count(m, obj);
        size_t j;

        if (!mps_member_is_present(m, obj)) {
            continue;
        }
        for (j = 0; j < count; j++) {
            int exists = 0;
            const void *elem = mps_member_elem(m, obj, j, &exists);
            if (elem != NULL && exists) {
                print_elem(m, elem, j, depth);
            }
        }
    }
}

void mpschema_print(const mpschema_t *schema, const void *obj, const char *ctxt)
{
    if (schema == NULL || obj == NULL || g_mpschema_debug_func == NULL) {
        return;
    }
    if (ctxt != NULL) {
        char ruler[80];
        size_t ruler_len = strlen(ctxt) + 1 + schema->mps_name_len;

        ruler_len = MPS_MIN(ruler_len, sizeof(ruler) - 1);
        memset(ruler, '=', ruler_len);
        ruler[ruler_len] = '\0';
        MPS_DEBUG("%s %s\n", ctxt, schema->mps_name ? schema->mps_name : "");
        MPS_DEBUG("%s\n", ruler);
    }
    print_object(schema, obj, 0);
}

/* ------------------------------------------------------------------------ */
/* JSON                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct json_out_ {
    mpschema_print_func_t emit;
    void *arg;
    int quoted;
} json_out_t;

static void put(const json_out_t *out, const char *s, size_t len)
{
    if (len != 0) {
        (void)out->emit(out->arg, s, len);
    }
}

static void puts_raw(const json_out_t *out, const char *s)
{
    put(out, s, strlen(s));
}

/* Writes @p s as a JSON string literal (quoted mode) or verbatim. */
static void put_string(const json_out_t *out, const char *s, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    size_t start = 0;
    size_t i;

    if (!out->quoted) {
        put(out, s, len);
        return;
    }
    put(out, "\"", 1);
    for (i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)s[i];
        char esc[6];
        size_t esc_len = 2;

        if (c >= 0x20 && c != '"' && c != '\\') {
            continue;
        }
        put(out, s + start, i - start);
        start = i + 1;
        esc[0] = '\\';
        switch (c) {
        case '"':
            esc[1] = '"';
            break;
        case '\\':
            esc[1] = '\\';
            break;
        case '\n':
            esc[1] = 'n';
            break;
        case '\r':
            esc[1] = 'r';
            break;
        case '\t':
            esc[1] = 't';
            break;
        default:
            esc[1] = 'u';
            esc[2] = '0';
            esc[3] = '0';
            esc[4] = hex[c >> 4];
            esc[5] = hex[c & 0xF];
            esc_len = 6;
            break;
        }
        put(out, esc, esc_len);
    }
    put(out, s + start, len - start);
    put(out, "\"", 1);
}

static void put_object(const json_out_t *out, const mpschema_t *schema, const void *obj);

static void put_value(const json_out_t *out, const mpschema_member_t *m, const void *elem)
{
    char num[64];

    if (is_nested(m)) {
        put_object(out, m->sm_nested, elem);
    } else if (is_string(m)) {
        size_t len;
        const char *s = elem_string(m, elem, &len);
        put_string(out, s, len);
    } else {
        format_number(m, elem, num, sizeof(num));
        puts_raw(out, num);
    }
}

static void put_object(const json_out_t *out, const mpschema_t *schema, const void *obj)
{
    const mpschema_member_t *m;
    int first = 1;

    put(out, "{", 1);
    for (m = schema->mps_members; !is_mpschema_eof(m); m++) {
        const size_t count = mps_member_elem_count(m, obj);
        size_t j;

        if (!mps_member_is_present(m, obj)) {
            continue;
        }
        if (!first) {
            put(out, ", ", 2);
        }
        first = 0;
        put_string(out, m->sm_name, m->sm_name_len);
        put(out, ": ", 2);

        if (m->sm_max_elems > 1) {
            put(out, "[", 1);
        }
        for (j = 0; j < count; j++) {
            const void *elem = mps_member_elem(m, obj, j, NULL);
            if (j != 0) {
                put(out, ", ", 2);
            }
            if (elem != NULL) {
                put_value(out, m, elem);
            }
        }
        if (m->sm_max_elems > 1) {
            put(out, "]", 1);
        }
    }
    put(out, first ? "}" : " }", first ? 1 : 2);
}

void mpschema_print_json_2(const mpschema_t *schema, const void *obj, const char *ctxt, char *buf,
                           size_t *sz, int quoted)
{
    mpschema_buffer_t sink;
    json_out_t out;

    if (buf == NULL || sz == NULL || *sz == 0) {
        return;
    }
    memset(&sink, 0, sizeof(sink));
    sink.data = buf;
    sink.alloced = *sz - 1; /* keep room for the terminator */

    out.emit = mpschema_buffer_write;
    out.arg = &sink;
    out.quoted = quoted;

    if (ctxt != NULL) {
        puts_raw(&out, ctxt);
    }
    if (schema != NULL && obj != NULL) {
        if (!quoted && schema->mps_name != NULL) {
            /* log-friendly form: prefix the object with the schema name */
            put(&out, "\"", 1);
            puts_raw(&out, schema->mps_name);
            put(&out, "\": ", 3);
        }
        put_object(&out, schema, obj);
    }
    buf[sink.used] = '\0';
    *sz = sink.used;
}

void mpschema_print_json(const mpschema_t *schema, const void *obj, const char *ctxt, char *buf,
                         size_t *sz)
{
    mpschema_print_json_2(schema, obj, ctxt, buf, sz, 0);
}
