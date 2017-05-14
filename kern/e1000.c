#include <inc/error.h>
#include <inc/string.h>
#include <kern/pci.h>
#include <kern/e1000.h>
#include <kern/pmap.h>

// the location of the virtual memory mapping for the E1000's BAR 0
volatile uint32_t *attached_e1000;

// Allocate a region of memory for the transmit descriptor list.
// Software should insure this memory is aligned on a paragraph (16-byte) boundary.
struct tx_desc tx_ring[NTXDESC] __attribute__ ((aligned (16)));
char tx_desc_buffers[NTXDESC][TPACK_MAX_SIZE];

// Allocate a region of memory for the receive descriptor list.
// Software should insure this memory is aligned on a paragraph (16-byte) boundary.
struct rx_desc rx_ring[NRXDESC] __attribute__ ((aligned (16)));
char rx_desc_buffers[NTXDESC][RPACK_MAX_SIZE];

uint16_t mac_address[3];

#define TDTR_PTR (&attached_e1000[TDT])
#define RDTR_PTR (&attached_e1000[RDT])

void read_eeprom();

inline int is_td_free(int index) {
	return (tx_ring[index].status & TDESC_STATUS_DD) == TDESC_STATUS_DD;
}

inline int is_rd_used(int index) {
	return (rx_ring[index].status & RDESC_STATUS_DD) == RDESC_STATUS_DD;
}

inline int is_rd_eop(int index) {
	return (rx_ring[index].status & RDESC_STATUS_EOP) == RDESC_STATUS_EOP;
}

// simply hard-code QEMU's default MAC address of 52:54:00:12:34:56
inline void init_mac_address() {
	uint16_t x = 0x1234;
	// assert big endian
	assert(*(char *) &x == 0x34);

	// hard-code mac address
	// attached_e1000[RAL(0)] = 0x12005452;
	// attached_e1000[RAH(0)] = 0x00005634 | RAH_AV;

	// read mac address from EEPROM
	read_eeprom();
	attached_e1000[RAL(0)] = mac_address[0] | (mac_address[1] << 16);
	attached_e1000[RAH(0)] = mac_address[2] | RAH_AV;
}

void init_transmit_registers();
void init_tx_ring();
void init_receive_registers();
void init_rx_ring();


int pci_attach_82540em(struct pci_func *f) {
	pci_func_enable(f);
	attached_e1000 = mmio_map_region(f->reg_base[0], f->reg_size[0]);

	init_transmit_registers();
	// init transmit descriptor ring
	init_tx_ring();
	init_receive_registers();
	// init receive descriptor ring
	init_rx_ring();

	return 0;
}

void init_transmit_registers() {
	// The Transmit Descriptor Base Address of the region
	// NOTICE: physical address!
	attached_e1000[TDBAL] = PADDR(tx_ring);
	attached_e1000[TDBAH] = 0;
	// The size (in bytes) of the descriptor ring.
	// This value must be a multiple of 128 bytes
	attached_e1000[TDLEN] = sizeof(tx_ring);
	// Transmit Descriptor Head and Tail
	attached_e1000[TDH] = 0;
	attached_e1000[TDT] = 0;
	// Initialize the Transmit Control Register:
	//
	// * set the Enable (TCTL.EN) bit to 1b for normal operation.
	// * set the Pad Short Packets (TCTL.PSP) bit to 1b.
	// * configure the Collision Threshold (TCTL.CT) to the desired value (10h).
	// * configure the Collision Distance (TCTL.COLD) to 40h.
	attached_e1000[TCTL] = TCTL_EN(1)
		| TCTL_PSP(1)
		| TCTL_CT(0x10)
		| TCTL_COLD(0x40);
	// the Transmit IPG (TIPG) register
	attached_e1000[TIPG] = TIPG_IPGT(0xA)
		| TIPG_IPGR1(0x8)
		| TIPG_IPGR2(0xC);
}

void init_receive_registers() {
	// setup RAL[0]/RAH[0] to store the MAC address of the Ethernet controller.
	init_mac_address();
	// Initialize the MTA (Multicast Table Array) to 0b
	memset((void *) &attached_e1000[MTA_BASE], 0, MTA_SIZE);
	// For now, don't configure the card to use interrupts
	attached_e1000[IMS] = 0;
	// init the receive descriptor list
	attached_e1000[RDBAL] = PADDR(rx_ring);
	attached_e1000[RDBAH] = 0;
	attached_e1000[RDLEN] = sizeof(rx_ring);
	// Receive Descriptor Head and Tail
	// Head point to the first valid receive descriptor
	attached_e1000[RDH] = 0;
	// Tail point to one descriptor beyond the last valid descriptor
	attached_e1000[RDT] = NRXDESC;
	// Receive Control (RCTL) register
	attached_e1000[RCTL] = RCTL_EN(1)
		| RCTL_LPE(0)
		| RCTL_LBM(0)
		| RCTL_BSIZE(0)
		| RCTL_SECRC(1);
}

void init_tx_ring() {
	memset(tx_ring, 0, sizeof(tx_ring));
	int i;
	for (i = 0; i < NTXDESC; i++) {
		tx_ring[i].addr = PADDR(tx_desc_buffers[i]);
		tx_ring[i].status = TDESC_STATUS_DD;
	}
}

int send_data_at(void *addr, uint16_t len) {
	if (addr == NULL || len == 0) {
		return 0;
	}

	int next = *TDTR_PTR;
	int remain = len;

	do {
		// checking that the next descriptor is free
		if (is_td_free(next)) {
			char *buffer = KADDR(tx_ring[next].addr);
			size_t n = MIN(TPACK_MAX_SIZE, remain);

			// copying the packet data into the next packet buffer
			memmove(buffer, addr + len - remain, n);
			tx_ring[next].length = n;
			remain -= n;

			if (remain > 0) {
				tx_ring[next].cmd |= TDESC_CMD_RS;
			} else {
				tx_ring[next].cmd |= TDESC_CMD_RS | TDESC_CMD_EOP;
			}
			tx_ring[next].status ^= TDESC_STATUS_DD;

			// updating the TDT (transmit descriptor tail) register
			*TDTR_PTR = (next + 1) % NTXDESC;
			// if the transmit queue is full
		} else {
			// simply drop the packet
			return -E_SEND_QUEUE_FULL;
		}
	} while (remain > 0);

	return len;
}

void init_rx_ring() {
	memset(rx_ring, 0, sizeof(rx_ring));
	int i;
	for (i = 0; i < NRXDESC; i++) {
		rx_ring[i].addr = PADDR(rx_desc_buffers[i]);
	}
}

// return received length or error code
int recv_data_at(void *addr, uint16_t len, struct recv_res *res) {
	if (addr == NULL || len == 0) {
		return 0;
	}

	int next = *RDTR_PTR % NRXDESC;
	int nread = 0;

	do {
		if (is_rd_used(next)) {
			char *buffer = KADDR(rx_ring[next].addr);
			size_t n = MIN(rx_ring[next].length - res->offset, len - res->nread);

			memmove(addr + res->nread, buffer + res->offset, n);
			nread += n;
			res->nread += n;
			res->offset += n;

			// if not consume entire buffer
			if (res->offset >= rx_ring[next].length) {
				res->is_eop = is_rd_eop(next);
				res->offset = 0;
				rx_ring[next].status = 0;
				rx_ring[next].length = 0;
				*RDTR_PTR = (next + 1) % NRXDESC;
			}
		} else {
			return -E_RECV_QUEUE_EMPTY;
		}
	} while (res->nread < len && !res->is_eop);

	return nread;
}

// Challenge: load the E1000's MAC address out of the EEPROM
void read_eeprom() {
	int i;
	for (i = 0x00; i <= 0x02; i++) {
		attached_e1000[EERD] = EERD_ADDR(i) | EERD_START(1);
		while (EERD_DONE(1) != (attached_e1000[EERD] & EERD_DONE(1))) {
			;
		}
		mac_address[i] = attached_e1000[EERD] >> 16;
	}
}
