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

The full build compiles all static-library variants, runs native API and CLI correctness tests, benchmarks the candidates, selects the fastest in-process suite winner, and copies the selected files into `dist\`. The `--skip-bench` path still builds and tests the candidates, then copies the configured release variant.

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

The native in-process benchmark reports per-call time after warmups. Lower is better. The release library is selected by the geometric mean of the six in-process median timings, not by `single20` alone. The full per-variant report is written to `build\benchmark-report.txt`.

Five-run median results from the 2026-04-28 native AVX512 optimization pass:

| Case | Candidate | Median | p95 |
| --- | --- | ---: | ---: |
| `passgen_fill20_unchecked` (`single20`) | `repeat1-20` | `0.781 ns` | `0.976 ns` |
| `passgen_fill20_line_unchecked` (`single20-line`) | `repeat1-20` | `0.781 ns` | `0.976 ns` |
| suite `single20` | `weyl-avx512` | `2.929 ns` | `2.929 ns` |
| suite `single20-line` | `weyl-avx512` | `2.929 ns` | `2.929 ns` |
| `passgen_fill_unchecked(15, 20)` | `weyl-avx512` | `35.937 ns` | `36.328 ns` |
| `passgen_fill(15, 20)` | `weyl-avx512` | `36.718 ns` | `37.500 ns` |
| `passgen_fill_unchecked(1000, 32)` | `weyl-avx512` | `0.513 us` | `0.513 us` |
| `passgen_fill_unchecked(100000, 32)` | `weyl-avx512` | `51.100 us` | `54.200 us` |

The five-run median suite score was `81.526 ns` for the selected AVX512 path. The `repeat*-20` variants still win the isolated `single20` lower-bound test, but the release library is selected by the full suite so the much faster fixed-32 AVX512 path is included.

One-shot CLI timings include Windows process startup and are therefore dominated by launch overhead:

| Case | Median | p95 |
| --- | ---: | ---: |
| zero-import process floor | `4.342 ms` | `5.448 ms` |
| `passgen.exe 15 20` to file | `4.494 ms` | `5.465 ms` |
| `passgen.exe 15 20 123456789` to `NUL` | `4.350 ms` | `5.360 ms` |

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
