#include "passgen.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef void *handle;

#define PASSGEN_TEST_SIZE 4096ull

__declspec(dllimport) handle __stdcall GetProcessHeap(void);
__declspec(dllimport) void *__stdcall HeapAlloc(handle hHeap, u32 dwFlags, u64 dwBytes);
__declspec(dllimport) void __stdcall ExitProcess(u32 uExitCode);

static int is_letter(u8 byte) {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

static int mem_equal(const char *a, const char *b, u64 len) {
    u64 i;
    for (i = 0; i < len; i += 1) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int valid_output(const char *data, u64 count, u64 length) {
    u64 offset = 0;
    u64 i;
    u64 j;

    for (i = 0; i < count; i += 1) {
        for (j = 0; j < length; j += 1) {
            if (!is_letter((u8)data[offset++])) {
                return 0;
            }
        }
        if (data[offset++] != '\n') {
            return 0;
        }
    }

    return 1;
}

static int valid_letters(const char *data, u64 length) {
    u64 i;
    for (i = 0; i < length; i += 1) {
        if (!is_letter((u8)data[i])) {
            return 0;
        }
    }
    return 1;
}

static void fail_if(int condition) {
    if (condition) {
        ExitProcess(1);
    }
}

void mainCRTStartup(void) {
    u64 size = 0;
    u64 written = 0;
    char *a = (char *)HeapAlloc(GetProcessHeap(), 0, PASSGEN_TEST_SIZE);
    char *b = (char *)HeapAlloc(GetProcessHeap(), 0, PASSGEN_TEST_SIZE);
    char *c = (char *)HeapAlloc(GetProcessHeap(), 0, PASSGEN_TEST_SIZE);

    fail_if(!a || !b || !c);

    fail_if(passgen_output_size(15, 20, &size) != PASSGEN_OK);
    fail_if(size != 315);

    fail_if(passgen_fill(a, PASSGEN_TEST_SIZE, 15, 20, 0, &written) != PASSGEN_OK);
    fail_if(written != 315);
    fail_if(!valid_output(a, 15, 20));

    fail_if(passgen_fill(b, PASSGEN_TEST_SIZE, 15, 20, 0, &written) != PASSGEN_OK);
    fail_if(!mem_equal(a, b, 315));

    fail_if(passgen_fill(a, PASSGEN_TEST_SIZE, 15, 20, 123456789, &written) != PASSGEN_OK);
    fail_if(passgen_fill(b, PASSGEN_TEST_SIZE, 15, 20, 123456789, &written) != PASSGEN_OK);
    fail_if(passgen_fill(c, PASSGEN_TEST_SIZE, 15, 20, 987654321, &written) != PASSGEN_OK);
    fail_if(!mem_equal(a, b, 315));
    fail_if(mem_equal(a, c, 315));

    fail_if(passgen_output_size(0, 20, &size) != PASSGEN_ERR_INVALID);
    fail_if(passgen_output_size(15, 0, &size) != PASSGEN_ERR_INVALID);
    fail_if(passgen_output_size(0xffffffffffffffffull, 20, &size) != PASSGEN_ERR_OVERFLOW);
    fail_if(passgen_output_size(15, 20, 0) != PASSGEN_ERR_INVALID);

    fail_if(passgen_fill(0, PASSGEN_TEST_SIZE, 15, 20, 0, &written) != PASSGEN_ERR_INVALID);
    fail_if(passgen_fill(a, PASSGEN_TEST_SIZE, 15, 20, 0, 0) != PASSGEN_ERR_INVALID);
    fail_if(passgen_fill(a, 314, 15, 20, 0, &written) != PASSGEN_ERR_BUFFER_TOO_SMALL);
    fail_if(written != 0);

    passgen_fill_unchecked(a, 15, 20, 0);
    fail_if(!valid_output(a, 15, 20));

    passgen_fill20_unchecked(a, 0);
    fail_if(!valid_letters(a, 20));
    fail_if(a[19] == '\n');

    passgen_fill20_line_unchecked(a, 0);
    fail_if(!valid_output(a, 1, 20));

    passgen_fill20_unchecked(a, 123456789);
    passgen_fill20_unchecked(b, 123456789);
    passgen_fill20_unchecked(c, 987654321);
    fail_if(!mem_equal(a, b, 20));
    fail_if(mem_equal(a, c, 20));

    ExitProcess(0);
}
