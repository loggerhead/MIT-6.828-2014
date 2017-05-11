#include <inc/string.h>
#include <kern/pci.h>
#include <kern/e1000.h>
#include <kern/pmap.h>

// the location of the virtual memory mapping for the E1000's BAR 0
volatile uint32_t *attached_e1000;
struct tx_desc tx_ring[NTXDESC] __attribute__ ((aligned (16)));
packet_buffer tx_desc_buffers[NTXDESC];


#define tdtr_ptr (&attached_e1000[TDT])


inline int is_td_free(int index) {
	return (tx_ring[index].status & TDESC_STATUS_DD) == TDESC_STATUS_DD;
}

// LAB 6: Your driver code here

int pci_attach_82540em(struct pci_func *f) {
	pci_func_enable(f);
	attached_e1000 = mmio_map_region(f->reg_base[0], f->reg_size[0]);

	// Allocate a region of memory for the transmit descriptor list.
	// Software should insure this memory is aligned on a paragraph (16-byte) boundary.
	physaddr_t td_addr = PADDR(tx_ring);
	size_t td_len = sizeof(tx_ring);

	// The Transmit Descriptor Base Address of the region
	// NOTICE: physical address!
	attached_e1000[TDBAL] = td_addr;
	attached_e1000[TDBAH] = 0;

	// The size (in bytes) of the descriptor ring.
	// This value must be a multiple of 128 bytes
	attached_e1000[TDLEN] = td_len;

	// The Transmit Descriptor Head and Tail
	attached_e1000[TDH] = 0;
	attached_e1000[TDT] = 0;

	// Initialize the Transmit Control Register:
	//
	// * set the Enable (TCTL.EN) bit to 1b for normal operation.
	// * set the Pad Short Packets (TCTL.PSP) bit to 1b.
	// * configure the Collision Threshold (TCTL.CT) to the desired value (10h).
	// * configure the Collision Distance (TCTL.COLD) to 40h.
	attached_e1000[TCTL] = TCTL_EN(1) | TCTL_PSP(1) | TCTL_CT(0x10) | TCTL_COLD(0x40);

	// the Transmit IPG (TIPG) register
	attached_e1000[TIPG] = TIPG_IPGT(0xA) | TIPG_IPGR1(0x8) | TIPG_IPGR2(0xC);

	// init transmit descriptor ring
	memset(tx_ring, 0, sizeof(tx_ring));
	int i;
	for (i = 0; i < NTXDESC; i++) {
		tx_ring[i].addr = PADDR(tx_desc_buffers[i]);
		tx_ring[i].status = TDESC_STATUS_DD;
	}

	// TODO: debug
	int r = send_data_at("hello", 5);
	cprintf("send out %d bytes\n", r);
	return 0;
}

int send_data_at(void *addr, uint16_t len) {
	if (addr == NULL || len == 0) {
		return 0;
	}

	size_t n = len > PACKET_MAX_SIZE? PACKET_MAX_SIZE: len;

	int next = *tdtr_ptr;
	// checking that the next descriptor is free
	if (is_td_free(next)) {
		// copying the packet data into the next packet buffer
		void *buffer = KADDR(tx_ring[next].addr);
		memmove(buffer, addr, n);
		tx_ring[next].length = n;
		tx_ring[next].cmd |= TDESC_CMD_RS | TDESC_CMD_EOP;
		tx_ring[next].status ^= TDESC_STATUS_DD;
		// updating the TDT (transmit descriptor tail) register
		*tdtr_ptr = (next + 1) % NTXDESC;

		return n;
	// if the transmit queue is full
	} else {
		// simply drop the packet
		// TODO: consider use a meaningful return value
		return -1;
	}
}
