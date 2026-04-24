# PassGen

PassGen is a tiny Windows-native benchmark project for generating deterministic ASCII-letter password-shaped output. Its primary deliverable is a static C library, with a small compatibility CLI for shell use.

This project is intentionally non-cryptographic. Do not use it for real passwords, account credentials, secrets, tokens, or anything security-sensitive.

## Artifacts

The build writes release artifacts to `dist\`:

```text
dist\passgen.h
dist\passgen.lib
dist\passgen.exe
```

`passgen.lib` is the primary artifact. `passgen.exe` is a CLI wrapper over the same generator core.

## Requirements

- Windows x64
- Python 3.10 or newer
- LLVM for Windows on `PATH`: `clang`, `lld-link`, and `llvm-lib`
- Windows SDK import libraries available locally, especially `kernel32.lib` and `ntdll.lib`

## Build

From a Developer PowerShell, Command Prompt, or any shell with the tools above on `PATH`:

```bat
build.bat
```

For a faster build/test without benchmark timing:

```bat
build.bat --skip-bench
```

The full build compiles all static-library variants, runs native API and CLI correctness tests, benchmarks the candidates, selects the fastest configured `single20` variant, and copies the selected files into `dist\`. The `--skip-bench` path still builds and tests the candidates, then copies the configured release variant.

The benchmark report is written to:

```text
build\benchmark-report.txt
```

## Speed Benchmarks

Last local run:

```bat
python .\scripts\build_bench.py
```

Environment:

- AMD Ryzen AI 9 HX 370 w/ Radeon 890M
- Microsoft Windows NT 10.0.26100.0
- clang 22.1.2, targeting `x86_64-pc-windows-msvc`
- Python 3.14.3

The native in-process benchmark reports per-call time after warmups. Lower is better. The full per-variant report is written to `build\benchmark-report.txt`.

Fastest measured in-process cases from the 2026-04-24 run:

| Case | Candidate | Median | p95 |
| --- | --- | ---: | ---: |
| `passgen_fill20_unchecked` (`single20`) | `repeat1-20` | `0.781 ns` | `0.976 ns` |
| `passgen_fill20_line_unchecked` (`single20-line`) | `repeat1-20` | `0.781 ns` | `0.976 ns` |
| `passgen_fill_unchecked(15, 20)` | `weyl-table` | `33.984 ns` | `34.765 ns` |
| `passgen_fill(15, 20)` | `weyl-table` | `35.156 ns` | `35.546 ns` |
| `passgen_fill_unchecked(1000, 32)` | `weyl-table` | `4.563 us` | `4.588 us` |
| `passgen_fill_unchecked(100000, 32)` | `weyl-table` | `456.950 us` | `461.200 us` |

The full build selected `repeat1-20` as the release library by `single20` median. The `repeat*-20` variants are lower-bound experiments; the `weyl-table` rows above are the fastest non-repeat generator cases from this run.

One-shot CLI timings include Windows process startup and are therefore dominated by launch overhead:

| Case | Median | p95 |
| --- | ---: | ---: |
| zero-import process floor | `3.651 ms` | `4.242 ms` |
| `passgen.exe 15 20` to file | `3.562 ms` | `3.938 ms` |
| `passgen.exe 15 20 123456789` to `NUL` | `3.650 ms` | `4.537 ms` |

## CLI Usage

```text
passgen <count> <length> [seed]
```

Examples:

```bat
dist\passgen.exe 15 20
dist\passgen.exe 15 20 123456789
dist\passgen.exe 15 20 > out.txt
```

Behavior:

- `count` and `length` must be positive decimal integers.
- `seed` is an optional decimal `uint64_t`.
- Omitting `seed` uses a hard-coded deterministic default.
- Successful output is only generated lines on stdout.
- Each line contains ASCII letters from `A-Z` and `a-z`, followed by `\n`.

## Static Library API

```c
#include "passgen.h"

int passgen_output_size(uint64_t count, uint64_t length, uint64_t *out_size);
int passgen_fill(
    char *dst,
    uint64_t dst_len,
    uint64_t count,
    uint64_t length,
    uint64_t seed,
    uint64_t *written);
void passgen_fill_unchecked(char *dst, uint64_t count, uint64_t length, uint64_t seed);
void passgen_fill20_unchecked(char *dst, uint64_t seed);
void passgen_fill20_line_unchecked(char *dst, uint64_t seed);
```

`passgen_fill` validates arguments and buffer size. `passgen_fill_unchecked` and the `fill20` functions are hot paths; the caller must provide valid arguments and enough writable memory.

Minimal example:

```c
#include "passgen.h"
#include <stdio.h>

int main(void) {
    char output[21];
    passgen_fill20_line_unchecked(output, 0);
    fwrite(output, 1, sizeof output, stdout);
    return 0;
}
```

Build the example:

```bat
clang -O3 demo.c dist\passgen.lib -I dist -o demo.exe
```

## Notes On Benchmarks

The harness uses native `QueryPerformanceCounter` timing loops. Python only orchestrates builds and report formatting.

The fastest experimental variants optimize latency over output quality. The `repeat*-20` variants exist to explore the lower bound for a single 20-character call and intentionally trade away useful password diversity. They are benchmark experiments, not meaningful password generators.

## Layout

```text
c\                  C implementation, CLI wrapper, tests, benchmark tools
include\            Public C header
scripts\            Build, test, benchmark, and selection harness
build.bat           Windows batch build entry point
build.ps1           PowerShell wrapper around the Python harness
```

## License

MIT. See `LICENSE`.
