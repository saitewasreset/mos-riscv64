#include <env.h>
#include <lib.h>
#include <process.h>

static struct Process process_list[NENV];

static const char *env_status_to_string[3] = {[ENV_FREE] = "ENV_FREE",
                                              [ENV_NOT_RUNNABLE] =
                                                  "ENV_NOT_RUNNABLE",
                                              [ENV_RUNNABLE] = "ENV_RUNNABLE"};

void dump_process() {
    size_t process_count = get_process_list(NENV, process_list);

    debugf("%16s\t%8s\t%8s\t%8s\t%8s\t%s\n", "NAME", "PID", "PPID", "PRI",
           "RUNS", "STAT");

    for (size_t i = 0; i < process_count; i++) {
        struct Process *cur = &process_list[i];
        debugf("%16s\t%08x\t%08x\t%8u\t%8lu\t%s\n", cur->env_name, cur->env_id,
               cur->env_parent_id, cur->env_pri, cur->env_runs,
               env_status_to_string[cur->env_status]);
    }
}

int main(void) {
    dump_process();

    for (size_t i = 0; i < 10000000; i++) {
    }

    debugf("\n\n");

    dump_process();
}