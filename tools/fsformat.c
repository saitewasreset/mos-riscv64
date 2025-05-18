#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#include "../user/include/fs.h"

/* Static assert, for compile-time assertion checking */
#define static_assert(c) (void)(char(*)[(c) ? 1 : -1])0

#define nelem(x) (sizeof(x) / sizeof((x)[0]))
typedef struct Super Super;
typedef struct File File;

#define NBLOCK 1024 // The number of blocks in the disk.
uint32_t nbitblock; // the number of bitmap blocks.
uint32_t nextbno;   // next availiable block.

struct Super super; // super block.

enum {
  BLOCK_FREE = 0,
  BLOCK_BOOT = 1,
  BLOCK_BMAP = 2,
  BLOCK_SUPER = 3,
  // 用于存储普通文件的内容
  BLOCK_DATA = 4,
  // 用于存储目录文件的内容（目录中的文件对应的File结构体）
  BLOCK_FILE = 5,
  BLOCK_INDEX = 6,
};

// 在内存中表示硬盘镜像
// 注意：磁盘块的类型只在内存中的struct Block结构中记录
// 实际上的磁盘镜像中并没有该内容
struct Block {
  uint8_t data[BLOCK_SIZE];
  uint32_t type;
} disk[NBLOCK];

// reverse: mutually transform between little endian and big endian.
void reverse(uint32_t *p) {
  uint8_t *x = (uint8_t *)p;
  uint32_t y = *(uint32_t *)x;
  x[3] = y & 0xFF;
  x[2] = (y >> 8) & 0xFF;
  x[1] = (y >> 16) & 0xFF;
  x[0] = (y >> 24) & 0xFF;
}

// reverse_block: reverse proper filed in a block.
void reverse_block(struct Block *b) {
  int i, j;
  struct Super *s;
  struct File *f, *ff;
  uint32_t *u;

  switch (b->type) {
  case BLOCK_FREE:
  case BLOCK_BOOT:
    break; // do nothing.
  case BLOCK_SUPER:
    s = (struct Super *)b->data;
    reverse(&s->s_magic);
    reverse(&s->s_nblocks);

    ff = &s->s_root;
    reverse(&ff->f_size);
    reverse(&ff->f_type);
    for (i = 0; i < NDIRECT; ++i) {
      reverse(&ff->f_direct[i]);
    }
    reverse(&ff->f_indirect);
    break;
  case BLOCK_FILE:
    f = (struct File *)b->data;
    for (i = 0; i < FILE2BLK; ++i) {
      ff = f + i;
      if (ff->f_name[0] == 0) {
        break;
      } else {
        reverse(&ff->f_size);
        reverse(&ff->f_type);
        for (j = 0; j < NDIRECT; ++j) {
          reverse(&ff->f_direct[j]);
        }
        reverse(&ff->f_indirect);
      }
    }
    break;
  case BLOCK_INDEX:
  case BLOCK_BMAP:
    u = (uint32_t *)b->data;
    for (i = 0; i < BLOCK_SIZE / 4; ++i) {
      reverse(u + i);
    }
    break;
  }
}

// 初始化磁盘镜像：
// 建立分区表、超级块、空闲位图
void init_disk() {
  int i, diff;

  // Step 1: Mark boot sector block.
  disk[0].type = BLOCK_BOOT;

  // Step 2: Initialize boundary.
  nbitblock = (NBLOCK + BLOCK_SIZE_BIT - 1) / BLOCK_SIZE_BIT;
  nextbno = 2 + nbitblock;

  // Step 2: Initialize bitmap blocks.
  for (i = 0; i < nbitblock; ++i) {
    disk[2 + i].type = BLOCK_BMAP;
  }
  for (i = 0; i < nbitblock; ++i) {
    memset(disk[2 + i].data, 0xff, BLOCK_SIZE);
  }

  // 若磁盘Block的数量并非BLOCK_SIZE_BIT的倍数，在最后一个位图块中将存在不对应任何磁盘Block的“多余部分”
  // 多余部分需标记为占用（填充0）
  if (NBLOCK != nbitblock * BLOCK_SIZE_BIT) {
    diff = NBLOCK % BLOCK_SIZE_BIT / 8;
    memset(disk[2 + (nbitblock - 1)].data + diff, 0x00, BLOCK_SIZE - diff);
  }

  // Step 3: Initialize super block.
  disk[1].type = BLOCK_SUPER;
  super.s_magic = FS_MAGIC;
  super.s_nblocks = NBLOCK;
  super.s_root.f_type = FTYPE_DIR;
  strcpy(super.s_root.f_name, "/");
}

// 获取下一个可用的磁盘块ID，并将该块的类型设置为`type`
// 注意：磁盘块的类型只在内存中的struct Block结构中记录
// 实际上的磁盘镜像中并没有该内容
int next_block(int type) {
  disk[nextbno].type = type;
  return nextbno++;
}

// 将磁盘块占用情况更新到占用位图中
// 实际上，我们认为当前`nextbno`前的所有块都被占用
void flush_bitmap() {
  int i;
  // update bitmap, mark all bit where corresponding block is used.
  for (i = 0; i < nextbno; ++i) {
    ((uint32_t *)disk[2 + i / BLOCK_SIZE_BIT]
         .data)[(i % BLOCK_SIZE_BIT) / 32] &= ~(1 << (i % 32));
  }
}

// 将内存中的硬盘镜像表示写入镜像文件
void finish_fs(char *name) {
  int fd, i;

  // Prepare super block.
  memcpy(disk[1].data, &super, sizeof(super));

  // Dump data in `disk` to target image file.
  fd = open(name, O_RDWR | O_CREAT, 0666);
  for (i = 0; i < 1024; ++i) {
#ifdef CONFIG_REVERSE_ENDIAN
    reverse_block(disk + i);
#endif
    // 注意，只写入struct Block的data部分，type部分仅在内存中存在
    ssize_t n = write(fd, disk[i].data, BLOCK_SIZE);
    assert(n == BLOCK_SIZE);
  }

  // Finish.
  close(fd);
}

/*
 * 概述：
 *  设置文件`f`的第`nblk`个存储块为指向磁盘的第`bno`个块。
 *  若需要，会自动分配存储间接指针的存储块。
 *
 * Precondition：
 *   - 文件结构体f必须已初始化且有效
 *   - nblk必须小于NINDIRECT（由断言保证）
 *
 * Postcondition：
 *   - 块号bno被记录到文件f的第nblk位置
 *   - 若需要间接块且未分配，将自动创建并更新f_indirect
 *
 * 副作用：
 *   - 可能修改文件结构体的f_direct或f_indirect字段
 *   - 可能通过next_block分配新块，改变全局块分配状态
 *   - 修改间接块磁盘数据内容
 *
 * 关键点：
 *   - 间接块数据区按uint32_t数组解析，每个元素对应逻辑块号
 *   - 间接块前NDIRECT个条目保留未使用（与文件结构设计相关）
 */
void save_block_link(struct File *f, int nblk, int bno) {
  assert(nblk < NINDIRECT); // if not, file is too large !

  if (nblk < NDIRECT) {
    f->f_direct[nblk] = bno;
  } else {
    if (f->f_indirect == 0) {
      // create new indirect block.
      f->f_indirect = next_block(BLOCK_INDEX);
    }
    ((uint32_t *)(disk[f->f_indirect].data))[nblk] = bno;
  }
}

/*
 * 概述：
 *   为**目录文件**创建新的链接块。
 *   分配新块并将目录文件的第`nblk`个存储块与分配的块的关联。
 *
 *   本函数只应用于分配块拓展文件的大小，而不是替换目录文件中已分配的块。
 *
 *   虽然本函数理论上也可以用于拓展普通文件的大小，但函数将分配的块的类型
 *   设置为`BLOCK_FILE`，表示用于存储目录文件，而不是普通文件`BLOCK_DATA`。
 *   再虽然，块的类型只在内存中存在，写入磁盘镜像时将被忽略，故实际上确实也可以
 *   用于拓展普通文件的大小（）。
 *
 * Precondition：
 *   - dirf必须为有效的文件（FTYPE_DIR）
 *   - nblk必须等于当前文件块数（由调用逻辑保证）
 *
 * Postcondition：
 *   - 新块号被链接到文件的第nblk位置
 *   - 文件大小增加BLOCK_SIZE（4096字节）
 *   - 返回新分配块号
 *
 * 副作用：
 *   - 修改文件的块链接和f_size字段
 *   - 通过next_block分配新块，改变全局块分配状态
 *
 * 关键点：
 *   - 新块类型标记为BLOCK_FILE，用于存储文件结构体
 */
int make_link_block(struct File *dirf, int nblk) {
  int bno = next_block(BLOCK_FILE);
  save_block_link(dirf, nblk, bno);
  dirf->f_size += BLOCK_SIZE;
  return bno;
}

/*
 * 概述：
 *   在目录中创建新文件条目。优先复用无效的File结构体，
 *   必要时分配新块扩展目录文件空间。
 *
 * Precondition：
 *   - dirf必须为有效目录文件且已初始化
 *
 * Postcondition：
 *   - 返回指向空闲File结构体的指针，该结构体已加入目录
 *   - 目录文件可能新增存储块（当现有块无空闲时）
 *
 * 副作用：
 *   - 可能通过make_link_block分配新磁盘块
 *   - 修改目录文件块内容（标记已使用File条目）
 *
 * 关键点：
 *   - 通过f_name[0]=='\0'判断空闲条目，实现删除标记
 *   - 新分配块返回首个File结构体，后续条目初始为全零（即空闲状态）
 *
 * 潜在问题：
 *   - 若分配新的存储块用于存储File结构体，
 *     则目录文件大小将直接增加BLOCK_SIZE=4096字节
 *     而一个File结构体只占用FILE_STRUCT_SIZE=256字节
 *     多余的空间应当作为无效文件，
 *     而实现中无效文件的判断方法是`f->f_name[0] == '\0'`
 *     这要求新的存储块应当默认用零填充，但在分配函数中未手动填零
 *     这实际上依赖于`disk`是静态初始化为零的全局变量
 *     若之后改为动态分配`disk`，可能导致问题
 */
// Checked by DeepSeek-R1 20250508 1753
struct File *create_file(struct File *dirf) {
  int nblk = dirf->f_size / BLOCK_SIZE;

  // Step 1: Iterate through all existing blocks in the directory.
  // 先查找已分配给目录的空间，看其中存储的文件信息有没有无效的
  // 注意，无效文件可能有两种情况：
  // 1. 该文件曾经存在，但已经被删除
  // 2. 给目录“文件”分配了磁盘块，但该磁盘块中的该位置还从未存储过文件结构体
  for (int i = 0; i < nblk; ++i) {
    int bno; // the block number
    // If the block number is in the range of direct pointers (NDIRECT), get
    // the 'bno' directly from 'f_direct'. Otherwise, access the indirect
    // block on 'disk' and get the 'bno' at the index.
    /* Exercise 5.5: Your code here. (1/3) */

    if (i < NDIRECT) {
      // 访问目录“文件”的直接指针部分，直接指针包含了存储目录“文件”内容的磁盘块编号
      bno = dirf->f_direct[i];
    } else {

      // 访问目录“文件”的间接指针部分，间接指针包含一个磁盘块编号，
      // 该磁盘块中包含了存储目录“文件”内容的磁盘块编号（一个编号4字节，一个磁盘块可存储1024个编号，但是前NDIRECT=10个不使用）
      // 从间接指针对应的磁盘块中取得存储目录“文件”内容的磁盘块编号
      // First NDIRECT uint32_t in the indirect are unused
      bno = ((uint32_t *)disk[dirf->f_indirect].data)[i];
    }

    // Get the directory block using the block number.
    // 一个存储目录“文件”内容的磁盘块中，
    // 可存储FILE2BLK = BLOCK_SIZE / FILE_STRUCT_SIZE
    // = 4096 / 256 = 16个File结构体
    struct File *blk = (struct File *)(disk[bno].data);

    // Iterate through all 'File's in the directory block.
    // 访问该磁盘块中存储的所有目录结构体
    for (struct File *f = blk; f < blk + FILE2BLK; ++f) {
      // If the first byte of the file name is null, the 'File' is unused.
      // Return a pointer to the unused 'File'.
      /* Exercise 5.5: Your code here. (2/3) */

      // 若存在无效的文件，直接复用该位置放置新的文件
      if (f->f_name[0] == '\0') {
        return f;
      }
    }
  }

  // Step 2: If no unused file is found, allocate a new block using
  // 'make_link_block' function and return a pointer to the new block on
  // 'disk'.
  /* Exercise 5.5: Your code here. (3/3) */
  // 若在该目录的已有块中都找不到无效的文件，分配新的块放置该文件的File结构体
  int bno = make_link_block(dirf, nblk);

  return (struct File *)(disk[bno].data);
}

/*
 * 概述：
 *   将宿主机文件写入磁盘镜像的指定目录。创建新文件条目后，
 *   读取宿主机文件内容并按块写入磁盘，建立文件块链接。
 *
 * Precondition：
 *   - dirf必须为有效目录文件（FTYPE_DIR）
 *   - 路径path必须存在且可读
 *
 * Postcondition：
 *   - 在目录dirf下创建新文件条目，包含完整元数据
 *   - 新文件条目的名称为`path`的basename部分
 *   - 宿主机文件内容被分割为BLOCK_SIZE块写入磁盘
 *   - 文件控制块的f_direct/f_indirect建立正确块链接
 *
 * 副作用：
 *   - 修改目录文件内容，可能触发新块分配（通过create_file）
 *   - 通过next_block分配数据块，改变全局块分配状态
 *   - 写入disk数组模拟磁盘数据变更
 *
 * 潜在问题：
 *   - 文件名提取方式依赖路径分隔符'/'，需确保路径格式正确
 *   - 文件大小通过lseek获取，未处理大文件超过MAXFILESIZE的情况
 *   - 直接使用nextbno标识当前块，隐含块连续分配的假设
 *   - 创建新文件时依赖disk数组初始零值标记空闲条目
 */
void write_file(struct File *dirf, const char *path) {
  int iblk = 0, r = 0, n = sizeof(disk[0].data);
  struct File *target = create_file(dirf);

  /* in case `create_file` is't filled */
  // 注意：在目前的`create_file`实现中，其不可能返回NULL
  // 以下判断没有作用
  if (target == NULL) {
    return;
  }

  int fd = open(path, O_RDONLY);

  // Get file name with no path prefix.
  const char *fname = strrchr(path, '/');
  if (fname) {
    fname++;
  } else {
    fname = path;
  }
  strcpy(target->f_name, fname);

  target->f_size = lseek(fd, 0, SEEK_END);
  target->f_type = FTYPE_REG;

  // Start reading file.
  lseek(fd, 0, SEEK_SET);
  while ((r = read(fd, disk[nextbno].data, n)) > 0) {
    save_block_link(target, iblk++, next_block(BLOCK_DATA));
  }
  close(fd); // Close file descriptor.
}

/*
 * 概述：
 *   递归写入宿主机目录到磁盘镜像。在指定目录下创建子目录条目，
 *   并遍历处理所有子项（文件/目录），构建完整目录树结构。
 *
 *   具体地，将在`dirf`目录下创建新目录，其名称为`path`的basename
 *   并递归地将`path`下的所有文件及目录写入磁盘镜像中创建的新目录中
 *
 * Precondition：
 *   - dirf必须为有效目录文件（FTYPE_DIR）
 *   - 路径path必须存在且可读（通过opendir验证）
 *
 * Postcondition：
 *   - 在dirf下创建新目录条目，名称为path的basename
 *   - 递归处理所有子文件/目录，构建镜像中的完整目录结构
 *   - 新目录的f_type标记为FTYPE_DIR
 *
 * 副作用：
 *   - 通过create_file修改目录文件内容，可能触发块分配
 *   - 递归调用可能大量分配磁盘块，改变全局块分配状态
 *   - 文件名超长时直接exit(1)终止进程
 *
 * 关键点：
 *   - 使用basename提取目录名，需确保路径不含尾部'/'
 *   - 跳过"."和".."目录项，避免递归循环
 *   - 动态构建子路径时使用malloc+拼接，需注意内存释放
 */
void write_directory(struct File *dirf, char *path) {
  DIR *dir = opendir(path);
  if (dir == NULL) {
    perror("opendir");
    return;
  }
  struct File *pdir = create_file(dirf);
  strncpy(pdir->f_name, basename(path), MAXNAMELEN - 1);
  if (pdir->f_name[MAXNAMELEN - 1] != 0) {
    fprintf(stderr, "file name is too long: %s\n", path);
    // File already created, no way back from here.
    exit(1);
  }
  pdir->f_type = FTYPE_DIR;
  for (struct dirent *e; (e = readdir(dir)) != NULL;) {
    if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
      char *buf = malloc(strlen(path) + strlen(e->d_name) + 2);
      sprintf(buf, "%s/%s", path, e->d_name);
      if (e->d_type == DT_DIR) {
        write_directory(pdir, buf);
      } else {
        write_file(pdir, buf);
      }
      free(buf);
    }
  }
  closedir(dir);
}

/*
fsformat - 构建文件系统镜像工具

用法：
  fsformat <镜像文件> [文件或目录...]

描述：
  本工具用于将宿主机文件及目录结构写入到文件系统镜像中。支持递归处理目录，
  自动创建对应的目录结构，并将普通文件内容按块写入镜像。生成的镜像符合MOS文件系统规范。

选项：
  <镜像文件>   指定输出的文件系统镜像路径
  [文件或目录...] 要添加到镜像中的文件/目录路径（至少一个）
                 若为目录，将递归处理其所有子项（跳过.和..）

文件要求：
  - 文件类型仅支持普通文件（S_ISREG）和目录（S_ISDIR）
  - 文件名长度不超过127字节（包含终止符）
  - 文件总大小不超过文件系统容量（由NBLOCK定义）

错误处理：
  - 参数不足时退出码为1
  - 遇到非法文件类型时退出码为2
  - 文件名超长时直接终止进程(退出码为1)

示例：
  # 创建镜像并添加多个文件/目录
  fsformat fs.img /path/to/file1 /path/to/dir1

  # 递归添加整个目录结构
  fsformat rootfs.img /home/user/documents

实现细节：
  - 镜像结构：
    - 块0：引导扇区
    - 块1：超级块（含根目录）
    - 块2~n：位图块
    - 其余块：数据块/文件目录块
  - 目录条目自动复用空闲的File结构体
  - 文件内容按4KB块分配，支持直接/间接指针
*/
int main(int argc, char **argv) {
  static_assert(sizeof(struct File) == FILE_STRUCT_SIZE);
  init_disk();

  if (argc < 3) {
    fprintf(stderr, "Usage: fsformat <img-file> [files or directories]...\n");
    exit(1);
  }

  for (int i = 2; i < argc; i++) {
    char *name = argv[i];
    struct stat stat_buf;
    int r = stat(name, &stat_buf);
    assert(r == 0);
    if (S_ISDIR(stat_buf.st_mode)) {
      printf("writing directory '%s' recursively into disk\n", name);
      write_directory(&super.s_root, name);
    } else if (S_ISREG(stat_buf.st_mode)) {
      printf("writing regular file '%s' into disk\n", name);
      write_file(&super.s_root, name);
    } else {
      fprintf(stderr, "'%s' has illegal file mode %o\n", name,
              stat_buf.st_mode);
      exit(2);
    }
  }

  flush_bitmap();
  finish_fs(argv[1]);

  return 0;
}
