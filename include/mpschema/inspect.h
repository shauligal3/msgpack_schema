/* SPDX-License-Identifier: MIT */
/**
 * @file inspect.h
 * @brief Introspection helpers: iterate, pretty-print and render objects as JSON.
 */
#ifndef MPSCHEMA_INSPECT_H
#define MPSCHEMA_INSPECT_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback invoked by mpschema_foreach() for each scalar or string value.
 * @param schema  the schema being walked
 * @param sm_name the member name
 * @param type    the member type
 * @param data    pointer to the value inside the object (for VARLEN_STRING,
 *                a pointer to the `char *`)
 * @param context the user context passed to mpschema_foreach()
 * @return 0 to continue, any other value to stop the walk and return it
 */
typedef int mpschema_foreach_cb_t(const mpschema_t *schema, const char *sm_name,
                                  mpschema_tag_type_t type, const void *data, void *context);

/**
 * @brief Calls @p cb for every element of every non-nested member.
 * @param skip_noexist if non-zero, values that would not be encoded (zero,
 *                     empty, NULL) are skipped
 * @return 0, or the first non-zero value returned by @p cb
 */
int mpschema_foreach(const mpschema_t *schema, const void *obj, void *context,
                     mpschema_foreach_cb_t *cb, int skip_noexist);

/**
 * @brief Dumps the present members of @p obj through the debug log hook.
 * @param ctxt optional title printed with an underline
 */
void mpschema_print(const mpschema_t *schema, const void *obj, const char *ctxt);

/** Sink used by the JSON renderer. Return value is ignored. */
typedef int (*mpschema_print_func_t)(void *arg, const char *str, size_t len);

/**
 * @brief Renders @p obj as compact single-line JSON-like text into @p buf.
 *
 * Output looks like `"schema_name": {x: 1, label: hello }` — keys and
 * strings are unquoted, which is convenient for log lines. Use
 * mpschema_print_json_2() with `quoted = 1` for strict JSON.
 *
 * @param ctxt optional raw prefix written before the object
 * @param buf  output buffer, always NUL-terminated
 * @param sz   in: capacity of @p buf; out: length written (excluding NUL).
 *             Output that does not fit is truncated.
 */
void mpschema_print_json(const mpschema_t *schema, const void *obj, const char *ctxt, char *buf,
                         size_t *sz);

/**
 * @brief Like mpschema_print_json(); with @p quoted non-zero, keys and strings
 *        are quoted and escaped and the schema name prefix is omitted, so the
 *        output is valid JSON.
 */
void mpschema_print_json_2(const mpschema_t *schema, const void *obj, const char *ctxt, char *buf,
                           size_t *sz, int quoted);

#ifdef __cplusplus
}
#endif

#endif /* MPSCHEMA_INSPECT_H */
