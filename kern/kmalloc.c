#include "pmap.h"
#include <kmalloc.h>
#include <printk.h>

struct MBlock_list mblock_list;

size_t total_requested_bytes = 0;
size_t total_free_bytes = 0;

void kmalloc_init() {
    int ret = 0;

    printk("kmalloc_init: begin\n");

    // 分配一页用于二级页表，并将其映射到一级页表项中
    // 该映射将被复制到所有进程的地址空间中

    struct Page *p2page = NULL;

    if ((ret = page_alloc(&p2page)) < 0) {
        panic("kmalloc_init: failed to allocate page: %d\n", ret);
    }

    p2page->pp_ref++;

    kernel_boot_pgdir[P1X(KMALLOC_BEGIN_VA)] = (page2ppn(p2page) << 10) | PTE_V;

    LIST_INIT(&mblock_list);

    struct MBlock *heap_begin = (struct MBlock *)KMALLOC_BEGIN_VA;

    printk("kmalloc_init: heap_begin: 0x%016lx\n", heap_begin);

    heap_begin->size = KMALLOC_HEAP_SIZE - MBLOCK_SIZE;
    heap_begin->ptr = (void *)heap_begin->data;
    heap_begin->free = 1;

    LIST_INSERT_HEAD(&mblock_list, heap_begin, mb_link);

    printk("kmalloc_init: end\n");
}

void *kmalloc(size_t size) {
    total_requested_bytes += size;

    size = ROUND(size, 8);

    struct MBlock *cur_block = NULL;

    LIST_FOREACH(cur_block, &mblock_list, mb_link) {
        if (cur_block->free == 0) {
            continue;
        }
        if (size <= cur_block->size) {
            size_t remain_size = cur_block->size - size;

            if (remain_size < (MBLOCK_SIZE + 8)) {
                size += remain_size;

                cur_block->size = size;
                cur_block->free = 0;

            } else {
                size_t next_node_addr =
                    (size_t)(cur_block) + MBLOCK_SIZE + size;
                struct MBlock *next_node = (struct MBlock *)next_node_addr;

                next_node->size = remain_size - MBLOCK_SIZE;
                next_node->ptr = (void *)(next_node_addr + MBLOCK_SIZE);
                next_node->free = 1;

                cur_block->free = 0;
                cur_block->size = size;

                LIST_INSERT_AFTER(cur_block, next_node, mb_link);
            }

            return cur_block->ptr;
        }
    }

    return NULL;
}

void kfree(void *p) {

    size_t ptr_addr = (size_t)p;

    if ((ptr_addr < (KMALLOC_BEGIN_VA + MBLOCK_SIZE)) ||
        (ptr_addr > KMALLOC_END_VA)) {

        panic("kmalloc: free: invalid pointer: 0x%016lx\n", ptr_addr);
        return;
    }

    size_t node_addr = ptr_addr - MBLOCK_SIZE;

    struct MBlock *cur_node = (struct MBlock *)node_addr;

    if (((size_t)cur_node->ptr) != ((size_t)(cur_node->data))) {
        panic("kmalloc: free: invalid node structure: node = 0x%016lx "
              "node->ptr = 0x%016lx node->data = 0x%016lx\n",
              cur_node, (size_t)cur_node->ptr, (size_t)(cur_node->data));
        return;
    }

    total_free_bytes += cur_node->size;

    struct MBlock *next_node = LIST_NEXT(cur_node, mb_link);
    struct MBlock *prev_node = NULL;

    if (LIST_FIRST(&mblock_list) != cur_node) {
        prev_node = MBLOCK_PREV(cur_node, mb_link);
    }

    if (next_node != NULL) {
        if (next_node->free == 1) {
            cur_node->size += (next_node->size + MBLOCK_SIZE);
            LIST_REMOVE(next_node, mb_link);
        }
    }

    if (prev_node != NULL) {
        if (prev_node->free == 1) {
            prev_node->size += (cur_node->size + MBLOCK_SIZE);
            LIST_REMOVE(cur_node, mb_link);
        }
    }

    cur_node->free = 1;
}

void allocation_summarize() {
    size_t allocated = 0;
    size_t left = 0;
    size_t block_count = 0;

    struct MBlock *cur_block = NULL;

    LIST_FOREACH(cur_block, &mblock_list, mb_link) {
        block_count++;

        allocated += sizeof(struct MBlock);

        if (cur_block->free == 0) {
            allocated += cur_block->size;
        } else {
            left += cur_block->size;
        }
    }

    debugk("allocation_summarize",
           "block = %lu total = %lu allocated = %lu left = %lu total requested "
           "= %lu total freed = %lu\n",
           block_count, allocated + left, allocated, left,
           total_requested_bytes, total_free_bytes);
}