// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
// Lab 2 challenge: finish `showmappings` function
#include <kern/pmap.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display the stack backtrace", mon_backtrace },
	{ "showmappings", "Display VA to PA mappings", mon_showmappings },
	{ "dumpva", "Display VA contents", mon_dumpva },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t ebp = read_ebp();
	cprintf("Stack backtrace:\n");

	while (ebp != 0) {
		uintptr_t eip = *(uintptr_t *)(ebp + 0x4);
		struct Eipdebuginfo info;
		debuginfo_eip(eip, &info);

		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n"
				"         %s:%d: %.*s+%d\n",
				ebp, eip,
				*(uint32_t *)(ebp + 0x8), *(uint32_t *)(ebp + 0xc),
				*(uint32_t *)(ebp + 0x10), *(uint32_t *)(ebp + 0x14),
				*(uint32_t *)(ebp + 0x18),
				info.eip_file, info.eip_line,
				info.eip_fn_namelen, info.eip_fn_name,
				eip - info.eip_fn_addr
		);

		ebp = *(uint32_t *)ebp;
	}

	return 0;
}

// Lab 2 challenge
int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 3) {
		cprintf("Usage: showmappings <begin_va> <end_va>\n");
		return 1;
	}

	char *tokens[] = {
		"P", "W", "U",
		"PWT", "PCD", "A",
		"D", "PS", "G"
	};

	uintptr_t begin = ROUNDDOWN(strtol(argv[1], NULL, 16), PGSIZE);
	uintptr_t end = ROUNDUP(strtol(argv[2], NULL, 16), PGSIZE);

	for (; begin <= end; begin += PGSIZE) {
		pte_t *ppte;
		page_lookup(kern_pgdir, (void *) begin, &ppte);

		if (ppte == NULL) {
			cprintf("0x%x not mapped\n", begin);
			continue;
		} else {
			cprintf("0x%x -> 0x%x: ", begin, PTE_ADDR(*ppte));
		}

		// print permissions
		int i;
		int perm = *ppte & 0x3FF;
		for (i = 0; perm > 0; i++) {
			if (perm & 1)
				cprintf("%s ", tokens[i]);
			perm = perm >> 1;
		}

		cprintf("\n");
	}

	return 0;
}

// Lab 2 challenge
int
mon_dumpva(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 3) {
		cprintf("Usage: dumpva <begin_va> <end_va>\n");
		return 1;
	}

	uintptr_t begin = strtol(argv[1], NULL, 16);
	uintptr_t end = strtol(argv[2], NULL, 16);

	for (; begin <= end; begin += 4) {
		pte_t *ppte;
		page_lookup(kern_pgdir, (void *) ROUNDDOWN(begin, PGSIZE), &ppte);

		if (ppte == NULL) {
			cprintf("0x%x: Cannot access memory\n", begin);
			begin = ROUNDDOWN(begin + PGSIZE, PGSIZE) - 4;
		} else {
			uint32_t content = *(uint32_t *) begin;
			cprintf("0x%x: 0x%x\n", begin, content);
		}
	}

	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
