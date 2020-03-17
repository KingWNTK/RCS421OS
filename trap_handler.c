#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdio.h>

#include "memory_manager.h"
#include "trap_handler.h"
#include "kernel_call_handler.h"
//the trap vector table;
void (*trap_vec_tb[TRAP_VECTOR_SIZE])(ExceptionInfo *);

tty_buf tty[NUM_TERMINALS];

char line_buf[TERMINAL_MAX_LINE];

#undef LEVEL
#define LEVEL 5
void do_schedule() {
    //have to do ContextSwitch
    //or keep cur_pcb unchanged and restore cur_pcb's tick_exe to 2
    if (ready_q.size) {
        if (cur_pcb != idle_pcb) {
            push_back_q(&ready_q, cur_pcb);
        }
        pcb *nxt = (pcb *)pop_front_q(&ready_q);
        nxt->tick_exe = 2;
        if (ContextSwitch(switch_ctx, &cur_pcb->ctx, cur_pcb, nxt) == -1) {
            TracePrintf(LEVEL, "something wrong when calling ContextSwitch, going to halt\n");
            Halt();
        }
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
        info->regs[0] = (unsigned long)handle_wait((int *)info->regs[1]);
    } else if (info->code == YALNIX_TTY_READ) {
        info->regs[0] = (unsigned long)handle_tty_read((int)info->regs[1], (void *)info->regs[2], (int)info->regs[3]);
    } else if (info->code == YALNIX_TTY_WRITE) {
        info->regs[0] = (unsigned long)handle_tty_write((int)info->regs[1], (void *)info->regs[2], (int)info->regs[3]);
    }
}

void handle_trap_clock(ExceptionInfo *info) {
    store_exp_info(info);
    // printf("trap clock\n");
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
    tty_buf *term = &tty[info->code];
    int len = TtyReceive(info->code, line_buf, TERMINAL_MAX_LINE);
    //push the current line to the in buf
    char *buf = (char *)malloc(len);
    memcpy(buf, line_buf, len);
    push_back_q(&term->in_buf, buf);
    if (term->reading) {
        //wake up the reading process
        term->reading = 0;
        pcb *p = (pcb *)pop_front_q(&tty_read_q[info->code]);
        push_back_q(&ready_q, p);
        if (cur_pcb == idle_pcb) {
            do_schedule();
        }
    }
}

void handle_trap_tty_transmit(ExceptionInfo *info) {
    store_exp_info(info);
    printf("trap tty transmit\n");
    tty_buf *term = &tty[info->code];
    if (term->writing) {
        //wake up the writing process
        term->writing = 0;
        pcb *p = (pcb *)pop_front_q(&tty_write_q[info->code]);
        push_back_q(&ready_q, p);
        if (cur_pcb == idle_pcb) {
            do_schedule();
        }
    }
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

    for (i = 0; i < NUM_TERMINALS; i++) {
        init_q(&tty[i].in_buf);
    }
}