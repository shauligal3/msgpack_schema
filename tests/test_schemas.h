/* SPDX-License-Identifier: MIT */
/**
 * @file test_schemas.h
 * @brief Structs and schemas exercised by the unit tests.
 */
#ifndef MPSCHEMA_TEST_SCHEMAS_H
#define MPSCHEMA_TEST_SCHEMAS_H

#include <stdint.h>

#include "mpschema/mpschema.h"

#define MPSCHEMA_MODULE test_

enum { MSG_POINT = 1, MSG_SHAPE = 2, MSG_RECORD = 3, MSG_DOC = 4, MSG_V2 = 5 };

/* ---- point: plain scalars (v1) ------------------------------------------ */
typedef struct test_point_ {
    uint32_t x;
    uint32_t y;
} test_point_t;

enum { e_point__x, e_point__y };

extern mpschema_t point_schema;

/* ---- record: every scalar type, strings and arrays (v1) ------------------ */
typedef struct test_record_ {
    uint8_t flag;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    double ratio;
    char name[8];
    uint16_t samples[4];
    uint16_t samples_ct;
    uint32_t level;
    uint8_t set_level;
} test_record_t;

enum {
    e_record__flag,
    e_record__u8,
    e_record__u16,
    e_record__u32,
    e_record__u64,
    e_record__ratio,
    e_record__retired,
    e_record__name,
    e_record__samples,
    e_record__level
};

extern mpschema_t record_schema;

/* ---- shape: nested struct and nested array (v1) -------------------------- */
typedef struct test_shape_ {
    uint8_t kind;
    test_point_t origin;
    test_point_t corners[3];
    uint16_t corners_ct;
} test_shape_t;

enum { e_shape__kind, e_shape__origin, e_shape__corners };

extern mpschema_t shape_schema;

/* ---- doc: arena-backed members (v1 with size) ---------------------------- */
typedef struct test_doc_ {
    char *title;
    test_point_t *anchor;
    test_point_t *path;
    uint16_t path_ct;
} test_doc_t;

enum { e_doc__title, e_doc__anchor, e_doc__path };

extern mpschema_t doc_schema;

/* ---- v2 object ----------------------------------------------------------- */
typedef struct test_v2_ {
    uint32_t id;
    char tag[12];
    test_point_t pos;
} test_v2_t;

enum { e_v2__objtype, e_v2__id, e_v2__tag, e_v2__pos };

extern mpschema_t v2_schema;

#endif /* MPSCHEMA_TEST_SCHEMAS_H */
