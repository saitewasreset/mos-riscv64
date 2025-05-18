#ifndef _INC_TYPES_H_
#define _INC_TYPES_H_

#include <stddef.h>
#include <stdint.h>

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

typedef int64_t reg_t;
typedef uint64_t u_reg_t;

#define MIN(_a, _b)                                                            \
  ({                                                                           \
    typeof(_a) __a = (_a);                                                     \
    typeof(_b) __b = (_b);                                                     \
    __a <= __b ? __a : __b;                                                    \
  })

// 查找大于等于`a`的、最近的是`n`的倍数的整数，要求`n`必须是 2 的正整数幂
#define ROUND(a, n) (((((u_long)(a)) + (n) - 1)) & ~((n) - 1))
// 查找小于等于`a`的、最近的是`n`的倍数的整数，要求`n`必须是 2 的正整数幂
#define ROUNDDOWN(a, n) (((u_long)(a)) & ~((n) - 1))

#endif /* !_INC_TYPES_H_ */
