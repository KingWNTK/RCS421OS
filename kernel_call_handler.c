#include "kernel_call_handler.h"

int handle_getpid() {
    return cur_pcb->pid;
}

int handle_fork() {
    pcb *child_pcb = (pcb *)malloc(sizeof(pcb));
    if (init_pcb(child_pcb) == -1) {
        //not enough memory
    }
    cur_pcb->tick_exe = 2;
    push_q(READY_Q, cur_pcb);
    int pre_pid = cur_pcb->pid;
    ContextSwitch(copy_region_0_and_switch, &cur_pcb->ctx, cur_pcb, child_pcb);
    if (cur_pcb->pid == pre_pid) {
        return 0;
    } else {
        return cur_pcb->pid;
    }
}