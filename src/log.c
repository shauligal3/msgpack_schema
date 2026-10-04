/* SPDX-License-Identifier: MIT */
/**
 * @file log.c
 * @brief Process-wide diagnostics hooks.
 */
#include "internal.h"

mpschema_debug_func_t g_mpschema_debug_func;
mpschema_warning_func_t g_mpschema_warning_func;

void mpschema_set_logging(mpschema_debug_func_t debug_func, mpschema_warning_func_t warning_func)
{
    g_mpschema_debug_func = debug_func;
    g_mpschema_warning_func = warning_func;
}
