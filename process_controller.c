#include "process_controller.h"

#undef LEVEL
#define LEVEL 5

pcb *cur_pcb;

pcb *idle_pcb;

queue ready_q;

static unsigned int pid_max = 0;

/**
 * helper functions for operating the queue list 
 */
void insert_after(q_node *nd1, q_node *nd2) {
    if(!nd1 || !nd2) return;
    if(nd1->nxt) {
        nd1->nxt->pre = nd2;
    }
    nd2->nxt = nd1->nxt;
    nd1->nxt = nd2;
    nd2->pre = nd1;
}
void insert_before(q_node *nd1, q_node *nd2) {
    if(!nd1 || !nd2) return;
    if(nd1->pre) {
        nd1->pre->nxt = nd2;
    }
    nd2->pre = nd1->pre;
    nd1->pre = nd2;
    nd2->nxt = nd1;
}
void erase_after(q_node *nd) {
    q_node *nxt = nd->nxt;
    if(!nxt) return;
    if(nd->nxt->nxt) {
        nd->nxt->nxt->pre = nd;
        nd->nxt = nd->nxt->nxt;
    }
    else {
        nd->nxt = NULL;
    }
    free(nxt);
}
void erase_before(q_node *nd) {
    q_node *pre = nd->pre;
    if(!pre) return;
    if(nd->pre->pre) {
        nd->pre->pre->nxt = nd;
        nd->pre = nd->pre->pre;
    }
    else {
        nd->pre = NULL;
    }
    free(pre);
}

void init_q(queue *q) {
    if(q->head) {
        free(q->head);
    }
    if(q->tail) {
        free(q->tail);
    }
    q->head = (q_node *)malloc(sizeof(q_node));
    q->tail = (q_node *)malloc(sizeof(q_node));
    q->head->pre = q->head->pcb_ptr = q->tail->nxt = q->tail->pcb_ptr = NULL;
    q->head->nxt = q->tail;
    q->tail->pre = q->head;
    q->size = 0;
}

void push_q(int which, pcb *p) {
    q_node *nd = (q_node *)malloc(sizeof(q_node));
    nd->pcb_ptr = p;
    if(which == READY_Q) {
        insert_before(ready_q.tail, nd);
        ready_q.size++;
    }
    else {

    }
}

pcb *pop_q(int which) {
    if(which == READY_Q) {
        if(ready_q.size <= 0) return NULL;
        pcb *ret = ready_q.head->nxt->pcb_ptr;
        erase_after(ready_q.head);
        ready_q.size--;
        return ret;
    }
    else {

    }
}

int init_process_controller() {
    init_q(&ready_q);
}

int init_pcb(pcb *which) {
    which->pid = ++pid_max;
    ptl_node new_pt_info = pop_ptl();
    if(new_pt_info.nxt == -1) {
        //not enough phys pages
        //TODO
        return -1;
    }
    which->pt_info = new_pt_info;
    which->tick_exe = 2;
    return 0;
}

// void store_exp_info(ExceptionInfo *info) {
//     cur_pcb->sp = info->sp;
// }

// void restore_exp_info(ExceptionInfo *info) {
//     info->sp = cur_pcb->sp;
// }

SavedContext *switch_ctx(SavedContext *ctxp, void *p1, void *p2) {
    pcb *pcb1 = (pcb *)p1, *pcb2 = (pcb *)p2;
    cur_pcb = pcb2;
    change_pt0(pcb2->pt_info);
    return &pcb2->ctx;
}

SavedContext *copy_kernel_stack_and_switch(SavedContext *ctxp, void *p1, void *p2) {
    TracePrintf(LEVEL, "doing copy_kernel_stack_and_switch\n");
    //trying to get current process'es info
    pcb *pcb1 = (pcb *)p1, *pcb2 = (pcb *)p2;
    pcb2->ctx = pcb1->ctx;
    void *tmp = malloc(KERNEL_STACK_SIZE);
    memcpy(tmp, KERNEL_STACK_BASE, KERNEL_STACK_SIZE);
    //copy the kernel stack into p2
    copy_kernel_stack(tmp, &pcb2->pt_info);
    change_pt0(pcb2->pt_info);
    // TracePrintf(LEVEL, "change pt0 done\n");
    cur_pcb = pcb2;
    free(tmp);
    return &pcb2->ctx;
}

SavedContext *copy_region_0_and_switch(SavedContext *ctxp, void *p1, void *p2) {
    TracePrintf(LEVEL, "doing copy_region_0_and_switch\n");
    //trying to get current process'es info
    pcb *pcb1 = (pcb *)p1, *pcb2 = (pcb *)p2;
    pcb2->ctx = pcb1->ctx;
    pcb2->sp = pcb1->sp;
    pcb2->brk = pcb1->brk;
    void *tmp = malloc(KERNEL_STACK_SIZE);
    memcpy(tmp, KERNEL_STACK_BASE, KERNEL_STACK_SIZE);
    //copy the kernel stack into p2
    copy_kernel_stack(tmp, &pcb2->pt_info);
    // TracePrintf(LEVEL, "copy kernel stack done\n");
    // TracePrintf(LEVEL, "pcb1 brk: 0x%x, pcb2 sp: 0x%x\n", pcb1->brk, pcb1->sp);
    copy_user_space(pcb2->brk, pcb2->sp, &pcb2->pt_info);
    // TracePrintf(LEVEL, "copy user space done\n");
    change_pt0(pcb2->pt_info);
    // TracePrintf(LEVEL, "change pt0 done\n");
    cur_pcb = pcb2;
    free(tmp);
    return &pcb2->ctx;
}

SavedContext *get_cur_ctx(SavedContext *ctxp, void *p1, void *p2) {
    return ctxp;
}