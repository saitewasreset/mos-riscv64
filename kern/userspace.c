#include "env.h"
#include "types.h"
#include <mmu.h>
#include <pmap.h>
#include <printk.h>
#include <string.h>
#include <userspace.h>

void copy_user_space(const void *restrict src, void *restrict dst, size_t len) {

    if (curenv == NULL) {
        panic("copy_user_space called while curenv is NULL");
    }

    Pte *src_pte = NULL;
    Pte *dst_pte = NULL;

    page_lookup(curenv->env_pgdir, (u_reg_t)src, &src_pte);
    page_lookup(curenv->env_pgdir, (u_reg_t)dst, &dst_pte);

    if (src_pte == NULL) {
        panic("trying to copy from unmapped va 0x%016lx\n", (u_reg_t)src);
    }

    if (dst_pte == NULL) {
        if ((u_reg_t)dst >= ULIM) {
            panic("trying to copy to unmapped kernel va 0x%016lx\n",
                  (u_reg_t)dst);
        } else {
            passive_alloc((u_reg_t)dst, curenv->env_pgdir, curenv->env_asid);
        }
    }

    allow_access_user_space();

    memcpy(dst, src, len);

    disallow_access_user_space();
}

void copy_user_space_to_env(struct Env *env, const void *restrict src,
                            void *restrict dst, size_t len) {
    uint16_t c_asid = 0;

    if (curenv != NULL) {
        c_asid = curenv->env_asid;
    }

    Pte *c_pgdir = cur_pgdir;

    set_page_table(env->env_asid, env->env_pgdir);

    copy_user_space(src, dst, len);

    set_page_table(c_asid, c_pgdir);
}

void map_user_vpt(struct Env *env) {
    Pte *p1 = env->env_pgdir;

    // 映射一级页表
    u_reg_t uvpt_p1_begin_va = UVPT + (UVPT >> 9) + (UVPT >> 18);

    page_insert(env->env_pgdir, env->env_asid, pa2page(PADDR(env->env_pgdir)),
                uvpt_p1_begin_va, PTE_RO | PTE_USER);

    u_reg_t uvpt_p2_begin_va = UVPT + (UVPT >> 9);
    // 映射二级页表
    for (size_t p1x = 0; p1x <= P1X(USTACKTOP); p1x++) {
        Pte p1_entry = p1[p1x];

        if ((p1_entry & PTE_V) != 0) {
            u_reg_t uvpt_p2_current_va = uvpt_p2_begin_va + p1x * PAGE_SIZE;

            if (uvpt_p2_current_va == uvpt_p1_begin_va) {
                continue;
            }

            page_insert(env->env_pgdir, env->env_asid,
                        pa2page(PTE_ADDR(p1_entry)), uvpt_p2_current_va,
                        PTE_RO | PTE_USER);
        }
    }

    u_reg_t uvpt_p3_begin_va = UVPT;

    // 映射三级页表
    for (size_t p1x = 0; p1x <= P1X(USTACKTOP); p1x++) {
        Pte p1_entry = p1[p1x];

        if ((p1_entry & PTE_V) != 0) {
            u_reg_t p2_base_addr = P2KADDR(PTE_ADDR(p1_entry));

            for (size_t p2x = 0; p2x < (PAGE_SIZE / sizeof(Pte)); p2x++) {
                Pte p2_entry = ((Pte *)p2_base_addr)[p2x];

                if ((p2_entry & PTE_V) != 0) {
                    u_reg_t uvpt_p3_current_va =
                        (p1x * (PAGE_SIZE / sizeof(Pte)) + p2x) * PAGE_SIZE +
                        uvpt_p3_begin_va;

                    if ((uvpt_p3_current_va >= uvpt_p2_begin_va) &&
                        (uvpt_p3_current_va <
                         (uvpt_p2_begin_va + 2 * 1024 * 1024))) {
                        continue;
                    }

                    page_insert(env->env_pgdir, env->env_asid,
                                pa2page(PTE_ADDR(p2_entry)), uvpt_p3_current_va,
                                PTE_RO | PTE_USER);
                }
            }
        }
    }
}

void unmap_user_vpt(struct Env *env) {
    Pte *p1 = env->env_pgdir;
    u_reg_t p1x = P1X(UVPT);

    Pte p1_entry = p1[p1x];
    u_reg_t p2_base_addr = P2KADDR(PTE_ADDR(p1_entry));

    for (size_t p2x = 0; p2x < (PAGE_SIZE / sizeof(Pte)); p2x++) {
        Pte p2_entry = ((Pte *)p2_base_addr)[p2x];

        if ((p2_entry & PTE_V) != 0) {
            u_reg_t p3_base_addr = P2KADDR(PTE_ADDR(p2_entry));

            for (size_t p3x = 0; p3x < (PAGE_SIZE / sizeof(Pte)); p3x++) {
                Pte p3_entry = ((Pte *)p3_base_addr)[p3x];

                u_reg_t vpn = (p1x << 18) | (p2x << 9) | p3x;

                if ((p3_entry & PTE_V) != 0) {
                    page_remove(env->env_pgdir, env->env_asid, vpn << 12);
                }
            }
        }
    }
}