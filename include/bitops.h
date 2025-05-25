#ifndef _SYS_BITOPS_H_
#define _SYS_BITOPS_H_

#define BITS_PER_LONG 64
#define BITS_PER_LONG_LONG 64

/*
 * 创建一个将第[h, l]位置为1的掩码，位编号从0开始。例如
 * GENMASK_ULL(39, 21) 会生成 64 位向量 0x000000ffffe00000。
 */
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

#define GENMASK_ULL(h, l)                                                      \
    (((~0ULL) << (l)) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))

#define LOG_1(n) (((n) >= 2) ? 1 : 0)
#define LOG_2(n) (((n) >= (1 << 2)) ? (2 + LOG_1((n) >> 2)) : LOG_1(n))
#define LOG_4(n) (((n) >= (1 << 4)) ? (4 + LOG_2((n) >> 4)) : LOG_2(n))
#define LOG_8(n) (((n) >= (1 << 8)) ? (8 + LOG_4((n) >> 8)) : LOG_4(n))
// 计算 ⌊log2​(n)⌋
// 输入：32位无符号整数，n >= 1
// 时间复杂度：O(loglogn)
#define LOG2(n) (((n) >= (1 << 16)) ? (16 + LOG_8((n) >> 16)) : LOG_8(n))

#endif
