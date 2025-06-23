#include <env.h>
#include <env_interrupt.h>
#include <error.h>
#include <mmu.h>
#include <plic.h>
#include <printk.h>
#include <trap.h>
#include <userspace.h>

static uint32_t interrupt_code_to_envid[MAX_INTERRUPT] = {0};

void register_env_interrupt(uint32_t interrupt_code, struct Env *env,
                            u_reg_t handler_function_va) {
    if (interrupt_code >= MAX_INTERRUPT) {
        panic("register_env_interrupt: invalid interrupt code: %u",
              interrupt_code);
    }
    if ((handler_function_va < UTEMP) || (handler_function_va >= USTACKTOP)) {
        panic("register_env_interrupt: invalid hanlder function va: 0x%016lx",
              handler_function_va);
    }

    env->handler_function_va = handler_function_va;

    interrupt_code_to_envid[interrupt_code] = env->env_id;
}

void handle_env_interrupt(struct Trapframe *tf, uint32_t interrupt_code) {
    int ret = 0;

    if (interrupt_code >= MAX_INTERRUPT) {
        panic("handle_env_interrupt: invalid interrupt code: %u",
              interrupt_code);
    }

    if (interrupt_code_to_envid[interrupt_code] == 0) {
        debugk("handle_env_interrupt",
               "no env interrupt handler set for interrupt code: %u\n",
               interrupt_code);
        return;
    }

    struct Env *env = NULL;

    if ((ret = envid2env(interrupt_code_to_envid[interrupt_code], &env, 0)) !=
        0) {
        debugk("handle_env_interrupt",
               "invalid envid %u envid2env returned: %d\n",
               interrupt_code_to_envid[interrupt_code], ret);
        return;
    }

    if (env->env_status == ENV_NOT_RUNNABLE) {
        if (env->env_in_syscall == 1) {
            env->env_in_syscall = 0;

            // 将当前系统调用的返回值设置为-E_INTR
            // 10 -> ra
            env->env_tf.regs[10] = (u_reg_t)-E_INTR;
        }

        // 唤醒进程
        env->env_status = ENV_RUNNABLE;
        TAILQ_INSERT_TAIL(&env_sched_list, env, env_sched_link);
    }

    // 将当前进程上下文保存到用户异常栈
    // 2 -> sp
    u_reg_t user_sp = tf->regs[2];

    if ((user_sp >= USTACKTOP) && (user_sp < UXSTACKTOP)) {
        // 用户异常重入
        user_sp -= sizeof(struct Trapframe);
    } else {
        user_sp = UXSTACKTOP - sizeof(struct Trapframe);
    }

    copy_user_space(tf, (void *)user_sp, sizeof(struct Trapframe));

    env->env_tf = *tf;

    // 2 -> sp
    tf->regs[2] = user_sp;

    // 设置异常返回地址：调用用户异常处理函数
    tf->sepc = env->handler_function_va;

    // 暂时关闭该外部中断
    plic_set_prority(interrupt_code, 0);
}

int ret_env_interrupt(struct Trapframe *tf) {
    if (curenv == NULL) {
        panic("ret_env_interrupt called while curenv is NULL");
    }

    // 执行该系统调用的函数必须内联！
    // 否则，`user_sp`不指向用户异常帧！
    // 2 -> sp
    u_reg_t user_sp = tf->regs[2];

    if ((user_sp >= UXSTACKTOP) || (user_sp < USTACKTOP)) {
        debugk("ret_env_interrupt", "invalid user sp: 0x%016lx\n", user_sp);
        return -E_INVAL;
    }

    copy_user_space((void *)user_sp, tf, sizeof(struct Trapframe));

    uint32_t target_interrupt_code = 0;

    for (uint32_t i = 0; i < MAX_INTERRUPT; i++) {
        if (interrupt_code_to_envid[i] == curenv->env_id) {
            target_interrupt_code = i;
            break;
        }
    }

    if (target_interrupt_code != 0) {
        // 重新允许该外部中断
        plic_set_prority(target_interrupt_code, 1);
    }

    return 0;
}