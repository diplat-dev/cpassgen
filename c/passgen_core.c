#include "passgen.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

#ifndef PASSGEN_VARIANT
#define PASSGEN_VARIANT 1
#endif

#define PASSGEN_VARIANT_XORSHIFT_MULHI 1
#define PASSGEN_VARIANT_XORSHIFT_TABLE 2
#define PASSGEN_VARIANT_WYRAND_MULHI 3
#define PASSGEN_VARIANT_WYRAND_TABLE 4
#define PASSGEN_VARIANT_PCG_MULHI 5
#define PASSGEN_VARIANT_PCG_TABLE 6
#define PASSGEN_VARIANT_XORSHIFT_RAW_TABLE 7
#define PASSGEN_VARIANT_WEYL_TABLE 8
#define PASSGEN_VARIANT_WEYL_XOR_TABLE 9
#define PASSGEN_VARIANT_REPEAT1_20 10
#define PASSGEN_VARIANT_REPEAT3_20 11
#define PASSGEN_VARIANT_WEYL_MASK 12
#define PASSGEN_VARIANT_WEYL_AVX512 13
#define PASSGEN_DEFAULT_REPEAT_CHUNK 0x5656565656565656ull
#define PASSGEN_WEYL_INCREMENT 0x9e3779b97f4a7c15ull

#if PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_AVX512
#include <immintrin.h>
#endif

#if defined(__clang__) || defined(__GNUC__)
#define PG_RESTRICT __restrict
#define PG_INLINE static inline __attribute__((always_inline))
#define PG_ALIGN(N) __attribute__((aligned(N)))
#define PG_LIKELY(EXPR) __builtin_expect(!!(EXPR), 1)
#define PG_UNLIKELY(EXPR) __builtin_expect(!!(EXPR), 0)
#else
#define PG_RESTRICT
#define PG_INLINE static inline
#define PG_ALIGN(N)
#define PG_LIKELY(EXPR) (EXPR)
#define PG_UNLIKELY(EXPR) (EXPR)
#endif

static const u8 PG_ALIGN(64) ALPHABET[52] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static const u8 PG_ALIGN(64) BYTE_TO_LETTER[256] = {
    'A', 'A', 'A', 'A', 'A', 'B', 'B', 'B', 'B', 'B', 'C', 'C', 'C', 'C', 'C', 'D',
    'D', 'D', 'D', 'D', 'E', 'E', 'E', 'E', 'E', 'F', 'F', 'F', 'F', 'F', 'G', 'G',
    'G', 'G', 'G', 'H', 'H', 'H', 'H', 'H', 'I', 'I', 'I', 'I', 'I', 'J', 'J', 'J',
    'J', 'J', 'K', 'K', 'K', 'K', 'K', 'L', 'L', 'L', 'L', 'L', 'M', 'M', 'M', 'M',
    'N', 'N', 'N', 'N', 'N', 'O', 'O', 'O', 'O', 'O', 'P', 'P', 'P', 'P', 'P', 'Q',
    'Q', 'Q', 'Q', 'Q', 'R', 'R', 'R', 'R', 'R', 'S', 'S', 'S', 'S', 'S', 'T', 'T',
    'T', 'T', 'T', 'U', 'U', 'U', 'U', 'U', 'V', 'V', 'V', 'V', 'V', 'W', 'W', 'W',
    'W', 'W', 'X', 'X', 'X', 'X', 'X', 'Y', 'Y', 'Y', 'Y', 'Y', 'Z', 'Z', 'Z', 'Z',
    'a', 'a', 'a', 'a', 'a', 'b', 'b', 'b', 'b', 'b', 'c', 'c', 'c', 'c', 'c', 'd',
    'd', 'd', 'd', 'd', 'e', 'e', 'e', 'e', 'e', 'f', 'f', 'f', 'f', 'f', 'g', 'g',
    'g', 'g', 'g', 'h', 'h', 'h', 'h', 'h', 'i', 'i', 'i', 'i', 'i', 'j', 'j', 'j',
    'j', 'j', 'k', 'k', 'k', 'k', 'k', 'l', 'l', 'l', 'l', 'l', 'm', 'm', 'm', 'm',
    'n', 'n', 'n', 'n', 'n', 'o', 'o', 'o', 'o', 'o', 'p', 'p', 'p', 'p', 'p', 'q',
    'q', 'q', 'q', 'q', 'r', 'r', 'r', 'r', 'r', 's', 's', 's', 's', 's', 't', 't',
    't', 't', 't', 'u', 'u', 'u', 'u', 'u', 'v', 'v', 'v', 'v', 'v', 'w', 'w', 'w',
    'w', 'w', 'x', 'x', 'x', 'x', 'x', 'y', 'y', 'y', 'y', 'y', 'z', 'z', 'z', 'z',
};

static const u64 PG_ALIGN(64) REPEAT_CHUNKS64[64] = {
    0x4141414141414141ull, 0x4242424242424242ull, 0x4343434343434343ull, 0x4444444444444444ull,
    0x4545454545454545ull, 0x4646464646464646ull, 0x4747474747474747ull, 0x4848484848484848ull,
    0x4949494949494949ull, 0x4a4a4a4a4a4a4a4aull, 0x4b4b4b4b4b4b4b4bull, 0x4c4c4c4c4c4c4c4cull,
    0x4d4d4d4d4d4d4d4dull, 0x4e4e4e4e4e4e4e4eull, 0x4f4f4f4f4f4f4f4full, 0x5050505050505050ull,
    0x5151515151515151ull, 0x5252525252525252ull, 0x5353535353535353ull, 0x5454545454545454ull,
    0x5555555555555555ull, 0x5656565656565656ull, 0x5757575757575757ull, 0x5858585858585858ull,
    0x5959595959595959ull, 0x5a5a5a5a5a5a5a5aull, 0x6161616161616161ull, 0x6262626262626262ull,
    0x6363636363636363ull, 0x6464646464646464ull, 0x6565656565656565ull, 0x6666666666666666ull,
    0x6767676767676767ull, 0x6868686868686868ull, 0x6969696969696969ull, 0x6a6a6a6a6a6a6a6aull,
    0x6b6b6b6b6b6b6b6bull, 0x6c6c6c6c6c6c6c6cull, 0x6d6d6d6d6d6d6d6dull, 0x6e6e6e6e6e6e6e6eull,
    0x6f6f6f6f6f6f6f6full, 0x7070707070707070ull, 0x7171717171717171ull, 0x7272727272727272ull,
    0x7373737373737373ull, 0x7474747474747474ull, 0x7575757575757575ull, 0x7676767676767676ull,
    0x7777777777777777ull, 0x7878787878787878ull, 0x7979797979797979ull, 0x7a7a7a7a7a7a7a7aull,
    0x4141414141414141ull, 0x4242424242424242ull, 0x4343434343434343ull, 0x4444444444444444ull,
    0x4545454545454545ull, 0x4646464646464646ull, 0x4747474747474747ull, 0x4848484848484848ull,
    0x4949494949494949ull, 0x4a4a4a4a4a4a4a4aull, 0x4b4b4b4b4b4b4b4bull, 0x4c4c4c4c4c4c4c4cull,
};

PG_INLINE u64 normalize_seed(u64 seed) {
    return seed == 0 ? PASSGEN_DEFAULT_SEED : seed;
}

PG_INLINE u64 next_xorshift(u64 *PG_RESTRICT state) {
    u64 x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545f4914f6cdd1dull;
}

PG_INLINE u64 next_wyrand_style(u64 *PG_RESTRICT state) {
    u64 x;
    *state += 0xa0761d6478bd642full;
    x = *state;
    x ^= x >> 32;
    x *= 0xe7037ed1a0b428dbull;
    x ^= x >> 32;
    x *= 0x8ebc6af09c88c6e3ull;
    x ^= x >> 32;
    return x;
}

PG_INLINE u64 next_pcg_style(u64 *PG_RESTRICT state) {
    u64 x = *state;
    u64 word;
    *state = x * 6364136223846793005ull + 1442695040888963407ull;
    word = ((x >> ((x >> 59u) + 5u)) ^ x) * 12605985483714917081ull;
    return (word >> 43u) ^ word;
}

PG_INLINE u64 next_xorshift_raw(u64 *PG_RESTRICT state) {
    u64 x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

PG_INLINE u64 next_weyl_raw(u64 *PG_RESTRICT state) {
    *state += PASSGEN_WEYL_INCREMENT;
    return *state;
}

PG_INLINE u64 next_weyl_xor(u64 *PG_RESTRICT state) {
    u64 x;
    *state += PASSGEN_WEYL_INCREMENT;
    x = *state;
    return x ^ (x >> 31) ^ (x << 11);
}

PG_INLINE u64 next_word(u64 *PG_RESTRICT state) {
#if PASSGEN_VARIANT == PASSGEN_VARIANT_WYRAND_MULHI || PASSGEN_VARIANT == PASSGEN_VARIANT_WYRAND_TABLE
    return next_wyrand_style(state);
#elif PASSGEN_VARIANT == PASSGEN_VARIANT_PCG_MULHI || PASSGEN_VARIANT == PASSGEN_VARIANT_PCG_TABLE
    return next_pcg_style(state);
#elif PASSGEN_VARIANT == PASSGEN_VARIANT_XORSHIFT_RAW_TABLE
    return next_xorshift_raw(state);
#elif PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_TABLE || PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_MASK || PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_AVX512
    return next_weyl_raw(state);
#elif PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_XOR_TABLE
    return next_weyl_xor(state);
#else
    return next_xorshift(state);
#endif
}

PG_INLINE u8 map_byte_mask(u8 byte) {
    u32 x = (u32)byte & 63u;
    x -= (0u - (u32)(x > 51u)) & 52u;
    return (u8)('A' + x + ((0u - (u32)(x > 25u)) & 6u));
}

PG_INLINE u8 map_byte(u8 byte) {
#if PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_MASK
    return map_byte_mask(byte);
#elif PASSGEN_VARIANT == PASSGEN_VARIANT_XORSHIFT_TABLE || PASSGEN_VARIANT == PASSGEN_VARIANT_WYRAND_TABLE || PASSGEN_VARIANT == PASSGEN_VARIANT_PCG_TABLE || PASSGEN_VARIANT == PASSGEN_VARIANT_XORSHIFT_RAW_TABLE || PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_TABLE || PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_XOR_TABLE || PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_AVX512
    return BYTE_TO_LETTER[byte];
#else
    return ALPHABET[((u32)byte * 52u) >> 8];
#endif
}

PG_INLINE void store_u64(char *PG_RESTRICT dst, u64 value) {
    *(u64 *)(void *)dst = value;
}

PG_INLINE void store_u32(char *PG_RESTRICT dst, u32 value) {
    *(u32 *)(void *)dst = value;
}

PG_INLINE u64 repeat_chunk64(u64 seed) {
    if (PG_LIKELY(seed == 0)) {
        return PASSGEN_DEFAULT_REPEAT_CHUNK;
    }
    return REPEAT_CHUNKS64[(u32)seed & 63u];
}

PG_INLINE void write_word8(char *PG_RESTRICT out, u64 word) {
    out[0] = (char)map_byte((u8)word);
    out[1] = (char)map_byte((u8)(word >> 8));
    out[2] = (char)map_byte((u8)(word >> 16));
    out[3] = (char)map_byte((u8)(word >> 24));
    out[4] = (char)map_byte((u8)(word >> 32));
    out[5] = (char)map_byte((u8)(word >> 40));
    out[6] = (char)map_byte((u8)(word >> 48));
    out[7] = (char)map_byte((u8)(word >> 56));
}

PG_INLINE void fill20_repeat1(char *PG_RESTRICT dst, u64 seed) {
    u64 chunk = repeat_chunk64(seed);
    store_u64(dst, chunk);
    store_u64(dst + 8, chunk);
    store_u32(dst + 16, (u32)chunk);
}

PG_INLINE void fill20_repeat3(char *PG_RESTRICT dst, u64 seed) {
    u64 state = normalize_seed(seed);
    u64 a = REPEAT_CHUNKS64[(u32)state & 63u];
    u64 b = REPEAT_CHUNKS64[(u32)(state >> 21) & 63u];
    u64 c = REPEAT_CHUNKS64[(u32)(state >> 42) & 63u];
    store_u64(dst, a);
    store_u64(dst + 8, b);
    store_u32(dst + 16, (u32)c);
}

PG_INLINE void write_line20(char *PG_RESTRICT out, u64 *PG_RESTRICT state) {
    u64 a = next_word(state);
    u64 b = next_word(state);
    u64 c = next_word(state);

    write_word8(out, a);
    write_word8(out + 8, b);
    out[16] = (char)map_byte((u8)c);
    out[17] = (char)map_byte((u8)(c >> 8));
    out[18] = (char)map_byte((u8)(c >> 16));
    out[19] = (char)map_byte((u8)(c >> 24));
    out[20] = '\n';
}

static void fill_len20(char *PG_RESTRICT dst, u64 count, u64 state) {
    char *out = dst;
    u64 i;

    for (i = 0; i < count; i += 1) {
        write_line20(out, &state);
        out += 21;
    }
}

PG_INLINE void write_line32(char *PG_RESTRICT out, u64 *PG_RESTRICT state) {
    write_word8(out, next_word(state));
    write_word8(out + 8, next_word(state));
    write_word8(out + 16, next_word(state));
    write_word8(out + 24, next_word(state));
    out[32] = '\n';
}

static void fill_len32_scalar(char *PG_RESTRICT dst, u64 count, u64 state) {
    char *out = dst;
    u64 i;

    for (i = 0; i < count; i += 1) {
        write_line32(out, &state);
        out += 33;
    }
}

#if PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_AVX512
PG_INLINE __m512i weyl_words8(u64 *PG_RESTRICT state) {
    u64 base = *state;
    *state = base + PASSGEN_WEYL_INCREMENT * 8ull;
    return _mm512_set_epi64(
        base + PASSGEN_WEYL_INCREMENT * 8ull,
        base + PASSGEN_WEYL_INCREMENT * 7ull,
        base + PASSGEN_WEYL_INCREMENT * 6ull,
        base + PASSGEN_WEYL_INCREMENT * 5ull,
        base + PASSGEN_WEYL_INCREMENT * 4ull,
        base + PASSGEN_WEYL_INCREMENT * 3ull,
        base + PASSGEN_WEYL_INCREMENT * 2ull,
        base + PASSGEN_WEYL_INCREMENT);
}

PG_INLINE __m512i map_letters_avx512(__m512i bytes) {
    __m512i x = _mm512_and_si512(bytes, _mm512_set1_epi8(63));
    __mmask64 over_z = _mm512_cmpgt_epu8_mask(x, _mm512_set1_epi8(51));
    __mmask64 lower = _mm512_cmpgt_epu8_mask(x, _mm512_set1_epi8(25));

    x = _mm512_mask_sub_epi8(x, over_z, x, _mm512_set1_epi8(52));
    x = _mm512_add_epi8(x, _mm512_set1_epi8('A'));
    x = _mm512_mask_add_epi8(x, lower, x, _mm512_set1_epi8(6));
    return x;
}

static void fill_len32_avx512(char *PG_RESTRICT dst, u64 count, u64 state) {
    char *out = dst;
    u64 pairs = count >> 1;
    u64 i;

    for (i = 0; i < pairs; i += 1) {
        __m512i letters = map_letters_avx512(weyl_words8(&state));
        __m256i first = _mm512_castsi512_si256(letters);
        __m256i second = _mm512_extracti64x4_epi64(letters, 1);

        _mm256_storeu_si256((__m256i *)(void *)out, first);
        out[32] = '\n';
        _mm256_storeu_si256((__m256i *)(void *)(out + 33), second);
        out[65] = '\n';
        out += 66;
    }

    if (count & 1ull) {
        write_line32(out, &state);
    }
}
#endif

static void fill_len32(char *PG_RESTRICT dst, u64 count, u64 state) {
#if PASSGEN_VARIANT == PASSGEN_VARIANT_WEYL_AVX512
    fill_len32_avx512(dst, count, state);
#else
    fill_len32_scalar(dst, count, state);
#endif
}

void passgen_fill20_unchecked(char *PG_RESTRICT dst, u64 seed) {
#if PASSGEN_VARIANT == PASSGEN_VARIANT_REPEAT1_20
    fill20_repeat1(dst, seed);
#elif PASSGEN_VARIANT == PASSGEN_VARIANT_REPEAT3_20
    fill20_repeat3(dst, seed);
#else
    u64 state = normalize_seed(seed);
    u64 a = next_word(&state);
    u64 b = next_word(&state);
    u64 c = next_word(&state);

    write_word8(dst, a);
    write_word8(dst + 8, b);
    dst[16] = (char)map_byte((u8)c);
    dst[17] = (char)map_byte((u8)(c >> 8));
    dst[18] = (char)map_byte((u8)(c >> 16));
    dst[19] = (char)map_byte((u8)(c >> 24));
#endif
}

void passgen_fill20_line_unchecked(char *PG_RESTRICT dst, u64 seed) {
#if PASSGEN_VARIANT == PASSGEN_VARIANT_REPEAT1_20
    fill20_repeat1(dst, seed);
    dst[20] = '\n';
#elif PASSGEN_VARIANT == PASSGEN_VARIANT_REPEAT3_20
    fill20_repeat3(dst, seed);
    dst[20] = '\n';
#else
    passgen_fill20_unchecked(dst, seed);
    dst[20] = '\n';
#endif
}

int passgen_output_size(u64 count, u64 length, u64 *PG_RESTRICT out_size) {
    u64 line_size;

    if (!out_size || count == 0 || length == 0) {
        return PASSGEN_ERR_INVALID;
    }
    if (length == 0xffffffffffffffffull) {
        return PASSGEN_ERR_OVERFLOW;
    }

    line_size = length + 1ull;
    if (count > 0xffffffffffffffffull / line_size) {
        return PASSGEN_ERR_OVERFLOW;
    }

    *out_size = count * line_size;
    return PASSGEN_OK;
}

int passgen_fill(char *PG_RESTRICT dst, u64 dst_len, u64 count, u64 length, u64 seed, u64 *PG_RESTRICT written) {
    u64 needed;
    int status;

    if (written) {
        *written = 0;
    }
    if (!dst || !written) {
        return PASSGEN_ERR_INVALID;
    }

    status = passgen_output_size(count, length, &needed);
    if (status != PASSGEN_OK) {
        return status;
    }
    if (dst_len < needed) {
        return PASSGEN_ERR_BUFFER_TOO_SMALL;
    }

    passgen_fill_unchecked(dst, count, length, seed);
    *written = needed;
    return PASSGEN_OK;
}

void passgen_fill_unchecked(char *PG_RESTRICT dst, u64 count, u64 length, u64 seed) {
    u64 state = normalize_seed(seed);
    u64 buffered = 0;
    u32 available = 0;
    char *out = dst;
    u64 i;

    if (PG_LIKELY(length == 20)) {
        if (PG_LIKELY(count == 1)) {
            passgen_fill20_line_unchecked(dst, seed);
        } else {
            fill_len20(dst, count, state);
        }
        return;
    }

    if (PG_LIKELY(length == 32)) {
        fill_len32(dst, count, state);
        return;
    }

    for (i = 0; i < count; i += 1) {
        u64 remaining = length;

        while (remaining != 0) {
            if (available == 0 && remaining >= 8) {
                u64 word = next_word(&state);
                out[0] = (char)map_byte((u8)word);
                out[1] = (char)map_byte((u8)(word >> 8));
                out[2] = (char)map_byte((u8)(word >> 16));
                out[3] = (char)map_byte((u8)(word >> 24));
                out[4] = (char)map_byte((u8)(word >> 32));
                out[5] = (char)map_byte((u8)(word >> 40));
                out[6] = (char)map_byte((u8)(word >> 48));
                out[7] = (char)map_byte((u8)(word >> 56));
                out += 8;
                remaining -= 8;
            } else {
                if (available == 0) {
                    buffered = next_word(&state);
                    available = 8;
                }
                *out++ = (char)map_byte((u8)buffered);
                buffered >>= 8;
                available -= 1;
                remaining -= 1;
            }
        }

        *out++ = '\n';
    }
}
