#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdio.h>

#include "memory_manager.h"
#include "trap_handler.h"
#include "load_program.h"
#include "process_controller.h"
/**
 * This kernel file contains the data structures 
 * and common functions the kernel
 */

// #define MEMORY_MANAGER_UNIT_TEST

#undef LEVEL
#define LEVEL 5

void test_memory_alloc() {
    printf("\n>>>>start testing memory alloc\ninit free pages: %d\n", get_fpl_size());
    char *t;
    t = (char *)malloc(100000);
    char *c = (char *)malloc(10000);
    free(t);
    t = (char *)malloc(10000);
    free(t);
    free(c);
    printf("current free pages: %d\n>>>>end testing memory alloc\n\n", get_fpl_size());
}

void test_grab_free() {
    printf("\n>>>>start testing grab free\n");
    int i = 0;
    for (i = 0; i < 10; i++) {
        grab_pg(600 + i, PROT_READ | PROT_WRITE, 0);
    }
    for (i = 0; i < 5; i++) {
        free_pg(600 + i);
    }
    for (i = 0; i < 10; i++) {
        grab_pg(700 + i, PROT_READ | PROT_WRITE, 0);
    }
    for (i = 0; i < 110; i++) {
        free_pg(600 + i);
    }
    printf(">>>>end testing grab free\n\n");
}

void test_ptl() {
    printf("\n>>>>start testing page table list\n");

    int i = 0;
    ptl_node nd[5];
    for (i = 0; i < 5; i++) {
        nd[i] = pop_ptl();
        printf("got: (%d, %d), ptl now looks like: ", nd[i].pfn, nd[i].which_half);
        print_ptl();
        printf("current fpl size: %d\n\n", get_fpl_size());
    }
    for (i = 0; i < 5; i += 2) {
        push_ptl(nd[i].pfn, nd[i].which_half);
        printf("pushed: (%d, %d), ptl now looks like: ", nd[i].pfn, nd[i].which_half);
        print_ptl();
        printf("current fpl size: %d\n\n", get_fpl_size());
    }
    for (i = 1; i < 5; i += 2) {
        push_ptl(nd[i].pfn, nd[i].which_half);
        printf("pushed: (%d, %d), ptl now looks like: ", nd[i].pfn, nd[i].which_half);
        print_ptl();
        printf("current fpl size: %d\n\n", get_fpl_size());
    }

    printf(">>>>end testing page table list\n\n");
}

void KernelStart(ExceptionInfo *info, unsigned int pmem_size, void *orig_brk, char **cmd_args) {
    TracePrintf(LEVEL, "kernel start called\n");

    init_memory_manager(orig_brk, pmem_size);
    TracePrintf(LEVEL, "init memory manager done\n");

    init_trap_handler();
    TracePrintf(LEVEL, "init trap handler done\n");

    init_process_controller();
    TracePrintf(LEVEL, "init process controller done\n");
#ifdef MEMORY_MANAGER_UNIT_TEST
    test_grab_free();
    test_memory_alloc();
    test_ptl();
#endif
    //now we should get ready to load the first program.
    //we first load idle, and use the current region 0 as its region 0
    char *argv[1];
    argv[0] = NULL;
    idle_pcb = (pcb *)malloc(sizeof(pcb));
    int ret = LoadProgram("idle", argv, info, get_pt0(), idle_pcb);
    if (ret != 0) {
        //can't even load the idle process, nothing we can do
        TracePrintf(LEVEL, "KERNEL_START: can't even load the idle process, nothing we can do, going to halt\n");
        Halt();
    }
    ptl_node idle_pt_info;
    idle_pt_info.pfn = idle_pt_info.which_half = -1;
    idle_pt_info.nxt = NULL;

    idle_pcb->pid = 0;
    idle_pcb->pt_info = idle_pt_info;
    idle_pcb->tick_exe = 0;

    //then we initialize init's pcb
    pcb *new_pcb = (pcb *)malloc(sizeof(pcb));
    if (init_pcb(new_pcb) == -1) {
        //don't even have space the init process's pcb, nothing we can do
        TracePrintf(LEVEL, "KERNEL_START: don't even have space the init process's pcb, nothing we can do, going to halt\n");
        Halt();
    }

    //copy the kernel stack of idle to init and context switch to init
    if (ContextSwitch(copy_kernel_stack_and_switch, &idle_pcb->ctx, idle_pcb, new_pcb) == -1) {
        TracePrintf(LEVEL, "something wrong when calling ContextSwitch, going to halt\n");
        Halt();
    }
    if (cur_pcb->pid == 0) {
        //it's in the idle process
    } else {
        //it's in the init process
        ret = LoadProgram("init", argv, info, get_pt0(), new_pcb);
        if (ret != 0) {
            //can't load the init process
            TracePrintf(LEVEL, "KERNEL_START: can't load the init process, going to halt\n");
            Halt();
        }
    }

    TracePrintf(LEVEL, "after KernelStart remaining available phys pages: %d\n", get_fpl_size());
}