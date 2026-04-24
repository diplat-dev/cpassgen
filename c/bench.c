typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef long i32;
typedef long long i64;
typedef unsigned long long u64;
typedef unsigned long long usize;
typedef void *handle;

#define STD_INPUT_HANDLE ((u32)-10)
#define STD_OUTPUT_HANDLE ((u32)-11)
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define STARTF_USESTDHANDLES 0x00000100u
#define INFINITE 0xffffffffu
#define FILE_BEGIN 0u
#define INVALID_HANDLE_VALUE ((handle)(usize)-1)

struct large_integer {
    i64 quad_part;
};

struct security_attributes {
    u32 nLength;
    void *lpSecurityDescriptor;
    i32 bInheritHandle;
};

struct startup_info_w {
    u32 cb;
    u16 *lpReserved;
    u16 *lpDesktop;
    u16 *lpTitle;
    u32 dwX;
    u32 dwY;
    u32 dwXSize;
    u32 dwYSize;
    u32 dwXCountChars;
    u32 dwYCountChars;
    u32 dwFillAttribute;
    u32 dwFlags;
    u16 wShowWindow;
    u16 cbReserved2;
    u8 *lpReserved2;
    handle hStdInput;
    handle hStdOutput;
    handle hStdError;
};

struct process_information {
    handle hProcess;
    handle hThread;
    u32 dwProcessId;
    u32 dwThreadId;
};

struct argv_view {
    i32 argc;
    u16 **argv;
};

__declspec(dllimport) u16 *__stdcall GetCommandLineW(void);
__declspec(dllimport) handle __stdcall GetStdHandle(u32 nStdHandle);
__declspec(dllimport) handle __stdcall GetProcessHeap(void);
__declspec(dllimport) void *__stdcall HeapAlloc(handle hHeap, u32 dwFlags, usize dwBytes);
__declspec(dllimport) i32 __stdcall WriteFile(
    handle hFile,
    const void *lpBuffer,
    u32 nNumberOfBytesToWrite,
    u32 *lpNumberOfBytesWritten,
    void *lpOverlapped);
__declspec(dllimport) handle __stdcall CreateFileW(
    const u16 *lpFileName,
    u32 dwDesiredAccess,
    u32 dwShareMode,
    struct security_attributes *lpSecurityAttributes,
    u32 dwCreationDisposition,
    u32 dwFlagsAndAttributes,
    handle hTemplateFile);
__declspec(dllimport) i32 __stdcall CreateProcessW(
    const u16 *lpApplicationName,
    u16 *lpCommandLine,
    struct security_attributes *lpProcessAttributes,
    struct security_attributes *lpThreadAttributes,
    i32 bInheritHandles,
    u32 dwCreationFlags,
    void *lpEnvironment,
    const u16 *lpCurrentDirectory,
    struct startup_info_w *lpStartupInfo,
    struct process_information *lpProcessInformation);
__declspec(dllimport) u32 __stdcall WaitForSingleObject(handle hHandle, u32 dwMilliseconds);
__declspec(dllimport) i32 __stdcall GetExitCodeProcess(handle hProcess, u32 *lpExitCode);
__declspec(dllimport) i32 __stdcall CloseHandle(handle hObject);
__declspec(dllimport) i32 __stdcall QueryPerformanceCounter(struct large_integer *lpPerformanceCount);
__declspec(dllimport) i32 __stdcall QueryPerformanceFrequency(struct large_integer *lpFrequency);
__declspec(dllimport) i32 __stdcall SetFilePointerEx(
    handle hFile,
    struct large_integer liDistanceToMove,
    struct large_integer *lpNewFilePointer,
    u32 dwMoveMethod);
__declspec(dllimport) i32 __stdcall SetEndOfFile(handle hFile);
__declspec(dllimport) void __stdcall ExitProcess(u32 uExitCode);

static u16 NUL_NAME[] = {'N', 'U', 'L', 0};

void *__cdecl memset(void *dest, int value, usize count) {
    u8 *p = (u8 *)dest;
    while (count != 0) {
        *p++ = (u8)value;
        count -= 1;
    }
    return dest;
}

usize __cdecl wcslen(const u16 *s) {
    const u16 *p = s;
    while (*p) {
        p += 1;
    }
    return (usize)(p - s);
}

static usize str_len16(const u16 *s) {
    const u16 *p = s;
    while (*p) {
        p += 1;
    }
    return (usize)(p - s);
}

static int is_space16(u16 ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == 0x0b || ch == 0x0c;
}

static int eq_ascii16(const u16 *s, const char *ascii) {
    while (*s && *ascii) {
        if (*s != (u16)(u8)*ascii) {
            return 0;
        }
        s += 1;
        ascii += 1;
    }
    return *s == 0 && *ascii == 0;
}

static struct argv_view parse_command_line(void) {
    u16 *command = GetCommandLineW();
    usize len = str_len16(command);
    u16 *copy = (u16 *)HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(u16));
    u16 **argv = (u16 **)HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(u16 *));
    u16 *p;
    struct argv_view view;
    usize i;

    view.argc = 0;
    view.argv = argv;
    if (!copy || !argv) {
        return view;
    }

    for (i = 0; i <= len; i += 1) {
        copy[i] = command[i];
    }

    p = copy;
    while (*p) {
        while (*p && is_space16(*p)) {
            p += 1;
        }
        if (!*p) {
            break;
        }

        if (*p == '"') {
            p += 1;
            argv[view.argc++] = p;
            while (*p && *p != '"') {
                p += 1;
            }
            if (*p == '"') {
                *p++ = 0;
            }
        } else {
            argv[view.argc++] = p;
            while (*p && !is_space16(*p)) {
                p += 1;
            }
            if (*p) {
                *p++ = 0;
            }
        }
    }

    return view;
}

static int parse_u32(const u16 *s, u32 *out) {
    u64 value = 0;
    if (!*s) {
        return 0;
    }
    while (*s) {
        if (*s < '0' || *s > '9') {
            return 0;
        }
        value = value * 10u + (u32)(*s - '0');
        if (value > 0xffffffffull) {
            return 0;
        }
        s += 1;
    }
    *out = (u32)value;
    return 1;
}

static int needs_quotes(const u16 *s) {
    if (!*s) {
        return 1;
    }
    while (*s) {
        if (is_space16(*s) || *s == '"') {
            return 1;
        }
        s += 1;
    }
    return 0;
}

static usize arg_size(const u16 *s, int force_quote) {
    usize len = str_len16(s);
    return len + (force_quote || needs_quotes(s) ? 3 : 1);
}

static u16 *append_arg(u16 *out, const u16 *s, int force_quote) {
    int quote = force_quote || needs_quotes(s);

    if (quote) {
        *out++ = '"';
    }
    while (*s) {
        *out++ = *s++;
    }
    if (quote) {
        *out++ = '"';
    }
    *out++ = ' ';
    return out;
}

static u16 *build_child_command(i32 argc, u16 **argv, i32 first_child_arg, usize *out_len) {
    usize len = 0;
    i32 i;
    u16 *command;
    u16 *p;

    for (i = first_child_arg; i < argc; i += 1) {
        len += arg_size(argv[i], i == first_child_arg);
    }

    command = (u16 *)HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(u16));
    if (!command) {
        return 0;
    }

    p = command;
    for (i = first_child_arg; i < argc; i += 1) {
        p = append_arg(p, argv[i], i == first_child_arg);
    }
    if (p != command) {
        p -= 1;
    }
    *p = 0;
    *out_len = (usize)(p - command);
    return command;
}

static void copy16(u16 *dest, const u16 *src, usize len) {
    usize i;
    for (i = 0; i <= len; i += 1) {
        dest[i] = src[i];
    }
}

static void insertion_sort(u64 *values, u32 count) {
    u32 i;
    for (i = 1; i < count; i += 1) {
        u64 key = values[i];
        u32 j = i;
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

static void write_all(handle out, const char *s, u32 len) {
    u32 written;
    WriteFile(out, s, len, &written, 0);
}

static void write_u64(handle out, u64 value) {
    char buf[32];
    u32 len = 0;
    u32 i;

    if (value == 0) {
        write_all(out, "0", 1);
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

    write_all(out, buf, len);
}

static void truncate_output(handle output) {
    struct large_integer zero;
    zero.quad_part = 0;
    SetFilePointerEx(output, zero, 0, FILE_BEGIN);
    SetEndOfFile(output);
}

static u32 run_benchmark(
    u32 iterations,
    u32 warmups,
    int file_sink,
    handle output,
    handle stderr_nul,
    const u16 *application,
    const u16 *command_template,
    usize command_len) {
    u32 total = iterations + warmups;
    u64 *times = (u64 *)HeapAlloc(GetProcessHeap(), 0, (usize)iterations * sizeof(u64));
    u16 *command = (u16 *)HeapAlloc(GetProcessHeap(), 0, (command_len + 1) * sizeof(u16));
    struct large_integer frequency;
    u32 collected = 0;
    u32 i;

    if (!times || !command || !QueryPerformanceFrequency(&frequency)) {
        return 1;
    }

    for (i = 0; i < total; i += 1) {
        struct startup_info_w si;
        struct process_information pi;
        struct large_integer start;
        struct large_integer end;
        u32 exit_code = 1;

        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = output;
        si.hStdError = stderr_nul;

        if (file_sink) {
            truncate_output(output);
        }
        copy16(command, command_template, command_len);

        QueryPerformanceCounter(&start);
        if (!CreateProcessW(application, command, 0, 0, 1, 0, 0, 0, &si, &pi)) {
            return 1;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        QueryPerformanceCounter(&end);

        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (exit_code != 0) {
            return 1;
        }

        if (i >= warmups) {
            times[collected++] = to_ns(end.quad_part - start.quad_part, frequency.quad_part);
        }
    }

    insertion_sort(times, iterations);
    {
        u64 min = times[0];
        u64 median = (iterations & 1)
            ? times[iterations / 2]
            : times[iterations / 2 - 1] + ((times[iterations / 2] - times[iterations / 2 - 1]) / 2ull);
        u32 p95_index = ((iterations - 1u) * 95u) / 100u;
        u64 p95 = times[p95_index];
        handle stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);

        write_u64(stdout_handle, min);
        write_all(stdout_handle, " ", 1);
        write_u64(stdout_handle, median);
        write_all(stdout_handle, " ", 1);
        write_u64(stdout_handle, p95);
        write_all(stdout_handle, "\n", 1);
    }

    return 0;
}

void mainCRTStartup(void) {
    struct argv_view argv = parse_command_line();
    u32 iterations;
    u32 warmups;
    int file_sink;
    struct security_attributes inherit_sa;
    handle output;
    handle stderr_nul;
    u16 *command_template;
    usize command_len;
    u32 result;

    if (argv.argc < 6 ||
        !parse_u32(argv.argv[1], &iterations) ||
        !parse_u32(argv.argv[2], &warmups) ||
        iterations == 0) {
        ExitProcess(1);
    }

    file_sink = eq_ascii16(argv.argv[3], "file");
    if (!file_sink && !eq_ascii16(argv.argv[3], "nul")) {
        ExitProcess(1);
    }

    inherit_sa.nLength = sizeof(inherit_sa);
    inherit_sa.lpSecurityDescriptor = 0;
    inherit_sa.bInheritHandle = 1;

    if (file_sink) {
        output = CreateFileW(
            argv.argv[4],
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &inherit_sa,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            0);
    } else {
        output = CreateFileW(
            NUL_NAME,
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &inherit_sa,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            0);
    }
    stderr_nul = CreateFileW(
        NUL_NAME,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inherit_sa,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        0);

    if (output == INVALID_HANDLE_VALUE || stderr_nul == INVALID_HANDLE_VALUE) {
        ExitProcess(1);
    }

    command_template = build_child_command(argv.argc, argv.argv, 5, &command_len);
    if (!command_template) {
        ExitProcess(1);
    }

    result = run_benchmark(
        iterations,
        warmups,
        file_sink,
        output,
        stderr_nul,
        argv.argv[5],
        command_template,
        command_len);

    CloseHandle(output);
    CloseHandle(stderr_nul);
    ExitProcess(result);
}
