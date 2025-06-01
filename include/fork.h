#ifndef __FORK_H
#define __FORK_H

#include <mmu.h>

void dup_userspace(Pte *parent_pgdir, Pte *child_pgdir, uint16_t child_asid);

#endif