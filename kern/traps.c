#include <env.h>
#include <pmap.h>
#include <printk.h>
#include <trap.h>

extern void handle_int(void);
extern void handle_tlb(void);
extern void handle_sys(void);
extern void handle_mod(void);
extern void handle_reserved(void);

/*
 * 部分异常的`ExcCode`：
 * | Value | Code      | What just happend?             |
 * | ----- | --------- | ------------------------------ |
 * | 0     | Int       | 中断                             |
 * | 1     | Mod       | 向TLB中标记为只读的页写入（Store）          |
 * | 2     | TLBL      | 从TLB中标记为无效的页读取（Load, Fetch）    |
 * | 3     | TLBS      | 向TLB中标记为无效的页写入（Store）          |
 * | 4/5   | AdEL/AdES | 地址错误：不满足对其要求、访问违例              |
 * | 8     | Sys       | 系统调用                           |
 * | 9     | Bp        | 断点（`break`指令）                  |
 * | 10    | RI        | 非法指令                           |
 * | 12    | Ov        | 算数运算溢出（算数指令的trapping变体，如`add`）
 */

// int arr[5] = {[2 ... 3] = 1}是GNU C拓展语法，等效于：
// arr[2] = 1; arr[3] = 1;
void (*exception_handlers[32])(void) = {
    [0 ... 31] = handle_reserved,
    [0] = handle_int,
    [2 ... 3] = handle_tlb,
#if !defined(LAB) || LAB >= 4
    [1] = handle_mod,
    [8] = handle_sys,
#endif
};

/* Overview:
 *   The fallback handler when an unknown exception code is encountered.
 *   'genex.S' wraps this function in 'handle_reserved'.
 */
void do_reserved(struct Trapframe *tf) {
    print_tf(tf);
    panic("Unknown ExcCode %2d", (tf->cp0_cause >> 2) & 0x1f);
}
