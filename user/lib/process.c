#include <env.h>
#include <lib.h>
#include <limits.h>
#include <process.h>
#include <string.h>

static struct Process process_list[NENV];

size_t get_process_list(size_t max_len, struct Process *out_process_list) {
    if (max_len > INT_MAX) {
        user_panic("get_process_list: invalid max_len: %lu", max_len);
    }

    int ret = syscall_get_process_list((int)max_len, (u_reg_t)out_process_list);

    if (ret < 0) {
        user_panic("get_process_list: syscall_get_process_list returned: %d\n",
                   ret);
    }

    return (size_t)ret;
}

size_t find_process_by_name(const char *name, size_t max_len,
                            struct Process *out_process_list) {

    int ret = syscall_get_process_list(NENV, (u_reg_t)process_list);

    if (ret < 0) {
        user_panic(
            "find_process_by_name: syscall_get_process_list returned: %d\n",
            ret);
    }

    size_t count = 0;

    for (int i = 0; i < ret; i++) {
        if (count >= max_len) {
            break;
        }

        if (strcmp(process_list[i].env_name, name) == 0) {
            memcpy(&out_process_list[count], &process_list[i],
                   sizeof(struct Process));
            count++;
        }
    }

    return count;
}

uint32_t get_envid(const char *name) {
    int ret = syscall_get_process_list(NENV, (u_reg_t)process_list);

    if (ret < 0) {
        user_panic("get_envid: syscall_get_process_list returned: %d\n", ret);
    }

    uint32_t envid = 0;

    for (int i = 0; i < ret; i++) {
        if (strcmp(process_list[i].env_name, name) == 0) {
            envid = process_list[i].env_id;

            break;
        }
    }

    return envid;
}