#include "passgen.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned long long usize;
typedef void *handle;

#define STACK_BUFFER_SIZE 1024ull
#define WRITE_CHUNK_MAX 0xffffffffull
#define MEM_COMMIT_RESERVE 0x3000u
#define PAGE_READWRITE 0x04u

__declspec(dllimport) long __stdcall NtAllocateVirtualMemory(
    handle ProcessHandle,
    void **BaseAddress,
    usize ZeroBits,
    usize *RegionSize,
    u32 AllocationType,
    u32 Protect);
__declspec(dllimport) long __stdcall NtWriteFile(
    handle FileHandle,
    handle Event,
    void *ApcRoutine,
    void *ApcContext,
    void *IoStatusBlock,
    const void *Buffer,
    u32 Length,
    void *ByteOffset,
    void *Key);

unsigned __int64 __readgsqword(unsigned long Offset);
#pragma intrinsic(__readgsqword)

struct unicode_string {
    u16 length;
    u16 maximum_length;
    u16 *buffer;
};

struct args {
    u64 count;
    u64 length;
    u64 seed;
};

struct io_status_block {
    void *status;
    usize information;
};

static void *current_process_parameters(void) {
    u8 *peb = (u8 *)__readgsqword(0x60);
    return *(void **)(peb + 0x20);
}

static handle current_stdout(void) {
    u8 *params = (u8 *)current_process_parameters();
    return *(handle *)(params + 0x28);
}

static struct unicode_string *current_command_line(void) {
    u8 *params = (u8 *)current_process_parameters();
    return (struct unicode_string *)(params + 0x70);
}

static int is_space(u16 ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == 0x0b || ch == 0x0c;
}

static void skip_space(u16 **cursor, u16 *end) {
    while (*cursor < end && is_space(**cursor)) {
        *cursor += 1;
    }
}

static void skip_program_name(u16 **cursor, u16 *end) {
    skip_space(cursor, end);

    if (*cursor < end && **cursor == '"') {
        *cursor += 1;
        while (*cursor < end && **cursor != '"') {
            *cursor += 1;
        }
        if (*cursor < end && **cursor == '"') {
            *cursor += 1;
        }
    } else {
        while (*cursor < end && !is_space(**cursor)) {
            *cursor += 1;
        }
    }
}

static int parse_u64_token(u16 **cursor, u16 *end, u64 *out) {
    u64 value = 0;

    skip_space(cursor, end);
    if (*cursor >= end || **cursor < '0' || **cursor > '9') {
        return 0;
    }

    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        u64 digit = (u64)(**cursor - '0');
        if (value > (0xffffffffffffffffull - digit) / 10ull) {
            return 0;
        }
        value = value * 10ull + digit;
        *cursor += 1;
    }

    if (*cursor < end && !is_space(**cursor)) {
        return 0;
    }

    *out = value;
    return 1;
}

static int parse_positive_u64(u16 **cursor, u16 *end, u64 *out) {
    u64 value;
    if (!parse_u64_token(cursor, end, &value) || value == 0) {
        return 0;
    }
    *out = value;
    return 1;
}

static int parse_args(struct args *out) {
    struct unicode_string *command_line = current_command_line();
    u16 *cursor = command_line->buffer;
    u16 *end = cursor + (command_line->length / 2);

    skip_program_name(&cursor, end);
    if (!parse_positive_u64(&cursor, end, &out->count)) {
        return 0;
    }
    if (!parse_positive_u64(&cursor, end, &out->length)) {
        return 0;
    }

    skip_space(&cursor, end);
    if (cursor == end) {
        out->seed = 0;
        return 1;
    }

    if (!parse_u64_token(&cursor, end, &out->seed)) {
        return 0;
    }
    skip_space(&cursor, end);
    return cursor == end;
}

static u8 *allocate_output(u64 total_size, u8 *stack_output) {
    void *base = 0;
    usize region_size = (usize)total_size;

    if (total_size <= STACK_BUFFER_SIZE) {
        return stack_output;
    }

    if (NtAllocateVirtualMemory(
            (handle)(usize)-1,
            &base,
            0,
            &region_size,
            MEM_COMMIT_RESERVE,
            PAGE_READWRITE) < 0 ||
        !base) {
        return 0;
    }

    return (u8 *)base;
}

static int write_stdout(const u8 *output, u64 total_size) {
    handle stdout_handle = current_stdout();
    u64 offset = 0;

    if (!stdout_handle) {
        return 0;
    }

    while (offset < total_size) {
        u64 remaining = total_size - offset;
        u32 chunk = (u32)(remaining > WRITE_CHUNK_MAX ? WRITE_CHUNK_MAX : remaining);
        struct io_status_block iosb;

        if (NtWriteFile(stdout_handle, 0, 0, 0, &iosb, output + offset, chunk, 0, 0) < 0) {
            return 0;
        }

        offset += (u64)chunk;
    }

    return 1;
}

int mainCRTStartup(void) {
    struct args args;
    u64 total_size;
    u8 *output;
    u8 stack_output[STACK_BUFFER_SIZE];

    if (!parse_args(&args)) {
        return 1;
    }
    if (passgen_output_size(args.count, args.length, &total_size) != PASSGEN_OK) {
        return 1;
    }

    output = allocate_output(total_size, stack_output);
    if (!output) {
        return 1;
    }

    passgen_fill_unchecked((char *)output, args.count, args.length, args.seed);
    if (!write_stdout(output, total_size)) {
        return 1;
    }

    return 0;
}
