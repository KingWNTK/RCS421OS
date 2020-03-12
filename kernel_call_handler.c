#include "kernel_call_handler.h"
#include "load_program.h"
#undef LEVEL
#define LEVEL 5

void schedule_next() {
    //have to do ContextSwitch
    //or keep cur_pcb unchanged and restore cur_pcb's tick_exe to 2
    if (ready_q.size) {
        pcb *nxt = (pcb *)pop_front_q(&ready_q);
        nxt->tick_exe = 2;
        ContextSwitch(switch_ctx, &cur_pcb->ctx, cur_pcb, nxt);
    } else if (cur_pcb != idle_pcb) {
        ContextSwitch(switch_ctx, &cur_pcb->ctx, cur_pcb, idle_pcb);
    }
}

int handle_getpid() {
    return cur_pcb->pid;
}

int handle_fork() {
    pcb *child_pcb = (pcb *)malloc(sizeof(pcb));
    if (init_pcb(child_pcb) == -1) {
        //not enough memory
    }
    cur_pcb->tick_exe = 2;
    push_back_q(&ready_q, (void *)cur_pcb);
    pcb *pre_pcb = cur_pcb;
    ContextSwitch(copy_region_0_and_switch, &cur_pcb->ctx, cur_pcb, child_pcb);
    if (cur_pcb == pre_pcb) {
        print_topo(cur_pcb);
        return 0;
    } else {
        cur_pcb->parent = pre_pcb;
        print_topo(cur_pcb);
        return cur_pcb->pid;
    }
}

int handle_delay(int delay) {
    if (delay == 0)
        return 0;
    if (delay < 0)
        return ERROR;
    cur_pcb->tick_delay = delay;
    push_back_q(&delay_q, cur_pcb);
    schedule_next();
    return 0;
}

int handle_brk(void *addr) {
    addr = (void *)UP_TO_PAGE(addr);
    if (addr >= KERNEL_STACK_BASE)
        return ERROR;
    TracePrintf(LEVEL, "current brk: 0x%x\n", cur_pcb->brk);
    if (addr < cur_pcb->brk) {
        cur_pcb->brk -= PAGESIZE;
        while (cur_pcb->brk != addr) {
            free_pg(addr_to_pn(cur_pcb->brk));
            cur_pcb->brk -= PAGESIZE;
        }
        free_pg(addr_to_pn(cur_pcb->brk));
    } else {
        while (cur_pcb->brk != addr) {
            if (grab_pg(addr_to_pn(cur_pcb->brk), PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE) != -1) {
                cur_pcb->brk += PAGESIZE;
            } else {
                //not enough memory
                return ERROR;
            }
        }
    }
    return 0;
}

int handle_exec(char *filename, char **argvec, ExceptionInfo *info) {
    //TODO parameter check
    TracePrintf(LEVEL, "in exec\n");

    // char **argv;
    // int argc = 0;
    // while(argvec[argc] != NULL) {
    //     argc++;
    // }
    // argv = (char **)malloc((argc + 1) * sizeof(char *));
    // int i;
    // for(i = 0; i < argc; i++) {
    //     argv[i] = (char *)malloc(strlen(argvec[i]) + 1);
    //     memcpy(argv[i], argvec[i], strlen(argvec[i]) + 1);
    //     TracePrintf(LEVEL, "argv[%d]: %s\n", i, argvec[i]);
    // }
    // argv[argc] = NULL;

    //need to copy the name since LoadProgram is not buffering it
    char *name = (char *)malloc(strlen(filename) + 1);
    memcpy(name, filename, strlen(filename) + 1);
    TracePrintf(LEVEL, "before loading in exec\n");
    int ret = LoadProgram(filename, argvec, info, get_pt0(), cur_pcb);
    free(name);
    // for(i = 0; i < argc; i++) {
    //     free(argv[i]);
    // }
    // free(argv);
    return ret;
}

void handle_exit(int status) {
    if (cur_pcb->parent->waiting) {
        q_node *p = wait_q.head->nxt;
        while (p != wait_q.tail) {
            pcb *tmp = (pcb *)p->ptr;
            TracePrintf(LEVEL, "searching wait_q, pid: %d\n", tmp->pid);
            if (tmp == cur_pcb->parent) {
                TracePrintf(LEVEL, "waiting parent found\n");
                erase_before(p->nxt);
                void **args;
                args = (void **)malloc(sizeof(void *) * 2);
                exit_status *es = (exit_status *)malloc(sizeof(exit_status));
                es->pid = cur_pcb->pid;
                es->status = status;
                args[0] = tmp;
                args[1] = es;
                ContextSwitch(save_wait_ret, &cur_pcb->ctx, cur_pcb, args);
                free(es);
                free(args);
                push_back_q(&ready_q, tmp);
                break;
            }
            p = p->nxt;
        }
    } else {
        exit_status *es = (exit_status *)malloc(sizeof(exit_status));
        es->pid = cur_pcb->pid;
        es->parent_pid = cur_pcb->parent->pid;
        es->status = status;
        push_back_q(&exit_q, es);
    }
    //clear all the child processes of this process in the exit_q
    q_node *p = exit_q.head->nxt;
    while (p != exit_q.tail) {
        q_node *nxt = p->nxt;
        if (((exit_status *)p->ptr)->parent_pid == cur_pcb->pid) {
            free((exit_status *)p->ptr);
            erase_before(nxt);
        }
        p = nxt;
    }
    TracePrintf(LEVEL, "cleaning the exit process\n");
    //TODO free the resource used by this process
    pcb *pre_pcb = cur_pcb;
    pcb *nxt_pcb = ready_q.size ? (pcb *)pop_front_q(&ready_q) : idle_pcb;
    if (nxt_pcb != idle_pcb) {
        nxt_pcb->tick_exe = 2;
    }
    ContextSwitch(clean_and_switch, &pre_pcb->ctx, pre_pcb, nxt_pcb);
}

int handle_wait(int *status_ptr) {
    TracePrintf(LEVEL, "in handle wait\n");
    q_node *p = exit_q.head->nxt;
    while (p != exit_q.tail) {
        
        exit_status *tmp = (exit_status *)p->ptr;
        TracePrintf(LEVEL, "%d %d %d\n", tmp->pid, tmp->parent_pid, tmp->status);

        if (tmp->parent_pid == cur_pcb->pid) {
            erase_before(p->nxt);
            *status_ptr = tmp->status;
            return tmp->pid;
        }
        p = p->nxt;
    }

    //no exited child, put it to wait_q
    push_back_q(&wait_q, cur_pcb);
    cur_pcb->waiting = 1;
    schedule_next();
    cur_pcb->waiting = 0;
    return (int)cur_pcb->exp_info->regs[0];
}