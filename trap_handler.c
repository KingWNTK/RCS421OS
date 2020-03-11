#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdio.h>

#include "memory_manager.h"
#include "trap_handler.h"
#include "process_controller.h"
#include "kernel_call_handler.h"
//the trap vector table;
void (*trap_vec_tb[TRAP_VECTOR_SIZE])(ExceptionInfo *);

#undef LEVEL
#define LEVEL 5

void handle_trap_kernel(ExceptionInfo *info) {
    TracePrintf(LEVEL, "trap kernel\n");
    if(info->code == YALNIX_GETPID) {
        info->regs[0] = (unsigned long)handle_getpid();
    }
    else if(info->code == YALNIX_FORK) {
        info->regs[0] = (unsigned long)handle_fork();
    }
    
}

void do_schedule() {
    if (cur_pcb != idle_pcb && --cur_pcb->tick_exe)
        return;
    //have to do ContextSwitch now
    //or keep cur_pcb unchanged and restore cur_pcb's tick_exe to 2
    if (ready_q.size) {
        pcb *pre_pcb = cur_pcb;
        if (cur_pcb != idle_pcb) {
            push_q(READY_Q, cur_pcb);
        }
        cur_pcb = pop_q(READY_Q);
        ContextSwitch(switch_ctx, &pre_pcb->ctx, pre_pcb, cur_pcb);
    }
    cur_pcb->tick_exe = 2;
}

void handle_trap_clock(ExceptionInfo *info) {
    printf("trap clock\n");
    // TracePrintf(LEVEL, "current available page: %d\n", get_fpl_size());
    do_schedule();
}

void hanlde_trap_illegal(ExceptionInfo *info) {
    printf("trap illegal\n");
}

void handle_trap_memory(ExceptionInfo *info) {
    printf("trap memory\n");
    printf("%x\n", info->addr);
}

void handle_trap_math(ExceptionInfo *info) {
    printf("trap math\n");
}

void handle_trap_tty_receive(ExceptionInfo *info) {
    printf("trap tty receive\n");
}

void handle_trap_tty_transmit(ExceptionInfo *info) {
    printf("trap tty transmit\n");
}

void init_trap_handler() {
    trap_vec_tb[TRAP_KERNEL] = handle_trap_kernel;
    trap_vec_tb[TRAP_CLOCK] = handle_trap_clock;
    trap_vec_tb[TRAP_ILLEGAL] = hanlde_trap_illegal;
    trap_vec_tb[TRAP_MEMORY] = handle_trap_memory;
    trap_vec_tb[TRAP_MATH] = handle_trap_math;
    trap_vec_tb[TRAP_TTY_RECEIVE] = handle_trap_tty_receive;
    trap_vec_tb[TRAP_TTY_TRANSMIT] = handle_trap_tty_transmit;
    int i;
    for (i = TRAP_TTY_TRANSMIT + 1; i < TRAP_VECTOR_SIZE; i++)
        trap_vec_tb[i] = NULL;
    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal)trap_vec_tb);
}