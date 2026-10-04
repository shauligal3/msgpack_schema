/* SPDX-License-Identifier: MIT */
/**
 * @file log.h
 * @brief Optional diagnostics hooks.
 *
 * The library is silent by default. Install printf-style callbacks to receive
 * debug traces (e.g. values skipped by the decoder) and warnings (e.g. socket
 * failures). The hooks are process-wide; install them once at start-up.
 */
#ifndef MPSCHEMA_LOG_H
#define MPSCHEMA_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/** printf-style callback for debug traces. */
typedef void (*mpschema_debug_func_t)(const char *fmt, ...);
/** printf-style callback for warnings. */
typedef void (*mpschema_warning_func_t)(const char *fmt, ...);

/**
 * @brief Installs the logging hooks. Either argument may be NULL to disable
 *        that level.
 */
void mpschema_set_logging(mpschema_debug_func_t debug_func, mpschema_warning_func_t warning_func);

#ifdef __cplusplus
}
#endif

#endif /* MPSCHEMA_LOG_H */
