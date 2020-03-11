#include <comp421/hardware.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "memory_manager.h"

#define VMEM_0_PN addr_to_pn(VMEM_0_LIMIT)


#undef LEVEL
#define LEVEL 5

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
static int tmp_vpn;


void print_kernel_brk() {
    TracePrintf(LEVEL, "kernel_brk is: %d 0x%x\n", addr_to_pn(kernel_brk), kernel_brk);
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
    if(vpn < VMEM_0_PN) {
        pt0[vpn].valid = 0;
    }
    else {
        pg_tb[vpn].valid = 0;
    }

}

int get_fpl_size() {
    return fpl_size;
}

//pop the free page list
void pop_fpl() {
    //no free page in head, do nothing
    if (get_fpl_size() == -1)
        return;
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
        return -1;
    }
    return pg_tb[fpl_head_vpn].pfn;
}

//grab a page from the free page list
int grab_pg(int vpn, int kprot, int uprot) {
    //phys memory is full, return -1
    if (fpl_size == 0) {
        return -1;
    }
    //grab one free phys page and put it into pg_tb
    //put the current available phys page into pg_tb[vpn];
    TracePrintf(LEVEL, "grabed phys page: %d\n", pg_tb[fpl_head_vpn].pfn);
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

    TracePrintf(LEVEL, "freed phys page: %d\n", pt[vpn].pfn);
    push_fpl(pt[vpn].pfn);
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

void clear_pt0(ptl_node *pt_info) {
    //use the reserved tmp page to clear the page table
    set_pg_tb_entry(tmp_vpn, 1, PROT_READ | PROT_WRITE, 0, pt_info->pfn);
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(tmp_vpn));
    struct pte *pt = (struct pte *)(pt_info->which_half == 0? pn_to_addr(tmp_vpn) : pn_to_addr(tmp_vpn) + PAGE_TABLE_SIZE);
    memset(pt, 0, PAGE_TABLE_SIZE);
}

//pop a node from the page table list
ptl_node pop_ptl() {
    ptl_node ret;
    if (!ptl_head.nxt) {
        //no more available ptl node, add a pair of ptl node
        if (add_ptl() == -1) {
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
        printf("(%d, %d)->", p->pfn, p->which_half);
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
    TracePrintf(LEVEL, "trying to change page table 0\n");
    if(pt_info.which_half == -1) {
        TracePrintf(LEVEL, "new page page table's pfn is %d, which half is %d\n", pt_info.pfn, pt_info.which_half);
        //it is the idle process
        pt0 = pg_tb;
        WriteRegister(REG_PTR0, pg_tb);
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    }
    else {
        //it is some half of a phys page
        pg_tb[pt0_vpn].pfn = pt_info.pfn;
        TracePrintf(LEVEL, "new page page table's pfn is %d, which half is %d\n", pt_info.pfn, pt_info.which_half);
        
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(pt0_vpn));
        pt0 = pt_info.which_half == 0 ? pn_to_addr(pt0_vpn) : pn_to_addr(pt0_vpn) + PAGE_TABLE_SIZE;
        void *addr = pt_info.which_half == 0 ? pn_to_addr(pg_tb[pt0_vpn].pfn) : pn_to_addr(pg_tb[pt0_vpn].pfn) + PAGE_TABLE_SIZE;
        WriteRegister(REG_PTR0, addr);

        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    }
}

int copy_kernel_stack(void *kernel_stack_cp, ptl_node* new_pt_info) {
    //use an entry in current pt0 to copy kernel stack
    pg_tb[tmp_vpn].pfn = new_pt_info->pfn;
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(tmp_vpn));
    struct pte *new_pt = (struct pte *)(new_pt_info->which_half == 0? pn_to_addr(tmp_vpn) : pn_to_addr(tmp_vpn) + PAGE_TABLE_SIZE);
    void *cur_addr = (void *)MEM_INVALID_SIZE;
    struct pte org_pte;
    memcpy(&org_pte, pt0 + MEM_INVALID_PAGES, sizeof(struct pte));
    int i;
    TracePrintf(LEVEL, "before actually copy kernel stack page\n");
    for(i = 0; i < KERNEL_STACK_PAGES; i++) {
        TracePrintf(LEVEL, "copying %dth kernel stack page\n", i);
        //try to grab a free page
        if(grab_pg(MEM_INVALID_PAGES, PROT_READ | PROT_WRITE, 0) == -1) {
            return -1;
        }
        //copy this page of kernel stack into that free page
        memcpy(cur_addr, kernel_stack_cp + (i * PAGESIZE), PAGESIZE);
        //copy the pte into the new page table
        memcpy(new_pt + i + addr_to_pn(KERNEL_STACK_BASE), pt0 + MEM_INVALID_PAGES, sizeof(struct pte));
    }
    memcpy(pt0 + MEM_INVALID_PAGES, &org_pte, sizeof(struct pte));
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(MEM_INVALID_PAGES));

    return 0;
}

int copy_user_space(void *brk, void *sp, ptl_node* new_pt_info) {
    int brk_npg = addr_to_pn((void *)UP_TO_PAGE(brk));
    int stack_npg = addr_to_pn((void *)DOWN_TO_PAGE(sp));
    //use a temp page to copy all the stuff
    char *tmp = malloc(PAGESIZE);
    //map the new page table's pfn into tmp_vpn
    set_pg_tb_entry(tmp_vpn, 1, PROT_READ | PROT_WRITE, 0, new_pt_info->pfn);
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pn_to_addr(tmp_vpn));

    struct pte *new_pt = (struct pte *)(new_pt_info->which_half == 0? pn_to_addr(tmp_vpn) : pn_to_addr(tmp_vpn) + PAGE_TABLE_SIZE);
    TracePrintf(LEVEL, "copy user space initialized\n");
    int i;
    for(i = MEM_INVALID_PAGES; i < addr_to_pn(USER_STACK_LIMIT); i++) {
        if(i == brk_npg) {
            i = stack_npg;
        }
        void *cur_addr = pn_to_addr(i);
        int cur_pfn = pt0[i].pfn;
        memcpy(tmp, cur_addr, PAGESIZE);
        //grab a new phys page
        if(grab_pg(i, pt0[i].kprot | PROT_WRITE, pt0[i].uprot) == -1) {
            free(tmp);
            return -1;
        }
        // TracePrintf(LEVEL, "kprot %d, uprot %d\n", pt0[i].kprot, pt0[i].uprot);     
        //write data to the new phys page
        memcpy(cur_addr, tmp, PAGESIZE);
        //set the pte of the new page table
        pt0[i].kprot = pt0[i].kprot ^ PROT_WRITE;
        memcpy(new_pt + i, pt0 + i, sizeof(struct pte));
        //finally restore the current pfn 
        pt0[i].pfn = cur_pfn;
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)cur_addr);
        //we've done copying everything below brk, and then we should copy the user stack
        
    }
    free(tmp);
    return 0;
}

int SetKernelBrk(void *addr) {
    TracePrintf(LEVEL, "set kernel brk called, new addr: 0x%x, page requested: %d\n", addr, addr_to_pn(UP_TO_PAGE(addr - kernel_brk)));
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
    TracePrintf(LEVEL, "set kernel brk done.\n\n");
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
    // printf("Before working on itr, itr is: %d, 0x%x\n", itr, pn_to_addr(itr));

    int brk_pn = addr_to_pn(kernel_brk);
    int text_pn = addr_to_pn(&_etext);
    // printf("&_etext is: %d, 0x%x\n", text_pn, &_etext);
    while (itr != brk_pn) {
        if (itr < text_pn) {
            set_pg_tb_entry(itr, 1, PROT_READ | PROT_EXEC, 0, itr);
        } else {
            set_pg_tb_entry(itr, 1, PROT_READ | PROT_WRITE, 0, itr);
        }
        //make the itr point to the next page
        itr++;
    }

    // printf("After working on itr, itr is now: 0x%x\n", pn_to_addr(itr));

    //have to call malloc here to let malloc know that we want to reserve three vpn for use
    malloc(PAGESIZE * 3);
    //reserve this vpn as the list head
    fpl_head_vpn = itr;
    //reserve this vpn to read page table 0
    pt0_vpn = itr + 1;
    //reserve this vpn in case we need it
    tmp_vpn = itr + 2;

    //set the pfn to addr_to_pn(MEM_INVALID_SIZE), since it is currently the head
    set_pg_tb_entry(fpl_head_vpn, 1, PROT_READ | PROT_WRITE, 0, addr_to_pn(MEM_INVALID_SIZE));

    //finally we link all the unused phys pages together

    //first link pages from MEM_INVALID_SIZE to VMEM_0_LIMT
    //since we can't access address of 0 to MEM_INVALID_SIZE yet
    void *p = (void *)MEM_INVALID_SIZE;
    while ((ll)p + PAGESIZE < VMEM_0_LIMIT) {
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
