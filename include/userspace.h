#ifndef __USERSPACE_H__
#define __USERSPACE_H__

#include <env.h>
#include <stddef.h>

extern char _user_buffer_start[];
extern char _user_buffer_end[];

#define KERNEL_BUFFER ((void *)_user_buffer_start)
#define KERNEL_BUFFER_SIZE ((size_t)((_user_buffer_end) - (_user_buffer_start)))

void allow_access_user_space();
void disallow_access_user_space();

void copy_user_space(const void *restrict src, void *restrict dst, size_t len);

void copy_user_space_to_env(struct Env *env, const void *restrict src,
                            void *restrict dst, size_t len);

void map_user_vpt(struct Env *env);
void unmap_user_vpt(struct Env *env);

#endif