# miniz (vendored)

Single-file-pair zip/deflate library: `miniz.c` + `miniz.h` only.

- Upstream: https://github.com/richgel999/miniz, tag 3.0.2 (MZ_VERSION "11.0.2")
- Files are the project's own amalgamation output (`amalgamate.sh`,
  i.e. `cmake -DAMALGAMATE_SOURCES=ON`), copied unmodified.
- License: MIT (see `LICENSE`).
- Used by `lib/outbound_archiver.cpp` to write the outbound archive.
