#ifndef __ENDIAN_H
#define __ENDIAN_H

#include <stdint.h>

uint32_t swap_uint32(uint32_t val);
uint64_t swap_uint64(uint64_t val);

#ifdef BE
// 主机为大端序
#define be32toh(val) (val)            // 大端转主机：无需转换
#define le32toh(val) swap_uint32(val) // 小端转主机：需要交换字节
#define be64toh(val) (val)            // 大端转主机：无需转换
#define le64toh(val) swap_uint64(val) // 小端转主机：需要交换字节
#else
// 主机为小端序
#define be32toh(val) swap_uint32(val) // 大端转主机：需要交换字节
#define le32toh(val) (val)            // 小端转主机：无需转换
#define be64toh(val) swap_uint64(val) // 大端转主机：需要交换字节
#define le64toh(val) (val)            // 小端转主机：无需转换
#endif

#endif