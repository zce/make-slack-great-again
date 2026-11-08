# msga sources

msga (native Slack client), built on its own platform layer `plat/`. The
**first goal is the smallest possible binary and resource use**; every choice
below serves that. Runtime ownership and event-flow invariants are documented in
[ARCHITECTURE.md](../docs/ARCHITECTURE.md); fork branch, CI and release policy are
in the [root README](../README.md#maintaining-this-fork).

## Layout

| Dir     | What                                                                  |
|---------|-----------------------------------------------------------------------|
| `base/` | UTF-8, JSON, files, logging, time — the only "core library"           |
| `net/`  | HTTP requests + WebSockets: WinHTTP / NSURLSession, own client on Linux |
| `gfx/`  | CPU painter on a premultiplied ARGB32 canvas, image decoding, icons   |
| `text/` | font discovery + fallback, shaping, glyph raster cache, rich layout   |
| `ui/`   | the toolkit: view tree, layout, input routing, scroll, text editing   |
| `app/`  | model, fake (fixture) backend, mrkdwn, the screens                    |
| `tools/`| build-time generators (icons) and the size report                     |
| `plat/` | the thin OS layer: windows, input, loop, desktop services             |
| `prim/` | string, UTF-8 and hash primitives shared by plat and base           |

Each module is a static library with its own `CMakeLists.txt`; the top-level
`../CMakeLists.txt` includes the ones that exist. Dependencies only go downwards:
`app → ui → text → gfx → base`, `app → net → base`, and everything may use `plat`.

The tests live outside `src/`, in `../tests/`: one directory per module
(`tests/base/`, `tests/app/screens/shell/`, …, `tests/plat/` for plat's
standalone build), shared helpers in `tests/support/`, and everything a test
reads from disk in `tests/assets/` (the fake workspace's `fixture.json` and
its media, the SVG fixtures).

## Size rules (enforced in review and by `tools/size_report.py`)

- Build: `-Oz` (`-Os` where the compiler has no `-Oz`), no exceptions, no RTTI,
  no unwind tables, function/data sections + gc-sections, identical-code
  folding, packed relocations. The fork's Linux release artifact is built with
  GCC on Ubuntu 24.04; Clang is used for cross-compiler verification on Ubuntu
  24.04. A file that is provably hot (the rasterizer, JSON parser) may opt into
  `-O2` in its CMakeLists with a comment saying why.
- Function bodies live in `.cpp` files; headers declare. Only trivial one-liners
  inline.
- No template-heavy libraries (no rpl, no Boost, no `std::regex`, no
  `<iostream>`/`<fstream>`/`<sstream>`, no `std::locale`, no `std::filesystem`,
  no floating-point `std::to_chars`/`from_chars`: in a static binary each pulls
  in 50–350 KB of libstdc++). `std::vector`/`std::string`/`std::string_view`
  /`std::unique_ptr`/`std::function` are fine; don't instantiate a container for
  every tiny type when an index or a flat array would do.
- Prefer a `switch` on an enum over `std::variant` visitors, and data tables
  over hand-written code (UI descriptions, JSON field mapping, settings).
- Lambdas stored in `std::function` are fine where they carry real behaviour;
  don't wrap every trivial handler in its own type-erased object.
- No bundled data the OS already has (fonts, emoji, spell dictionaries, CAs).
  Rarely used tables are stored compressed and unpacked on first use.
- Every module reports its size; growth beyond the module budget fails CI.

## Style

Same as the repo: clang-format (LLVM-based, 4 spaces, 100 cols;
`../scripts/clang-format-env.sh`), `_member` fields, camelCase methods,
comments explain *why*. UI text is sentence case ("Add channels", not "Add
Channels").

## Build

```
scripts/build.sh --test         # or: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DMSGA_BUILD_TESTS=ON
cmake --build build && ctest --test-dir build
```

The fork does not use the upstream release scripts. GitHub Actions builds a
GCC/Ubuntu 24.04 Linux x86_64 preview artifact for PR testing and a separate
production artifact on successful pushes to master. The production artifact is
dynamically linked; its dependency list and checksum travel with it. Clang on
Ubuntu 24.04 provides cross-compiler verification. Public releases reuse the
production artifact from the successful CI run for the exact selected commit;
see the [root README](../README.md#maintaining-this-fork). The generic
`MSGA_STATIC` option remains available for local experiments but does not produce
a published fork artifact.

The fake backend (`app/fake/`) is opt-in: `-DMSGA_BUILD_TESTS=ON` builds the
test suites on it (on `../tests/assets/fixture.json`). Without it nothing of
it is compiled, and a release `msga` never contains it.
