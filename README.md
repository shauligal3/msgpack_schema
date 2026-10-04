# mpschema

[![CI](https://github.com/shauligal3/msgpack_schema/actions/workflows/ci.yml/badge.svg)](https://github.com/shauligal3/msgpack_schema/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**Schema-driven MessagePack serialization for plain C structs.**

mpschema lets you send ordinary C structs over the wire without writing
per-message packing code. You describe each struct once with a few macros; the
library walks that description to encode the struct into a compact
[MessagePack](https://msgpack.org) map and to decode it back, with bounds
checks, forward compatibility and a streaming decoder for sockets.

It is aimed at control channels and telemetry: messages are small, cheap to
produce, and can evolve without breaking older peers.

```c
typedef struct demo_point_ { uint32_t x, y; char label[16]; } demo_point_t;

static mpschema_t point_schema = {
    MPSCHEMAV2_DEF(point, MSG_POINT)
    MPSCHEMA_MEMBER(point, x, UINT32),
    MPSCHEMA_MEMBER(point, y, UINT32),
    MPSCHEMA_STRING(point, label, 16),
    MPSCHEMA_MEMBER_EOF
};

demo_point_t p = { .x = 3, .y = 4, .label = "home" };
mpschema_env_encode(env, &point_schema, &p, buf, &len);   /* 13 bytes on the wire (24 in memory) */
```

## Features

- **Declarative schemas** next to your structs. No code generator and no IDL
  compiler: schemas are static data built by macros.
- **Compact, sparse encoding.** Members are keyed by small integer tags, and
  zero, empty or NULL members are omitted entirely.
- **Forward and backward compatible.** Unknown tags are ignored, retired tags
  keep their slot (`MPSCHEMA_MEMBER_OBSOLETE`), and type mismatches are skipped
  rather than fatal.
- **Rich member types:** booleans, unsigned integers up to 64 bits, doubles,
  fixed and variable-length strings, scalar arrays, nested structs, arrays of
  structs, and pointers to structs.
- **Zero heap allocation on the decode path for the struct itself.**
  Variable-length data (strings, referenced structs) is carved out of a
  caller-supplied arena placed right after the struct.
- **Streaming decoder** (`mpschema_env_*`) that reassembles objects from
  arbitrary chunks and identifies each object's type from the data itself.
- **Hardened against hostile input.** Every write is bounds-checked, and a
  non-allocating pre-scan stops a few malicious bytes from making msgpack-c
  reserve gigabytes (see Security below).
- **Socket helpers** for length-framed messages, plus debug printing and JSON
  rendering of any described struct.

## Quick start

### Requirements

- A C11 compiler with GNU extensions (GCC or Clang)
- CMake 3.16+
- [msgpack-c](https://github.com/msgpack/msgpack-c) 6.x (the `c_master`
  branch, CMake package `msgpack-c`)

```sh
# Install msgpack-c if your distribution does not package it
git clone --depth 1 -b c_master https://github.com/msgpack/msgpack-c.git
cmake -S msgpack-c -B msgpack-c/build -DMSGPACK_BUILD_TESTS=OFF -DMSGPACK_BUILD_EXAMPLES=OFF
sudo cmake --build msgpack-c/build --target install

# Build, test and run the example
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/examples/telemetry
```

Example output:

```
encoded 41 bytes (the struct is 48 bytes in memory)
first chunk: object incomplete, waiting for more bytes
decoded object type 1: rc=0
{"sensor_id": 42, "unit": "kPa", "value": 101.325, "calibrated": true, "history": [99, 100, 101], "where": {"lat_e6": 32085300, "lon_e6": 34781800 } }
```

### Using it in your CMake project

```cmake
add_subdirectory(msgpack_schema)        # or install it and use find_package
target_link_libraries(my_app PRIVATE mpschema::mpschema)
```

## Declaring a schema

1. Define `MPSCHEMA_MODULE` as the common prefix of your struct type names. A
   message called `point` then refers to the type `<MPSCHEMA_MODULE>point_t`.
2. Give every member a tag with an enumerator named `e_<msg>__<member>`. Tags
   must match the member's position in the schema. v2 schemas reserve tag 0
   for the object type.
3. List the members between a `*_DEF` macro and `MPSCHEMA_MEMBER_EOF`.

```c
#define MPSCHEMA_MODULE demo_

typedef struct demo_reading_ {
    uint32_t sensor_id;
    char     unit[8];
    double   value;
    uint16_t history[5];
    uint16_t history_ct;        /* arrays need an element counter */
} demo_reading_t;

enum { e_reading__objtype, e_reading__sensor_id, e_reading__unit,
       e_reading__value, e_reading__history };

static mpschema_t reading_schema = {
    MPSCHEMAV2_DEF(reading, MSG_READING)
    MPSCHEMA_MEMBER(reading, sensor_id, UINT32),
    MPSCHEMA_STRING(reading, unit, sizeof(((demo_reading_t *)0)->unit)),
    MPSCHEMA_MEMBER(reading, value, DOUBLE),
    MPSCHEMA_ARRAY(reading, history, UINT16, 5),
    MPSCHEMA_MEMBER_EOF
};
```

Call `mpschema_validate(&reading_schema)` once at start-up (or in a unit test)
to catch numbering mistakes.

### Member macros

| Macro | C declaration | Notes |
|---|---|---|
| `MPSCHEMA_MEMBER(msg, mem, TYPE)` | `uint8_t`…`uint64_t`, `double`, `uint8_t` for `BOOL` | `TYPE` is `BOOL`, `UINT8`, `UINT16`, `UINT32`, `UINT64` or `DOUBLE` |
| `MPSCHEMA_MEMBER_RECORD_SET(msg, mem, TYPE)` | as above, plus `uint8_t set_<mem>` | the flag is set when the member was decoded, for when 0 is meaningful |
| `MPSCHEMA_STRING(msg, mem, size)` | `char mem[size]` | at most `size - 1` characters are sent |
| `MPSCHEMA_VARLEN_STRING(msg, mem, max)` | `char *mem` | decoded into the arena; `max` limits the length (0 = unlimited) |
| `MPSCHEMA_ARRAY(msg, mem, TYPE, max)` | `TYPE mem[max]; uint16_t mem_ct;` | |
| `MPSCHEMA_NESTED(msg, mem, &schema)` | an embedded struct | |
| `MPSCHEMA_NARRAY(msg, mem, &schema, nested_msg, max)` | `nested_t mem[max]; uint16_t mem_ct;` | |
| `MPSCHEMA_REFERS(msg, mem, &schema, nested_msg)` | `nested_t *mem` | pointee decoded into the arena |
| `MPSCHEMA_NREFER(msg, mem, &schema, nested_msg, max)` | `nested_t *mem; uint16_t mem_ct;` | array decoded into the arena |
| `MPSCHEMA_MEMBER_OBSOLETE` | none | keeps a retired tag's slot |

| Schema opener | Use for |
|---|---|
| `MPSCHEMAV2_DEF(msg, type)` | v2 objects: the object type travels in the message (tag 0) |
| `MPSCHEMA_VARLEN_DEF(msg, type)` | v1 objects that need an arena (records the struct size) |
| `MPSCHEMA_DEF(type)` | plain v1 objects |

## API overview

Include `<mpschema/mpschema.h>`. Every header is documented in Doxygen style.

| Area | Functions | Header |
|---|---|---|
| One-shot codec | `mpschema_encode_obj`, `mpschema_decode_obj`, `mpschema_decode_varlen_obj_by_format`, `mpschema_export_obj`, `mpschema_validate` | [`codec.h`](include/mpschema/codec.h) |
| Streaming (v2) | `mpschema_env_init`, `mpschema_env_feed`, `mpschema_env_next`, `mpschema_env_next_decode`, `mpschema_env_encode` | [`env.h`](include/mpschema/env.h) |
| Sockets | `mpschema_xmit_obj`, `mpschema_pipe_alloc`, `mpschema_pipe_process` | [`transport.h`](include/mpschema/transport.h) |
| Framing | `mpschema_encode_header`, `mpschema_decode_header` | [`header.h`](include/mpschema/header.h) |
| Inspection | `mpschema_print`, `mpschema_print_json_2`, `mpschema_foreach` | [`inspect.h`](include/mpschema/inspect.h) |
| Arena | `mpschema_buffer_calloc`, `mpschema_buffer_strdup` | [`buffer.h`](include/mpschema/buffer.h) |
| Diagnostics | `mpschema_set_logging` | [`log.h`](include/mpschema/log.h) |

All functions return `mpschema_ok` (0) or a negative `mpschema_status_t`.

### Streaming decode

```c
mpschema_env_t *env = mpschema_env_init(NULL, NULL);

void on_bytes(const char *chunk, size_t len)
{
    uint16_t type;

    mpschema_env_feed(env, chunk, len);
    while (mpschema_env_next(env, &type) == mpschema_ok) {
        const mpschema_t *schema = schema_for(type);   /* your lookup table */
        union any_message msg = {0};
        mpschema_env_next_decode(env, schema, &msg, sizeof(msg));
        dispatch(type, &msg);
    }
}
```

### Variable-length data and the arena

Strings and referenced structs are decoded into the memory that follows the
struct, so a whole message lives in one buffer and is freed in one go:

```c
union { demo_doc_t doc; char bytes[sizeof(demo_doc_t) + 512]; } out = {0};
mpschema_decode_varlen_obj_by_format(&out, sizeof(out), &doc_schema, buf, len,
                                     MPSCHEMA_MSGPACK_CLEAR);
printf("%s\n", out.doc.title);   /* points into out.bytes */
```

## Wire format

An object is a MessagePack **map from tag to value**:

```
v1:  { 1: 3, 2: 4, 3: "home" }
v2:  { 0: <object type>, 1: 3, 2: 4, 3: "home" }
```

- Integers use MessagePack's smallest encoding, so a small tag costs one byte.
- Arrays (`sm_max_elems > 1`) are MessagePack arrays; nested structs are maps.
- Zero, empty and NULL members are omitted. Decoding leaves omitted members
  untouched, so zero-initialise the output struct first.
- `mpschema_export_obj` produces the same structure keyed by member **names**,
  for consumers that do not have the schema (log pipelines, scripts).

The socket helpers frame each message as `[type][length][payload]`, where
`type` and `length` are MessagePack unsigned integers (2 to 6 bytes in total).
v2 streams need no framing because every object carries its type.

`MPSCHEMA_MSGPACK_OBF` applies a rolling XOR to the payload. It deters casual
inspection only and is **not encryption**: use TLS when confidentiality
matters.

## Security

The decoder treats its input as untrusted:

- **Bounded writes.** Every write into the output struct and arena is checked
  against the size the caller supplied. Fixed strings are truncated to fit,
  arrays are clamped to their capacity, and integers that do not fit their
  member are rejected.
- **Bounded allocation.** msgpack-c reserves memory for a container as soon as
  it reads the container's header, based on the *declared* element count.
  Unchecked, a 5-byte message declaring a 4-billion-element map makes it ask
  the allocator for over 100 GB. mpschema first walks each object with a
  non-allocating scanner ([`src/scan.c`](src/scan.c)) and only parses objects
  whose declared elements are all present, so memory use is proportional to
  the input size. The streaming decoder also caps how much it buffers
  (`MPSCHEMA_ENV_MAX_BUFFERED`).
- **Fuzzed.** The test suite feeds tens of thousands of random inputs through
  the decoders under AddressSanitizer and UndefinedBehaviorSanitizer, and
  differentially checks the scanner's object boundaries against msgpack-c.

## Project layout

```
include/mpschema/   public headers (umbrella: mpschema.h)
src/                library sources
  schema.c            member access helpers and schema validation
  encode.c            struct -> MessagePack
  decode.c            MessagePack -> struct
  scan.c              non-allocating framing scanner
  codec.c             one-shot public API, wire formats
  env.c               v2 streaming environment
  header.c            message framing header
  transport.c         sockets: send and receive framed messages
  inspect.c           foreach, debug print, JSON
  buffer.c            bump-pointer buffer / arena
  log.c               diagnostics hooks
tests/              unit tests (no external framework)
examples/           runnable example
```

## Development

```sh
# Strict build with sanitizers
cmake -S . -B build-asan -DMPSCHEMA_SANITIZE=ON -DMPSCHEMA_WERROR=ON
cmake --build build-asan && ./build-asan/tests/mpschema_tests

# Formatting and static analysis
clang-format --dry-run --Werror include/mpschema/*.h src/*.[ch] tests/*.[ch] examples/*.c
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build src/*.c

# API reference (HTML in apidocs/html)
doxygen Doxyfile
```

CI runs all of the above on GCC and Clang for every push.

## Limitations

- The object type, tags and array counters are 16-bit.
- Integer members are unsigned. Use a cast or an offset for signed data.
- Schema definitions rely on a GCC/Clang extension (static initialisation of
  a flexible array member).
- The logging hooks are process-wide.

## License

[MIT](LICENSE)
