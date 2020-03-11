#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

typedef long long ll;

#define pn_to_addr(x) ((void *)((ll)(x) << PAGESHIFT))
#define addr_to_pn(x) ((ll)(x) >> PAGESHIFT)

//node of page table list, the list hold all the availble half pages for page tables 
struct ptl_node {
    int pfn;
    int which_half;
    struct ptl_node* nxt;
};

typedef struct ptl_node ptl_node;


/**
 * functions work on kernel's page table
 */
void set_pg_tb_entry(int vpn, int valid, int kprot, int uprot, int pfn);
void reset_pg_tb_entry(int vpn);

/** 
 * functions manage the free physical pages
 */
int get_fpl_size();
int grab_pg(int vpn, int kprot, int uprot);
void free_pg(int vpn);

/** 
 * functions operate on page table list
 */
void push_ptl(int pfn, int which_half);
ptl_node pop_ptl();
int add_ptl();


/**
 * functions used to perform context switch and fork
 */
void change_pt0(ptl_node pt_info);
int copy_kernel_stack(void *kernel_stack_cp, ptl_node* new_pt_info);
int copy_user_space(void *brk, void *sp, ptl_node* new_pt_info);

/**
 * implementation of SetKernelBrk
 */
int SetKernelBrk(void *addr);

/**
 * initialize the memory manager
 */
int init_memory_manager(void *org_brk, unsigned int pmem_size);


#endif