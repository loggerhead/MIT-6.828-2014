// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// TODO: why could we access PTEs though `uvpt`?

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if ((err & FEC_WR) != FEC_WR) {
		panic("pgfault failed: FEC_WR is not in the error code");
	}
	pte_t pte = uvpt[PGNUM(addr)];
	if ((pte & PTE_COW) != PTE_COW) {
		panic("pgfault failed: %p is not PTE_COW", addr);
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.

	// TODO: what's the difference among `curenv`, `thisenv`, and `&envs[0]`?
	// `inc/env.h`: "The envid_t == 0 stands for the current environment."
	envid_t envid = 0;
	int perm = PTE_W | PTE_U | PTE_P;

	r = sys_page_alloc(envid, (void *) PFTEMP, perm);
	if (r < 0) {
		panic("pgfault failed: sys_page_alloc failed");
	}

	void *addr_page_begin = ROUNDDOWN(addr, PGSIZE);
	memcpy((void *) PFTEMP, addr_page_begin, PGSIZE);

	r = sys_page_map(envid, (void *) PFTEMP, envid, addr_page_begin, perm);
	if (r < 0) {
		panic("pgfault failed: sys_page_map failed");
	}

	r = sys_page_unmap(envid, (void *) PFTEMP);
	if (r < 0) {
		panic("pgfault failed: sys_page_unmap failed");
	}
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	// LAB 4: Your code here.
	pte_t pte = uvpt[pn];
	void *va = (void *) (pn * PGSIZE);
	int perm = pte & 0xFFF;

	// use macro like a lambda function :P
#define SYS_PAGE_MAP(src_env_id, dst_env_id, va, perm) do { \
	    int r = sys_page_map(src_env_id, va, dst_env_id, va, perm); \
	    if (r < 0) { \
		    panic("duppage failed: sys_page_map(0x%x, %p, 0x%x, %p, 0x%x)", \
			  src_env_id, va, dst_env_id, va, perm); \
	    } \
	} while (0)

	if ((perm & PTE_W) == PTE_W || (perm & PTE_COW) == PTE_COW) {
		// the permission must be the below,
		// cannot be `perm = perm | PTE_COW`
		perm = PTE_COW | PTE_U | PTE_P;
		// order is important
		SYS_PAGE_MAP(thisenv->env_id, envid, va, perm);
		SYS_PAGE_MAP(thisenv->env_id, thisenv->env_id, va, perm);
	} else {
		SYS_PAGE_MAP(thisenv->env_id, envid, va, perm);
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
pfork(int priority)
{
	// LAB 4: Your code here.

	// Set up page fault handler
	set_pgfault_handler(pgfault);

	// Create a child.
	envid_t envid = sys_exofork();
	// child env will pause here util father env be freed,
	// because we don't have a auto scheduler currently
	if (envid < 0) {
		panic("fork failed: sys_exofork failed");
	}

	int r;
	// child
	if (envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		// Challenge: a fixed-priority scheduler
		sys_env_set_priority(priority);
		// cannot use `set_pgfault_handler(pgfault)` here,
		// because the static variable will cause a page fault
	} else {
		uintptr_t p;
		// minus one page to skip exception stack page
		for (p = 0; p < UTOP - PGSIZE; p += PGSIZE) {
			int pdx = PDX(p);
			int pgnum = PGNUM(p);
			// check permission to avoid page fault
			if ((uvpd[pdx] & PTE_P) == PTE_P && (uvpt[pgnum] & PTE_P) == PTE_P) {
				duppage(envid, pgnum);
			}
		}

		// alloc the exception stack to the child.
		int r = sys_page_alloc(envid, (void *) (UXSTACKTOP - PGSIZE), PTE_W | PTE_U | PTE_P);
		if (r < 0) {
			panic("fork failed: sys_page_alloc failed");
		}
		// page fault handler setup to the child.
		r = sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall);
		if (r < 0) {
			panic("fork failed: sys_env_set_pgfault_upcall failed");
		}
		// mark the child as runnable
		r = sys_env_set_status(envid, ENV_RUNNABLE);
		if (r < 0) {
			panic("fork failed: sys_env_set_status failed");
		}
	}

	return envid;
}

envid_t
fork(void)
{
	return pfork(0);
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
