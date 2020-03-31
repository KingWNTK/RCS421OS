#include "process_controller.h"

#undef LEVEL
#define LEVEL 1

pcb *cur_pcb;

pcb *idle_pcb;

queue ready_q, delay_q, exit_q, wait_q, tty_read_q[NUM_TERMINALS], tty_write_q[NUM_TERMINALS];

static queue pid_q;

static unsigned int pid_max = 0;

static tot_process = 0;

/**
 * helper functions for operating the queue list 
 */
void insert_after(queue *q, q_node *nd1, q_node *nd2) {
    if (!nd1 || !nd2)
        return;
    if (nd1->nxt) {
        nd1->nxt->pre = nd2;
    }
    nd2->nxt = nd1->nxt;
    nd1->nxt = nd2;
    nd2->pre = nd1;
    q->size++;
}
void insert_before(queue *q, q_node *nd1, q_node *nd2) {
    if (!nd1 || !nd2)
        return;
    if (nd1->pre) {
        nd1->pre->nxt = nd2;
    }
    nd2->pre = nd1->pre;
    nd1->pre = nd2;
    nd2->nxt = nd1;
    q->size++;
}
void erase_after(queue *q, q_node *nd) {
    q_node *nxt = nd->nxt;
    if (!nxt)
        return;
    if (nd->nxt->nxt) {
        nd->nxt->nxt->pre = nd;
        nd->nxt = nd->nxt->nxt;
    } else {
        nd->nxt = NULL;
    }
    free(nxt);
    q->size--;
}
void erase_before(queue *q, q_node *nd) {
    q_node *pre = nd->pre;
    if (!pre)
        return;
    if (nd->pre->pre) {
        nd->pre->pre->nxt = nd;
        nd->pre = nd->pre->pre;
    } else {
        nd->pre = NULL;
    }
    free(pre);
    q->size--;
}
void init_q(queue *q) {
    q->head = (q_node *)malloc(sizeof(q_node));
    q->tail = (q_node *)malloc(sizeof(q_node));
    q->head->pre = q->head->ptr = q->tail->nxt = q->tail->ptr = NULL;
    q->head->nxt = q->tail;
    q->tail->pre = q->head;
    q->size = 0;
}
void free_q(queue *q) {
    q_node *p = q->head->nxt;
    while (p != q->tail) {
        q_node *nxt = p->nxt;
        erase_before(q, nxt);
        p = nxt;
    }
    free(q->head);
    free(q->tail);
}

int find_and_erase_q(queue *q, void *p) {
    q_node *pt = q->head->nxt;
    int found = -1;
    while (pt != q->tail) {
        if (pt->ptr == p) {
            erase_before(q, pt->nxt);
            found = 1;
            break;
        }
        pt = pt->nxt;
    }
    return found;
}
void push_back_q(queue *q, void *p) {
    q_node *nd = (q_node *)malloc(sizeof(q_node));
    nd->ptr = p;
    insert_before(q, q->tail, nd);
}
void push_front_q(queue *q, void *p) {
    q_node *nd = (q_node *)malloc(sizeof(q_node));
    nd->ptr = p;
    insert_after(q, q->head, nd);
}

void *pop_front_q(queue *q) {
    if (q->size <= 0)
        return NULL;
    void *ret = q->head->nxt->ptr;
    erase_after(q, q->head);
    return ret;
}

void maintain_delay_q() {
    if (delay_q.size == 0)
        return;
    q_node *p = delay_q.head->nxt;
    while (p != delay_q.tail) {
        pcb *tmp = (pcb *)p->ptr;
        q_node *nxt = p->nxt;
        tmp->tick_delay--;
        if (tmp->tick_delay == 0) {
            //always do erase first, since it will generate some memory
            erase_before(&delay_q, nxt);
            push_back_q(&ready_q, tmp);
        }
        p = nxt;
    }
}

void print_topo(pcb *cur) {
    TracePrintf(LEVEL, "self: %d\n", cur->pid);
    if (cur->parent) {
        TracePrintf(LEVEL, "parent: %d\n", cur->parent->pid);
    } else {
        TracePrintf(LEVEL, "No parent\n");
    }
    TracePrintf(LEVEL, "children number: %d\n", cur->children.size);
    q_node *p = cur->children.head->nxt;
    while (p != cur->children.tail) {
        TracePrintf(LEVEL, "child %d\n", ((pcb *)p->ptr)->pid);
        p = p->nxt;
    }
}

int grab_pid() {
    if (pid_q.size) {
        return (int)pop_front_q(&pid_q);
    } else {
        return ++pid_max;
    }
    return ++pid_max;
}

void free_pid(int pid) {
    push_back_q(&pid_q, (void *)pid);
}

int init_pcb(pcb *which) {
    tot_process++;
    which->pid = grab_pid();
    ptl_node new_pt_info = pop_ptl();
    if (new_pt_info.nxt == -1) {
        //not enough phys pages
        TracePrintf(LEVEL, "INIT_PCB: not enough memory\n");
        return -1;
    }
    which->pt_info = new_pt_info;
    which->tick_exe = 2;
    which->tick_delay = 0;
    which->parent = NULL;
    init_q(&which->children);
    which->waiting = 0;
    which->exp_info = NULL;
    return 0;
}

int init_process_controller() {
    init_q(&ready_q);
    init_q(&delay_q);
    init_q(&exit_q);
    init_q(&wait_q);
    init_q(&pid_q);
    int i;
    for (i = 0; i < NUM_TERMINALS; i++) {
        init_q(&tty_read_q[i]);
        init_q(&tty_write_q[i]);
    }
}

void store_exp_info(ExceptionInfo *info) {
    cur_pcb->exp_info = info;
    if (test_heap() == -1) {
        TracePrintf(LEVEL, "Running out of memory, going to halt\n");
        Halt();
    }
}

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
    //copy the kernel stack into p2
    if (copy_kernel_stack(KERNEL_STACK_BASE, &pcb2->pt_info) == -1) {
        //there's nothing we can do, will need to halt the machine
        TracePrintf(LEVEL, "CTX_SWITCH: Run out of memory when copying kernel stack, going to halt\n");
        Halt();
    }
    change_pt0(pcb2->pt_info);
    // TracePrintf(LEVEL, "change pt0 done\n");
    cur_pcb = pcb2;
    return &pcb2->ctx;
}

SavedContext *copy_region_0(SavedContext *ctxp, void *p1, void *p2) {
    TracePrintf(LEVEL, "doing copy_region_0\n");
    //trying to get current process'es info
    pcb *pcb1 = (pcb *)p1, *pcb2 = (pcb *)p2;
    pcb2->ctx = pcb1->ctx;
    pcb2->stack_base = pcb1->stack_base;
    pcb2->brk = pcb1->brk;

    //copy the kernel stack into p2
    if (copy_kernel_stack(KERNEL_STACK_BASE, &pcb2->pt_info) == -1) {
        //there's nothing we can do, will need to halt the machine
        TracePrintf(LEVEL, "CTX_SWITCH: Run out of memory when copying kernel stack, going to halt\n");
        Halt();
    }
    // TracePrintf(LEVEL, "copy kernel stack done\n");
    // TracePrintf(LEVEL, "pcb1 brk: 0x%x, pcb2 sp: 0x%x\n", pcb1->brk, pcb1->sp);
    if (copy_user_space(pcb2->brk, pcb2->stack_base, &pcb2->pt_info) == -1) {
        //there's nothing we can do, will need to halt the machine
        TracePrintf(LEVEL, "CTX_SWITCH: Run out of memory when copying user space, going to halt\n");
        Halt();
    }
    // TracePrintf(LEVEL, "copy user space done\n");
    // change_pt0(pcb2->pt_info);
    // cur_pcb = pcb2;
    return ctxp;
}

SavedContext *copy_region_0_and_switch(SavedContext *ctxp, void *p1, void *p2) {
    TracePrintf(LEVEL, "doing copy_region_0_and_switch\n");
    //trying to get current process'es info
    pcb *pcb1 = (pcb *)p1, *pcb2 = (pcb *)p2;
    pcb2->ctx = pcb1->ctx;
    pcb2->stack_base = pcb1->stack_base;
    pcb2->brk = pcb1->brk;
  
    //copy the kernel stack into p2
    if (copy_kernel_stack(KERNEL_STACK_BASE, &pcb2->pt_info) == -1) {
        //there's nothing we can do, will need to halt the machine
        TracePrintf(LEVEL, "CTX_SWITCH: Run out of memory when copying kernel stack, going to halt\n");
        Halt();
    }
    // TracePrintf(LEVEL, "copy kernel stack done\n");
    // TracePrintf(LEVEL, "pcb1 brk: 0x%x, pcb2 sp: 0x%x\n", pcb1->brk, pcb1->sp);
    if (copy_user_space(pcb2->brk, pcb2->stack_base, &pcb2->pt_info) == -1) {
        //there's nothing we can do, will need to halt the machine
        TracePrintf(LEVEL, "CTX_SWITCH: Run out of memory when copying user space, going to halt\n");
        Halt();
    }
    // TracePrintf(LEVEL, "copy user space done\n");
    change_pt0(pcb2->pt_info);
    // TracePrintf(LEVEL, "change pt0 done\n");
    cur_pcb = pcb2;
    return &pcb2->ctx;
}

SavedContext *clean_and_switch(SavedContext *ctxp, void *p1, void *p2) {
    TracePrintf(LEVEL, "cleaning process\n");
    pcb *pcb1 = (pcb *)p1, *pcb2 = (pcb *)p2;
    if (--tot_process == 0) {
        //there's no running process
        TracePrintf(LEVEL, "No more running process, going to halt\n");
        Halt();
    }
    //if pcb1 has a parent, delete pcb1 from its parent's children
    if (pcb1->parent) {
        if (find_and_erase_q(&pcb1->parent->children, pcb1) == -1) {
            //its impossible
            TracePrintf(LEVEL, "CTX_SWITCH: can't find current process in its parent process's children list, going to halt\n");
            Halt();
        }
    }
    //for every child process of pcb1, their parent should be set to NULL
    q_node *p = pcb1->children.head->nxt;
    while (p != pcb1->children.tail) {
        ((pcb *)(p->ptr))->parent = NULL;
        p = p->nxt;
    }

    //clear all the child exit status of this process in the exit_q
    p = exit_q.head->nxt;
    while (p != exit_q.tail) {
        q_node *nxt = p->nxt;
        if (((exit_status *)(p->ptr))->parent_pid == pcb1->pid) {
            free((exit_status *)(p->ptr));
            erase_before(&exit_q, nxt);
        }
        p = nxt;
    }

    //free every used page;
    int i;
    for (i = MEM_INVALID_SIZE; i < VMEM_0_LIMIT; i += PAGESIZE) {
        free_pg(addr_to_pn(i));
    }
    //free the used pid
    free_pid(pcb1->pid);

    //free the children q of pcb1
    free_q(&pcb1->children);

    //free the page table 0 used by pcb1
    push_ptl(pcb1->pt_info.pfn, pcb1->pt_info.which_half);
    //finally free the pcb1
    free(pcb1);
    cur_pcb = pcb2;
    change_pt0(pcb2->pt_info);
    return &pcb2->ctx;
}

SavedContext *save_wait_ret(SavedContext *ctxp, void *p1, void *p2) {
    pcb *pcb1 = (pcb *)p1;
    void **args = (void **)p2;
    pcb *pcb2 = (pcb *)args[0];
    exit_status *es = (exit_status *)args[1];
    change_pt0(pcb2->pt_info);
    pcb2->exp_info->regs[0] = (unsigned long)es->pid;
    *(int *)(pcb2->exp_info->regs[1]) = es->status;
    change_pt0(pcb1->pt_info);
    return ctxp;
}

SavedContext *get_cur_ctx(SavedContext *ctxp, void *p1, void *p2) {
    return ctxp;
}