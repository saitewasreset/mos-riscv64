#include "mmu.h"
#include "types.h"
#include <device_tree.h>
#include <endian.h>
#include <kmalloc.h>
#include <printk.h>
#include <string.h>

struct device_tree device_tree = {NULL};

static void _free_device_tree_node(struct device_node *node);
static int is_type_equal(const char *haystack, const char *needle);

struct device_node *create_device_node(const char *name) {
    struct device_node *node =
        (struct device_node *)kmalloc(sizeof(struct device_node));

    if (node == NULL) {
        panic("create_device_node: cannot allocate memory for node\n");
    }

    size_t name_len = strlen(name);

    char *name_field = (char *)kmalloc((name_len + 1) * sizeof(char));

    if (name_field == NULL) {
        panic("create_device_node: cannot allocate memory for name field\n");
    }

    strcpy(name_field, name);

    node->name = name_field;
    node->parent = NULL;
    node->child = NULL;
    node->sibling = NULL;
    node->properties = NULL;

    return node;
}

// 这将释放device_node本身，不会释放子节点
void free_device_node(struct device_node *node) {
    struct property *current_property = node->properties;

    while (current_property != NULL) {
        struct property *next_proprery = current_property->next;
        free_property(current_property);
        current_property = next_proprery;
    }

    kfree((void *)node->name);
    kfree(node);
}

void device_node_insert_child(struct device_node *parent,
                              struct device_node *child) {
    child->parent = parent;
    child->sibling = parent->child;

    parent->child = child;
}

void device_node_insert_propertiy(struct device_node *node, const char *name,
                                  uint32_t length, const void *value) {
    struct property *new_property =
        (struct property *)kmalloc(sizeof(struct property));

    if (new_property == NULL) {
        panic("device_node_insert_propertiy: cannot allocate memory for "
              "property");
    }

    size_t name_len = strlen(name);

    char *name_field = (char *)kmalloc((name_len + 1) * sizeof(char));

    if (name_field == NULL) {
        panic("device_node_insert_propertiy: cannot allocate memory for name "
              "field\n");
    }

    strcpy(name_field, name);

    new_property->name = name_field;
    new_property->length = length;

    void *value_field = kmalloc(length);

    if (value_field == NULL) {
        panic("device_node_insert_propertiy: cannot allocate memory for value "
              "field\n");
    }

    memcpy(value_field, value, length);

    new_property->value = value_field;

    new_property->next = node->properties;

    node->properties = new_property;
}

// 这将释放property本身
void free_property(struct property *property) {
    kfree((void *)property->name);
    kfree(property->value);
    kfree(property);
}

// 这**不会**device_tree本身
void free_device_tree(struct device_tree *tree) {
    if (tree->root == NULL) {
        return;
    }

    _free_device_tree_node(tree->root);
}

static void _free_device_tree_node(struct device_node *node) {
    struct device_node *current_child = node->child;
    struct device_node *next_child = NULL;

    while (current_child != NULL) {
        next_child = current_child->sibling;

        _free_device_tree_node(current_child);

        current_child = next_child;
    }

    free_device_node(node);
}

static int is_type_equal(const char *haystack, const char *needle) {
    while ((*haystack != '\0') && (*needle != '\0')) {
        if (*haystack == *needle) {
            haystack++;
            needle++;
        } else {
            break;
        }
    }

    if (*haystack == *needle) {
        return 1;
    } else {
        if (*haystack == '@' && *needle == '\0') {
            return 1;
        } else {
            return 0;
        }
    }
}

size_t find_by_type(struct device_tree *tree, const char *type,
                    struct device_node **output, size_t max_count) {
    size_t count = 0;

    struct device_node *node_queue[ITER_MAX_QUEUE_LEN] = {0};

    size_t queue_count = 0;
    size_t front = 0;
    size_t rear = ITER_MAX_QUEUE_LEN - 1;

    if (tree->root == NULL) {
        return 0;
    }

    rear = (rear + 1) % ITER_MAX_QUEUE_LEN;
    node_queue[rear] = tree->root;
    queue_count++;

    while (queue_count > 0) {
        struct device_node *current_node = node_queue[front];
        front++;
        queue_count--;

        if (is_type_equal(current_node->name, type) != 0) {
            if (count < max_count) {
                output[count] = current_node;

                count++;
            } else {
                break;
            }
        }

        struct device_node *current_child = current_node->child;

        while (current_child != NULL) {
            if (queue_count >= ITER_MAX_QUEUE_LEN) {
                debugk("find_by_type", "queue overflow\n");

                goto finish;
            }

            rear = (rear + 1) % ITER_MAX_QUEUE_LEN;
            node_queue[rear] = current_child;
            queue_count++;

            current_child = current_child->sibling;
        }
    }

finish:
    return count;
}

struct property *get_property(struct device_node *node,
                              const char *property_name) {
    struct property *result = NULL;

    struct property *current_property = node->properties;

    while (current_property != NULL) {
        if (strcmp(current_property->name, property_name) == 0) {
            result = current_property;
            break;
        }

        current_property = current_property->next;
    }

    return result;
}

struct device_node *find_by_handle_id(struct device_tree *tree,
                                      uint32_t handle_id_le) {
    struct device_node *result = NULL;
    struct device_node *node_queue[ITER_MAX_QUEUE_LEN] = {0};

    size_t queue_count = 0;
    size_t front = 0;
    size_t rear = ITER_MAX_QUEUE_LEN - 1;

    if (tree->root == NULL) {
        return NULL;
    }

    rear = (rear + 1) % ITER_MAX_QUEUE_LEN;
    node_queue[rear] = tree->root;
    queue_count++;

    while (queue_count > 0) {
        struct device_node *current_node = node_queue[front];
        front++;
        queue_count--;

        void *property_value = get_property(current_node, "phandle");

        if (property_value != NULL) {
            uint32_t current_phandle = be32toh(*(uint32_t *)property_value);

            if (current_phandle == handle_id_le) {
                result = current_node;
                break;
            }
        }

        struct device_node *current_child = current_node->child;

        while (current_child != NULL) {
            if (queue_count >= ITER_MAX_QUEUE_LEN) {
                debugk("find_by_type", "queue overflow\n");

                goto finish;
            }

            rear = (rear + 1) % ITER_MAX_QUEUE_LEN;
            node_queue[rear] = current_child;
            queue_count++;

            current_child = current_child->sibling;
        }
    }

finish:
    return result;
}

struct device_tree parse_tree(void *begin) {
    struct device_tree device_tree = {NULL};
    struct device_node *node_stack[MAX_STACK_DEPTH] = {NULL};
    size_t node_stack_top = 0;

    struct fdt_header *header = (struct fdt_header *)begin;

    if (be32toh(header->magic) != FDT_MAGIC) {
        debugk("parse_tree", "invalid fdt magic: 0x%08x\n",
               be32toh(header->magic));

        goto finish;
    }

    debugk("parse_tree", "FDT version: %d\n", be32toh(header->version));

    debugk("parse_tree", "FDT total size: 0x%08x\n",
           be32toh(header->totalsize));

    debugk("parse_tree", "Offset of reserved memory: 0x%08x\n",
           be32toh(header->off_mem_rsvmap));

    debugk("parse_tree", "Offset of struct: 0x%08x size: 0x%08x\n",
           be32toh(header->off_dt_struct), be32toh(header->size_dt_struct));

    debugk("parse_tree", "Offset of strings: 0x%08x size: 0x%08x\n",
           be32toh(header->off_dt_strings), be32toh(header->size_dt_strings));

    debugk("parse_tree", "\nReserved Memory: \n");

    struct fdt_reserve_entry *current_reserve_entry =
        (struct fdt_reserve_entry *)(((size_t)begin) +
                                     be32toh(header->off_mem_rsvmap));

    while (!((current_reserve_entry->address == 0) &&
             (current_reserve_entry->size == 0))) {
        debugk("parse_tree", "begin addr = 0x%016lx, size = 0x%016lx\n",
               be64toh(current_reserve_entry->address),
               be64toh(current_reserve_entry->size));

        current_reserve_entry++;
    }

    void *struct_ptr = (void *)((size_t)begin + be32toh(header->off_dt_struct));
    void *string_ptr =
        (void *)((size_t)begin + be32toh(header->off_dt_strings));

    const char *current_ptr = struct_ptr;
    const char *current_node_begin = NULL;
    char name_buffer[NAME_BUFFER_LEN] = {0};

    while (1) {
        uint32_t current_token = be32toh(*(uint32_t *)current_ptr);

        current_ptr = (const char *)((size_t)current_ptr + sizeof(uint32_t));

        current_node_begin = current_ptr;

        struct fdt_prop_header *prop_header = NULL;

        switch (current_token) {
        case 0:
            // padding
            break;
        case FDT_BEGIN_NODE:
            while (*current_ptr != '\0') {
                current_ptr++;
            }
            // 同时复制'\0'
            memcpy(name_buffer, current_node_begin,
                   (size_t)(current_ptr - current_node_begin) + 1);

            struct device_node *parent_node = NULL;

            if (node_stack_top > 0) {
                parent_node = node_stack[node_stack_top - 1];
            }

            if (node_stack_top >= MAX_STACK_DEPTH) {
                debugk("parse_tree", "Device tree to deep!\n");
                goto finish;
            }

            struct device_node *current_node = create_device_node(name_buffer);

            if (parent_node != NULL) {
                device_node_insert_child(parent_node, current_node);
            }

            if (device_tree.root == NULL) {
                device_tree.root = current_node;
            }

            node_stack[node_stack_top] = current_node;
            node_stack_top++;

            current_ptr = (const char *)(ROUND((size_t)current_ptr, 4));

            break;
        case FDT_END_NODE:
            node_stack_top--;
            break;
        case FDT_PROP:
            prop_header = (struct fdt_prop_header *)current_node_begin;
            uint32_t prop_len = be32toh(prop_header->len);
            const char *prop_name =
                (const char *)((size_t)string_ptr +
                               be32toh(prop_header->nameoff));

            if (node_stack_top == 0) {
                debugk("parse_tree",
                       "No current node when encounter FDT_PROP\n");
                goto finish;
            }

            struct device_node *device_node = node_stack[node_stack_top - 1];

            current_ptr += sizeof(struct fdt_prop_header);

            device_node_insert_propertiy(device_node, prop_name, prop_len,
                                         current_ptr);

            current_ptr = (const char *)((size_t)current_ptr + prop_len);

            current_ptr = (const char *)(ROUND((size_t)current_ptr, 4));

            break;
        case FDT_NOP:
            break;
        case FDT_END:
            goto finish;
        default:
            debugk("parse_tree", "invalid token: 0x%08x\n", current_token);
            goto finish;
        }
    }

finish:
    return device_tree;
}

void print_stringlist(const char *stringlist, size_t total_length) {
    const char *p = stringlist;
    const char *end = stringlist + total_length;

    while (p < end) {

        printk("%s ", p);

        p += strlen(p) + 1;
    }
}

void device_tree_init(void *pa) {
    debugk("device_tree_init", "Begin device tree parsing at pa = 0x%016lx\n",
           (u_reg_t)pa);

    kmap(DTB_BEGIN_VA, (u_reg_t)pa, MAX_DEVICE_TREE_SIZE,
         PTE_V | PTE_RO | PTE_GLOBAL);

    device_tree = parse_tree((void *)DTB_BEGIN_VA);

    debugk("device_tree_init", "Finish device tree parsing\n");
}

int contains_string(const char *stringlist, size_t total_length,
                    const char *target) {
    const char *p = stringlist;
    const char *end = stringlist + total_length;

    while (p < end) {

        if (strcmp(p, target) == 0) {
            return 1;
        }

        p += strlen(p) + 1;
    }
    return 0;
}

uint32_t get_address_cells_count(struct device_node *node) {
    uint32_t count = 0;

    if (node->parent == NULL) {
        panic("get_address_cells_count: address cells count is undefined for "
              "root "
              "node");
    }

    node = node->parent;

    while (node != NULL) {
        const struct property *count_property =
            get_property(node, "#address-cells");

        if (count_property != NULL) {
            count = be32toh(*(uint32_t *)(count_property->value));
        }

        node = node->parent;
    }

    if (count == 0) {
        panic("get_address_cells_count: cannot find address cells count for "
              "node: %s\n",
              node->name);
    }

    return count;
}

uint32_t get_size_cells_count(struct device_node *node) {
    uint32_t count = 0;

    if (node->parent == NULL) {
        panic("get_size_cells_count: size cells count is undefined for root "
              "node");
    }

    node = node->parent;

    while (node != NULL) {
        const struct property *count_property =
            get_property(node, "#size-cells");

        if (count_property != NULL) {
            count = be32toh(*(uint32_t *)(count_property->value));
        }

        node = node->parent;
    }

    if (count == 0) {
        panic("get_size_cells_count: cannot find size cells count for "
              "node: %s\n",
              node->name);
    }

    return count;
}

uint32_t get_reg_list_len(struct device_node *node) {
    const struct property *reg_property = get_property(node, "reg");

    if (reg_property == NULL) {
        debugk("get_reg_list_len", "%s: no \"reg\" property\n", node->name);
        return 0;
    }

    uint32_t address_cells_count = get_address_cells_count(node);
    uint32_t size_cells_count = get_size_cells_count(node);

    if (reg_property->length % (address_cells_count + size_cells_count) != 0) {
        debugk("get_reg_list_len",
               "%s: invalid reg property length %u for address cells count %u "
               "size cells count %u\n",
               node->name, address_cells_count, size_cells_count);
        return 0;
    }

    return reg_property->length / (address_cells_count + size_cells_count);
}
int get_reg_item(struct device_node *node, uint32_t idx, u_reg_t *address,
                 u_reg_t *size) {
    const struct property *reg_property = get_property(node, "reg");

    if (reg_property == NULL) {
        debugk("get_reg_item", "%s: no \"reg\" property\n", node->name);
        return 1;
    }

    uint32_t address_cells_count = get_address_cells_count(node);
    uint32_t size_cells_count = get_size_cells_count(node);

    if (reg_property->length % (address_cells_count + size_cells_count) != 0) {
        debugk("get_reg_item",
               "%s: invalid reg property length %u for address cells count %u "
               "size cells count %u\n",
               node->name, address_cells_count, size_cells_count);
        return 1;
    }

    uint32_t skip_byte_count = idx * (address_cells_count + size_cells_count);

    if (skip_byte_count >= reg_property->length) {
        debugk("get_reg_item", "%s: idx out of bound: idx = %u count = %u\n",
               node->name, idx,
               reg_property->length / (address_cells_count + size_cells_count));
        return 1;
    }

    void *ptr = (void *)((size_t)reg_property->value + skip_byte_count);

    if (address_cells_count == 1) {
        *address = (u_reg_t)be32toh((*(uint32_t *)ptr));

        ptr = (void *)((size_t)ptr + sizeof(uint32_t));
    } else if (address_cells_count == 2) {
        *address = (u_reg_t)be64toh((*(uint64_t *)ptr));

        ptr = (void *)((size_t)ptr + sizeof(uint64_t));
    } else {
        debugk("get_reg_item", "%s: unsupported address cells count %u\n",
               node->name, address_cells_count);
    }

    if (size_cells_count == 1) {
        *size = (u_reg_t)be32toh((*(uint32_t *)ptr));
    } else if (size_cells_count == 2) {
        *size = (u_reg_t)be64toh((*(uint64_t *)ptr));
    } else {
        debugk("get_reg_item", "%s: unsupported size cells count %u\n",
               node->name, size_cells_count);
    }

    return 0;
}