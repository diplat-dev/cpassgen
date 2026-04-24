#include "passgen.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef long i32;
typedef long long i64;
typedef unsigned long long u64;
typedef void *handle;

struct large_integer {
    i64 quad_part;
};

__declspec(dllimport) char *__stdcall GetCommandLineA(void);
__declspec(dllimport) handle __stdcall GetStdHandle(u32 nStdHandle);
__declspec(dllimport) handle __stdcall GetProcessHeap(void);
__declspec(dllimport) void *__stdcall HeapAlloc(handle hHeap, u32 dwFlags, u64 dwBytes);
__declspec(dllimport) i32 __stdcall QueryPerformanceCounter(struct large_integer *lpPerformanceCount);
__declspec(dllimport) i32 __stdcall QueryPerformanceFrequency(struct large_integer *lpFrequency);
__declspec(dllimport) i32 __stdcall WriteFile(
    handle hFile,
    const void *lpBuffer,
    u32 nNumberOfBytesToWrite,
    u32 *lpNumberOfBytesWritten,
    void *lpOverlapped);
__declspec(dllimport) void __stdcall ExitProcess(u32 uExitCode);

#define STD_OUTPUT_HANDLE ((u32)-11)

static volatile u64 BENCH_SINK;

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0b || c == 0x0c;
}

static void skip_space(char **cursor) {
    while (is_space(**cursor)) {
        *cursor += 1;
    }
}

static void skip_program_name(char **cursor) {
    skip_space(cursor);
    if (**cursor == '"') {
        *cursor += 1;
        while (**cursor && **cursor != '"') {
            *cursor += 1;
        }
        if (**cursor == '"') {
            *cursor += 1;
        }
    } else {
        while (**cursor && !is_space(**cursor)) {
            *cursor += 1;
        }
    }
}

static int parse_u64(char **cursor, u64 *out) {
    u64 value = 0;

    skip_space(cursor);
    if (**cursor < '0' || **cursor > '9') {
        return 0;
    }

    while (**cursor >= '0' && **cursor <= '9') {
        u64 digit = (u64)(**cursor - '0');
        if (value > (0xffffffffffffffffull - digit) / 10ull) {
            return 0;
        }
        value = value * 10ull + digit;
        *cursor += 1;
    }

    if (**cursor && !is_space(**cursor)) {
        return 0;
    }

    *out = value;
    return 1;
}

enum bench_mode {
    MODE_UNCHECKED = 0,
    MODE_SAFE = 1,
    MODE_SINGLE20 = 2,
    MODE_SINGLE20_LINE = 3,
};

static int parse_mode(char **cursor, int *mode) {
    char *p;
    u64 len;

    skip_space(cursor);
    p = *cursor;
    while (**cursor && !is_space(**cursor)) {
        *cursor += 1;
    }

    len = (u64)(*cursor - p);

    if (len == 4 && p[0] == 's' && p[1] == 'a' && p[2] == 'f' && p[3] == 'e') {
        *mode = MODE_SAFE;
        return 1;
    }
    if (len == 9 &&
        p[0] == 'u' && p[1] == 'n' && p[2] == 'c' && p[3] == 'h' && p[4] == 'e' &&
        p[5] == 'c' && p[6] == 'k' && p[7] == 'e' && p[8] == 'd') {
        *mode = MODE_UNCHECKED;
        return 1;
    }
    if (len == 8 &&
        p[0] == 's' && p[1] == 'i' && p[2] == 'n' && p[3] == 'g' &&
        p[4] == 'l' && p[5] == 'e' && p[6] == '2' && p[7] == '0') {
        *mode = MODE_SINGLE20;
        return 1;
    }
    if (len == 13 &&
        p[0] == 's' && p[1] == 'i' && p[2] == 'n' && p[3] == 'g' &&
        p[4] == 'l' && p[5] == 'e' && p[6] == '2' && p[7] == '0' &&
        p[8] == '-' && p[9] == 'l' && p[10] == 'i' && p[11] == 'n' && p[12] == 'e') {
        *mode = MODE_SINGLE20_LINE;
        return 1;
    }

    return 0;
}

static void insertion_sort(u64 *values, u64 count) {
    u64 i;
    for (i = 1; i < count; i += 1) {
        u64 key = values[i];
        u64 j = i;
        while (j > 0 && values[j - 1] > key) {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = key;
    }
}

static u64 to_ns(i64 ticks, i64 frequency) {
    return (u64)(((u64)ticks * 1000000000ull) / (u64)frequency);
}

static u64 to_ps(i64 ticks, i64 frequency) {
    return (u64)(((u64)ticks * 1000000000000ull) / (u64)frequency);
}

static void write_all(const char *s, u32 len) {
    u32 written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, len, &written, 0);
}

static void write_u64(u64 value) {
    char buf[32];
    u32 len = 0;
    u32 i;

    if (value == 0) {
        write_all("0", 1);
        return;
    }

    while (value != 0) {
        buf[len++] = (char)('0' + (value % 10ull));
        value /= 10ull;
    }

    for (i = 0; i < len / 2; i += 1) {
        char tmp = buf[i];
        buf[i] = buf[len - i - 1];
        buf[len - i - 1] = tmp;
    }

    write_all(buf, len);
}

static void write_stats(u64 *times, u64 iterations) {
    u64 min;
    u64 median;
    u64 p95;

    insertion_sort(times, iterations);
    min = times[0];
    median = (iterations & 1ull)
        ? times[iterations / 2ull]
        : times[iterations / 2ull - 1ull] + ((times[iterations / 2ull] - times[iterations / 2ull - 1ull]) / 2ull);
    p95 = times[((iterations - 1ull) * 95ull) / 100ull];

    write_u64(min);
    write_all(" ", 1);
    write_u64(median);
    write_all(" ", 1);
    write_u64(p95);
    write_all("\n", 1);
}

void mainCRTStartup(void) {
    char *cursor = GetCommandLineA();
    u64 iterations;
    u64 warmups;
    u64 repeats;
    u64 count;
    u64 length;
    u64 seed;
    u64 needed;
    u64 total;
    u64 collected = 0;
    u64 i;
    int mode;
    char *buffer;
    u64 *times;
    struct large_integer frequency;

    skip_program_name(&cursor);
    if (!parse_u64(&cursor, &iterations) ||
        !parse_u64(&cursor, &warmups) ||
        !parse_u64(&cursor, &repeats) ||
        !parse_mode(&cursor, &mode) ||
        !parse_u64(&cursor, &count) ||
        !parse_u64(&cursor, &length) ||
        !parse_u64(&cursor, &seed) ||
        iterations == 0 ||
        repeats == 0 ||
        (mode != MODE_SINGLE20 && mode != MODE_SINGLE20_LINE && passgen_output_size(count, length, &needed) != PASSGEN_OK)) {
        ExitProcess(1);
    }

    if (mode == MODE_SINGLE20) {
        needed = 20;
    } else if (mode == MODE_SINGLE20_LINE) {
        needed = 21;
    }

    total = iterations + warmups;
    buffer = (char *)HeapAlloc(GetProcessHeap(), 0, needed);
    times = (u64 *)HeapAlloc(GetProcessHeap(), 0, iterations * sizeof(u64));
    if (!buffer || !times || !QueryPerformanceFrequency(&frequency)) {
        ExitProcess(1);
    }

    for (i = 0; i < total; i += 1) {
        struct large_integer start;
        struct large_integer end;
        u64 repeat;

        QueryPerformanceCounter(&start);

        if (mode == MODE_SAFE) {
            for (repeat = 0; repeat < repeats; repeat += 1) {
                u64 written;
                if (passgen_fill(buffer, needed, count, length, seed, &written) != PASSGEN_OK || written != needed) {
                    ExitProcess(1);
                }
            }
        } else if (mode == MODE_SINGLE20) {
            for (repeat = 0; repeat < repeats; repeat += 1) {
                passgen_fill20_unchecked(buffer, seed);
            }
        } else if (mode == MODE_SINGLE20_LINE) {
            for (repeat = 0; repeat < repeats; repeat += 1) {
                passgen_fill20_line_unchecked(buffer, seed);
            }
        } else {
            for (repeat = 0; repeat < repeats; repeat += 1) {
                passgen_fill_unchecked(buffer, count, length, seed);
            }
        }

        QueryPerformanceCounter(&end);

        BENCH_SINK ^= (u64)(u8)buffer[0] + ((u64)(u8)buffer[needed - 1] << 8);

        if (i >= warmups) {
            times[collected++] = to_ps(end.quad_part - start.quad_part, frequency.quad_part) / repeats;
        }
    }

    write_stats(times, iterations);
    ExitProcess((u32)(BENCH_SINK == 0xffffffffffffffffull));
}
