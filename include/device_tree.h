#ifndef __DEVICE_TREE_H
#define __DEVICE_TREE_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define FDT_MAGIC 0xd00dfeed

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE 0x00000002
#define FDT_PROP 0x00000003
#define FDT_NOP 0x00000004
#define FDT_END 0x00000009

#define NAME_BUFFER_LEN 32
// 遍历设备树时的队列的最大长度
#define ITER_MAX_QUEUE_LEN 64
// 解析FDT时的栈的最大深度
#define MAX_STACK_DEPTH 64

#define MAX_DEVICE_TREE_SIZE (16 * PAGE_SIZE)

extern struct device_tree device_tree;

// FDT头
struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

// FDT保留内存条目
struct fdt_reserve_entry {
    uint64_t address;
    uint64_t size;
};

// FDT属性头
struct fdt_prop_header {
    uint32_t len;
    uint32_t nameoff;
};

struct device_tree {
    struct device_node *root;
};

struct device_node {
    const char *name;            // 节点名
    struct device_node *parent;  // 父节点
    struct device_node *child;   // 首子节点
    struct device_node *sibling; // 兄弟节点
    struct property *properties; // 属性链表
};

struct property {
    const char *name;      // 属性名
    uint32_t length;       // 值长度
    void *value;           // 值指针
    struct property *next; // 下个属性
};

struct device_node *create_device_node(const char *name);
void free_device_node(struct device_node *node);
void device_node_insert_child(struct device_node *parent,
                              struct device_node *child);
void device_node_insert_propertiy(struct device_node *node, const char *name,
                                  uint32_t length, const void *value);
void free_property(struct property *property);

void free_device_tree(struct device_tree *tree);

uint32_t get_address_cells_count(struct device_node *node);
uint32_t get_size_cells_count(struct device_node *node);

uint32_t get_reg_list_len(struct device_node *node);
int get_reg_item(struct device_node *node, uint32_t idx, u_reg_t *address,
                 u_reg_t *size);

size_t find_by_type(struct device_tree *tree, const char *type,
                    struct device_node **output, size_t max_count);

struct property *get_property(struct device_node *node,
                              const char *property_name);
struct device_node *find_by_handle_id(struct device_tree *tree,
                                      uint32_t handle_id_le);

int contains_string(const char *stringlist, size_t total_length,
                    const char *target);

void print_stringlist(const char *stringlist, size_t total_length);

struct device_tree parse_tree(void *begin);

void device_tree_init(void *pa);
#endif