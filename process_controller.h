#ifndef PROCESS_CONTROL_H
#define PROCESS_CONTROL_H
#include <comp421/hardware.h>

#include <stdio.h>
#include "memory_manager.h"

#define READY_Q 1

typedef struct q_node {
    void *ptr;
    struct q_node *pre, *nxt;
} q_node;

typedef struct queue {
    int size;
    q_node *head, *tail
} queue;

typedef struct pcb {
    //process id
    int pid;
    //remaining execution ticks before context switch
    int tick_exe;
    //ramaining clock ticks before
    int tick_delay;
    //current brk;
    void *brk;
    //current sp, this is useful to copy the user stack
    void *sp;
    //the page table info, describe which phys page
    //and which half is this page table using
    ptl_node pt_info;
    //saved context, necessary for context switch
    SavedContext ctx;
    //parent of this process
    struct pcb *parent;

    queue children;

    int waiting;

    ExceptionInfo *exp_info;
} pcb;

typedef struct exit_status {
    int pid;
    int parent_pid;
    int status;
} exit_status;

extern queue ready_q, delay_q, exit_q, wait_q, tty_read_q[NUM_TERMINALS], tty_write_q[NUM_TERMINALS];

extern pcb *cur_pcb;

extern pcb *idle_pcb;

void insert_after(queue *q, q_node *nd1, q_node *nd2);

void insert_before(queue *q, q_node *nd1, q_node *nd2);

void erase_after(queue *q, q_node *nd);

void erase_before(queue *q, q_node *nd);

int find_and_erase_q(queue *q, void *p);

void init_q(queue *q);

void push_back_q(queue *q, void *p);

void push_front_q(queue *q, void *p);

void *pop_front_q(queue *q);

void store_exp_info(ExceptionInfo *info);

// void restore_exp_info(ExceptionInfo *info);

void maintain_delay_q();

void print_topo(pcb *cur);

int init_pcb(pcb *which);

int init_process_controller();

SavedContext *switch_ctx(SavedContext *ctxp, void *p1, void *p2);

SavedContext *copy_kernel_stack_and_switch(SavedContext *ctxp, void *p1, void *p2);

SavedContext *copy_region_0(SavedContext *ctxp, void *p1, void *p2);

SavedContext *copy_region_0_and_switch(SavedContext *ctxp, void *p1, void *p2);

SavedContext *clean_and_switch(SavedContext *ctxp, void *p1, void *p2);

SavedContext *get_cur_ctx(SavedContext *ctxp, void *p1, void *p2);

SavedContext *save_wait_ret(SavedContext *ctxp, void *p1, void *p2);

#endif