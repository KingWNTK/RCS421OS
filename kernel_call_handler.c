#include <comp421/hardware.h>
#include <comp421/yalnix.h>

#include "load_program.h"
#include "memory_manager.h"
#include "trap_handler.h"
#include "process_controller.h"

#undef LEVEL
#define LEVEL 5

void schedule_next() {
    //have to do ContextSwitch
    //or keep cur_pcb unchanged and restore cur_pcb's tick_exe to 2
    if (ready_q.size) {
        pcb *nxt = (pcb *)pop_front_q(&ready_q);
        nxt->tick_exe = 2;
        if (ContextSwitch(switch_ctx, &cur_pcb->ctx, cur_pcb, nxt) == -1) {
            TracePrintf(LEVEL, "something wrong when calling ContextSwitch, going to halt\n");
            Halt();
        }
    } else if (cur_pcb != idle_pcb) {
        if (ContextSwitch(switch_ctx, &cur_pcb->ctx, cur_pcb, idle_pcb) == -1) {
            TracePrintf(LEVEL, "something wrong when calling ContextSwitch, going to halt\n");
            Halt();
        }
    }
}

int handle_getpid() {
    return cur_pcb->pid;
}

int handle_fork() {
    pcb *child_pcb = (pcb *)malloc(sizeof(pcb));
    if (init_pcb(child_pcb) == -1) {
        //not enough space for the new pcb
        return ERROR;
    }
    //update the child-parent relation
    child_pcb->parent = cur_pcb;
    push_back_q(&cur_pcb->children, child_pcb);
    //push the child process into ready q
    push_back_q(&ready_q, child_pcb);

    
    if (get_fpl_size() < 4 + addr_to_pn(UP_TO_PAGE((void *)USER_STACK_LIMIT - cur_pcb->stack_base)) + addr_to_pn(UP_TO_PAGE(cur_pcb->brk - MEM_INVALID_SIZE))) {
        //not enough space to fork the current process
        return ERROR;
    }

    if (ContextSwitch(copy_region_0, &cur_pcb->ctx, cur_pcb, child_pcb) == -1) {
        //not enough space for the new process
        TracePrintf(LEVEL, "something wrong when calling ContextSwitch, going to halt\n");
        Halt();
    }
    if (cur_pcb == child_pcb) {
        print_topo(cur_pcb);
        return 0;
    } else {
        print_topo(cur_pcb);
        return child_pcb->pid;
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
    if(addr < MEM_INVALID_SIZE) {
        //impossible
        return ERROR;
    }
    if (addr >= cur_pcb->stack_base - PAGESIZE) {
        //should not cross the red zone
        return ERROR;
    }
    if(addr > cur_pcb->brk && addr_to_pn(addr - cur_pcb->brk) > get_fpl_size()) {
        //don't have enough physical pages
        return ERROR;
    }
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
                //impossible, we can do nothing here
                TracePrintf(LEVEL, "error grabbing page when calling brk, going to halt\n");
                Halt();
            }
        }
    }
    return 0;
}

int handle_exec(char *filename, char **argvec, ExceptionInfo *info) {
    //TODO parameter check
    TracePrintf(LEVEL, "in exec\n");

    //need to copy the name since LoadProgram is not buffering it
    char *name = (char *)malloc(strlen(filename) + 1);
    memcpy(name, filename, strlen(filename) + 1);
    int ret = LoadProgram(filename, argvec, info, get_pt0(), cur_pcb);
    if (ret == -1) {
        //current process is still runnable
        free(name);
        return ERROR;
    } else if (ret == -2) {
        //current process is not runnable any more, need to terminate it
        free(name);
        handle_exit(ERROR);
        //calling the handle exit will erase the kernel stack, so the following line will not execute
        return ERROR;
    }
    free(name);
    return ret;
}

void handle_exit(int status) {
    if (cur_pcb->parent) {
        if (cur_pcb->parent->waiting) {
            if (find_and_erase_q(&wait_q, cur_pcb->parent) == -1) {
                //something wrong
                //TODO
            }
            pcb *pa = cur_pcb->parent;
            void **args;
            args = (void **)malloc(sizeof(void *) * 2);
            exit_status *es = (exit_status *)malloc(sizeof(exit_status));
            es->pid = cur_pcb->pid;
            es->status = status;
            args[0] = pa;
            args[1] = es;
            if (ContextSwitch(save_wait_ret, &cur_pcb->ctx, cur_pcb, args) == -1) {
                TracePrintf(LEVEL, "something wrong when calling ContextSwitch, going to halt\n");
                Halt();
            }
            free(es);
            free(args);
            push_back_q(&ready_q, pa);
        } else {
            exit_status *es = (exit_status *)malloc(sizeof(exit_status));
            es->pid = cur_pcb->pid;
            es->parent_pid = cur_pcb->parent->pid;
            es->status = status;
            push_back_q(&exit_q, es);
        }
    }
    pcb *nxt_pcb = ready_q.size ? (pcb *)pop_front_q(&ready_q) : idle_pcb;
    if (nxt_pcb != idle_pcb) {
        nxt_pcb->tick_exe = 2;
    }
    if (ContextSwitch(clean_and_switch, &cur_pcb->ctx, cur_pcb, nxt_pcb) == -1) {
        TracePrintf(LEVEL, "something wrong when calling ContextSwitch, going to halt\n");
        Halt();
    }
}

int handle_wait(int *status_ptr) {
    TracePrintf(LEVEL, "in handle wait\n");
    q_node *p = exit_q.head->nxt;
    while (p != exit_q.tail) {
        exit_status *tmp = (exit_status *)p->ptr;
        if (tmp->parent_pid == cur_pcb->pid) {
            erase_before(&exit_q, p->nxt);
            *status_ptr = tmp->status;
            return tmp->pid;
        }
        p = p->nxt;
    }
    //no exited child and no running child
    if (cur_pcb->children.size == 0) {
        return ERROR;
    }

    //no exited child, but there's running child put it to wait_q
    push_back_q(&wait_q, cur_pcb);
    cur_pcb->waiting = 1;
    schedule_next();
    //once its waken up, one of its child processes must have exited
    cur_pcb->waiting = 0;
    return (int)cur_pcb->exp_info->regs[0];
}

int handle_tty_read(int tty_id, void *buf, int len) {
    tty_buf *term = &tty[tty_id];
    // if (term->reading) {
    //     //if some process is reading, simply wait
    //     push_back_q(&tty_read_q[tty_id], cur_pcb);
    //     schedule_next();
    // }
    while (term->in_buf.size == 0) {
        //if there's nothing to read, go back to the the queue
        push_back_q(&tty_read_q[tty_id], cur_pcb);
        term->reading = 0;
        schedule_next();
    }
    term->reading = 0;
    //now we can read the terminal
    char *line = (char *)pop_front_q(&term->in_buf);
    int l = 0;
    while (l < len && line[l] != '\n' && line[l] != '\r') {
        l++;
    }
    l++;
    memcpy(buf, line, l);
    //remember to free this line
    free(line);

    if (tty_read_q[tty_id].size && term->in_buf.size) {
        //wake up a waiting process
        pcb *p = (pcb *)pop_front_q(&tty_read_q[tty_id]);
        push_back_q(&ready_q, p);
    }

    return l;
}

int handle_tty_write(int tty_id, void *buf, int len) {
    if(len == 0) return 0;

    tty_buf *term = &tty[tty_id];
    write_node *wn = (write_node *)malloc(sizeof(write_node));
    if(wn == NULL) {
        //run out of memory
        return ERROR; 
    }
    wn->p = cur_pcb;
    wn->buf = malloc(len);
    wn->len = len;
    if(wn->buf == NULL) {
        //run out of memory
        return ERROR;
    }
    memcpy(wn->buf, buf, len);
    push_back_q(&tty_write_q[tty_id], wn);
    if(!term->writing) {
        term->writing = 1;
        TtyTransmit(tty_id, wn->buf, len);
    }
    schedule_next();
    // if (term->writing) {
    //     //if some process is writing, simply wait
    //     push_back_q(&tty_write_q[tty_id], cur_pcb);
    //     schedule_next();
    // }
    // //now we can write to the termnial
    // term->writing = 1;
    // memcpy(term->out_buf, buf, len);
    // TtyTransmit(tty_id, term->out_buf, len);

    // //wait until finishing transmission
    // push_front_q(&tty_write_q[tty_id], cur_pcb);
    // schedule_next();

    // term->writing = 0;
    // if (tty_write_q[tty_id].size) {
    //     //wake up a waiting process
    //     pcb *p = (pcb *)pop_front_q(&tty_write_q[tty_id]);
    //     push_back_q(&ready_q, p);
    // }
    return 0;
}
