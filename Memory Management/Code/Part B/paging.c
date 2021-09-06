#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "paging.h"
#include "fs.h"


static pte_t * walkpgdir(pde_t *pgdir, const void *va, int alloc);
int deallocuvmXV6(pde_t *pgdir, uint oldsz, uint newsz);
static int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

struct proc*
myprocXV6(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

void
swap_page_from_pte(pte_t *pte,int pid)
{
  uint physicalAddress=PTE_ADDR(*pte);          
  if(physicalAddress==0)
    cprintf("physicalAddress address is zero\n");
  uint diskPage=balloc_page(ROOTDEV);

  write_page_to_disk(ROOTDEV,(char*)P2V(physicalAddress),diskPage,pid,pte);    //write this page to disk
 
  *pte = (*pte & 0x000000);     //make pte = null;
  *pte = (diskPage << 12)| PTE_SWAPPED;
  *pte = *pte & ~PTE_P;

  kfree(P2V(physicalAddress));
}

int
swap_page(pde_t *pgdir,int pid)
{
  pte_t* pte=select_a_victim(pgdir);        
  if(pte==0){                                     
    clearaccessbit(pgdir);                     

    cprintf("Finding victim again, after clearing access bits of 10%% pages.");
    pte=select_a_victim(pgdir);                  
  }

  swap_page_from_pte(pte,pid);  //swap victim page to disk
  lcr3(V2P(pgdir));        
	return 1;
}

void
map_address(pde_t *pgdir, uint addr,int pid)
{
	struct proc *curproc = myprocXV6();

	uint cursz= curproc->sz;
	uint a= PGROUNDDOWN(rcr2());			//rounds the address to a multiple of page size (PGSIZE)

  pte_t *pte=walkpgdir(pgdir, (char*)a, 0);
  int blockid=-1;                 //disk id where the page was swapped

	char *mem=kalloc();    //allocate a physical page

  if(mem==0){
    swap_page(pgdir,pid);
    mem=kalloc();          
	}

  if(pte!=0){
    if(*pte & PTE_SWAPPED){
      blockid=getswappedblk(pgdir,a);  
      read_page_from_disk(ROOTDEV, mem, blockid);

      *pte=V2P(mem) | PTE_W | PTE_U | PTE_P;
      *pte &= ~PTE_SWAPPED;
      lcr3(V2P(pgdir));
      bfree_page(ROOTDEV,blockid);
    }
    else{
      memset(mem,0,PGSIZE);
    	if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_P | PTE_W | PTE_U )<0){
    		panic("allocuvm out of memory xv6 in mappages/n");
    		deallocuvmXV6(pgdir,cursz+PGSIZE, cursz);
    		kfree(mem);
    	}
    }
  }
}

void
handle_pgfault()
{
	unsigned addr;
	struct proc *curproc = myprocXV6();

	asm volatile ("movl %%cr2, %0 \n\t" : "=r" (addr));
	addr &= ~0xfff;
	map_address(curproc->pgdir, addr,curproc->pid);
}

static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

int
deallocuvmXV6(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    // if(*pte & PTE_P)
    //   panic("remap in mappages in paging.c");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}
