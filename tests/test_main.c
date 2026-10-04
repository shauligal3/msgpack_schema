/* SPDX-License-Identifier: MIT */
/**
 * @file test_main.c
 * @brief Unit tests for mpschema. Self-contained: no test framework needed.
 */
#include <errno.h>
#include <fcntl.h>
#include <msgpack.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "mpschema/mpschema.h"
#include "test_schemas.h"

/* ------------------------------------------------------------------------ */
/* Minimal test harness                                                     */
/* ------------------------------------------------------------------------ */

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(cond)) {                                                                             \
            g_failures++;                                                                          \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
        }                                                                                          \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                                         \
    do {                                                                                           \
        long long a_ = (long long)(a), b_ = (long long)(b);                                        \
        g_checks++;                                                                                \
        if (a_ != b_) {                                                                            \
            g_failures++;                                                                          \
            fprintf(stderr, "  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, \
                    a_, b_);                                                                       \
        }                                                                                          \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                                         \
    do {                                                                                           \
        const char *a_ = (a), *b_ = (b);                                                           \
        g_checks++;                                                                                \
        if (a_ == NULL || b_ == NULL || strcmp(a_, b_) != 0) {                                     \
            g_failures++;                                                                          \
            fprintf(stderr, "  FAIL %s:%d: %s == %s (\"%s\" != \"%s\")\n", __FILE__, __LINE__, #a, \
                    #b, a_ ? a_ : "(null)", b_ ? b_ : "(null)");                                   \
        }                                                                                          \
    } while (0)

/* Captures debug output so mpschema_print() can be checked. */
static char g_log[4096];
static size_t g_log_len;

static void capture_log(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(g_log + g_log_len, sizeof(g_log) - g_log_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        g_log_len += (size_t)n;
        if (g_log_len >= sizeof(g_log)) {
            g_log_len = sizeof(g_log) - 1;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

/* Builds a msgpack payload by hand, for inputs our encoder never produces. */
typedef struct {
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
} raw_t;

static void raw_init(raw_t *r)
{
    msgpack_sbuffer_init(&r->sbuf);
    msgpack_packer_init(&r->pk, &r->sbuf, msgpack_sbuffer_write);
}

static void raw_destroy(raw_t *r)
{
    msgpack_sbuffer_destroy(&r->sbuf);
}

static void pack_cstr(msgpack_packer *pk, const char *s)
{
    size_t n = strlen(s);
    msgpack_pack_str(pk, n);
    msgpack_pack_str_body(pk, s, n);
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                    */
/* ------------------------------------------------------------------------ */

static void test_validate(void)
{
    CHECK_EQ_INT(mpschema_validate(&point_schema), mpschema_ok);
    CHECK_EQ_INT(mpschema_validate(&record_schema), mpschema_ok);
    CHECK_EQ_INT(mpschema_validate(&shape_schema), mpschema_ok);
    CHECK_EQ_INT(mpschema_validate(&doc_schema), mpschema_ok);
    CHECK_EQ_INT(mpschema_validate(&v2_schema), mpschema_ok);
    CHECK_EQ_INT(mpschema_validate(NULL), mpschema_invalid_param);

    {
        /* tag numbering does not match the member positions */
        /* clang-format off */
        static mpschema_t bad = {
            MPSCHEMA_DEF(99)
            {.sm_tag = 1, .sm_name = "x", .sm_type = SCHEMA_TAG_UINT8, .sm_max_elems = 1,
             .sm_flags = SM_HAS_DATA_OFF},
            MPSCHEMA_MEMBER_EOF
        };
        /* clang-format on */
        CHECK_EQ_INT(mpschema_validate(&bad), mpschema_malformed);
    }
    {
        /* fixed string without a capacity */
        /* clang-format off */
        static mpschema_t bad = {
            MPSCHEMA_DEF(99)
            {.sm_tag = 0, .sm_name = "s", .sm_type = SCHEMA_TAG_STRING, .sm_max_elems = 1,
             .sm_flags = SM_HAS_DATA_OFF},
            MPSCHEMA_MEMBER_EOF
        };
        /* clang-format on */
        CHECK_EQ_INT(mpschema_validate(&bad), mpschema_malformed);
    }
}

static void test_roundtrip_scalars(void)
{
    test_record_t in, out;
    char buf[256];
    size_t sz = sizeof(buf);

    memset(&in, 0, sizeof(in));
    in.flag = 1;
    in.u8 = 200;
    in.u16 = 60000;
    in.u32 = 4000000000u;
    in.u64 = 0x1122334455667788ull;
    in.ratio = 3.25;
    strcpy(in.name, "abc");
    in.level = 7;

    CHECK_EQ_INT(mpschema_encode_obj(&in, &record_schema, buf, &sz), mpschema_ok);
    CHECK(sz > 0);

    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_decode_obj(&out, &record_schema, buf, sz), mpschema_ok);
    CHECK_EQ_INT(out.flag, 1);
    CHECK_EQ_INT(out.u8, 200);
    CHECK_EQ_INT(out.u16, 60000);
    CHECK_EQ_INT(out.u32, 4000000000u);
    CHECK(out.u64 == 0x1122334455667788ull);
    CHECK(out.ratio == 3.25);
    CHECK_EQ_STR(out.name, "abc");
    CHECK_EQ_INT(out.level, 7);
    CHECK_EQ_INT(out.set_level, 1);
    CHECK_EQ_INT(out.samples_ct, 0);
}

static void test_sparse_encoding(void)
{
    test_record_t in, out;
    char buf[64];
    size_t sz = sizeof(buf);

    /* an all-zero object is an empty map: a single byte */
    memset(&in, 0, sizeof(in));
    CHECK_EQ_INT(mpschema_encode_obj(&in, &record_schema, buf, &sz), mpschema_ok);
    CHECK_EQ_INT(sz, 1);
    CHECK_EQ_INT((unsigned char)buf[0], 0x80);

    /* omitted members keep the caller's values */
    memset(&out, 0, sizeof(out));
    out.u16 = 42;
    CHECK_EQ_INT(mpschema_decode_obj(&out, &record_schema, buf, sz), mpschema_ok);
    CHECK_EQ_INT(out.u16, 42);
    CHECK_EQ_INT(out.set_level, 0);
}

static void test_array_with_leading_zero(void)
{
    /*
     * Regression: an array whose first element is zero used to be counted in
     * the map header but then skipped, producing corrupt output.
     */
    test_record_t in, out;
    char buf[64];
    size_t sz = sizeof(buf);

    memset(&in, 0, sizeof(in));
    in.samples[0] = 0;
    in.samples[1] = 5;
    in.samples[2] = 0;
    in.samples_ct = 3;

    CHECK_EQ_INT(mpschema_encode_obj(&in, &record_schema, buf, &sz), mpschema_ok);
    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_decode_obj(&out, &record_schema, buf, sz), mpschema_ok);
    CHECK_EQ_INT(out.samples_ct, 3);
    CHECK_EQ_INT(out.samples[0], 0);
    CHECK_EQ_INT(out.samples[1], 5);
    CHECK_EQ_INT(out.samples[2], 0);
}

static void test_array_counter_clamped(void)
{
    test_record_t in, out;
    char buf[64];
    size_t sz = sizeof(buf);

    memset(&in, 0, sizeof(in));
    in.samples[0] = 1;
    in.samples[3] = 4;
    in.samples_ct = 1000; /* bogus: larger than the capacity of 4 */

    CHECK_EQ_INT(mpschema_encode_obj(&in, &record_schema, buf, &sz), mpschema_ok);
    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_decode_obj(&out, &record_schema, buf, sz), mpschema_ok);
    CHECK_EQ_INT(out.samples_ct, 4);
    CHECK_EQ_INT(out.samples[3], 4);
}

static void test_string_truncation(void)
{
    test_record_t out;
    raw_t r;

    /* the wire string is longer than char name[8] */
    raw_init(&r);
    msgpack_pack_map(&r.pk, 1);
    msgpack_pack_uint16(&r.pk, e_record__name);
    pack_cstr(&r.pk, "abcdefghijkl");

    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_decode_obj(&out, &record_schema, r.sbuf.data, r.sbuf.size), mpschema_ok);
    CHECK_EQ_STR(out.name, "abcdefg");
    raw_destroy(&r);
}

static void test_unknown_and_mismatched_values(void)
{
    test_record_t out;
    raw_t r;

    raw_init(&r);
    msgpack_pack_map(&r.pk, 6);
    msgpack_pack_uint16(&r.pk, 500); /* unknown tag */
    msgpack_pack_uint8(&r.pk, 1);
    pack_cstr(&r.pk, "named"); /* non-integer key */
    msgpack_pack_uint8(&r.pk, 1);
    msgpack_pack_uint16(&r.pk, e_record__u8); /* does not fit in uint8_t */
    msgpack_pack_uint32(&r.pk, 70000);
    msgpack_pack_uint16(&r.pk, e_record__u16); /* wrong type */
    pack_cstr(&r.pk, "oops");
    msgpack_pack_uint16(&r.pk, e_record__retired); /* obsolete tag */
    msgpack_pack_uint8(&r.pk, 9);
    msgpack_pack_uint16(&r.pk, e_record__u32); /* the only valid entry */
    msgpack_pack_uint32(&r.pk, 123);

    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_decode_obj(&out, &record_schema, r.sbuf.data, r.sbuf.size), mpschema_ok);
    CHECK_EQ_INT(out.u8, 0);
    CHECK_EQ_INT(out.u16, 0);
    CHECK_EQ_INT(out.u32, 123);
    raw_destroy(&r);
}

static void test_double_decoding(void)
{
    /* Regression: DOUBLE fell through into the VARLEN_STRING case. */
    test_record_t out;
    raw_t r;

    raw_init(&r);
    msgpack_pack_map(&r.pk, 1);
    msgpack_pack_uint16(&r.pk, e_record__ratio);
    msgpack_pack_float(&r.pk, 0.5f);

    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_decode_obj(&out, &record_schema, r.sbuf.data, r.sbuf.size), mpschema_ok);
    CHECK(out.ratio == 0.5);
    raw_destroy(&r);
}

static void test_bool_legacy_uint(void)
{
    /* older encoders sent BOOL members as uint8 */
    test_record_t out;
    raw_t r;

    raw_init(&r);
    msgpack_pack_map(&r.pk, 1);
    msgpack_pack_uint16(&r.pk, e_record__flag);
    msgpack_pack_uint8(&r.pk, 1);

    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_decode_obj(&out, &record_schema, r.sbuf.data, r.sbuf.size), mpschema_ok);
    CHECK_EQ_INT(out.flag, 1);
    raw_destroy(&r);
}

static void test_nested(void)
{
    test_shape_t in, out;
    char buf[128];
    size_t sz = sizeof(buf);

    memset(&in, 0, sizeof(in));
    in.kind = 3;
    in.origin.x = 10;
    in.origin.y = 20;
    in.corners[0].x = 1;
    in.corners[1].y = 2;
    in.corners[2].x = 0; /* an empty element still keeps its position */
    in.corners_ct = 3;

    CHECK_EQ_INT(mpschema_encode_obj(&in, &shape_schema, buf, &sz), mpschema_ok);
    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_decode_obj(&out, &shape_schema, buf, sz), mpschema_ok);
    CHECK_EQ_INT(out.kind, 3);
    CHECK_EQ_INT(out.origin.x, 10);
    CHECK_EQ_INT(out.origin.y, 20);
    CHECK_EQ_INT(out.corners_ct, 3);
    CHECK_EQ_INT(out.corners[0].x, 1);
    CHECK_EQ_INT(out.corners[1].y, 2);
}

static void test_varlen_and_refers(void)
{
    test_point_t anchor = {7, 8};
    test_point_t path[2] = {{1, 2}, {3, 4}};
    test_doc_t in;
    char buf[128];
    size_t sz = sizeof(buf);
    /* struct followed by an arena for strings and referenced structs */
    union {
        test_doc_t doc;
        char bytes[sizeof(test_doc_t) + 256];
    } out;

    memset(&in, 0, sizeof(in));
    in.title = (char *)"hello arena";
    in.anchor = &anchor;
    in.path = path;
    in.path_ct = 2;

    CHECK_EQ_INT(mpschema_encode_obj(&in, &doc_schema, buf, &sz), mpschema_ok);

    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_decode_varlen_obj(&out, sizeof(out), &doc_schema, buf, sz), mpschema_ok);
    CHECK_EQ_STR(out.doc.title, "hello arena");
    CHECK(out.doc.anchor != NULL && out.doc.anchor->x == 7 && out.doc.anchor->y == 8);
    CHECK_EQ_INT(out.doc.path_ct, 2);
    CHECK(out.doc.path != NULL && out.doc.path[1].x == 3 && out.doc.path[1].y == 4);
    /* arena allocations are suitably aligned */
    CHECK(((uintptr_t)out.doc.anchor % _Alignof(test_point_t)) == 0);

    /* arena too small for the title */
    {
        char small[sizeof(test_doc_t) + 4];
        memset(small, 0, sizeof(small));
        CHECK_EQ_INT(mpschema_decode_varlen_obj(small, sizeof(small), &doc_schema, buf, sz),
                     mpschema_no_space);
    }
    /* output smaller than the struct itself */
    CHECK_EQ_INT(mpschema_decode_varlen_obj(&out, 4, &doc_schema, buf, sz), mpschema_no_space);
    /* without an arena, varlen members cannot be decoded */
    CHECK_EQ_INT(mpschema_decode_obj(&out, &doc_schema, buf, sz), mpschema_invalid_param);
}

static void test_encode_errors(void)
{
    test_record_t in;
    char buf[4];
    size_t sz = sizeof(buf);

    memset(&in, 0, sizeof(in));
    in.u64 = UINT64_MAX;
    strcpy(in.name, "abcdefg");
    CHECK_EQ_INT(mpschema_encode_obj(&in, &record_schema, buf, &sz), mpschema_no_space);
    CHECK(sz <= sizeof(buf));

    sz = sizeof(buf);
    CHECK_EQ_INT(mpschema_encode_obj(NULL, &record_schema, buf, &sz), mpschema_invalid_param);
    CHECK_EQ_INT(mpschema_encode_obj(&in, NULL, buf, &sz), mpschema_invalid_param);
}

static void test_decode_errors(void)
{
    test_point_t out;
    const char not_a_map[] = {(char)0x05};
    const char truncated[] = {(char)0x82, 0x00};
    const char trailing[] = {(char)0x80, 0x00};

    CHECK_EQ_INT(mpschema_decode_obj(&out, &point_schema, not_a_map, sizeof(not_a_map)),
                 mpschema_malformed);
    CHECK_EQ_INT(mpschema_decode_obj(&out, &point_schema, truncated, sizeof(truncated)),
                 mpschema_malformed);
    CHECK_EQ_INT(mpschema_decode_obj(&out, &point_schema, trailing, sizeof(trailing)),
                 mpschema_malformed);
    CHECK_EQ_INT(mpschema_decode_obj(&out, &point_schema, NULL, 0), mpschema_invalid_param);
}

static void test_fuzz_decode(void)
{
    /* random input must never crash or write outside the object */
    unsigned seed = 12345;
    int iter;

    for (iter = 0; iter < 20000; iter++) {
        char input[64];
        union {
            test_doc_t doc;
            char bytes[sizeof(test_doc_t) + 64];
        } out_doc;
        test_shape_t out_shape;
        size_t len = (size_t)(rand_r(&seed) % (int)sizeof(input)) + 1;
        size_t i;

        for (i = 0; i < len; i++) {
            input[i] = (char)rand_r(&seed);
        }
        input[0] = (char)(0x80 | (input[0] & 0x0F)); /* bias towards maps */

        memset(&out_shape, 0, sizeof(out_shape));
        (void)mpschema_decode_obj(&out_shape, &shape_schema, input, len);
        memset(&out_doc, 0, sizeof(out_doc));
        (void)mpschema_decode_varlen_obj(&out_doc, sizeof(out_doc), &doc_schema, input, len);
    }
    CHECK(1);
}

static void test_hostile_container_sizes(void)
{
    /* a 5-byte payload declaring a 4-billion element array must be rejected
       without asking the allocator for the memory */
    const char huge_array[] = {(char)0x81, 0x00,       (char)0xdd, (char)0xff,
                               (char)0xff, (char)0xff, (char)0xff};
    const char huge_map[] = {(char)0xdf, (char)0x7f, (char)0xff, (char)0xff, (char)0xff};
    test_record_t out;
    mpschema_env_t *env;
    uint16_t type;

    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_decode_obj(&out, &record_schema, huge_array, sizeof(huge_array)),
                 mpschema_malformed);
    CHECK_EQ_INT(mpschema_decode_obj(&out, &record_schema, huge_map, sizeof(huge_map)),
                 mpschema_malformed);

    env = mpschema_env_init(NULL, NULL);
    CHECK_EQ_INT(mpschema_env_decode_type(env, huge_map, sizeof(huge_map), &type),
                 mpschema_malformed);
    /* in a stream it is merely incomplete: nothing is allocated meanwhile */
    CHECK_EQ_INT(mpschema_env_feed(env, huge_map, sizeof(huge_map)), mpschema_ok);
    CHECK_EQ_INT(mpschema_env_next(env, &type), mpschema_not_found);
    mpschema_env_destroy(env);

    /* invalid MessagePack in a stream drops the buffered bytes */
    env = mpschema_env_init(NULL, NULL);
    {
        const char bad[] = {(char)0xc1, 0x01, 0x02};
        mpschema_env_feed(env, bad, sizeof(bad));
        CHECK_EQ_INT(mpschema_env_next(env, &type), mpschema_malformed);
        CHECK_EQ_INT(mpschema_env_next(env, &type), mpschema_not_found);
    }
    /* the buffering limit is enforced */
    {
        static char chunk[64 * 1024];
        size_t fed = 0;
        int rc = mpschema_ok;
        memset(chunk, 0x91, sizeof(chunk)); /* endless nested arrays: never complete */
        while (rc == mpschema_ok && fed <= MPSCHEMA_ENV_MAX_BUFFERED) {
            rc = mpschema_env_feed(env, chunk, sizeof(chunk));
            fed += sizeof(chunk);
        }
        CHECK_EQ_INT(rc, mpschema_no_space);
    }
    mpschema_env_destroy(env);
}

static void test_env_feed_while_pending(void)
{
    /* growing the stream buffer must not invalidate a pending object */
    mpschema_env_t *env = mpschema_env_init(NULL, NULL);
    static char filler[16 * 1024];
    test_v2_t in, out;
    char wire[64];
    size_t sz = sizeof(wire);
    uint16_t type = 0;

    memset(&in, 0, sizeof(in));
    in.id = 5;
    strcpy(in.tag, "pinned");
    mpschema_env_encode(env, &v2_schema, &in, wire, &sz);
    mpschema_env_feed(env, wire, sz);
    CHECK_EQ_INT(mpschema_env_next(env, &type), mpschema_ok);

    memset(filler, 0x90, sizeof(filler)); /* a run of empty arrays */
    CHECK_EQ_INT(mpschema_env_feed(env, filler, sizeof(filler)), mpschema_ok);

    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_env_next_decode(env, &v2_schema, &out, sizeof(out)), mpschema_ok);
    CHECK_EQ_STR(out.tag, "pinned");
    /* the filler objects follow; they carry no object type */
    CHECK_EQ_INT(mpschema_env_next(env, &type), mpschema_malformed);
    mpschema_env_destroy(env);
}

/* Packs a random MessagePack value of bounded depth. */
static void pack_random(msgpack_packer *pk, unsigned *seed, int depth)
{
    int kind = rand_r(seed) % (depth > 0 ? 12 : 9);
    int r1 = rand_r(seed); /* drawn up front: argument evaluation order is unspecified */
    int r2 = rand_r(seed);
    uint32_t i, n;
    char bytes[300];

    switch (kind) {
    case 0:
        msgpack_pack_nil(pk);
        break;
    case 1:
        msgpack_pack_true(pk);
        break;
    case 2:
        msgpack_pack_uint64(pk, (uint64_t)r1 << (r2 % 40));
        break;
    case 3:
        msgpack_pack_int64(pk, -(int64_t)r1 * (r2 % 3 ? 1 : 100000));
        break;
    case 4:
        msgpack_pack_double(pk, r1 / 7.0);
        break;
    case 5:
        msgpack_pack_float(pk, (float)r1);
        break;
    case 6:
        n = (uint32_t)(rand_r(seed) % (int)sizeof(bytes));
        memset(bytes, 'a', n);
        msgpack_pack_str(pk, n);
        msgpack_pack_str_body(pk, bytes, n);
        break;
    case 7:
        n = (uint32_t)(rand_r(seed) % (int)sizeof(bytes));
        memset(bytes, 1, n);
        msgpack_pack_bin(pk, n);
        msgpack_pack_bin_body(pk, bytes, n);
        break;
    case 8:
        n = (uint32_t)(rand_r(seed) % 20);
        memset(bytes, 2, n);
        msgpack_pack_ext(pk, n, 7);
        msgpack_pack_ext_body(pk, bytes, n);
        break;
    case 9:
    case 10:
        n = (uint32_t)(rand_r(seed) % 20);
        msgpack_pack_array(pk, n);
        for (i = 0; i < n; i++) {
            pack_random(pk, seed, depth - 1);
        }
        break;
    default:
        n = (uint32_t)(rand_r(seed) % 20);
        msgpack_pack_map(pk, n);
        for (i = 0; i < 2 * n; i++) {
            pack_random(pk, seed, depth - 1);
        }
        break;
    }
}

static void test_scan_matches_msgpack(void)
{
    /* the streaming decoder must find the same object boundaries as msgpack-c */
    unsigned seed = 777;
    int iter;

    for (iter = 0; iter < 500; iter++) {
        raw_t r;
        mpschema_env_t *env = mpschema_env_init(NULL, NULL);
        msgpack_unpacked u;
        size_t off = 0, cut;
        uint16_t type;
        int objects = 1 + rand_r(&seed) % 3, k, found = 0;

        raw_init(&r);
        for (k = 0; k < objects; k++) {
            /* a v2-shaped object wrapping a random value */
            msgpack_pack_map(&r.pk, 2);
            msgpack_pack_uint8(&r.pk, 0);
            msgpack_pack_uint16(&r.pk, MSG_V2);
            msgpack_pack_uint8(&r.pk, 99);
            pack_random(&r.pk, &seed, 3);
        }
        msgpack_unpacked_init(&u);
        for (k = 0; k < objects; k++) {
            CHECK_EQ_INT(msgpack_unpack_next(&u, r.sbuf.data, r.sbuf.size, &off),
                         MSGPACK_UNPACK_SUCCESS);
        }
        CHECK_EQ_INT(off, r.sbuf.size);
        msgpack_unpacked_destroy(&u);

        /* feed in two arbitrary pieces and count the objects found */
        cut = (size_t)rand_r(&seed) % (r.sbuf.size + 1);
        mpschema_env_feed(env, r.sbuf.data, cut);
        while (mpschema_env_next(env, &type) == mpschema_ok) {
            found++;
        }
        mpschema_env_feed(env, r.sbuf.data + cut, r.sbuf.size - cut);
        while (mpschema_env_next(env, &type) == mpschema_ok) {
            CHECK_EQ_INT(type, MSG_V2);
            found++;
        }
        CHECK_EQ_INT(found, objects);
        mpschema_env_destroy(env);
        raw_destroy(&r);
    }
}

static void test_export(void)
{
    test_point_t in = {3, 0};
    char buf[32];
    size_t sz = sizeof(buf);
    msgpack_unpacked u;
    size_t off = 0;

    CHECK_EQ_INT(mpschema_export_obj(&in, &point_schema, buf, &sz), mpschema_ok);
    msgpack_unpacked_init(&u);
    CHECK_EQ_INT(msgpack_unpack_next(&u, buf, sz, &off), MSGPACK_UNPACK_SUCCESS);
    CHECK_EQ_INT(u.data.type, MSGPACK_OBJECT_MAP);
    CHECK_EQ_INT(u.data.via.map.size, 1);
    CHECK_EQ_INT(u.data.via.map.ptr[0].key.type, MSGPACK_OBJECT_STR);
    CHECK(u.data.via.map.ptr[0].key.via.str.size == 1 &&
          u.data.via.map.ptr[0].key.via.str.ptr[0] == 'x');
    msgpack_unpacked_destroy(&u);

    /* an empty object exports to nothing */
    in.x = 0;
    sz = sizeof(buf);
    CHECK_EQ_INT(mpschema_export_obj(&in, &point_schema, buf, &sz), mpschema_ok);
    CHECK_EQ_INT(sz, 0);
}

static void test_header(void)
{
    char buf[16];
    size_t sz = sizeof(buf), off = 0;
    uint16_t type = 0, len = 0;

    CHECK_EQ_INT(mpschema_get_header_max_size(), 6);

    CHECK_EQ_INT(mpschema_encode_header(300, 5, buf, &sz), mpschema_ok);
    CHECK_EQ_INT(sz, 4); /* 300 -> 3 bytes, 5 -> 1 byte */
    CHECK_EQ_INT(mpschema_decode_header(&type, &len, &off, buf, sz), mpschema_ok);
    CHECK_EQ_INT(type, 300);
    CHECK_EQ_INT(len, 5);
    CHECK_EQ_INT(off, 4);

    CHECK_EQ_INT(mpschema_decode_header(&type, &len, &off, buf, 2), mpschema_incomplete);
    CHECK_EQ_INT(mpschema_decode_header(&type, &len, &off, buf, 0), mpschema_incomplete);

    sz = sizeof(buf);
    mpschema_encode_header(0, 5, buf, &sz);
    CHECK_EQ_INT(mpschema_decode_header(&type, &len, &off, buf, sz), mpschema_malformed);

    sz = 2;
    CHECK_EQ_INT(mpschema_encode_header(300, 300, buf, &sz), mpschema_no_space);
}

static void test_print_json(void)
{
    test_record_t rec;
    test_shape_t shape;
    char buf[256];
    size_t sz;

    memset(&rec, 0, sizeof(rec));
    rec.flag = 1;
    rec.u16 = 9;
    rec.ratio = 0.5;
    strcpy(rec.name, "a\"b");
    rec.samples[0] = 1;
    rec.samples[1] = 2;
    rec.samples_ct = 2;

    sz = sizeof(buf);
    mpschema_print_json_2(&record_schema, &rec, NULL, buf, &sz, 1);
    CHECK_EQ_STR(buf, "{\"flag\": true, \"u16\": 9, \"ratio\": 0.5, \"name\": \"a\\\"b\", "
                      "\"samples\": [1, 2] }");
    CHECK_EQ_INT(sz, strlen(buf));

    memset(&shape, 0, sizeof(shape));
    shape.origin.x = 4;
    sz = sizeof(buf);
    mpschema_print_json(&shape_schema, &shape, "ctx ", buf, &sz);
    CHECK_EQ_STR(buf, "ctx \"MSG_SHAPE\": {origin: {x: 4 } }");

    /* truncation keeps the buffer terminated */
    sz = 8;
    mpschema_print_json(&shape_schema, &shape, NULL, buf, &sz);
    CHECK_EQ_INT(sz, 7);
    CHECK_EQ_INT(strlen(buf), 7);
}

static void test_print_log(void)
{
    test_point_t pt = {5, 0};

    g_log_len = 0;
    g_log[0] = '\0';
    mpschema_set_logging(capture_log, NULL);
    mpschema_print(&point_schema, &pt, "dump");
    mpschema_set_logging(NULL, NULL);
    CHECK_EQ_STR(g_log, "dump point\n==========\nuint32 x[0]: 5\n");
}

static int count_cb(const mpschema_t *schema, const char *name, mpschema_tag_type_t type,
                    const void *data, void *context)
{
    (void)schema;
    (void)name;
    (void)type;
    (void)data;
    (*(int *)context)++;
    return 0;
}

static void test_foreach(void)
{
    test_record_t rec;
    int n = 0;

    memset(&rec, 0, sizeof(rec));
    rec.u8 = 1;
    rec.samples_ct = 2;
    CHECK_EQ_INT(mpschema_foreach(&record_schema, &rec, &n, count_cb, 1), 0);
    CHECK_EQ_INT(n, 1); /* only u8; zero array elements skipped */

    n = 0;
    CHECK_EQ_INT(mpschema_foreach(&record_schema, &rec, &n, count_cb, 0), 0);
    /* 6 scalars + name + 2 array elements + level */
    CHECK_EQ_INT(n, 10);
}

static void test_env_stream(void)
{
    mpschema_env_t *env = mpschema_env_init(NULL, NULL);
    test_v2_t a, b, out;
    char stream[128];
    size_t used = 0, sz, i;
    uint16_t type = 0;
    int decoded = 0;

    CHECK(env != NULL);
    memset(&a, 0, sizeof(a));
    a.id = 1;
    strcpy(a.tag, "first");
    memset(&b, 0, sizeof(b));
    b.id = 2;
    b.pos.y = 9;

    sz = sizeof(stream);
    CHECK_EQ_INT(mpschema_env_encode(env, &v2_schema, &a, stream, &sz), mpschema_ok);
    used = sz;
    sz = sizeof(stream) - used;
    CHECK_EQ_INT(mpschema_env_encode(env, &v2_schema, &b, stream + used, &sz), mpschema_ok);
    used += sz;

    /* nothing buffered yet */
    CHECK_EQ_INT(mpschema_env_next(env, &type), mpschema_not_found);

    /* feed one byte at a time to exercise reassembly */
    for (i = 0; i < used; i++) {
        CHECK_EQ_INT(mpschema_env_feed(env, stream + i, 1), mpschema_ok);
        while (mpschema_env_next(env, &type) == mpschema_ok) {
            size_t need = 0;
            CHECK_EQ_INT(type, MSG_V2);
            CHECK_EQ_INT(mpschema_env_next_obj_size(env, &v2_schema, &need), mpschema_ok);
            CHECK_EQ_INT(need, sizeof(test_v2_t));
            memset(&out, 0, sizeof(out));
            CHECK_EQ_INT(mpschema_env_next_decode(env, &v2_schema, &out, sizeof(out)), mpschema_ok);
            if (decoded == 0) {
                CHECK_EQ_INT(out.id, 1);
                CHECK_EQ_STR(out.tag, "first");
            } else {
                CHECK_EQ_INT(out.id, 2);
                CHECK_EQ_INT(out.pos.y, 9);
            }
            decoded++;
        }
    }
    CHECK_EQ_INT(decoded, 2);

    /* v1 schemas are rejected by the v2 API */
    sz = sizeof(stream);
    CHECK_EQ_INT(mpschema_env_encode(env, &point_schema, &a, stream, &sz), mpschema_unsupported);

    /* a non-v2 object in the stream is dropped and the stream continues */
    {
        char junk[1] = {(char)0x80}; /* empty map: no object type */
        mpschema_env_feed(env, junk, 1);
        CHECK_EQ_INT(mpschema_env_next(env, &type), mpschema_malformed);
        CHECK_EQ_INT(mpschema_env_next(env, &type), mpschema_not_found);
    }

    /* one-shot API */
    sz = sizeof(stream);
    CHECK_EQ_INT(mpschema_env_encode(env, &v2_schema, &a, stream, &sz), mpschema_ok);
    CHECK_EQ_INT(mpschema_env_decode_type(env, stream, sz, &type), mpschema_ok);
    CHECK_EQ_INT(type, MSG_V2);
    memset(&out, 0, sizeof(out));
    CHECK_EQ_INT(mpschema_env_decode_one(env, &v2_schema, &out, sizeof(out)), mpschema_ok);
    CHECK_EQ_INT(out.id, 1);

    /* bounds are enforced */
    CHECK_EQ_INT(mpschema_env_decode_one(env, &v2_schema, &out, 4), mpschema_no_space);

    mpschema_env_destroy(env);
}

typedef struct {
    char data[256];
    size_t used;
    int fail_after;
} sink_t;

static int sink_write(void *arg, const char *buf, size_t len)
{
    sink_t *s = (sink_t *)arg;
    if (s->fail_after >= 0 && s->used + len > (size_t)s->fail_after) {
        return -1;
    }
    memcpy(s->data + s->used, buf, len);
    s->used += len;
    return 0;
}

static void test_env_external_writer(void)
{
    mpschema_env_cfg_t cfg = {.external_writer = 1};
    mpschema_env_cback_t cb = {.writer_cb = sink_write};
    mpschema_env_t *env;
    test_v2_t in;
    sink_t sink;
    size_t sz = 0;

    /* inconsistent configurations are rejected */
    CHECK(mpschema_env_init(&cfg, NULL) == NULL);
    CHECK(mpschema_env_init(NULL, &cb) == NULL);

    env = mpschema_env_init(&cfg, &cb);
    CHECK(env != NULL);

    memset(&in, 0, sizeof(in));
    in.id = 77;
    memset(&sink, 0, sizeof(sink));
    sink.fail_after = -1;
    CHECK_EQ_INT(mpschema_env_encode(env, &v2_schema, &in, &sink, &sz), mpschema_ok);
    CHECK_EQ_INT(sz, sink.used);
    CHECK(sz > 0);

    memset(&sink, 0, sizeof(sink));
    sink.fail_after = 2;
    CHECK_EQ_INT(mpschema_env_encode(env, &v2_schema, &in, &sink, &sz), mpschema_io_error);

    mpschema_env_destroy(env);
}

typedef struct {
    int count;
    uint32_t last_x;
    int fail_on;
} pipe_ctx_t;

static int on_message(void *ctxt, uint16_t msgtype, char *msg, size_t len)
{
    pipe_ctx_t *ctx = (pipe_ctx_t *)ctxt;
    test_point_t pt;

    memset(&pt, 0, sizeof(pt));
    if (msgtype != MSG_POINT || mpschema_decode_obj(&pt, &point_schema, msg, len) != mpschema_ok) {
        return -100;
    }
    ctx->count++;
    ctx->last_x = pt.x;
    return ctx->count == ctx->fail_on ? -200 : 0;
}

static void test_pipe(void)
{
    int sv[2];
    mpschema_pipe_t *pipe;
    pipe_ctx_t ctx = {0, 0, -1};
    test_point_t pt = {0, 0};
    char frame[64];
    size_t i, n;

    CHECK_EQ_INT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    CHECK_EQ_INT(fcntl(sv[1], F_SETFL, O_NONBLOCK), 0);
    pipe = mpschema_pipe_alloc(sv[1], &ctx);
    CHECK(pipe != NULL);

    /* nothing to read */
    CHECK_EQ_INT(mpschema_pipe_process(pipe, on_message), mpschema_ok);

    /* three back-to-back messages */
    for (i = 1; i <= 3; i++) {
        pt.x = (uint32_t)i;
        CHECK_EQ_INT(mpschema_xmit_obj(sv[0], &pt, &point_schema), mpschema_ok);
    }
    CHECK_EQ_INT(mpschema_pipe_process(pipe, on_message), mpschema_ok);
    CHECK_EQ_INT(ctx.count, 3);
    CHECK_EQ_INT(ctx.last_x, 3);

    /* a frame split across several reads, starting mid-header */
    {
        char payload[32];
        size_t payload_sz = sizeof(payload), hdr_sz = sizeof(frame);
        pt.x = 300;
        mpschema_encode_obj(&pt, &point_schema, payload, &payload_sz);
        mpschema_encode_header(MSG_POINT, (uint16_t)payload_sz, frame, &hdr_sz);
        memcpy(frame + hdr_sz, payload, payload_sz);
        n = hdr_sz + payload_sz;
        for (i = 0; i < n; i++) {
            CHECK_EQ_INT(write(sv[0], frame + i, 1), 1);
            CHECK_EQ_INT(mpschema_pipe_process(pipe, on_message), mpschema_ok);
        }
        CHECK_EQ_INT(ctx.count, 4);
        CHECK_EQ_INT(ctx.last_x, 300);
    }

    /* handler errors are propagated */
    ctx.fail_on = 5;
    pt.x = 1;
    mpschema_xmit_obj(sv[0], &pt, &point_schema);
    CHECK_EQ_INT(mpschema_pipe_process(pipe, on_message), -200);

    /* oversized messages are refused by the sender */
    {
        static char big[MPSCHEMA_MAX_MSG_SZ];
        CHECK_EQ_INT(mpschema_xmit_msg(sv[0], 1, big, sizeof(big)), mpschema_invalid_param);
        CHECK_EQ_INT(mpschema_xmit_msg(sv[0], 1, big, 0), mpschema_invalid_param);
    }

    /* a corrupt header is reported */
    {
        const char bad[] = {(char)0xc1, 0x00, 0x00};
        CHECK_EQ_INT(write(sv[0], bad, sizeof(bad)), (long long)sizeof(bad));
        CHECK_EQ_INT(mpschema_pipe_process(pipe, on_message), mpschema_malformed);
    }
    mpschema_pipe_free(pipe);

    /* peer close */
    pipe = mpschema_pipe_alloc(sv[1], &ctx);
    close(sv[0]);
    CHECK_EQ_INT(mpschema_pipe_process(pipe, on_message), mpschema_closed);
    mpschema_pipe_free(pipe);
    close(sv[1]);
}

static void test_buffer(void)
{
    char mem[64];
    mpschema_buffer_t b = {0, sizeof(mem), mem};
    char *s;
    void *p;

    s = mpschema_buffer_strdup(&b, "hey", 3);
    CHECK_EQ_STR(s, "hey");
    CHECK_EQ_INT(b.used, 4);

    p = mpschema_buffer_calloc(&b, 2, 8);
    CHECK(p != NULL && ((uintptr_t)p % _Alignof(max_align_t)) == 0);

    /* failures leave the arena untouched */
    {
        size_t before = b.used;
        CHECK(mpschema_buffer_calloc(&b, 1, 1000) == NULL);
        CHECK(mpschema_buffer_calloc(&b, SIZE_MAX, 2) == NULL);
        CHECK(mpschema_buffer_strdup(&b, mem, 200) == NULL);
        CHECK_EQ_INT(b.used, before);
    }

    /* writes are truncated and reported */
    b.used = 60;
    CHECK_EQ_INT(mpschema_buffer_write(&b, "abcdef", 6), mpschema_no_space);
    CHECK_EQ_INT(b.used, 64);
}

/* ------------------------------------------------------------------------ */

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case_t;

static const test_case_t k_tests[] = {
    {"validate", test_validate},
    {"roundtrip_scalars", test_roundtrip_scalars},
    {"sparse_encoding", test_sparse_encoding},
    {"array_with_leading_zero", test_array_with_leading_zero},
    {"array_counter_clamped", test_array_counter_clamped},
    {"string_truncation", test_string_truncation},
    {"unknown_and_mismatched_values", test_unknown_and_mismatched_values},
    {"double_decoding", test_double_decoding},
    {"bool_legacy_uint", test_bool_legacy_uint},
    {"nested", test_nested},
    {"varlen_and_refers", test_varlen_and_refers},
    {"encode_errors", test_encode_errors},
    {"decode_errors", test_decode_errors},
    {"fuzz_decode", test_fuzz_decode},
    {"hostile_container_sizes", test_hostile_container_sizes},
    {"env_feed_while_pending", test_env_feed_while_pending},
    {"scan_matches_msgpack", test_scan_matches_msgpack},
    {"export", test_export},
    {"header", test_header},
    {"print_json", test_print_json},
    {"print_log", test_print_log},
    {"foreach", test_foreach},
    {"env_stream", test_env_stream},
    {"env_external_writer", test_env_external_writer},
    {"pipe", test_pipe},
    {"buffer", test_buffer},
};

int main(void)
{
    size_t i;

    for (i = 0; i < sizeof(k_tests) / sizeof(k_tests[0]); i++) {
        int before = g_failures;
        k_tests[i].fn();
        printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", k_tests[i].name);
    }
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
