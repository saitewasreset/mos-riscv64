#include <num.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;

    // 处理8字节对齐的可能性
    if ((((uintptr_t)s & 7) == ((uintptr_t)d & 7))) {
        // 复制到对齐边界
        while (n > 0 && ((uintptr_t)s & 7)) {
            *d++ = *s++;
            n--;
        }

        // 用64位块复制
        uint64_t *d64 = (uint64_t *)d;
        const uint64_t *s64 = (const uint64_t *)s;
        for (; n >= 8; n -= 8) {
            *d64++ = *s64++;
        }
        d = (unsigned char *)d64;
        s = (const unsigned char *)s64;
    }

    // 处理4字节对齐的可能性
    if (n >= 4 && (((uintptr_t)s & 3) == ((uintptr_t)d & 3))) {
        // 复制到4字节对齐
        while (n > 0 && ((uintptr_t)s & 3)) {
            *d++ = *s++;
            n--;
        }

        // 用32位块复制
        uint32_t *d32 = (uint32_t *)d;
        const uint32_t *s32 = (const uint32_t *)s;
        for (; n >= 4; n -= 4) {
            *d32++ = *s32++;
        }
        d = (unsigned char *)d32;
        s = (const unsigned char *)s32;
    }

    // 处理2字节对齐的可能性
    if (n >= 2 && (((uintptr_t)s & 1) == ((uintptr_t)d & 1))) {
        // 仅处理对齐到2字节
        if (((uintptr_t)s & 1) && n > 0) {
            *d++ = *s++;
            n--;
        }

        // 用16位块复制
        uint16_t *d16 = (uint16_t *)d;
        const uint16_t *s16 = (const uint16_t *)s;
        for (; n >= 2; n -= 2) {
            *d16++ = *s16++;
        }
        d = (unsigned char *)d16;
        s = (const unsigned char *)s16;
    }

    // 处理剩余的字节
    while (n-- > 0) {
        *d++ = *s++;
    }

    return dest;
}

void *memset(void *dst_, int c, size_t n) {
    char *dst = (char *)dst_;
    const char *max = dst + n;
    uint8_t byte = c & 0xff;
    uint64_t word = (uint64_t)byte << 56 | (uint64_t)byte << 48 |
                    (uint64_t)byte << 40 | (uint64_t)byte << 32 |
                    (uint64_t)byte << 24 | (uint64_t)byte << 16 |
                    (uint64_t)byte << 8 | (uint64_t)byte;

    // 处理未对齐的起始部分
    while ((uintptr_t)dst % 8 != 0 && dst < max) {
        *dst++ = byte;
    }

    // 以64位字填充对齐部分
    while (dst + 8 <= max) {
        *(uint64_t *)dst = word;
        dst += 8;
    }

    // 处理剩余字节
    while (dst < max) {
        *dst++ = byte;
    }
    return dst_;
}

size_t strlen(const char *s) {
    size_t n;

    for (n = 0; *s; s++) {
        n++;
    }

    return n;
}

char *strcpy(char *dst, const char *src) {
    char *ret = dst;

    while ((*dst++ = *src++) != 0) {
    }

    return ret;
}

const char *strchr(const char *s, int c) {
    for (; *s; s++) {
        if (*s == c) {
            return s;
        }
    }
    return 0;
}

int strcmp(const char *p, const char *q) {
    while (*p && *p == *q) {
        p++, q++;
    }

    if ((u_int)*p < (u_int)*q) {
        return -1;
    }

    if ((u_int)*p > (u_int)*q) {
        return 1;
    }

    return 0;
}

int parse_number(const char *str, int base, const char **next_token) {
    int result = 0;

    while (*str != '\0') {
        char current_digit_char = *str;
        int current_digit = 0;

        if (current_digit_char >= '0' && current_digit_char <= '9') {
            current_digit = current_digit_char - '0';
        } else if (current_digit_char >= 'a' && current_digit_char <= 'z') {
            current_digit = current_digit_char - 'a' + 10;
        } else if (current_digit_char >= 'A' && current_digit_char <= 'Z') {
            current_digit = current_digit_char - 'A' + 10;
        } else {
            break;
        }

        if (current_digit >= base) {
            break;
        }

        result = result * base + current_digit;

        str++;
    }

    if (next_token != NULL) {
        *next_token = str;
    }

    return result;
}