#include <comp421/hardware.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "memory_manager.h"

#define VMEM_0_PN addr_to_pn(VMEM_0_LIMIT)

#undef LEVEL
#define LEVEL 15

//the initial page table of kernel
static struct pte pg_tb[PAGE_TABLE_LEN * 2];

static void *kernel_brk;

static bool vmem_enabled;

//head of the free page list
//its gauranteed to locate in region 1(both virtual memory and phys memory)
static int fpl_head_vpn;
static int fpl_size;

//virtual page number of page table 0
static int pt0_vpn;
static struct pte *pt0;
//head of the page table list
static ptl_node ptl_head;
static int ptl_size;

//reserve a whole page in case we need it
static int tmp_vpn_0, tmp_vpn_1;

void print_kernel_brk() {
    TracePrintf(LEVEL, "MEM_MGR: current kernel brk is: %d 0x%x\n", addr_to_pn(kernel_brk), kernel_brk);
}

struct pte *get_pt0() {
    return pt0;
}

void set_pg_tb_entry(int vpn, int valid, int kprot, int uprot, int pfn) {
    if (vpn < VMEM_0_PN) {
        pt0[vpn].valid = valid;
        pt0[vpn].kprot = kprot;
        pt0[vpn].uprot = uprot;
        pt0[vpn].pfn = pfn;
    } else {
        pg_tb[vpn].valid = valid;
        pg_tb[vpn].kprot = kprot;
        pg_tb[vpn].uprot = uprot;
        pg_tb[vpn].pfn = pfn;
    }
}

void reset_pg_tb_entry(int vpn) {
    if (vpn < VMEM_0_PN) {
        pt0[vpn].valid = 0;
    } else {
        pg_tb[vpn].valid = 0;
    }
}

int get_fpl_size() {
    return fpl_size;
}

//pop the free page list
void pop_fpl() {
    //no free page in head, do nothing
    if (fpl_size == 0) {
        return;
    }
    //make the fpl_head_vpn point to the next available phys page
    int nxt = addr_to_pn((void *)(*(ll *)pn_to_addr(fpl_head_vpn)));
    set_pg_tb_entry(fpl_head_vpn, 1, PROT_READ | PROT_WRITE, 0, nxt);
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(fpl_head_vpn));
    fpl_size--;
}

//push the phys page into free page list
void push_fpl(int pfn) {
    //if currently the list is empty, can't create a link between pages
    //simply set this page as the head
    if (fpl_size == 0) {
        fpl_size++;
        set_pg_tb_entry(fpl_head_vpn, 1, PROT_READ | PROT_WRITE, 0, pfn);
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(fpl_head_vpn));
        return;
    }
    fpl_size++;
    //get the phys addr of the next available phys page
    void *cur = pn_to_addr(pg_tb[fpl_head_vpn].pfn);
    //put the freed phys page in front of the cur in the list
    set_pg_tb_entry(fpl_head_vpn, 1, PROT_READ | PROT_WRITE, 0, pfn);
    *(ll *)pn_to_addr(fpl_head_vpn) = (ll)cur;
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(fpl_head_vpn));
}

//get the next free pfn, if there's no phys page left, return -1;
int get_free_pfn() {
    if (fpl_size == 0) {
        TracePrintf(LEVEL, "MEM_MGR: get free pfn failed, no more free phys pages\n");
        return -1;
    }
    return pg_tb[fpl_head_vpn].pfn;
}

//grab a page from the free page list
int grab_pg(int vpn, int kprot, int uprot) {
    //phys memory is full, return -1
    if (fpl_size == 0) {
        TracePrintf(LEVEL, "MEM_MGR: grab page failed, no more free phys pages\n");
        return -1;
    }
    //grab one free phys page and put it into pg_tb
    //put the current available phys page into pg_tb[vpn];
    TracePrintf(LEVEL, "MEM_MGR: grabed phys page: %d\n", pg_tb[fpl_head_vpn].pfn);
    set_pg_tb_entry(vpn, 1, kprot, uprot, pg_tb[fpl_head_vpn].pfn);
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(vpn));

    pop_fpl();

    return vpn < VMEM_0_PN ? pt0[vpn].pfn : pg_tb[vpn].pfn;
}

//free a page back to the free page list
void free_pg(int vpn) {
    //first free this page from the kernel page table
    struct pte *pt = vpn < VMEM_0_PN ? pt0 : pg_tb;
    if (!pt[vpn].valid) {
        return;
    }
    reset_pg_tb_entry(vpn);
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(vpn));

    TracePrintf(LEVEL, "MEM_MGR: freed phys page: %d\n", pt[vpn].pfn);
    push_fpl(pt[vpn].pfn);
}

//clear the pt0
void clear_pt0(ptl_node *pt_info) {
    //use the reserved tmp page to clear the page table
    set_pg_tb_entry(tmp_vpn_0, 1, PROT_READ | PROT_WRITE, 0, pt_info->pfn);
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(tmp_vpn_0));
    struct pte *pt = (struct pte *)(pt_info->which_half == 0 ? pn_to_addr(tmp_vpn_0) : pn_to_addr(tmp_vpn_0) + PAGE_TABLE_SIZE);
    memset(pt, 0, PAGE_TABLE_SIZE);
}

//push a node back into page table list
void push_ptl(int pfn, int which_half) {
    ptl_node *p = &ptl_head;
    ptl_node *pre = NULL;
    if (ptl_size > 1) {
        while (p->nxt) {
            pre = p;
            p = p->nxt;
            if (p->pfn == pfn) {
                //we have a whole free page, put it back to free page list
                push_fpl(pfn);
                //delete p from page table list
                pre->nxt = p->nxt;
                ptl_size--;
                free(p);
                return;
            }
        }
    }
    ptl_size++;
    ptl_node *nd = (ptl_node *)malloc(sizeof(ptl_node));
    nd->pfn = pfn;
    nd->which_half = which_half;
    //link it behind the head
    nd->nxt = ptl_head.nxt;
    ptl_head.nxt = nd;
}

//pop a node from the page table list
ptl_node pop_ptl() {
    ptl_node ret;
    if (!ptl_head.nxt) {
        //no more available ptl node, add a pair of ptl node
        if (add_ptl() == -1) {
            TracePrintf(LEVEL, "MEM_MGR: add new page table page failed \n");
            ret.pfn = ret.which_half = -1;
            ret.nxt = NULL;
            return ret;
        }
    }
    ptl_size--;
    ptl_node *next = ptl_head.nxt;
    ret.pfn = next->pfn;
    ret.which_half = next->which_half;
    ret.nxt = NULL;
    ptl_head.nxt = next->nxt;
    free(next);
    //must clear the page table
    clear_pt0(&ret);
    return ret;
}

//add a phys page to the page table list, if no phys page available, return -1;
int add_ptl() {
    int pfn = get_free_pfn();
    if (pfn == -1) {
        return -1;
    }
    push_ptl(pfn, 0);
    push_ptl(pfn, 1);
    //we have taken a phys page out, pop the fpl
    pop_fpl();
}

void print_ptl() {
    ptl_node *p = &ptl_head;
    TracePrintf(LEVEL, "ptl: ");
    while (p->nxt) {
        p = p->nxt;
        TracePrintf(LEVEL, "(%d, %d)->", p->pfn, p->which_half);
    }
    TracePrintf(LEVEL, "\n");
}

void init_ptl() {
    //do some initialization on page table list
    ptl_head.nxt = NULL;
    ptl_head.pfn = ptl_head.which_half = -1;
}

//must only be called inside ContextSwitch
void change_pt0(ptl_node pt_info) {
    if (pt_info.which_half == -1) {
        TracePrintf(LEVEL, "MEM_MGR: change pt0, new pt0's pfn is %d, which half is %d\n", pt_info.pfn, pt_info.which_half);
        //it is the idle process
        pt0 = pg_tb;
        WriteRegister(REG_PTR0, pg_tb);
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    } else {
        //it is some half of a phys page
        pg_tb[pt0_vpn].pfn = pt_info.pfn;
        TracePrintf(LEVEL, "MEM_MGR: change pt0, new pt0's pfn is %d, which half is %d\n", pt_info.pfn, pt_info.which_half);

        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(pt0_vpn));
        pt0 = pt_info.which_half == 0 ? pn_to_addr(pt0_vpn) : pn_to_addr(pt0_vpn) + PAGE_TABLE_SIZE;
        void *addr = pt_info.which_half == 0 ? pn_to_addr(pg_tb[pt0_vpn].pfn) : pn_to_addr(pg_tb[pt0_vpn].pfn) + PAGE_TABLE_SIZE;
        WriteRegister(REG_PTR0, addr);

        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    }
}

int copy_kernel_stack(void *kernel_stack_cp, ptl_node *new_pt_info) {
    TracePrintf(LEVEL, "MEM_MGR: copying current kernel stack\n");
    //use an entry in current pt0 to copy kernel stack
    pg_tb[tmp_vpn_0].pfn = new_pt_info->pfn;
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(tmp_vpn_0));
    struct pte *new_pt = (struct pte *)(new_pt_info->which_half == 0 ? pn_to_addr(tmp_vpn_0) : pn_to_addr(tmp_vpn_0) + PAGE_TABLE_SIZE);
    int i;
    for (i = 0; i < KERNEL_STACK_PAGES; i++) {
        //try to grab a free page
        if (grab_pg(tmp_vpn_1, PROT_READ | PROT_WRITE, 0) == -1) {
            return -1;
        }
        //copy this page of kernel stack into that free page
        memcpy(pn_to_addr(tmp_vpn_1), kernel_stack_cp + (i * PAGESIZE), PAGESIZE);
        //copy the pte into the new page table
        memcpy(new_pt + i + addr_to_pn(KERNEL_STACK_BASE), pg_tb + tmp_vpn_1, sizeof(struct pte));
    }

    return 0;
}

int copy_user_space(void *brk, void *stack_base, ptl_node *new_pt_info) {
    TracePrintf(LEVEL, "MEM_MGR: copying current user space\n");
    int brk_npg = addr_to_pn((void *)UP_TO_PAGE(brk));
    int stack_npg = addr_to_pn((void *)DOWN_TO_PAGE(stack_base));
    //use a temp page to copy all the stuff
    char *tmp = malloc(PAGESIZE);
    //map the new page table's pfn into tmp_vpn_0
    set_pg_tb_entry(tmp_vpn_0, 1, PROT_READ | PROT_WRITE, 0, new_pt_info->pfn);
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(tmp_vpn_0));

    struct pte *new_pt = (struct pte *)(new_pt_info->which_half == 0 ? pn_to_addr(tmp_vpn_0) : pn_to_addr(tmp_vpn_0) + PAGE_TABLE_SIZE);
    int i;
    for (i = MEM_INVALID_PAGES; i < addr_to_pn(USER_STACK_LIMIT); i++) {
        if(i == brk_npg) {
            i = stack_npg;
        }
        //grab a new phys page
        if (grab_pg(tmp_vpn_1, PROT_READ | PROT_WRITE, 0) == -1) {
            free(tmp);
            return -1;
        }
        //write data to the new phys page
        memcpy(pn_to_addr(tmp_vpn_1), pn_to_addr(i), PAGESIZE);
        //set the pte of the new page table
        memcpy(new_pt + i, pg_tb + tmp_vpn_1, sizeof(struct pte));
        (new_pt + i)->kprot = pt0[i].kprot;
        (new_pt + i)->uprot = pt0[i].uprot;
    }
    free(tmp);
    return 0;
}

int test_heap() {
    //we need at least some bytes availble in heap to terminate the current process
    void *p = malloc(64);
    if(p == NULL) {
        return -1;
    }
    free(p);
    return 0;
}

int SetKernelBrk(void *addr) {
    TracePrintf(LEVEL, "MEM_MGR: set kernel brk, new addr: 0x%x, number of pages needed: %d\n", addr, addr_to_pn(UP_TO_PAGE(addr - kernel_brk)));
    //if the requested addr is too large
    if (addr >= VMEM_1_LIMIT || addr <= kernel_brk)
        return -1;
    //ensure addr is multiple of page
    addr = (void *)UP_TO_PAGE(addr);
    if (vmem_enabled) {
        while (kernel_brk != addr) {
            if (grab_pg(addr_to_pn(kernel_brk), PROT_READ | PROT_WRITE, 0) != -1) {
                kernel_brk += PAGESIZE;
            } else {
                return -1;
            }
        }
    } else {
        int i;
        for (i = addr_to_pn(kernel_brk); i < addr_to_pn(addr); i++) {
            set_pg_tb_entry(i, 1, PROT_READ | PROT_WRITE, 0, i);
        }
        kernel_brk = addr;
    }
    print_kernel_brk();
    return 0;
}

int init_memory_manager(void *org_brk, unsigned int pmem_size) {
    kernel_brk = org_brk;

    memset(pg_tb, 0, sizeof(pg_tb));
    vmem_enabled = false;

    //initialize the pt0, which is going to vary among different processesr
    pt0 = pg_tb;
    WriteRegister(REG_PTR0, (RCS421RegVal)pg_tb);
    WriteRegister(REG_PTR1, (RCS421RegVal)(pg_tb + PAGE_TABLE_LEN));

    //init page table for kernel stack in region 0
    int itr = addr_to_pn(KERNEL_STACK_BASE);
    while (itr != addr_to_pn(KERNEL_STACK_LIMIT)) {
        set_pg_tb_entry(itr, 1, PROT_READ | PROT_WRITE, 0, itr);
        itr++;
    }

    //map the phys pages already used in region 1 to the same page number in virtual memory
    itr = addr_to_pn(VMEM_1_BASE);

    int brk_pn = addr_to_pn(kernel_brk);
    int text_pn = addr_to_pn(&_etext);
    while (itr != brk_pn) {
        if (itr < text_pn) {
            set_pg_tb_entry(itr, 1, PROT_READ | PROT_EXEC, 0, itr);
        } else {
            set_pg_tb_entry(itr, 1, PROT_READ | PROT_WRITE, 0, itr);
        }
        //make the itr point to the next page
        itr++;
    }


    //have to call malloc here to let malloc know that we want to reserve four vpn for use
    malloc(PAGESIZE * 4);
    //reserve this vpn as the list head
    fpl_head_vpn = itr;
    //reserve this vpn to read page table 0
    pt0_vpn = itr + 1;
    //reserve some vpn in case we need it
    tmp_vpn_0 = itr + 2;
    tmp_vpn_1 = itr + 3;


    //set the pfn to addr_to_pn(MEM_INVALID_SIZE), since it is currently the head
    set_pg_tb_entry(fpl_head_vpn, 1, PROT_READ | PROT_WRITE, 0, addr_to_pn(MEM_INVALID_SIZE));

    //finally we link all the unused phys pages together

    //first link pages from MEM_INVALID_SIZE to VMEM_0_LIMT
    //since we can't access address of 0 to MEM_INVALID_SIZE yet
    void *p = (void *)MEM_INVALID_SIZE;
    while ((ll)p + PAGESIZE < KERNEL_STACK_BASE) {
        *(ll *)p = (ll)p + PAGESIZE;
        p += PAGESIZE;
        fpl_size++;
    }
    //then link pages higher than the kernel break to phys_limt
    *(ll *)p = (ll)kernel_brk;
    p = kernel_brk;
    fpl_size++;
    ll pmem_limit = DOWN_TO_PAGE(pmem_size);
    while ((ll)p + PAGESIZE < pmem_limit) {
        *(ll *)p = (ll)p + PAGESIZE;
        p += PAGESIZE;
        fpl_size++;
    }
    //fpl_size should be the number of edges + 1
    fpl_size++;

    //we've done everying, enable virtual memory
    WriteRegister(REG_VM_ENABLE, (RCS421RegVal)1);
    vmem_enabled = true;

    //now we can access phys address 0 to MEM_INVALID_SIZE
    //with help of virtual memory, put them into the list
    p = (void *)MEM_INVALID_SIZE;
    do {
        p -= PAGESIZE;
        fpl_size++;
        int nxt = pg_tb[fpl_head_vpn].pfn;
        set_pg_tb_entry(fpl_head_vpn, 1, PROT_READ | PROT_WRITE, 0, addr_to_pn(p));
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(fpl_head_vpn));
        *(ll *)pn_to_addr(fpl_head_vpn) = (ll)pn_to_addr(nxt);
    } while (p > 0);

    init_ptl();

    return 0;
}
