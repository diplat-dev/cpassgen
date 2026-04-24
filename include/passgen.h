#ifndef PASSGEN_H
#define PASSGEN_H

#include <stdint.h>

#define PASSGEN_OK 0
#define PASSGEN_ERR_INVALID 1
#define PASSGEN_ERR_OVERFLOW 2
#define PASSGEN_ERR_BUFFER_TOO_SMALL 3

#define PASSGEN_DEFAULT_SEED UINT64_C(0x9e3779b97f4a7c15)

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
