#include <num.h>
#include <types.h>

void *memcpy(void *dst, const void *src, size_t n) {
    void *dstaddr = dst;
    void *max = dst + n;

    if (((u_long)src & 3) != ((u_long)dst & 3)) {
        while (dst < max) {
            *(char *)dst++ = *(char *)src++;
        }
        return dstaddr;
    }

    while (((u_long)dst & 3) && dst < max) {
        *(char *)dst++ = *(char *)src++;
    }

    // copy machine words while possible
    while (dst + 4 <= max) {
        *(uint32_t *)dst = *(uint32_t *)src;
        dst += 4;
        src += 4;
    }

    // finish the remaining 0-3 bytes
    while (dst < max) {
        *(char *)dst++ = *(char *)src++;
    }
    return dstaddr;
}

void *memset(void *dst, int c, size_t n) {
    void *dstaddr = dst;
    void *max = dst + n;
    u_char byte = c & 0xff;
    uint32_t word = byte | byte << 8 | byte << 16 | byte << 24;

    while (((u_long)dst & 3) && dst < max) {
        *(u_char *)dst++ = byte;
    }

    // fill machine words while possible
    while (dst + 4 <= max) {
        *(uint32_t *)dst = word;
        dst += 4;
    }

    // finish the remaining 0-3 bytes
    while (dst < max) {
        *(u_char *)dst++ = byte;
    }
    return dstaddr;
}

size_t strlen(const char *s) {
    int n;

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