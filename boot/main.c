#include <inc/x86.h>
#include <inc/elf.h>

/**********************************************************************
 * This a dirt simple boot loader, whose sole job is to boot
 * an ELF kernel image from the first IDE hard disk.
 *
 * DISK LAYOUT
 *  * This program(boot.S and main.c) is the bootloader.  It should
 *    be stored in the first sector of the disk.
 *
 *  * The 2nd sector onward holds the kernel image.
 *
 *  * The kernel image must be in ELF format.
 *
 * BOOT UP STEPS
 *  * when the CPU boots it loads the BIOS into memory and executes it
 *
 *  * the BIOS intializes devices, sets of the interrupt routines, and
 *    reads the first sector of the boot device(e.g., hard-drive)
 *    into memory and jumps to it.
 *
 *  * Assuming this boot loader is stored in the first sector of the
 *    hard-drive, this code takes over...
 *
 *  * control starts in boot.S -- which sets up protected mode,
 *    and a stack so C code then run, then calls bootmain()
 *
 *  * bootmain() in this file takes over, reads in the kernel and jumps to it.
 **********************************************************************/

#define SECTSIZE	512
#define ELFHDR		((struct Elf *) 0x10000) // scratch space

void readsect(void*, uint32_t);
void readseg(uint32_t, uint32_t, uint32_t);

void
bootmain(void)
{
	struct Proghdr *ph, *eph;

	// read 1st page off disk
	// 8*512=4096 from 0x10000 to 0x11000
	readseg((uint32_t) ELFHDR, SECTSIZE*8, 0);

	// is this a valid ELF?
	if (ELFHDR->e_magic != ELF_MAGIC)
		goto bad;

	// load each program segment (ignores ph flags)
	// mov 0x1001c,%ebx  =>  ebx = 0x34
	// ebx = 0x10000 + ebx
	ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
	// movzwl 0x1002c,%eax  =>  eax = 0x3
	// ELFHDR->e_phnum = eax << 5
	// esi = ebx + eax
	eph = ph + ELFHDR->e_phnum;
	// 0x10034 < 0x10094
	// ph++ => add $0x20,%ebx
	for (; ph < eph; ph++)
		// p_pa is the load address of this segment (as well
		// as the physical address)
		// 0x00100000, 0x0000739f, 0x00001000
		// 0x00108000, 0x0000a944, 0x00009000
		// 0x00000000, 0x00000000, 0x00000000
		readseg(ph->p_pa, ph->p_memsz, ph->p_offset);
	// loaded 3 segments

	// call the entry point from the ELF header
	// note: does not return!
	// 7d63:    call *0x10018
	((void (*)(void)) (ELFHDR->e_entry))();

bad:
	outw(0x8A00, 0x8A00);
	outw(0x8A00, 0x8E00);
	while (1)
		/* do nothing */;
}

// Read 'count' bytes at 'offset' from kernel into physical address 'pa'.
// Might copy more than asked
void
//			0x8,	0xc,  0x10
// readseg(0x10000, 0x1000, 0)
readseg(uint32_t pa, uint32_t count, uint32_t offset)
{
	uint32_t end_pa;
	// ebx = pa
	// edi = count
	// esi = offset

	// edi = ebx + edi;
	end_pa = pa + count;

	// round down to sector boundary
	pa &= ~(SECTSIZE - 1);

	// translate from bytes to sectors, and kernel starts at sector 1
	offset = (offset / SECTSIZE) + 1;

	// If this is too slow, we could read lots of sectors at a time.
	// We'd write more to memory than asked, but it doesn't matter --
	// we load in increasing order.
	// 0x10000 < 0x11000
	while (pa < end_pa) {
		// Since we haven't enabled paging yet and we're using
		// an identity segment mapping (see boot.S), we can
		// use physical addresses directly.  This won't be the
		// case once JOS enables the MMU.
		readsect((uint8_t*) pa, offset);
		// pa += 0x200;
		pa += SECTSIZE;
		offset++;
	}
}

void
waitdisk(void)
{
	// wait for disk reaady
	while ((inb(0x1F7) & 0xC0) != 0x40)
		/* do nothing */;
}

void
readsect(void *dst, uint32_t offset)
{
	// wait for disk to be ready
	waitdisk();

	outb(0x1F2, 1);		// count = 1
	outb(0x1F3, offset);
	outb(0x1F4, offset >> 8);
	outb(0x1F5, offset >> 16);
	outb(0x1F6, (offset >> 24) | 0xE0);
	outb(0x1F7, 0x20);	// cmd 0x20 - read sectors

	// wait for disk to be ready
	waitdisk();

	// read a sector
	//		  , 0x10000, 0x80
	insl(0x1F0, dst, SECTSIZE/4);
}

