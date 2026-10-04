/* SPDX-License-Identifier: MIT */
#include "test_schemas.h"

/* clang-format off */
mpschema_t point_schema = {
    MPSCHEMA_VARLEN_DEF(point, MSG_POINT)
    MPSCHEMA_MEMBER(point, x, UINT32),
    MPSCHEMA_MEMBER(point, y, UINT32),
    MPSCHEMA_MEMBER_EOF
};
/* clang-format on */

/* clang-format off */
mpschema_t record_schema = {
    MPSCHEMA_DEF(MSG_RECORD)
    MPSCHEMA_MEMBER(record, flag, BOOL),
    MPSCHEMA_MEMBER(record, u8, UINT8),
    MPSCHEMA_MEMBER(record, u16, UINT16),
    MPSCHEMA_MEMBER(record, u32, UINT32),
    MPSCHEMA_MEMBER(record, u64, UINT64),
    MPSCHEMA_MEMBER(record, ratio, DOUBLE),
    MPSCHEMA_MEMBER_OBSOLETE,
    MPSCHEMA_STRING(record, name, sizeof(((test_record_t *)0)->name)),
    MPSCHEMA_ARRAY(record, samples, UINT16, 4),
    MPSCHEMA_MEMBER_RECORD_SET(record, level, UINT32),
    MPSCHEMA_MEMBER_EOF
};
/* clang-format on */

/* clang-format off */
mpschema_t shape_schema = {
    MPSCHEMA_DEF(MSG_SHAPE)
    MPSCHEMA_MEMBER(shape, kind, UINT8),
    MPSCHEMA_NESTED(shape, origin, &point_schema),
    MPSCHEMA_NARRAY(shape, corners, &point_schema, point, 3),
    MPSCHEMA_MEMBER_EOF
};
/* clang-format on */

/* clang-format off */
mpschema_t doc_schema = {
    MPSCHEMA_VARLEN_DEF(doc, MSG_DOC)
    MPSCHEMA_VARLEN_STRING(doc, title, 32),
    MPSCHEMA_REFERS(doc, anchor, &point_schema, point),
    MPSCHEMA_NREFER(doc, path, &point_schema, point, 8),
    MPSCHEMA_MEMBER_EOF
};
/* clang-format on */

/* clang-format off */
mpschema_t v2_schema = {
    MPSCHEMAV2_DEF(v2, MSG_V2)
    MPSCHEMA_MEMBER(v2, id, UINT32),
    MPSCHEMA_STRING(v2, tag, sizeof(((test_v2_t *)0)->tag)),
    MPSCHEMA_NESTED(v2, pos, &point_schema),
    MPSCHEMA_MEMBER_EOF
};
/* clang-format on */
