#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <inc/types.h>
#include <kern/pci.h>

// Transmit Descriptor
#define TCTL   (0x00400 / 4)
#define TDBAL  (0x03800 / 4)
#define TDBAH  (0x03804 / 4)
#define TDLEN  (0x03808 / 4)
#define TDH    (0x03810 / 4)
#define TDT    (0x03818 / 4)
#define TIPG   (0x00410 / 4)

// Transmit Control register
#define TCTL_EN(x)    ((x) << 1)
#define TCTL_PSP(x)   ((x) << 3)
#define TCTL_CT(x)    ((x) << 4)
#define TCTL_COLD(x)  ((x) << 12)

// Transmit IPG register
#define TIPG_IPGT(x)  (x)
#define TIPG_IPGR1(x) ((x) << 10)
#define TIPG_IPGR2(x) ((x) << 20)

#define TDESC_CMD_RS    (1 << 3)
#define TDESC_CMD_EOP   1
#define TDESC_STATUS_DD 1

#define NTXDESC 32
#define PACKET_MAX_SIZE 1528

typedef char packet_buffer[PACKET_MAX_SIZE];

// the Legacy Transmit Descriptor
//
// 63            48 47   40 39   32 31   24 23   16 15             0
// +---------------------------------------------------------------+
// |                         Buffer address                        |
// +---------------+-------+-------+-------+-------+---------------+
// |    Special    |  CSS  | Status|  Cmd  |  CSO  |    Length     |
// +---------------+-------+-------+-------+-------+---------------+
//
// spec 3.3.3 Legacy Transmit Descriptor Format
struct tx_desc {
	uint64_t addr;
	uint16_t length;
	uint8_t cso;
	uint8_t cmd;
	uint8_t status;
	uint8_t css;
	uint16_t special;
} __attribute__((packed));


int pci_attach_82540em(struct pci_func *pcif);
int send_data_at(void *addr, uint16_t len);

#endif	// JOS_KERN_E1000_H
