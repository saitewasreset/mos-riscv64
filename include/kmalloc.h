#ifndef _KMALLOC_H_
#define _KMALLOC_H_

#include <mmu.h>
#include <pmap.h>

// MBLOCK_SIZE：40字节
#define MBLOCK_SIZE sizeof(struct MBlock)

#define MBLOCK_PREV(elm, field)                                                \
    (struct MBlock *)((elm)->field.le_prev) // place entry at first

LIST_HEAD(MBlock_list, MBlock);
typedef LIST_ENTRY(MBlock) MBlock_LIST_entry_t;

struct MBlock {
    MBlock_LIST_entry_t
        mb_link; // 注意：若要MBLOCK_PREV宏正常工作，mb_link属性必须放在结构体开始处！16字节

    uint64_t size; // size of the available space, if allocated, is the size of
                   // allocated space，8字节
    void *ptr; // pointer to the begin of block, as a magic number to check for
               // validity，8字节
    uint32_t free;    // 1 if block is free, 0 if the block is allocated，4字节
    uint32_t padding; // padding to make size of block multiple of 8，4字节
    char data[];      // data of the block, allocated for user，0字节
};

void kmalloc_init(void);
void *kmalloc(size_t size);
void kfree(void *p);

void allocation_summarize();

#endif
