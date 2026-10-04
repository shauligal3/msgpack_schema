/* SPDX-License-Identifier: MIT */
/**
 * @file telemetry.c
 * @brief End-to-end example: declare a schema, encode a struct, decode it
 *        back from a byte stream, and print it as JSON.
 *
 * Build and run:  cmake -B build && cmake --build build && ./build/examples/telemetry
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mpschema/mpschema.h"

/* 1. Declare the structs. MPSCHEMA_MODULE is the common type-name prefix. */
#define MPSCHEMA_MODULE demo_

enum { MSG_READING = 1 };

typedef struct demo_location_ {
    uint32_t lat_e6; /* microdegrees, offset to keep them unsigned */
    uint32_t lon_e6;
} demo_location_t;

typedef struct demo_reading_ {
    uint32_t sensor_id;
    char unit[8];
    double value;
    uint8_t calibrated;
    uint16_t history[5];
    uint16_t history_ct;
    demo_location_t where;
} demo_reading_t;

/* 2. Give every member a tag. In v2 schemas tag 0 holds the object type. */
enum { e_location__lat_e6, e_location__lon_e6 };
enum {
    e_reading__objtype,
    e_reading__sensor_id,
    e_reading__unit,
    e_reading__value,
    e_reading__calibrated,
    e_reading__history,
    e_reading__where
};

/* 3. Describe the structs. */
/* clang-format off */
static mpschema_t location_schema = {
    MPSCHEMA_VARLEN_DEF(location, 0)
    MPSCHEMA_MEMBER(location, lat_e6, UINT32),
    MPSCHEMA_MEMBER(location, lon_e6, UINT32),
    MPSCHEMA_MEMBER_EOF
};
/* clang-format on */

/* clang-format off */
static mpschema_t reading_schema = {
    MPSCHEMAV2_DEF(reading, MSG_READING)
    MPSCHEMA_MEMBER(reading, sensor_id, UINT32),
    MPSCHEMA_STRING(reading, unit, sizeof(((demo_reading_t *)0)->unit)),
    MPSCHEMA_MEMBER(reading, value, DOUBLE),
    MPSCHEMA_MEMBER(reading, calibrated, BOOL),
    MPSCHEMA_ARRAY(reading, history, UINT16, 5),
    MPSCHEMA_NESTED(reading, where, &location_schema),
    MPSCHEMA_MEMBER_EOF
};
/* clang-format on */

int main(void)
{
    demo_reading_t in;
    demo_reading_t out;
    char wire[256];
    size_t wire_sz = sizeof(wire);
    char json[MPSCHEMA_MAX_JSON_SZ];
    size_t json_sz = sizeof(json);
    uint16_t type = 0;
    mpschema_env_t *env;
    int rc;

    if (mpschema_validate(&reading_schema) != mpschema_ok) {
        fprintf(stderr, "invalid schema\n");
        return 1;
    }

    memset(&in, 0, sizeof(in));
    in.sensor_id = 42;
    strcpy(in.unit, "kPa");
    in.value = 101.325;
    in.calibrated = 1;
    in.history[0] = 99;
    in.history[1] = 100;
    in.history[2] = 101;
    in.history_ct = 3;
    in.where.lat_e6 = 32085300;
    in.where.lon_e6 = 34781800;

    env = mpschema_env_init(NULL, NULL);
    if (env == NULL) {
        return 1;
    }

    /* 4. Encode. */
    rc = mpschema_env_encode(env, &reading_schema, &in, wire, &wire_sz);
    if (rc != mpschema_ok) {
        fprintf(stderr, "encode failed: %d\n", rc);
        mpschema_env_destroy(env);
        return 1;
    }
    printf("encoded %zu bytes (the struct is %zu bytes in memory)\n", wire_sz, sizeof(in));

    /* 5. Decode from a stream, in two arbitrary chunks. */
    mpschema_env_feed(env, wire, wire_sz / 2);
    if (mpschema_env_next(env, &type) == mpschema_not_found) {
        printf("first chunk: object incomplete, waiting for more bytes\n");
    }
    mpschema_env_feed(env, wire + wire_sz / 2, wire_sz - wire_sz / 2);

    memset(&out, 0, sizeof(out));
    if (mpschema_env_next(env, &type) == mpschema_ok && type == MSG_READING) {
        rc = mpschema_env_next_decode(env, &reading_schema, &out, sizeof(out));
        printf("decoded object type %u: rc=%d\n", (unsigned)type, rc);
    }

    /* 6. Inspect. */
    mpschema_print_json_2(&reading_schema, &out, NULL, json, &json_sz, 1);
    printf("%s\n", json);

    mpschema_env_destroy(env);
    return 0;
}
