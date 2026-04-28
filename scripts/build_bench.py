#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
DIST = ROOT / "dist"
INCLUDE = ROOT / "include"
REPORT = BUILD / "benchmark-report.txt"

NATIVE_FLAGS = [
    "-Ofast",
    "-funroll-loops",
    "-mavx512f",
    "-mavx512bw",
    "-mavx512vl",
    "-mavx2",
    "-mbmi2",
]

LTO_FLAGS = ["-flto=full"]


@dataclass(frozen=True)
class VariantSpec:
    name: str
    variant: int
    flags: tuple[str, ...] = ()


VARIANTS = [
    VariantSpec("xorshift-mulhi", 1),
    VariantSpec("xorshift-table", 2),
    VariantSpec("wyrand-mulhi", 3),
    VariantSpec("wyrand-table", 4),
    VariantSpec("pcg-mulhi", 5),
    VariantSpec("pcg-table", 6),
    VariantSpec("xorshift-raw-table", 7),
    VariantSpec("weyl-table", 8),
    VariantSpec("weyl-xor-table", 9),
    VariantSpec("repeat1-20", 10),
    VariantSpec("repeat3-20", 11),
    VariantSpec("repeat1-20-native", 10, tuple(NATIVE_FLAGS)),
    VariantSpec("weyl-table-native", 8, tuple(NATIVE_FLAGS)),
    VariantSpec("weyl-mask", 12, tuple(NATIVE_FLAGS)),
    VariantSpec("weyl-avx512", 13, tuple(NATIVE_FLAGS)),
    VariantSpec("weyl-avx512-lto", 13, tuple(NATIVE_FLAGS + LTO_FLAGS)),
]

DEFAULT_SKIP_BENCH_WINNER = "weyl-avx512"

SUITE_CASES = [
    "single20",
    "single20-line",
    "unchecked-15x20",
    "safe-15x20",
    "unchecked-medium",
    "unchecked-large",
]


@dataclass
class BuildResult:
    name: str
    path: Path | None
    ok: bool
    detail: str


@dataclass
class LibCandidate:
    name: str
    variant: int
    lib: Path
    bench_exe: Path
    test_exe: Path


def run_command(cmd: list[str], *, cwd: Path = ROOT) -> tuple[int, str]:
    completed = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return completed.returncode, completed.stdout.strip()


def latest_import_lib(name: str) -> Path | None:
    roots = [
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Windows Kits",
    ]
    matches: list[Path] = []
    for root in roots:
        if root.exists():
            matches.extend(root.glob(fr"**\Lib\*\um\x64\{name}"))
    if not matches:
        path = shutil.which(name)
        return Path(path) if path else None
    return sorted(matches, key=lambda p: p.as_posix())[-1]


def clang() -> str | None:
    return shutil.which("clang")


def lld_link() -> str | None:
    return shutil.which("lld-link")


def llvm_lib() -> str | None:
    return shutil.which("llvm-lib")


def compile_obj(source: Path, obj: Path, extra: list[str] | None = None, opt: str = "-O3") -> tuple[int, str]:
    cc = clang()
    if not cc:
        return 1, "clang was not found on PATH"
    obj.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        cc,
        "-target",
        "x86_64-pc-windows-msvc",
        opt,
        "-march=native",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-builtin-memset",
        "-fno-stack-protector",
        "-ffunction-sections",
        "-fdata-sections",
        "-I",
        str(INCLUDE),
    ]
    if extra:
        cmd.extend(extra)
    cmd.extend(["-c", str(source), "-o", str(obj)])
    return run_command(cmd)


def link_exe(obj: Path, exe: Path, libs: list[Path], entry: str = "mainCRTStartup") -> tuple[int, str]:
    linker = lld_link()
    if not linker:
        return 1, "lld-link was not found on PATH"
    exe.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        linker,
        "/nologo",
        "/machine:x64",
        f"/entry:{entry}",
        "/subsystem:console",
        "/nodefaultlib",
        "/opt:ref",
        "/opt:icf",
        f"/out:{exe}",
        str(obj),
    ]
    cmd.extend(str(lib) for lib in libs)
    return run_command(cmd)


def build_static_lib(spec: VariantSpec) -> BuildResult:
    archiver = llvm_lib()
    if not archiver:
        return BuildResult(spec.name, None, False, "llvm-lib was not found on PATH")

    out_dir = BUILD / "lib" / spec.name
    obj = out_dir / "passgen_core.obj"
    lib = out_dir / f"passgen-{spec.name}.lib"
    code, output = compile_obj(
        ROOT / "c" / "passgen_core.c",
        obj,
        [f"-DPASSGEN_VARIANT={spec.variant}", *spec.flags],
    )
    if code != 0:
        return BuildResult(spec.name, None, False, output)

    code, output = run_command([archiver, "/nologo", f"/out:{lib}", str(obj)])
    if code == 0 and lib.exists():
        detail = f"built variant {spec.variant}"
        if spec.flags:
            detail += f" flags={' '.join(spec.flags)}"
        return BuildResult(spec.name, lib, True, detail)
    return BuildResult(spec.name, None, False, output or f"missing {lib}")


def build_api_test(candidate: BuildResult) -> BuildResult:
    kernel32 = latest_import_lib("kernel32.lib")
    if not kernel32:
        return BuildResult(f"{candidate.name}-api-test", None, False, "kernel32.lib was not found")
    assert candidate.path is not None

    out_dir = BUILD / "lib" / candidate.name
    obj = out_dir / "passgen_api_test.obj"
    exe = out_dir / "passgen_api_test.exe"
    code, output = compile_obj(ROOT / "c" / "passgen_api_test.c", obj)
    if code != 0:
        return BuildResult(f"{candidate.name}-api-test", None, False, output)

    code, output = link_exe(obj, exe, [candidate.path, kernel32])
    if code == 0 and exe.exists():
        return BuildResult(f"{candidate.name}-api-test", exe, True, "built")
    return BuildResult(f"{candidate.name}-api-test", None, False, output or f"missing {exe}")


def build_lib_bench(candidate: BuildResult) -> BuildResult:
    kernel32 = latest_import_lib("kernel32.lib")
    if not kernel32:
        return BuildResult(f"{candidate.name}-libbench", None, False, "kernel32.lib was not found")
    assert candidate.path is not None

    out_dir = BUILD / "lib" / candidate.name
    obj = out_dir / "libbench.obj"
    exe = out_dir / "libbench.exe"
    code, output = compile_obj(ROOT / "c" / "libbench.c", obj, opt="-O2")
    if code != 0:
        return BuildResult(f"{candidate.name}-libbench", None, False, output)

    code, output = link_exe(obj, exe, [candidate.path, kernel32])
    if code == 0 and exe.exists():
        return BuildResult(f"{candidate.name}-libbench", exe, True, "built")
    return BuildResult(f"{candidate.name}-libbench", None, False, output or f"missing {exe}")


def run_exe(exe: Path, args: list[str] | None = None, timeout: int = 60) -> tuple[int, bytes, bytes]:
    completed = subprocess.run(
        [str(exe), *(args or [])],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )
    return completed.returncode, completed.stdout, completed.stderr


def native_stats(raw: bytes, unit: str) -> dict[str, float]:
    parts = raw.decode("ascii", errors="replace").strip().split()
    if len(parts) != 3:
        raise RuntimeError(f"native benchmark returned unexpected output: {raw!r}")
    min_value, median_value, p95_value = (int(part) for part in parts)
    divisor = 1_000_000 if unit == "ns" else 1_000_000_000
    return {
        "min_ms": min_value / divisor,
        "median_ms": median_value / divisor,
        "p95_ms": p95_value / divisor,
    }


def benchmark_lib(
    bench_exe: Path,
    iterations: int,
    warmups: int,
    repeats: int,
    mode: str,
    count: int,
    length: int,
    seed: int,
) -> dict[str, float]:
    code, stdout, stderr = run_exe(
        bench_exe,
        [str(iterations), str(warmups), str(repeats), mode, str(count), str(length), str(seed)],
        timeout=180,
    )
    if code != 0:
        raise RuntimeError(f"{bench_exe} failed: exit {code}, stdout={stdout!r}, stderr={stderr!r}")
    return native_stats(stdout, "ps")


def assert_valid_password_output(raw: bytes, count: int, length: int) -> None:
    if not raw.endswith(b"\n"):
        raise AssertionError("output does not end with a newline")
    lines = raw.splitlines()
    if len(lines) != count:
        raise AssertionError(f"expected {count} lines, got {len(lines)}")
    for line in lines:
        if len(line) != length:
            raise AssertionError(f"expected line length {length}, got {len(line)}")
        if not re.fullmatch(rb"[A-Za-z]+", line):
            raise AssertionError(f"non-letter output line: {line!r}")


def cli_correctness(exe: Path) -> list[str]:
    failures: list[str] = []

    def record(name: str, fn) -> None:
        try:
            fn()
        except Exception as exc:  # noqa: BLE001
            failures.append(f"{name}: {exc}")

    def basic() -> None:
        code, stdout, stderr = run_exe(exe, ["15", "20"])
        if code != 0:
            raise AssertionError(f"exit {code}, stderr={stderr!r}")
        assert_valid_password_output(stdout, 15, 20)

    def deterministic_default() -> None:
        a = run_exe(exe, ["15", "20"])
        b = run_exe(exe, ["15", "20"])
        if a[1] != b[1]:
            raise AssertionError("default output differs across runs")

    def seed_behavior() -> None:
        a = run_exe(exe, ["15", "20", "123456789"])
        b = run_exe(exe, ["15", "20", "123456789"])
        c = run_exe(exe, ["15", "20", "987654321"])
        if a[0] != 0 or b[0] != 0 or c[0] != 0:
            raise AssertionError("seeded invocation failed")
        if a[1] != b[1]:
            raise AssertionError("same explicit seed produced different output")
        if a[1] == c[1]:
            raise AssertionError("different explicit seeds produced identical output")

    invalid_cases = [
        ["0", "20"],
        ["15", "0"],
        ["-1", "20"],
        ["15", "-1"],
        ["x", "20"],
        ["15"],
        ["15", "20", "1", "2"],
        ["18446744073709551616", "20"],
        ["15", "18446744073709551616"],
        ["15", "20", "18446744073709551616"],
    ]

    def invalid_inputs() -> None:
        for args in invalid_cases:
            code, stdout, _stderr = run_exe(exe, args)
            if code == 0:
                raise AssertionError(f"{args!r} unexpectedly succeeded")
            if stdout:
                raise AssertionError(f"{args!r} wrote stdout: {stdout!r}")

    record("basic output", basic)
    record("deterministic default", deterministic_default)
    record("seed behavior", seed_behavior)
    record("invalid inputs", invalid_inputs)
    return failures


def build_cli(selected_lib: Path) -> BuildResult:
    ntdll = latest_import_lib("ntdll.lib")
    if not ntdll:
        return BuildResult("passgen-cli", None, False, "ntdll.lib was not found")
    out_dir = BUILD / "cli"
    obj = out_dir / "passgen_nt.obj"
    exe = out_dir / "passgen.exe"
    code, output = compile_obj(ROOT / "c" / "passgen_nt.c", obj)
    if code != 0:
        return BuildResult("passgen-cli", None, False, output)
    code, output = link_exe(obj, exe, [selected_lib, ntdll])
    if code == 0 and exe.exists():
        return BuildResult("passgen-cli", exe, True, f"built with {selected_lib.name}")
    return BuildResult("passgen-cli", None, False, output or f"missing {exe}")


def build_native_process_bench() -> BuildResult:
    kernel32 = latest_import_lib("kernel32.lib")
    if not kernel32:
        return BuildResult("native-process-bench", None, False, "kernel32.lib was not found")
    out_dir = BUILD / "bench-tool"
    obj = out_dir / "bench.obj"
    exe = out_dir / "bench.exe"
    code, output = compile_obj(ROOT / "c" / "bench.c", obj, opt="-O2")
    if code != 0:
        return BuildResult("native-process-bench", None, False, output)
    code, output = link_exe(obj, exe, [kernel32])
    if code == 0 and exe.exists():
        return BuildResult("native-process-bench", exe, True, "built")
    return BuildResult("native-process-bench", None, False, output or f"missing {exe}")


def build_noop() -> BuildResult:
    out_dir = BUILD / "c"
    obj = out_dir / "noop-nt.obj"
    exe = out_dir / "noop-nt.exe"
    code, output = compile_obj(ROOT / "c" / "noop_nt.c", obj)
    if code != 0:
        return BuildResult("noop-zero-import", None, False, output)
    code, output = link_exe(obj, exe, [])
    if code == 0 and exe.exists():
        return BuildResult("noop-zero-import", exe, True, "built with no imports")
    return BuildResult("noop-zero-import", None, False, output or f"missing {exe}")


def benchmark_process(bench: Path, exe: Path, iterations: int, warmups: int, sink: str, args: list[str]) -> dict[str, float]:
    bench_dir = BUILD / "bench"
    bench_dir.mkdir(parents=True, exist_ok=True)
    target = "-" if sink == "nul" else str(bench_dir / f"cli-{sink}.tmp")
    code, stdout, stderr = run_exe(
        bench,
        [str(iterations), str(warmups), sink, target, str(exe), *args],
        timeout=180,
    )
    if code != 0:
        raise RuntimeError(f"process benchmark failed: exit {code}, stdout={stdout!r}, stderr={stderr!r}")
    return native_stats(stdout, "ns")


def write_report(lines: list[str]) -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n", encoding="utf-8")


def fmt_stats(stats: dict[str, float]) -> str:
    return (
        f"min={stats['min_ms']:.9f}ms/{stats['min_ms'] * 1_000_000:.3f}ns "
        f"median={stats['median_ms']:.9f}ms/{stats['median_ms'] * 1_000_000:.3f}ns "
        f"p95={stats['p95_ms']:.9f}ms/{stats['p95_ms'] * 1_000_000:.3f}ns"
    )


def suite_score(results: dict[str, dict[str, float]]) -> float:
    return math.exp(sum(math.log(results[label]["median_ms"]) for label in SUITE_CASES) / len(SUITE_CASES))


def fmt_score(score_ms: float) -> str:
    return f"{score_ms:.9f}ms/{score_ms * 1_000_000:.3f}ns"


def main() -> int:
    parser = argparse.ArgumentParser(description="Build, test, benchmark, and select PassGen static-library candidate.")
    parser.add_argument("--primary-iterations", type=int, default=10000)
    parser.add_argument("--warmups", type=int, default=1000)
    parser.add_argument("--process-iterations", type=int, default=300)
    parser.add_argument("--process-warmups", type=int, default=50)
    parser.add_argument("--skip-bench", action="store_true", help="build and test only; copy first passing library")
    args = parser.parse_args()

    BUILD.mkdir(parents=True, exist_ok=True)
    DIST.mkdir(parents=True, exist_ok=True)

    report: list[str] = ["PassGen static-library build and benchmark report", ""]
    report.append("Static library builds:")

    candidate_results = [build_static_lib(spec) for spec in VARIANTS]
    candidates: list[LibCandidate] = []
    for result in candidate_results:
        status = "ok" if result.ok else "failed"
        report.append(f"- {result.name}: {status} - {result.detail}")
        if not result.ok or not result.path:
            continue

        test_result = build_api_test(result)
        bench_result = build_lib_bench(result)
        report.append(f"  api-test build: {'ok' if test_result.ok else 'failed'} - {test_result.detail}")
        report.append(f"  libbench build: {'ok' if bench_result.ok else 'failed'} - {bench_result.detail}")
        if test_result.ok and test_result.path:
            code, stdout, stderr = run_exe(test_result.path)
            if code == 0:
                report.append("  api-test run: ok")
            else:
                report.append(f"  api-test run: failed exit={code} stdout={stdout!r} stderr={stderr!r}")
                continue
        else:
            continue

        if bench_result.ok and bench_result.path:
            candidates.append(LibCandidate(result.name, 0, result.path, bench_result.path, test_result.path))

    if not candidates:
        write_report(report)
        print("\n".join(report), file=sys.stderr)
        return 1

    winner = candidates[0]
    lib_results: dict[str, dict[str, dict[str, float]]] = {}

    if not args.skip_bench:
        report.append("")
        report.append("In-process library benchmarks:")
        for candidate in candidates:
            lib_results[candidate.name] = {}
            report.append(f"- {candidate.name}:")
            cases = [
                ("single20", "single20", 1, 20, 0, args.primary_iterations, args.warmups, 512),
                ("single20-line", "single20-line", 1, 20, 0, args.primary_iterations, args.warmups, 512),
                ("unchecked-15x20", "unchecked", 15, 20, 0, args.primary_iterations, args.warmups, 256),
                ("safe-15x20", "safe", 15, 20, 0, args.primary_iterations, args.warmups, 256),
                ("unchecked-medium", "unchecked", 1000, 32, 0, max(2000, args.primary_iterations // 10), 200, 8),
                ("unchecked-large", "unchecked", 100000, 32, 0, max(20, args.primary_iterations // 500), 5, 1),
            ]
            for label, mode, count, length, seed, iterations, warmups, repeats in cases:
                stats = benchmark_lib(candidate.bench_exe, iterations, warmups, repeats, mode, count, length, seed)
                lib_results[candidate.name][label] = stats
                report.append(f"  {label} (repeat={repeats}): {fmt_stats(stats)}")

        suite_winner = min(candidates, key=lambda c: suite_score(lib_results[c.name]))
        single20_winner = min(candidates, key=lambda c: lib_results[c.name]["single20"]["median_ms"])
        winner = suite_winner
        report.append("")
        report.append("Suite scores (geomean of in-process medians):")
        for candidate in sorted(candidates, key=lambda c: suite_score(lib_results[c.name])):
            report.append(f"- {candidate.name}: {fmt_score(suite_score(lib_results[candidate.name]))}")
        report.append("")
        report.append(f"Library winner: {winner.name} by suite score")
        report.append(
            f"Single20 winner: {single20_winner.name} "
            f"({fmt_stats(lib_results[single20_winner.name]['single20'])})"
        )
    else:
        winner = next((candidate for candidate in candidates if candidate.name == DEFAULT_SKIP_BENCH_WINNER), winner)
        report.append("")
        report.append(f"Library winner: {winner.name} (benchmark skipped)")

    cli_result = build_cli(winner.lib)
    process_bench = build_native_process_bench()
    noop = build_noop()
    report.append("")
    report.append("Compatibility CLI:")
    report.append(f"- cli build: {'ok' if cli_result.ok else 'failed'} - {cli_result.detail}")
    report.append(f"- native process bench: {'ok' if process_bench.ok else 'failed'} - {process_bench.detail}")
    report.append(f"- noop floor: {'ok' if noop.ok else 'failed'} - {noop.detail}")

    if not cli_result.ok or not cli_result.path:
        write_report(report)
        print("\n".join(report), file=sys.stderr)
        return 1

    failures = cli_correctness(cli_result.path)
    if failures:
        report.append("- cli correctness: failed")
        report.extend(f"  {failure}" for failure in failures)
        write_report(report)
        print("\n".join(report), file=sys.stderr)
        return 1
    report.append("- cli correctness: ok")

    if not args.skip_bench and process_bench.ok and process_bench.path:
        report.append("")
        report.append("One-shot process benchmarks:")
        if noop.ok and noop.path:
            floor = benchmark_process(process_bench.path, noop.path, args.process_iterations, args.process_warmups, "nul", [])
            report.append(f"- zero-import process floor: {fmt_stats(floor)}")
        cli_file = benchmark_process(process_bench.path, cli_result.path, args.process_iterations, args.process_warmups, "file", ["15", "20"])
        cli_seed = benchmark_process(process_bench.path, cli_result.path, args.process_iterations, args.process_warmups, "nul", ["15", "20", "123456789"])
        report.append(f"- passgen.exe 15x20 file: {fmt_stats(cli_file)}")
        report.append(f"- passgen.exe 15x20 seed nul: {fmt_stats(cli_seed)}")

    shutil.copy2(winner.lib, DIST / "passgen.lib")
    shutil.copy2(INCLUDE / "passgen.h", DIST / "passgen.h")
    shutil.copy2(cli_result.path, DIST / "passgen.exe")
    report.append("")
    report.append(f"Copied: {winner.lib} -> {DIST / 'passgen.lib'}")
    report.append(f"Copied: {INCLUDE / 'passgen.h'} -> {DIST / 'passgen.h'}")
    report.append(f"Copied: {cli_result.path} -> {DIST / 'passgen.exe'}")

    write_report(report)
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
