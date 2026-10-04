/* SPDX-License-Identifier: MIT */
/**
 * @file mpschema.h
 * @brief Umbrella header: include this to get the whole public API.
 *
 * mpschema maps plain C structs to compact MessagePack maps using static,
 * declarative schemas. See the README for an overview.
 *
 * | Header           | Contents                                         |
 * |------------------|--------------------------------------------------|
 * | types.h          | status codes, member types, schema descriptors   |
 * | schema_macros.h  | macros for declaring schemas                     |
 * | codec.h          | one-shot encode / decode                         |
 * | env.h            | v2 streaming environment                         |
 * | header.h         | message framing header                           |
 * | transport.h      | framed messages over sockets                     |
 * | inspect.h        | iterate, print, JSON                             |
 * | buffer.h         | bump-pointer buffer / arena                      |
 * | log.h            | diagnostics hooks                                |
 */
#ifndef MPSCHEMA_MPSCHEMA_H
#define MPSCHEMA_MPSCHEMA_H

#include "buffer.h"
#include "codec.h"
#include "env.h"
#include "header.h"
#include "inspect.h"
#include "log.h"
#include "schema_macros.h"
#include "transport.h"
#include "types.h"

/** Library version. */
#define MPSCHEMA_VERSION_MAJOR 2
#define MPSCHEMA_VERSION_MINOR 0
#define MPSCHEMA_VERSION_PATCH 0
#define MPSCHEMA_VERSION_STRING "2.0.0"

#endif /* MPSCHEMA_MPSCHEMA_H */
