// User-level IPC library routines

#include "error.h"
#include <env.h>
#include <lib.h>
#include <mmu.h>

// Send val to whom.  This function keeps trying until
// it succeeds.  It should panic() on any error other than
// -E_IPC_NOT_RECV.
//
// Hint: use syscall_yield() to be CPU-friendly.
int ipc_send(uint32_t whom, uint64_t val, const void *srcva, uint32_t perm) {
    int r;
    while ((r = syscall_ipc_try_send(whom, val, srcva, perm)) ==
           -E_IPC_NOT_RECV) {
        syscall_yield();
    }
    return r;
}

// Receive a value.  Return the value and store the caller's envid
// in *whom.
//
// Hint: use env to discover the value and who sent it.
int ipc_recv(uint32_t from, uint32_t *whom, uint64_t *out_val, void *dstva,
             uint32_t *perm) {
    int r = 0;

    uint32_t self = syscall_getenvid();

    r = syscall_ipc_recv(dstva, from);

    // u_reg_t pa = syscall_get_physical_address(dstva);
    // debugf("ipc_recv: [%08x] 0x%016lx -> 0x%016lx\n", self, dstva, pa);

    if (r != 0) {
        return r;
    }

    if (whom) {
        *whom = env->env_ipc_from;
    }

    if (perm) {
        *perm = env->env_ipc_perm;
    }

    if (out_val) {
        *out_val = env->env_ipc_value;
    }

    return r;
}
