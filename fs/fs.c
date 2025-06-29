#include "fs.h"
#include "error.h"
#include "lib.h"
#include "serv.h"
#include <mmu.h>

struct Super *super; // 由`read_super`函数设置

uint32_t *bitmap; // 由`read_bitmap`函数设置

void file_flush(struct File *);
int block_is_free(uint32_t);

/*
 * 概述：
 *   计算指定磁盘块在缓存中的虚拟地址。通过DISKMAP基地址与块号偏移计算，
 *   实现磁盘块到虚拟地址空间的线性映射。
 *
 * Precondition：
 *   - blockno必须为合法磁盘块号（由调用方保证，未显式校验）
 *
 * Postcondition：
 *   返回磁盘块映射到用户空间的虚拟地址，满足：
 *   address = DISKMAP + blockno * BLOCK_SIZE
 *
 * 副作用：
 *   无副作用，不修改任何全局状态
 *
 * 关键点：
 *   - 地址计算采用线性映射，复杂度O(1)
 *   - 不进行块号有效性校验（如blockno是否超过DISKMAX/BLOCK_SIZE）
 *
 * 潜在风险：
 *   若blockno超过最大支持块号（DISKMAX/BLOCK_SIZE=1GB/4KB=262144），
 *   会导致虚拟地址越界访问
 */
void *disk_addr(uint32_t blockno) {
    /* Exercise 5.6: Your code here. */

    return (void *)(size_t)(DISKMAP + blockno * BLOCK_SIZE);
}

/*
 * 概述：
 *   检查虚拟地址是否已建立有效页表映射。通过系统调用获取物理地址判断。
 *
 * Precondition：
 *   - 若`va = NULL`，也能得到正确结果（依赖UTEMP下页面未映射）
 *
 * Postcondition：
 *   - 返回1表示地址已映射，0表示未映射
 *
 * 副作用：
 *   无副作用
 */
int va_is_mapped(void *va) { return (syscall_get_physical_address(va) != 0); }

/*
 * 概述：
 *   检查磁盘块是否已建立缓存映射。通过查询页表项有效性实现。
 *
 * Precondition：
 *   - blockno必须为合法磁盘块号
 *   - 依赖全局DISKMAP地址空间布局
 *
 * Postcondition：
 *   - 返回映射的虚拟地址（若PTE_V位有效）
 *   - 未映射时返回NULL
 *
 * 副作用：
 *   无副作用，仅进行状态查询
 *
 * 关键点：
 *   - 同时检查页目录项和页表项的PTE_V位，确保完整映射路径有效
 *   - 通过disk_addr计算虚拟地址，依赖线性映射规则
 */
void *block_is_mapped(uint32_t blockno) {
    void *va = disk_addr(blockno);
    if (va_is_mapped(va)) {
        return va;
    }
    return NULL;
}

/*
 * 概述：
 *   检查虚拟地址对应的页是否标记为脏（已修改）。通过系统调用、页面DIRTY位判断。
 *
 * Precondition：
 *   - 无
 *
 * Postcondition：
 *   - 返回非零值表示脏页，0表示干净页或未映射
 *
 * 副作用：
 *   无副作用
 */
int va_is_dirty(void *va) { return syscall_is_dirty(va); }

/*
 * 概述：
 *   检查磁盘块是否为脏状态。通过查询对应虚拟地址的页表项PTE_DIRTY标志实现，
 *
 * Precondition：
 *   - blockno必须为合法磁盘块号（0 <= blockno < DISKMAX/BLOCK_SIZE）
 *   - blockno可以是**已映射**或**未映射**的磁盘块号
 *
 * Postcondition：
 *   - 返回非零值表示块已映射且为脏状态
 *   - 返回0表示块未映射或未标记为脏
 *
 * 副作用：
 *   无副作用，仅进行状态查询
 */
int block_is_dirty(uint32_t blockno) {
    void *va = disk_addr(blockno);
    return va_is_dirty(va);
}

/*
 * 概述：
 *   标记磁盘块为脏状态。通过系统调用重映射修改页表项权限标志实现，
 *   确保后续写入操作能触发脏页同步机制。
 *
 * Precondition：
 *   - blockno必须已建立有效缓存映射（通过va_is_mapped验证）
 *
 * Postcondition：
 *   - 成功：页表项PTE_DIRTY标志被设置，返回0
 *   - 失败：返回-E_NOT_FOUND（未映射）或系统调用错误码
 *
 * 副作用：
 *   - 通过系统调用改变内存映射属性，影响进程页表结构
 */
int dirty_block(uint32_t blockno) {
    void *va = disk_addr(blockno);

    if (!va_is_mapped(va)) {
        return -E_NOT_FOUND;
    }

    if (va_is_dirty(va)) {
        return 0;
    }

    return syscall_mem_map(0, va, 0, va, PTE_V | PTE_RW | PTE_USER | PTE_DIRTY);
}

/*
 * 概述：
 *   将缓存中的磁盘块数据写回磁盘。强制同步内存缓存与物理存储，
 *   确保数据持久化。
 *
 * Precondition：
 *   - blockno必须已被映射到缓存（通过block_is_mapped验证）
 *
 * Postcondition：
 *   - 块数据通过VirtIO接口写回磁盘，物理存储更新
 *   - 若块未映射，触发user_panic
 *
 * 副作用：
 *   - 修改磁盘物理存储内容
 *
 * 关键点：
 *   - 必须确保块已映射，否则直接终止进程
 */
void write_block(uint32_t blockno) {
    // Step 1: detect is this block is mapped, if not, can't write it's data to
    // disk.
    if (!block_is_mapped(blockno)) {
        user_panic("write unmapped block %08x", blockno);
    }

    // Step2: write data to block device.
    void *va = disk_addr(blockno);
    sector_write(blockno * SECT2BLK, va, SECT2BLK);
}

/*
 * 概述：
 *   确保磁盘块加载到内存缓存中，提供读时加载功能。
 *   按需发起磁盘读取，并管理缓存状态标记。
 *
 * Precondition：
 *   - 全局变量`super`必须指向超级块的内存缓存地址
 *   - 全局变量`bitmap`必须指向空闲区位图的内存缓存地址
 *   - `blk`可为`NULL`
 *   - `isnew`可为`NULL`
 *   - `blockno`须小于`s_nblocks`
 *   - 对应的磁盘块不能为空闲状态
 *
 * Postcondition：
 *   - 块数据存在于内存缓存，若blk != NULL，*blk指向缓存地址
 *   - 若isnew != NULL，*isnew反映是否为首次加载
 *   - 返回0表示成功
 *   - 若`blockno`大于等于`s_nblocks`，触发panic
 *   - 若对应的磁盘块为空闲状态，触发panic
 *   - 若为缓存分配内存失败，-E_NO_MEM
 *   - 若磁盘读取失败，触发panic
 *
 * 副作用：
 *   - 可能分配物理页并建立映射（页表修改）
 *   - 调用sector_read读取磁盘数据，改变设备状态
 *   - 设置isnew标志影响上层缓存管理逻辑
 *
 * 潜在问题：
 *   若全局变量`super`、`bitmap`未正确初始化，将跳过块数量检查、
 *   空闲检查，直接分配缓存并从磁盘读取。这是为了在读取超级块、空闲块位图
 *   时也能使用该函数。但这或许不是最好的实现方式（？）
 */
int read_block(uint32_t blockno, void **blk, uint32_t *isnew) {
    // Step 1: validate blockno. Make file the block to read is within the disk.
    if (super && blockno >= super->s_nblocks) {
        user_panic("reading non-existent block %08x\n", blockno);
    }

    // Step 2: validate this block is used, not free.
    // Hint:
    //  If the bitmap is NULL, indicate that we haven't read bitmap from disk to
    //  memory until now. So, before we check if a block is free using
    //  `block_is_free`, we must ensure that the bitmap blocks are already read
    //  from the disk to memory.
    if (bitmap && block_is_free(blockno)) {
        user_panic("reading free block %08x\n", blockno);
    }

    // Step 3: transform block number to corresponding virtual address.
    void *va = disk_addr(blockno);

    // Step 4: read disk and set *isnew.
    // Hint:
    //  If this block is already mapped, just set *isnew, else alloc memory and
    //  read data from IDE disk (use `syscall_mem_alloc` and `ide_read`).
    //  We have only one IDE disk, so the diskno of ide_read should be 0.
    if (block_is_mapped(blockno)) { // the block is in memory
        if (isnew) {
            *isnew = 0;
        }
    } else { // the block is not in memory
        if (isnew) {
            *isnew = 1;
        }
        try(syscall_mem_alloc(0, va, PTE_V | PTE_RW | PTE_USER));
        sector_read(blockno * SECT2BLK, va, SECT2BLK);
    }

    // Step 5: if blk != NULL, assign 'va' to '*blk'.
    if (blk) {
        *blk = va;
    }
    return 0;
}

/*
 * 概述：
 *   为磁盘块分配缓存页并建立虚拟内存映射。若块已映射则直接返回，
 *   否则通过系统调用分配物理页，权限设置为可写（PTE_RW）。
 *
 *   注意，本函数只分配空间，不实际读取磁盘内容。
 *
 * Precondition：
 *   - blockno必须为合法磁盘块号（0 <= blockno < DISKMAX/BLOCK_SIZE）
 *   - blockno可以是**已映射**或**未映射**的磁盘块号
 *   - 依赖全局DISKMAP虚拟地址空间布局
 *
 * Postcondition：
 *   - 成功：块对应虚拟地址已映射物理页，返回0
 *   - 已映射：立即返回0
 *   - 若内存不足，返回-E_NO_MEM
 *
 * 副作用：
 *   - 修改当前进程页表，新增虚拟地址映射
 *   - 可能分配物理页，改变物理内存状态
 *   - 设置页表项的PTE_D（脏页）标志位
 *
 * 关键点：
 *   - 直接使用disk_addr计算虚拟地址，依赖线性映射规则
 *   - 不处理块内容初始化，依赖后续读写操作填充
 */
// Checked by DeepSeek-R1 20250508 2149
int map_block(uint32_t blockno) {
    // Step 1: If the block is already mapped in cache, return 0.
    // Hint: Use 'block_is_mapped'.
    /* Exercise 5.7: Your code here. (1/5) */

    void *va = block_is_mapped(blockno);

    if (va != NULL) {
        return 0;
    }

    // Step 2: Alloc a page in permission 'PTE_D' via syscall.
    // Hint: Use 'disk_addr' for the virtual address.
    /* Exercise 5.7: Your code here. (2/5) */

    va = disk_addr(blockno);

    int r = syscall_mem_alloc(0, va, PTE_V | PTE_RW | PTE_USER);

    return r;
}

/*
 * 概述：
 *   解除磁盘块缓存映射。若块处于脏状态且未被释放，触发写回操作。
 *   执行缓存页解除映射，释放相关虚拟地址资源。
 *
 * Precondition：
 *   - blockno必须为合法块号
 *   - blockno可以是**已映射**或**未映射**的磁盘块号
 *   - 依赖页表项PTE_DIRTY标志位（硬件）状态
 *
 * Postcondition：
 *   - 成功：虚拟地址解除映射，脏数据写回磁盘
 *   - 未映射块：静默跳过
 *   - 最后断言确认解除状态（block_is_mapped == NULL）
 *
 * 副作用：
 *   - 可能修改磁盘存储内容（脏页写回）
 *   - 释放虚拟地址映射，可能导致TLB刷新
 *   - 减少物理页引用计数（若未被其他映射引用）
 *
 * 关键点：
 *   - 写回条件需同时满足非空闲块且脏页状态
 *   - 使用user_assert保证解除映射彻底完成
 */
// Checked by DeepSeek-R1 20250508 2149
void unmap_block(uint32_t blockno) {
    // Step 1: Get the mapped address of the cache page of this block using
    // 'block_is_mapped'.
    void *va;
    /* Exercise 5.7: Your code here. (3/5) */

    va = block_is_mapped(blockno);

    // Step 2: If this block is used (not free) and dirty in cache, write it
    // back to the disk first. Hint: Use 'block_is_free', 'block_is_dirty' to
    // check, and 'write_block' to sync.
    /* Exercise 5.7: Your code here. (4/5) */

    // va != NULL is checked in block_is_dirty
    if ((block_is_free(blockno) == 0) && (block_is_dirty(blockno) != 0)) {
        write_block(blockno);
    }

    // Step 3: Unmap the virtual address via syscall.
    /* Exercise 5.7: Your code here. (5/5) */
    if (va != NULL) {
        syscall_mem_unmap(0, va);
    }

    user_assert(!block_is_mapped(blockno));
}

/*
 * 概述：
 *   检查指定磁盘块是否空闲。通过查询位图数据结构判断块状态，
 *   依赖全局超级块结构确定磁盘总块数。
 *
 * Precondition：
 *   - 全局超级块指针super必须已初始化（非NULL）
 *   - 全局位图数组bitmap必须正确映射磁盘块状态
 *
 * Postcondition：
 *   - 返回1表示块空闲（位图对应位为1）
 *   - 返回0表示块已占用或输入参数非法（blockno越界/super未初始化）
 *
 * 副作用：
 *   - 无副作用，仅进行状态查询不修改任何全局状态
 *
 * 关键点：
 *   - 位图索引计算：blockno/32定位到32位整型数组元素
 *   - 位掩码生成：1左移(blockno%32)位实现位级访问
 */
int block_is_free(uint32_t blockno) {
    if (super == 0 || blockno >= super->s_nblocks) {
        return 0;
    }

    if (bitmap[blockno / 32] & (1 << (blockno % 32))) {
        return 1;
    }

    return 0;
}

/*
 * 概述：
 *   将指定磁盘块标记为空闲状态。通过设置位图对应位实现，
 *   包含参数有效性检查及位图更新操作。
 *   此函数**不**将修改同步到磁盘。
 *
 * 一致性：
 *   对空闲位图的修改仅影响内存中的缓存
 *
 * Precondition：
 *   - 全局超级块指针super必须已初始化（否则触发panic）
 *   - 全局位图数组bitmap必须可写
 *   - blockno参数必须为有效逻辑块号（0 < blockno < super->s_nblocks）
 *
 * Postcondition：
 *   - 成功：位图对应位被置1，该块可被后续分配
 *   - 无效输入：blockno为0或越界时静默返回
 *
 * 副作用：
 *   - 修改全局位图数组内容
 *   - 可能触发user_panic中断进程（当super未初始化时）
 *
 * 关键点：
 *   - 块号0的特殊处理（保留块不可释放）
 *   - 位操作需先读取原值再或运算，避免覆盖其他位状态
 *   - 直接操作内存映射的位图，未同步回磁盘（由上层逻辑处理）
 */
// Checked by DeepSeek-R1 20250508 1716
void free_block(uint32_t blockno) {
    // You can refer to the function 'block_is_free' above.
    // Step 1: If 'blockno' is invalid (0 or >= the number of blocks in
    // 'super'), return.
    /* Exercise 5.4: Your code here. (1/2) */

    if (super == NULL) {
        user_panic("free_block called while super is NULL");
    }

    if ((blockno == 0) || (blockno >= super->s_nblocks)) {
        return;
    }

    // Step 2: Set the flag bit of 'blockno' in 'bitmap'.
    // Hint: Use bit operations to update the bitmap, such as b[n / W] |= 1 <<
    // (n % W).
    /* Exercise 5.4: Your code here. (2/2) */

    uint32_t target_bitmap_block = bitmap[blockno / 32];
    target_bitmap_block |= (1 << (blockno % 32));
    bitmap[blockno / 32] = target_bitmap_block;
}

/*
 * 概述：
 *   在位图中查找并分配一个空闲磁盘块。使用线性扫描策略，
 *   跳过前3个保留块（引导块、超级块），标记找到的块为已使用并**同步至磁盘**。
 *
 * 一致性：
 *   对空闲位图的修改被立即写回磁盘
 *
 * Precondition：
 *   - 全局超级块super必须已初始化（s_nblocks有效）
 *   - 位图数组bitmap必须已加载至内存
 *
 * Postcondition：
 *   - 成功返回分配的物理块号(>=3)
 *   - 无空闲块时返回-E_NO_DISK
 *   - **修改的位图块已写回磁盘**
 *
 * 副作用：
 *   - 修改全局位图数组内容
 *   - 调用write_block修改磁盘存储的位图信息
 *
 * 关键点：
 *   - 从块号3开始遍历，避开保留块（0:引导块，1:超级块，2:位图块（？））
 */
int alloc_block_num(void) {
    int blockno;
    // walk through this bitmap, find a free one and mark it as used, then sync
    // this block to IDE disk (using `write_block`) from memory.
    for (blockno = 3; blockno < super->s_nblocks; blockno++) {
        if (bitmap[blockno / 32] & (1 << (blockno % 32))) { // the block is free
            bitmap[blockno / 32] &= ~(1 << (blockno % 32));
            write_block(blockno / BLOCK_SIZE_BIT + 2); // write to disk.
            return blockno;
        }
    }
    // no free blocks.
    return -E_NO_DISK;
}

/*
 * 概述：
 *   分配并映射一个磁盘块。包含分配逻辑与内存映射两步操作，
 *   实现磁盘空间与缓存页的协同管理。
 *   提供原子性保证，分配失败时自动回滚。
 *
 * 一致性：
 *   分配成功时，对空闲位图的修改被立即写回磁盘；
 *   内存分配失败回滚时，`free_block`仅修改内存中的空闲位图
 *
 * Precondition：
 *
 * Postcondition：
 *   - 若成功，返回分配的块号(>=3)
 *   - 若无可用空闲磁盘块，返回-E_NO_DISK
 *
 * 副作用：
 *   - 可能修改位图状态并同步至磁盘
 *   - 可能分配物理内存页并建立虚拟地址映射
 *   - 失败时通过free_block回滚磁盘块分配
 *
 * 关键点：
 *   - 两步操作确保资源完全获取：先占磁盘块，再映射内存
 *   - 错误处理保证资源泄漏：映射失败立即释放已分配的磁盘块
 *   - 返回原始块号而非虚拟地址，需配合disk_addr获取缓存位置
 *
 * 潜在问题：
 *   在`alloc_block_num`中，对空闲位图的修改被立即写回磁盘。
 *   但在内存分配失败回滚时，`free_block`仅修改内存中的空闲位图
 *   未写回磁盘。
 */
int alloc_block(void) {
    int r, bno;
    // Step 1: find a free block.
    if ((r = alloc_block_num()) < 0) { // failed.
        return r;
    }
    bno = r;

    // Step 2: map this block into memory.
    if ((r = map_block(bno)) < 0) {
        free_block(bno);
        return r;
    }

    // Step 3: return block number.
    return bno;
}

/*
 * 概述：
 *   读取并验证文件系统超级块。该函数是文件系统初始化的核心步骤，
 *   确保磁盘上的元数据符合预期格式，为后续操作提供基准验证。
 *
 * Precondition：
 *   - 超级块必须位于磁盘块1（由固定位置约定）
 *   - read_block函数需在super未初始化时跳过块范围检查（允许加载超级块本身）
 *
 * Postcondition：
 *   - 成功：全局super指针指向有效超级块的缓存地址
 *   - 失败：触发user_panic中断进程，错误原因包括：
 *     - 超级块读取错误（磁盘I/O故障或内存不足）
 *     - 魔数不匹配（非预期文件系统类型）
 *     - 磁盘大小超限（超过DISKMAX容量）
 *
 * 副作用：
 *   - 设置全局super变量，改变文件系统全局状态
 *   - 调用read_block分配物理页并建立块1的虚拟内存映射
 *   - 可能触发磁盘IDE读取操作，修改设备寄存器状态
 *
 * 关键点：
 *   - 块1的硬编码假设：文件系统布局依赖于超级块固定存储在第二个块
 *   - 魔数验证防止加载损坏或非兼容文件系统镜像
 *   - 磁盘大小检查避免逻辑块号越界引发后续缓存操作错误
 */
void read_super(void) {
    int r;
    void *blk;

    // Step 1: read super block.
    if ((r = read_block(1, &blk, 0)) < 0) {
        user_panic("cannot read superblock: %d", r);
    }

    super = blk;

    // Step 2: Check fs magic nunber.
    if (super->s_magic != FS_MAGIC) {
        user_panic("bad file system magic number %x %x", super->s_magic,
                   FS_MAGIC);
    }

    // Step 3: validate disk size.
    if (super->s_nblocks > DISKMAX / BLOCK_SIZE) {
        user_panic("file system is too large");
    }

    debugf("superblock is good\n");
}

/*
 * 概述：
 *   加载并验证文件系统空闲位图。通过读取所有位图块至内存，
 *   确保元数据块及位图块自身标记为已使用，维持文件系统完整性。
 *
 * Precondition：
 *   - 全局超级块super必须已正确初始化（通过read_super）
 *   - read_block函数需在super已设置时跳过块的空闲检查（因bitmap尚未加载）
 *   - disk_addr函数需正确映射块号至虚拟地址，支持连续位图存储
 *
 * Postcondition：
 *   - 全局bitmap变量指向第一个位图块内存地址
 *   - 所有位图块加载至内存，形成连续虚拟地址空间
 *   - 块0（引导块）、块1（超级块）及所有位图块验证为非空闲
 *   - 任何验证失败触发user_assert中断进程
 *
 * 副作用：
 *   - 设置全局变量bitmap指向位图内存区域
 *   - read_block调用为每个位图块建立缓存映射，可能分配物理页
 *   - 多次调用read_block触发IDE设备操作，改变磁盘控制器状态
 *
 * 关键点：
 *   - 循环读取块i+2：位图块起始于磁盘块2，与超级块布局约定一致
 *   - user_assert检查保障元数据块不可被回收，防止文件系统逻辑损坏
 *   - read_block参数blk实际未被利用，通过disk_addr确保块加载
 */
void read_bitmap(void) {
    uint32_t i;
    void *blk = NULL;

    // Step 1: Calculate the number of the bitmap blocks, and read them into
    // memory.
    uint32_t nbitmap = super->s_nblocks / BLOCK_SIZE_BIT + 1;
    for (i = 0; i < nbitmap; i++) {
        read_block(i + 2, blk, 0);
    }

    bitmap = disk_addr(2);

    // Step 2: Make sure the reserved and root blocks are marked in-use.
    // Hint: use `block_is_free`
    user_assert(!block_is_free(0));
    user_assert(!block_is_free(1));

    // Step 3: Make sure all bitmap blocks are marked in-use.
    for (i = 0; i < nbitmap; i++) {
        user_assert(!block_is_free(i + 2));
    }

    debugf("read_bitmap is good\n");
}

/*
 * 概述：
 *   验证write_block函数的正确性。通过覆写并恢复超级块，
 *   测试块写入、内存映射状态及数据持久化能力。
 *
 * 一致性：
 *   在测试过程中，损坏的超级块被写入磁盘；但若测试完成，将重新写入正确的内容；
 *   磁盘第0块（引导扇区）在内存中的缓存被覆盖为超级块的内容，但磁盘上的内容不受影响。
 *
 * Precondition：
 *   - IDE设备需正常工作保证磁盘读写
 *
 * Postcondition：
 *   - 超级块内容被完整恢复，super指针有效
 *   - 所有断言验证通过，证明：
 *     a) 块1写入磁盘后能正确从磁盘读取
 *     b) 块映射状态随操作正常变化
 *     c) 写入操作对磁盘的修改具备持久性
 *
 * 副作用：
 *   - 测试过程中，`super`全局变量被暂时设置为NULL，防止读取到错误的超级块
 *   - 临时修改磁盘块1（超级块）内容（内存+磁盘）
 *   - 解除并重建块1的内存映射，影响页表状态
 *   - 验证过程中触发多次磁盘I/O操作
 *
 * 关键点：
 *   - 备份使用块0临时存储原始超级块，避免数据丢失
 *   - "OOPS!\n"作为魔数简化数据验证
 *   - block_is_mapped验证TLB状态与物理存储的同步
 *   - 通过syscall_mem_unmap强制缓存失效，验证磁盘持久化
 */
void check_write_block(void) {
    super = 0;

    // backup the super block.
    // copy the data in super block to the first block on the disk.
    panic_on(read_block(0, 0, 0));
    memcpy((char *)disk_addr(0), (char *)disk_addr(1), BLOCK_SIZE);

    // smash it
    strcpy((char *)disk_addr(1), "OOPS!\n");
    write_block(1);
    user_assert(block_is_mapped(1));

    // clear it out
    panic_on(syscall_mem_unmap(0, disk_addr(1)));
    user_assert(!block_is_mapped(1));

    // validate the data read from the disk.
    panic_on(read_block(1, 0, 0));
    user_assert(strcmp((char *)disk_addr(1), "OOPS!\n") == 0);

    // restore the super block.
    memcpy((char *)disk_addr(1), (char *)disk_addr(0), BLOCK_SIZE);
    write_block(1);
    super = (struct Super *)disk_addr(1);
}

/*
 * 概述：
 *   文件系统初始化入口。分阶段加载并验证核心元数据，
 *   确保磁盘结构合法且基础功能正常。
 *
 * Precondition：
 *   - 磁盘设备已完成底层初始化（如IDE控制器）
 *   - 磁盘内容符合文件系统格式规范
 *
 * Postcondition：
 *   - 超级块、位图全局变量被正确初始化
 *   - 文件系统进入可操作状态，支持后续文件操作
 *   - 若任一阶段失败，进程终止（panic或断言）
 *
 * 副作用：
 *   - 初始化全局变量super和bitmap，改变系统状态
 *   - 建立超级块、位图的虚拟内存映射
 *   - 预留块和位图块被标记为已使用（验证过程）
 *
 * 项目初始化顺序说明：
 *   1. read_super — 加载超级块并验证魔数/容量
 *   2. check_write_block — 核心存储功能测试
 *   3. read_bitmap — 加载空闲位图结构并验证元数据一致性
 */
void fs_init(void) {
    read_super();
    check_write_block();
    read_bitmap();
}

/*
 * 概述：
 *   获取文件`f`的第`filebno`个数据块对应的磁盘块指针（ppdiskbno）。
 *   若文件还没有第`filebno`个数据块，
 *   可按需（`alloc`参数）分配间接块并建立映射。
 *
 *   注意：这**不会**修改文件的大小。
 *
 *   具体地，`*ppdiskbno`是一个`uint32_t *`的指针，它将
 *   指向存储有文件`f`的第`filebno`个数据块到磁盘块的映射的地址。
 *   更具体地，
 *
 *   对于直接指针范围内的文件数据块，
 *   `*ppdiskbno`指向`&f->f_direct[filebno]`
 *
 *   对于间接指针范围内的文件数据块，
 *   `*ppdiskbno`指向存储间接指针的数据块中的对应位置
 *
 *   以上地址都对应文件缓存中的地址。
 *
 * 一致性：
 *   若分配新块用于存储间接指针，则对空闲位图的修改被立即写回磁盘。
 *   其它修改（设置文件`f`的间接指针等）都只存在于缓存。
 *
 * Precondition：
 *   - 文件结构f必须已初始化且有效
 *   - filebno须小于NINDIRECT=1024
 *   - 间接块分配时依赖全局位图状态
 *
 * Postcondition：
 *   - 若成功，*ppdiskbno指向目标块号存储地址（直接或间接块条目），返回0
 *   - 若文件还没有第`filebno`个数据块，且alloc=0，返回-E_NOT_FOUND
 *   - 若为文件分配间接指针存储块时无空闲块，返回-E_NO_DISK
 *   - 若为文件分配间接指针存储块进行缓存时，分配内存失败，-E_NO_MEM
 *   - 若`filebno`大于NINDIRECT=1024，返回-E_INVAL
 *
 * 副作用：
 *   - 可能分配磁盘块并设置f->f_indirect
 *   - 可能调用read_block加载间接块到内存，修改缓存映射
 *   - 修改间接块内容（若为新分配块，内容未初始化）
 */
int file_block_walk(struct File *f, uint32_t filebno, uint32_t **ppdiskbno,
                    uint32_t alloc) {
    int r;
    uint32_t *ptr;
    uint32_t *blk;

    if (filebno < NDIRECT) {
        // Step 1: if the target block is corresponded to a direct pointer, just
        // return the disk block number.
        ptr = &f->f_direct[filebno];
    } else if (filebno < NINDIRECT) {
        // Step 2: if the target block is corresponded to the indirect block,
        // but there's no
        //  indirect block and `alloc` is set, create the indirect block.
        if (f->f_indirect == 0) {
            if (alloc == 0) {
                return -E_NOT_FOUND;
            }

            if ((r = alloc_block()) < 0) {
                return r;
            }
            f->f_indirect = r;
        }

        // Step 3: read the new indirect block to memory.
        if ((r = read_block(f->f_indirect, (void **)&blk, 0)) < 0) {
            return r;
        }
        ptr = blk + filebno;
    } else {
        return -E_INVAL;
    }

    // Step 4: store the result into *ppdiskbno, and return 0.
    *ppdiskbno = ptr;
    return 0;
}

/*
 * 概述：
 *   获取或分配文件`f`的第`filebno`个数据块对应的磁盘块号。
 *   支持按需分配磁盘块（`alloc`参数）。
 *
 *   注意：这**不会**修改文件的大小。
 *
 * 一致性：
 *   若分配新块用于存储间接指针，则对空闲位图的修改被立即写回磁盘。
 *   若分配新块用于存储文件数据，则对空闲位图的修改被立即写回磁盘。
 *   其它修改（设置文件`f`的第`filebno`个数据块指向分配的磁盘块等）都只存在于缓存。
 *
 * Precondition：
 *   - 文件结构`f`必须已有效初始化且符合磁盘格式
 *   - filebno必须小于NINDIRECT=1024
 *   - 当alloc=1时，需要足够的磁盘块和内存资源
 *
 * Postcondition：
 *   - 成功：*diskbno获得有效磁盘块号，返回0
 *   - 若文件还没有第`filebno`个数据块，且alloc=0，返回-E_NOT_FOUND
 *   - 若为文件分配间接指针存储块时无空闲块，返回-E_NO_DISK
 *   - 若为文件分配间接指针存储块进行缓存时，分配内存失败，-E_NO_MEM
 *   - 若`filebno`大于NINDIRECT=1024，返回-E_INVAL
 *   - 若为文件分配新的存储块时无空闲块，返回-E_NO_DISK
 *
 * 副作用：
 *   - 可能通过alloc_block分配新磁盘块，修改全局位图
 *   - 可能更新文件结构的直接/间接指针内容（存于内存缓存）
 *   - 间接块修改未立即同步磁盘
 *
 * 关键点：
 *   - 两阶段操作：先寻址后分配，保证指针有效性
 *   - *diskbno返回实际存储的值，可能为0（未初始化块）需上层处理
 */
int file_map_block(struct File *f, uint32_t filebno, uint32_t *diskbno,
                   uint32_t alloc) {
    int r;
    uint32_t *ptr;

    // Step 1: find the pointer for the target block.
    if ((r = file_block_walk(f, filebno, &ptr, alloc)) < 0) {
        return r;
    }

    // Step 2: if the block not exists, and create is set, alloc one.
    if (*ptr == 0) {
        if (alloc == 0) {
            return -E_NOT_FOUND;
        }

        if ((r = alloc_block()) < 0) {
            return r;
        }
        *ptr = r;
    }

    // Step 3: set the pointer to the block in *diskbno and return 0.
    *diskbno = *ptr;
    return 0;
}

/*
 * 概述：
 *   移除文件指定逻辑块与物理块的映射，释放磁盘空间。
 *
 *   若目标块未分配，但存储其直接/间接指针的空间已分配，静默成功。
 *   若存储其间接指针的空间未分配，返回-E_NOT_FOUND。
 *
 *   注意：这**不会**修改文件的大小。
 *
 * 一致性：
 *   对空闲位图、文件直接/间接指针的修改仅影响内存中的缓存
 *
 * Precondition：
 *   - 文件结构f必须已正确初始化且有效
 *   - filebno须为合法块号（<1024）
 *
 * Postcondition：
 *   - 成功：解除磁盘块引用并标记为空闲，返回0
 *   - 错误：file_block_walk可能返回越界或块不存在的错误，函数直接传递错误码
 *   - 文件f的f_direct或间接块中对应槽位被置零
 *
 * 副作用：
 *   - 修改文件结构中的块指针（内存缓存中）
 *   - 调用free_block修改全局位图，影响后续块分配
 *   - 间接块若完全空闲，仍保留占用状态（需显式回收）
 *
 * 关键点：
 *   - alloc=0确保不自动分配间接块，块不存在时file_block_walk返回-E_NOT_FOUND：
 *     函数逻辑会返回此错误而非静默成功，与说明存在矛盾需特别注意
 *   - 仅清除指针不回收间接块，可能导致间接块内存泄漏（需上层逻辑处理）
 *   - 文件大小未调整可能导致逻辑尾端外内容仍被访问（与块删除不一致）
 */
int file_clear_block(struct File *f, uint32_t filebno) {
    int r;
    uint32_t *ptr;

    if ((r = file_block_walk(f, filebno, &ptr, 0)) < 0) {
        return r;
    }

    if (*ptr) {
        free_block(*ptr);
        *ptr = 0;
    }

    return 0;
}

/*
 * 概述：
 *   获取文件`f`第`filebno`个数据块在缓存中的地址（确保数据加载至缓存）。
 *   若第`filebno`个数据块还不存在，将分配磁盘块。
 *
 * 一致性：
 *   若分配新块用于存储间接指针，则对空闲位图的修改被立即写回磁盘。
 *   若分配新块用于存储文件数据，则对空闲位图的修改被立即写回磁盘。
 *   其它修改（设置文件`f`的第`filebno`个数据块指向分配的磁盘块等）都只存在于缓存。
 *
 * Precondition：
 *   - 文件结构`f`必须已有效初始化且符合磁盘格式
 *   - 全局超级块和位图须已正确加载
 *   - filebno须在有效范围内
 *   - 需要可用磁盘块（若需分配）和物理内存资源
 *
 * Postcondition：
 *   - 若成功，*blk指向文件数据块缓存地址，返回0
 *   - 若为文件分配间接指针存储块时无空闲块，返回-E_NO_DISK
 *   - 若为文件分配间接指针存储块进行缓存时，分配内存失败，返回-E_NO_MEM
 *   - 若`filebno`大于NINDIRECT=1024，返回-E_INVAL
 *   - 若为文件分配新的存储块时无空闲块，返回-E_NO_DISK
 *   - 若读取磁盘块到缓存中时内存不足，返回-E_NO_MEM
 *
 * 副作用：
 *   - 可能通过file_map_block分配新磁盘块，修改全局位图
 *   - 可能更新文件结构的直接/间接指针（内存中）
 *   - 调用read_block分配物理页，修改页表映射
 *   - 可能触发IDE磁盘读取操作，改变设备状态
 */
int file_get_block(struct File *f, uint32_t filebno, void **blk) {
    int r;
    uint32_t diskbno;
    uint32_t isnew;

    // Step 1: find the disk block number is `f` using `file_map_block`.
    if ((r = file_map_block(f, filebno, &diskbno, 1)) < 0) {
        return r;
    }

    // Step 2: read the data in this disk to blk.
    if ((r = read_block(diskbno, blk, &isnew)) < 0) {
        return r;
    }
    return 0;
}

/*
 * 概述：
 *   标记文件`f`指定偏移地址所在数据块为脏状态。通过定位文件逻辑块对应的磁盘块，
 *   对其内存缓存页面设置脏页标志，确保后续同步机制能写回修改。
 *
 * Precondition：
 *   - 文件结构`f`必须已正确初始化且有效
 *   - 偏移地址offset**无需**对齐块边界，会自动转换为逻辑块号
 *   - offset需要对应文件已经分配的数据块
 *
 * Postcondition：
 *   - 成功：对应的磁盘块标记为脏，返回0
 *   - 若逻辑块号越界，返回-E_INVAL
 *   - 若对应逻辑块未分配，返回-E_NOT_FOUND
 *
 * 副作用：
 *   - 修改磁盘块缓存页面的页表项标志位（PTE_DIRTY）
 *   - 通过syscall_mem_map改变内存映射权限
 *
 * 关键点：
 *   - 使用offset/BLOCK_SIZE整除计算逻辑块号，隐含向下取整
 *   - alloc=0确保不自动分配新块，块不存在时直接传递错误而非静默处理
 *   - 仅设置脏标记，不触发立即写回，依赖后续同步操作持久化
 */
int file_dirty(struct File *f, uint32_t offset) {
    int r;
    uint32_t diskbno;

    if ((r = file_map_block(f, offset / BLOCK_SIZE, &diskbno, 0)) < 0) {
        return r;
    }

    return dirty_block(diskbno);
}

/*
 * 概述：
 *   在目录`dir`中查找指定文件名`name`对应的文件条目。
 *   遍历目录的数据块，逐块搜索有效文件项，**匹配成功后设置文件的父目录指针**。
 *
 *   注意：文件的父目录指针指向的是文件缓存中的地址，故其只在内存中有效。
 *
 * Precondition：
 *   - `dir`必须为有效目录结构，`f_type`需为FTYPE_DIR
 *   - `name`必须以空字符结尾
 *
 * Postcondition：
 *   - 若找到目标文件，*file指向其结构体，返回0
 *   - 若未找到目标文件，返回-E_NOT_FOUND
 *
 * 副作用：
 *   - 修改匹配文件项的`f_dir`字段，指向当前目录的虚拟地址
 *   - 可能加载目录数据块到内存缓存，建立页表映射
 *
 */
// Checked by DeepSeek-R1 20250509 1415
// Liberty save me!
int dir_lookup(struct File *dir, char *name, struct File **file) {
    // Step 1: Calculate the number of blocks in 'dir' via its size.
    uint32_t nblock;
    /* Exercise 5.8: Your code here. (1/3) */
    nblock = dir->f_size / BLOCK_SIZE;

    // Step 2: Iterate through all blocks in the directory.
    for (int i = 0; i < nblock; i++) {
        // Read the i'th block of 'dir' and get its address in 'blk' using
        // 'file_get_block'.
        void *blk;
        /* Exercise 5.8: Your code here. (2/3) */

        try(file_get_block(dir, i, &blk));

        struct File *files = (struct File *)blk;

        // Find the target among all 'File's in this block.
        for (struct File *f = files; f < files + FILE2BLK; ++f) {
            // Compare the file name against 'name' using 'strcmp'.
            // If we find the target file, set '*file' to it and set up its
            // 'f_dir' field.
            /* Exercise 5.8: Your code here. (3/3) */
            // 先检查文件是否有效（目录中被删除的文件文件名的第一个字节为'\0'）
            // 虽然，若不检查，只有当调用者传入的`name`为`\0`时才会引发错误：返回已被删除的文件，而不是-E_NOT_FOUND
            if (f->f_name[0] != '\0' && (strcmp(f->f_name, name) == 0)) {
                *file = f;
                // 副作用
                f->f_dir = dir;

                return 0;
            }
        }
    }

    return -E_NOT_FOUND;
}

/*
 * 概述：
 *   在指定目录下分配一个空闲的File结构。遍历目录数据块中的File条目，
 *   在现有块中查找空闲项，或在目录末尾新增数据块分配新条目。
 *
 * 一致性：
 *   若分配新块用于存储间接指针，则对空闲位图的修改被立即写回磁盘（同`file_get_block`）。
 *   若分配新块用于存储文件数据，则对空闲位图的修改被立即写回磁盘（同`file_get_block`）。
 *   其它修改（设置文件`f`的第`filebno`个数据块指向分配的磁盘块等）都只存在于缓存（同`file_get_block`）。
 *
 * Precondition：
 *   - `dir`必须是有效的目录结构（f_type为FTYPE_DIR）
 *   - 全局超级块super和位图bitmap必须已正确初始化
 *
 * Postcondition：
 *   - 成功：*file指向新分配的File结构，返回0
 *   - 当扩展目录时，目录的f_size会增加一个BLOCK_SIZE
 *   - 若为目录文件分配间接指针存储块时无空闲块，返回-E_NO_DISK
 *   - 若为目录文件分配间接指针存储块进行缓存时，分配内存失败，返回-E_NO_MEM
 *   - 若为文件分配新的存储块时无空闲块，返回-E_NO_DISK
 *   - 若读取磁盘块到缓存中时内存不足，返回-E_NO_MEM
 *
 * 副作用：
 *   - 可能调用file_get_block分配新磁盘块，修改空闲位图
 *   - 修改目录文件的f_size字段（内存中）
 *   - 新增数据块的分配会导致目录存储空间增长
 *
 * 关键点：
 *   - 双重循环结构：外层遍历目录数据块，内层遍历块中的FILE2BLK个File项
 *   - 空缺项判断标准：f_name[0] == '\0'（与文件删除标记方式一致）
 *   - 目录扩展时直接增加f_size并请求新块，依赖file_get_block处理块分配
 *   - 新分配块中的File结构默认未被初始化（需调用方填写文件名等信息）
 */
int dir_alloc_file(struct File *dir, struct File **file) {
    int r;
    uint32_t nblock, i, j;
    void *blk;
    struct File *f;

    nblock = dir->f_size / BLOCK_SIZE;

    for (i = 0; i < nblock; i++) {
        // read the block.
        if ((r = file_get_block(dir, i, &blk)) < 0) {
            return r;
        }

        f = blk;

        for (j = 0; j < FILE2BLK; j++) {
            if (f[j].f_name[0] == '\0') { // found free File structure.
                *file = &f[j];
                return 0;
            }
        }
    }

    // no free File structure in exists data block.
    // new data block need to be created.
    dir->f_size += BLOCK_SIZE;
    if ((r = file_get_block(dir, i, &blk)) < 0) {
        return r;
    }
    f = blk;
    *file = &f[0];

    return 0;
}

/*
 * 概述：
 *   跳过一个或多个`/`。
 *   返回指向第一个非`/`字符的指针。
 */
char *skip_slash(char *p) {
    while (*p == '/') {
        p++;
    }
    return p;
}

/*
 * 概述：
 *   解析给定路径名称，自文件系统根目录逐级向下查找目标文件/目录。
 *   支持部分路径存在时的父目录定位与最终路径成分捕获，为文件创建等操作提供前置条件。
 *
 *   实际上，对于目录名，本函数也能正确处理，此时`*pfile`对应目录文件。
 *
 * Precondition：
 *   - 全局超级块super必须已正确初始化
 *   - 路径中各文件/目录的名称长度不能超过MAXNAMELEN
 *   - `path`必须指向正确的、空字符结尾的路径字符串
 *   - `pfile`必须不为`NULL`
 *   - `pgdir`可为`NULL`
 *   - `lastelem`可为`NULL`
 *
 * Postcondition：
 *   - 成功（路径存在）：
 *     *pdir设为文件所在目录，*pfile指向文件结构体，返回0
 *   - 路径部分存在：
 *     当末级成分不存在时，*pdir设为最后存在的目录，lastelem填充末级名称，返回-E_NOT_FOUND
 *   - 若路径过长，返回-E_BAD_PATH
 *   - 若中间目录不存在，返回-E_NOT_FOUND
 *
 * 副作用：
 *   - 可能调用dir_lookup设置末级文件的f_dir指针（内存缓存的File结构）
 *   - 可能通过dir_lookup触发目录块加载，建立页表映射
 *   - 修改lastelem缓冲区内容（当需要时）
 *
 * 关键点：
 *   - skip_slash处理连续'/'提高路径兼容性（如"/usr//bin"）
 *   - 使用memcpy拷贝路径成分，确保截断并添加'\0'防止溢出
 *   - 每级路径必须为目录，类型检查防止文件被错误当作目录访问
 *   - 错误区分：路径中途无效返回即时错误，末级无效则传递lastelem用于后续创建
 */
int walk_path(char *path, struct File **pdir, struct File **pfile,
              char *lastelem) {
    char *p;
    char name[MAXNAMELEN];
    struct File *dir, *file;
    int r;

    // start at the root.
    path = skip_slash(path);
    file = &super->s_root;
    dir = 0;
    name[0] = 0;

    if (pdir) {
        *pdir = 0;
    }

    *pfile = 0;

    // find the target file by name recursively.
    while (*path != '\0') {
        // 当前目录
        dir = file;
        p = path;

        // 获取下一层级的目录/文件名
        while (*path != '/' && *path != '\0') {
            path++;
        }

        if (path - p >= MAXNAMELEN) {
            return -E_BAD_PATH;
        }

        // 下一层级的名称，可能是文件/目录
        memcpy(name, p, path - p);
        name[path - p] = '\0';

        path = skip_slash(path);

        // 检查当前“目录”类型确实为目录
        // 例如，`/usr/bin/for_super_earth`
        // 若`/usr/bin`实际上是文件，而不是目录
        // 直接返回`-E_NOT_FOUND`
        if (dir->f_type != FTYPE_DIR) {
            return -E_NOT_FOUND;
        }

        // 在当前目录中查找下一层级
        if ((r = dir_lookup(dir, name, &file)) < 0) {

            // 未找到basename，但其之前的目录结构都存在
            // 例如，`/usr/bin/for_super_earth`，存在`/usr/bin`目录，
            // 但该目录下不存在`for_super_earth`文件
            if (r == -E_NOT_FOUND && *path == '\0') {
                // 设置文件应当存在的目录
                if (pdir) {
                    *pdir = dir;
                }

                if (lastelem) {
                    // 设置lastelem为未找到的basename
                    strcpy(lastelem, name);
                }

                // 设置*pfile = NULL
                *pfile = 0;
            }

            // 若在路径当中文件就不存在，或发生其它错误，直接返回
            // 例如，`/usr/bin/for_super_earth`，若`/usr/bin`目录即不存在
            // 直接返回`-E_NOT_FOUND`
            return r;
        }
    }

    if (pdir) {
        *pdir = dir;
    }

    *pfile = file;
    return 0;
}

/*
 * 概述：
 *   打开指定路径的文件。通过路径解析定位目标文件，
 *   仅用于已存在文件的访问，不处理文件创建逻辑。
 *
 * Precondition：
 *   - 全局超级块super必须已正确初始化
 *   - 路径各层级目录必须存在且类型正确
 *   - 路径中各文件/目录的名称长度不能超过MAXNAMELEN
 *   - `path`必须指向正确的、空字符结尾的路径字符串
 *   - `file`必须不为`NULL`
 *
 * Postcondition：
 *   - 成功：*file指向目标文件结构体，返回0
 *   - 若路径过长，返回-E_BAD_PATH
 *   - 若路径不存在，返回-E_NOT_FOUND
 *
 * 副作用：
 *   - 可能通过walk_path触发目录块加载，建立页表映射
 *   - 修改目标文件的f_dir指针（内存中）
 *
 * 关键点：
 *   - 通过walk_path的pdir=0配置，仅关注最终文件是否存在
 *   - 不修改文件系统结构，纯查询操作
 */
int file_open(char *path, struct File **file) {
    return walk_path(path, 0, file, 0);
}

/*
 * 概述：
 *   创建指定路径的新文件。通过路径解析定位父目录，
 *   在确保文件不存在后分配新文件条目并初始化基础属性。
 *
 * 一致性：
 *   若分配新块用于存储目录的间接指针，则对空闲位图的修改被立即写回磁盘（同`file_get_block`）。
 *   若分配新块用于存储目录文件的数据，则对空闲位图的修改被立即写回磁盘（同`file_get_block`）。
 *   其它修改（设置目录下新创建的文件的文件名等）都只存在于缓存（同`file_get_block`）。
 *
 * Precondition：
 *   - 全局超级块super必须已正确初始化
 *   - 路径的父目录必须存在且为有效目录类型
 *   - 父目录需有空闲File条目或可扩展空间
 *
 * Postcondition：
 *   - 成功：*file指向新创建的文件结构体，返回0
 *   - 若路径过长，返回-E_BAD_PATH
 *   - 若中间目录不存在，返回-E_NOT_FOUND
 *   - 若文件已存在，返回-E_FILE_EXISTS
 *   - 若为目录文件分配间接指针存储块时无空闲块，返回-E_NO_DISK
 *   - 若为目录文件分配间接指针存储块进行缓存时，分配内存失败，返回-E_NO_MEM
 *   - 若为文件分配新的存储块时无空闲块，返回-E_NO_DISK
 *   - 若读取磁盘块到缓存中时内存不足，返回-E_NO_MEM
 *
 * 副作用：
 *   - 修改父目录内容：可能扩展目录大小，分配新数据块
 *   - 设置新文件的f_name和f_dir字段（内存中）
 *   - 可能通过dir_alloc_file修改全局位图状态
 *
 * 关键点：
 *   - 先验存在性检查：walk_path返回0时立即报错
 *   - 错误码-E_NOT_FOUND与dir==0的组合判断确保父目录有效
 *   - strcpy隐式依赖walk_path对name长度的前置校验
 *
 * 潜在问题：
 *   - 对于创建的文件结构体，仅显式地初始化了其名称，而其它属性都没有显式设置。
 *     这假定磁盘镜像中对应区域在初始化时已经0填充。
 *     注意，`dir_alloc_file`还可能复用目录中已经删除的文件对应的文件项，
 *     这要求删除文件时将文件大小设置为0
 *
 *     ！！问题！！：目录能否删除？删除时是否将`f_type`设置为了0？
 */
int file_create(char *path, struct File **file) {
    char name[MAXNAMELEN];
    int r;
    struct File *dir, *f;

    // 当文件不存在时，这不会设置`f`的`f_dir`指针！
    if ((r = walk_path(path, &dir, &f, name)) == 0) {
        return -E_FILE_EXISTS;
    }

    if (r != -E_NOT_FOUND || dir == 0) {
        return r;
    }

    // 这不会设置`f`的`f_dir`指针！
    if (dir_alloc_file(dir, &f) < 0) {
        return r;
    }

    // 未显式初始化文件结构体的其它部分！
    // 这假定磁盘镜像中对应的区域在初始化时已经0填充
    // `f_size` = 0
    // `f_type` = 0 = `FTYPE_REG`
    // `f_dir` = 0 = `NULL`
    strcpy(f->f_name, name);
    *file = f;
    return 0;
}

/*
 * 概述：
 *   截断文件至指定大小。释放超出新尺寸的磁盘块，
 *   并根据需要回收存储间接指针的磁盘块。
 *
 * 一致性：
 *   对空闲位图、文件直接/间接指针、文件大小的修改仅影响内存中的缓存
 *
 * Precondition：
 *   - 文件结构`f`必须已正确初始化且有效
 *   - 全局超级块super和位图bitmap必须已加载
 *   - newsize必须为非负整数，且不超过文件原大小
 *
 * Postcondition：
 *   - 文件大小更新为newsize，多余块被释放
 *   - 若新块数<=NDIRECT，间接块（若存在）被释放且指针置零
 *   - 所有块释放操作成功完成，否则触发panic
 *
 * 副作用：
 *   - 修改文件结构中的f_size和f_indirect字段
 *   - 调用file_clear_block和free_block修改全局位图状态
 *   - 若清除磁盘块时出现错误，将panic
 *
 * 关键点：
 *   - ROUND宏处理块对齐：newsize/BLOCK_SIZE向上取整计算所需块数
 *   - newsize=0的特殊处理：强制new_nblocks=0清空所有块
 *   - 间接块释放条件：仅当新块数<=NDIRECT时回收，避免悬空指针
 */
void file_truncate(struct File *f, uint32_t newsize) {
    uint32_t bno, old_nblocks, new_nblocks;

    old_nblocks = ROUND(f->f_size, BLOCK_SIZE) / BLOCK_SIZE;
    new_nblocks = ROUND(newsize, BLOCK_SIZE) / BLOCK_SIZE;

    if (newsize == 0) {
        new_nblocks = 0;
    }

    if (new_nblocks <= NDIRECT) {
        for (bno = new_nblocks; bno < old_nblocks; bno++) {
            panic_on(file_clear_block(f, bno));
        }
        if (f->f_indirect) {
            free_block(f->f_indirect);
            f->f_indirect = 0;
        }
    } else {
        for (bno = new_nblocks; bno < old_nblocks; bno++) {
            panic_on(file_clear_block(f, bno));
        }
    }
    f->f_size = newsize;
}

/*
 * 概述：
 *   设置文件逻辑大小为指定值。
 *   若新尺寸小于原尺寸，则截断超出部分并释放磁盘块。
 *   若新尺寸大于原尺寸，只增加文件大小，不分配存储块，当使用`file_get_block`获取内容时再分配。
 *
 * 一致性：
 *   若文件已设置`f_dir`指针，则
 *   将文件所在目录的数据写入磁盘，使得对文件结构体的修改被写入磁盘。
 *
 * Precondition：
 *   - 文件结构`f`必须已正确初始化且有效
 *   - newsize必须为非负整数
 *   - 依赖全局超级块和位图状态（通过file_truncate间接使用）
 *   - 文件的`f_dir`指针可为`NULL`，此时将不同步对文件元数据的修改到磁盘。
 *   - 若`f_dir`不为`NULL`，则必须指向有效的目录文件缓存。
 *
 * Postcondition：
 *   - 成功：文件f_size更新为newsize，超额块被释放（当newsize较小）
 *   - 若存在父目录，其元数据被刷新至磁盘
 *   - 函数始终返回0（假定子函数错误通过panic处理）
 *
 * 副作用：
 *   - 修改文件f_size字段及块映射关系（内存中）
 *   - 可能调用file_truncate释放磁盘块，修改全局位图
 *   - 若文件有父目录，触发目录元数据刷新（写回磁盘）
 *   - 若file_truncate、file_flush内部操作失败，触发panic
 *
 * 关键点：
 *   - 仅当newsize < 原大小时执行截断，增大尺寸需由其他逻辑处理
 *   - 父目录刷新确保目录条目及时更新，但可能引入额外I/O开销
 *   - 函数无条件返回0，错误处理依赖子函数panic机制
 */
int file_set_size(struct File *f, uint32_t newsize) {
    if (f->f_size > newsize) {
        file_truncate(f, newsize);
    }

    f->f_size = newsize;

    if (f->f_dir) {
        file_flush(f->f_dir);
    }

    return 0;
}

/*
 * 概述：
 *   将文件的所有脏块刷新至磁盘。遍历文件使用的所有块，
 *   将标记为脏的块通过写回磁盘，完成持久化存储。
 *
 * 一致性：
 *   文件的所有内容被写入磁盘。但不同步文件的元数据（File结构体）
 *
 * Precondition：
 *   - 文件结构`f`必须已正确初始化且有效
 *
 * Postcondition：
 *   - 所有脏块数据写回磁盘，相应块脏位被清除 ！！问题！！：脏位是否被清除？
 *   - 未映射或非脏块被安全跳过，不影响系统状态
 *   - 若file_map_block返回错误（如块未分配），静默跳过该块
 *
 * 副作用：
 *   - 修改磁盘物理存储内容，触发IDE设备操作
 *   - 清除脏块标记，更新内存缓存状态 ！！问题！！：脏位是否被清除？
 *   - 可能产生大量磁盘I/O操作，影响系统性能
 *
 * 关键点：
 *   - alloc=0确保不自动分配新块，仅处理已分配块
 *   - block_is_dirty依赖页表项检查，需确保块已映射
 *   - 使用ROUND计算块数（向上取整），包含可能部分使用的最后一字节块
 */
void file_flush(struct File *f) {
    uint32_t nblocks;
    uint32_t bno;
    uint32_t diskbno;
    int r;

    // ROUND -> ROUND UP
    // ROUNDDOWN -> ROUND DOWN
    nblocks = ROUND(f->f_size, BLOCK_SIZE) / BLOCK_SIZE;

    for (bno = 0; bno < nblocks; bno++) {
        if ((r = file_map_block(f, bno, &diskbno, 0)) < 0) {
            continue;
        }
        if (block_is_dirty(diskbno)) {
            write_block(diskbno);
        }
    }
}

/*
 * 概述：
 *   同步整个文件系统的脏块至磁盘。强制刷新所有标记为脏的块，
 *   包括元数据及用户数据，确保磁盘与内存缓存完全一致。
 *
 * 一致性：
 *   对文件系统的所有修改被写入磁盘。
 *
 * Precondition：
 *   - 全局超级块super必须已正确初始化且有效
 *
 * Postcondition：
 *   - 所有脏块数据物理写入磁盘，系统进入一致性状态
 *   - 清除所有脏块标记，使内存缓存与磁盘完全同步
 *
 * 副作用：
 *   - 对每个磁盘块进行I/O操作，导致高延迟和资源消耗
 *   - 修改全部脏块物理存储内容
 *
 * 关键点：
 *   - 全量遍历所有块号，暴力同步方式确保数据安全
 */
void fs_sync(void) {
    int i;
    for (i = 0; i < super->s_nblocks; i++) {
        if (block_is_dirty(i)) {
            write_block(i);
        }
    }
}

/*
 * 概述：
 *   关闭文件并同步相关数据。强制刷新文件内容至磁盘，
 *   并确保父目录中文件的元数据更新持久化。
 *
 * 一致性：
 *   若文件已设置`f_dir`指针，则
 *   将文件所在目录的数据写入磁盘，使得对文件结构体的修改被写入磁盘。
 *
 * Precondition：
 *   - 文件结构`f`必须已正确初始化且有效
 *   - 文件的`f_dir`指针可为`NULL`，此时将不同步对文件元数据的修改到磁盘。
 *   - 若`f_dir`不为`NULL`，则必须指向有效的目录文件缓存。
 *   - 依赖全局超级块和位图状态（通过file_flush间接使用）
 *
 * Postcondition：
 *   - 文件数据及元数据变更同步至磁盘
 *   - 父目录中该文件条目所在块被标记为脏并刷新
 *
 * 副作用：
 *   - 调用file_flush触发文件数据写回，修改磁盘内容
 *   - 修改父目录元数据块脏状态，可能触发IDE写入
 *   - 可能因file_map_block/read_block失败输出调试信息
 *
 * 关键点：
 *   - 双重刷新机制：先文件后目录，确保元数据一致性
 *   - 通过地址范围比对定位文件在目录块中的位置（files <= f < files+FILE2BLK）
 *   - 目录块遍历采用提前终止（找到即break），优化性能
 *   - 错误处理仅记录日志，不中断流程
 */
void file_close(struct File *f) {
    // 同步文件内容到磁盘中
    file_flush(f);
    if (f->f_dir) {
        uint32_t nblock = f->f_dir->f_size / BLOCK_SIZE;
        for (int i = 0; i < nblock; i++) {
            uint32_t diskbno;
            struct File *files;
            if (file_map_block(f->f_dir, i, &diskbno, 0) < 0) {
                debugf("file_close: file_map_block failed\n");
                break;
            }
            if (read_block(diskbno, (void **)&files, 0) < 0) {
                debugf("file_close: read_block failed\n");
                break;
            }

            // 若该磁盘块中存储了`f`的元数据，将其标记为脏块
            if (files <= f && f < files + FILE2BLK) {
                dirty_block(diskbno);
                break;
            }
        }

        // 同步文件所在目录的内容到磁盘中：将对文件元数据的修改同步到磁盘中
        file_flush(f->f_dir);
    }
}

/*
 * 概述：
 *   删除指定路径文件。
 *   截断文件、清空文件名并同步元数据。
 *
 * 一致性：
 *   若文件已设置`f_dir`指针，则
 *   将文件所在目录的数据写入磁盘，使得对文件元数据的修改被写入磁盘。
 *
 * Precondition：
 *   - 路径必须指向已存在的文件（通过walk_path验证）
 *   - 文件的`f_dir`指针可为`NULL`，此时将不同步对文件元数据的修改到磁盘。
 *   - 若`f_dir`不为`NULL`，则必须指向有效的目录文件缓存。
 *
 * Postcondition：
 *   - 文件大小截断为0，存储块被释放
 *   - 文件名首字节置零标记删除
 *   - 文件及父目录元数据同步至磁盘
 *   - 文件仍存在于目录结构中，但表现为无效状态
 *   - 若成功，返回0
 *   - 若路径不存在，返回-E_NOT_FOUND
 *   - 若路径过长，返回-E_BAD_PATH
 *
 * 副作用：
 *   - 修改文件f_size和f_name字段（内存中）
 *   - 调用file_truncate释放磁盘块，修改全局位图
 *   - 触发两次file_flush（文件、所在目录）产生磁盘I/O操作
 *
 * 关键点：
 *   - 软删除机制：依赖后续目录遍历跳过f_name[0]=='\0'的条目
 *   - 不回收目录条目空间，依赖后续文件创建重用
 */
int file_remove(char *path) {
    int r;
    struct File *f;

    // Step 1: find the file on the disk.
    if ((r = walk_path(path, 0, &f, 0)) < 0) {
        return r;
    }

    // Step 2: truncate it's size to zero.
    file_truncate(f, 0);

    // Step 3: clear it's name.
    f->f_name[0] = '\0';

    // Step 4: flush the file.
    // ！！问题！！：此时文件大小已经为0，操作没有作用。
    file_flush(f);
    if (f->f_dir) {
        file_flush(f->f_dir);
    }

    return 0;
}
