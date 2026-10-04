# Changelog

## 2.0.0 (2026-10-04)

A restructuring of the original single-file library into a documented,
tested, and hardened project.

### Structure
- Split `mpschema.c` / `mpschema.h` into focused modules under `src/` and
  public headers under `include/mpschema/`, with `mpschema.h` as the umbrella
  header. Headers are self-contained and C++-friendly (`extern "C"`).
- Ported to the current msgpack-c API (`str`/`bin` instead of the removed
  `raw` type). Strings and binary values are both accepted on decode.
- CMake build with install/export targets, unit tests, an example, sanitizer
  and `-Werror` options, `.clang-format`, `.clang-tidy`, Doxygen config, and
  GitHub Actions CI.
- Released under the MIT license.

### Security
- Added a non-allocating scanner that validates each MessagePack object
  before msgpack-c parses it. Previously a few bytes declaring a huge
  array or map made msgpack-c request gigabytes of memory.
- The streaming decoder buffers input itself and caps buffered data at
  `MPSCHEMA_ENV_MAX_BUFFERED`.
- `mpschema_xmit_msg` no longer copies the payload into a fixed stack buffer
  without a size check (stack overflow on large messages).
- Map keys are type-checked before use; out-of-range integers are rejected
  instead of silently truncated.
- Arena allocation no longer consumes the remaining space on failure, checks
  for size overflow, and always aligns allocations.
- JSON output escapes strings in quoted mode and never reads past its scratch
  buffers.

### Bug fixes
- `DOUBLE` decoding fell through into the `VARLEN_STRING` case.
- `BOOL` members were encoded but never decoded.
- An array whose first element was zero was counted in the map header but
  then skipped, producing corrupt output.
- Element counters larger than the array capacity caused out-of-bounds reads.
- Fixed strings without a terminating NUL were read past their end.
- Encoder and decoder errors were combined with `|=`, producing meaningless
  status codes; some were swallowed entirely (e.g. a failed map header
  returned success). Errors now propagate unchanged.
- Decoding a buffer that was not a single complete object returned success.
- `mpschema_pipe_process`: stored `read()`'s result in an unsigned variable,
  so errors were never detected; treated a partially received header as a
  fatal error; ignored handler errors for small messages; and could stall
  with a complete message buffered. It is now a buffered parser that
  dispatches every complete frame.
- `mpschema_print`: ruler buffer overflow with long names; missing newline
  after doubles; bools never printed.
- `mpschema_print_json`: bools produced dangling separators; `uint32` and
  `uint64` were printed as signed; arrays are now rendered as JSON arrays.
- `mpschema_env_init` accepted `external_writer` without a writer callback.
- `mpschema_get_header_max_size` never cached its result.
- `mpschema_decode_obj_by_format` left obfuscated input de-obfuscated; the
  caller's buffer is now restored.

### API changes
- Schema and input-object parameters are now `const`.
- `mpschema_foreach_cb_t` receives `const void *data` instead of
  `const char *`.
- Status codes are `mpschema_status_t` (`mpschema_env_status_t` remains as an
  alias). New codes: `mpschema_io_error`, `mpschema_incomplete`,
  `mpschema_closed`. `mpschema_err_max` was removed.
- `mpschema_decode_header` returns `mpschema_incomplete` when more bytes are
  needed.
- `mpschema_pipe_process` returns `mpschema_closed` when the peer disconnects.
- `BOOL` members are encoded as MessagePack booleans; integers are still
  accepted when decoding.
- `mpschema_env_cback_t::debug_cb` / `warning_cb`, when set, are installed
  as the process-wide logging hooks.
- Removed the undefined `mpschema_process_msgs` declaration and the internal
  `mpschema_test`, `mpschema_dump` and `g_mpschema_debug` symbols.
