#ifndef PROCESS_CONTROL_H
#define PROCESS_CONTROL_H
#include <comp421/hardware.h>

#include <stdio.h>
#include "memory_manager.h"

#define READY_Q 1

typedef struct pcb {
    //process id
    int pid;
    //remaining execution ticks before context switch
    int tick_exe;
    //current brk;
    void *brk;
    //current sp, this is useful to copy the user stack
    void *sp;
    //the page table info, describe which phys page
    //and which half is this page table using
    ptl_node pt_info;
    //saved context, necessary for context switch
    SavedContext ctx;    
}pcb;

typedef struct q_node {
    pcb *pcb_ptr;
    struct q_node *pre, *nxt;
}q_node;


typedef struct queue {
    int size;
    q_node *head, *tail
}queue;

extern queue ready_q;

extern pcb *cur_pcb;

extern pcb *idle_pcb;

void push_q(int which, pcb *p);

pcb *pop_q(int which);

// void store_exp_info(ExceptionInfo *info);

// void restore_exp_info(ExceptionInfo *info);

int init_pcb(pcb *which);

int init_process_controller();

SavedContext *switch_ctx(SavedContext *ctxp, void *p1, void *p2);

SavedContext *copy_kernel_stack_and_switch(SavedContext *ctxp, void *p1, void *p2);

SavedContext *copy_region_0_and_switch(SavedContext *ctxp, void *p1, void *p2);

SavedContext *get_cur_ctx(SavedContext *ctxp, void *p1, void *p2);

#endif