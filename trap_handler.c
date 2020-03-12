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
void do_schedule() {
    //have to do ContextSwitch
    //or keep cur_pcb unchanged and restore cur_pcb's tick_exe to 2
    if (ready_q.size) {
        if (cur_pcb != idle_pcb) {
            push_back_q(&ready_q, (void *)cur_pcb);
        }
        pcb *nxt = (pcb *)pop_front_q(&ready_q);
        nxt->tick_exe = 2;
        ContextSwitch(switch_ctx, &cur_pcb->ctx, cur_pcb, nxt);
    }
}

void handle_trap_kernel(ExceptionInfo *info) {
    store_exp_info(info);
    TracePrintf(LEVEL, "trap kernel, code: %d\n", info->code);
    if (info->code == YALNIX_GETPID) {
        info->regs[0] = (unsigned long)handle_getpid();
    } else if (info->code == YALNIX_FORK) {
        info->regs[0] = (unsigned long)handle_fork();
    } else if (info->code == YALNIX_DELAY) {
        info->regs[0] = (unsigned long)handle_delay((int)info->regs[1]);
    } else if (info->code == YALNIX_BRK) {
        info->regs[0] = (unsigned long)handle_brk((void *)info->regs[1]);
    } else if (info->code == YALNIX_EXEC) {
        info->regs[0] = (unsigned long)handle_exec((char *)info->regs[1], (char **)info->regs[2], info);
    } else if (info->code == YALNIX_EXIT) {
        handle_exit((int)info->regs[1]);
    } else if (info->code == YALNIX_WAIT) {
        TracePrintf(LEVEL, "%d\n", *(int *)info->regs[1]);
        info->regs[0] = (unsigned long)handle_wait((int *)info->regs[1]);
    }
}

void handle_trap_clock(ExceptionInfo *info) {
    store_exp_info(info);
    printf("trap clock\n");
    // TracePrintf(LEVEL, "current available page: %d\n", get_fpl_size());
    maintain_delay_q();
    if (cur_pcb != idle_pcb) {
        cur_pcb->tick_exe--;
    }
    if (!cur_pcb->tick_exe || (cur_pcb == idle_pcb && ready_q.size)) {
        do_schedule();
    }
}

void hanlde_trap_illegal(ExceptionInfo *info) {
    store_exp_info(info);

    printf("trap illegal\n");
}

void handle_trap_memory(ExceptionInfo *info) {
    store_exp_info(info);

    printf("trap memory\n");
    printf("%x\n", info->addr);
}

void handle_trap_math(ExceptionInfo *info) {
    store_exp_info(info);

    printf("trap math\n");
}

void handle_trap_tty_receive(ExceptionInfo *info) {
    store_exp_info(info);

    printf("trap tty receive\n");
}

void handle_trap_tty_transmit(ExceptionInfo *info) {
    store_exp_info(info);

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