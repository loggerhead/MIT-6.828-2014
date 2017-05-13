#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <inc/types.h>
#include <kern/pci.h>

#define _GEN_CONSTANTS(XX) \
	XX(TCTL  , 0x00400)			\
	XX(TDBAL , 0x03800)			\
	XX(TDBAH , 0x03804)			\
	XX(TDLEN , 0x03808)			\
	XX(TDH   , 0x03810)			\
	XX(TDT   , 0x03818)			\
	XX(TIPG  , 0x00410)			\
	XX(RCTL  , 0x00100)			\
	XX(RDBAL , 0x02800)			\
	XX(RDBAH , 0x02804)			\
	XX(RDLEN , 0x02808)			\
	XX(RDH   , 0x02810)			\
	XX(RDT   , 0x02818)			\
	XX(RAL_BASE, 0x05400)			\
	XX(RAH_BASE, 0x05404)			\
	XX(MTA_BASE, 0x05200)			\
	XX(IMS     , 0x000D0)

enum {
#define XX(NAME, OFFSET) NAME = ((OFFSET) / 4),
	  _GEN_CONSTANTS(XX)
#undef XX
};
#undef _GEN_CONSTANTS

#define RAL(i) (RAL_BASE + 8 * (i))
#define RAH(i) (RAH_BASE + 8 * (i))

// Transmit Control register
#define TCTL_EN(x)    ((x) << 1)
#define TCTL_PSP(x)   ((x) << 3)
#define TCTL_CT(x)    ((x) << 4)
#define TCTL_COLD(x)  ((x) << 12)
// Transmit IPG register
#define TIPG_IPGT(x)  (x)
#define TIPG_IPGR1(x) ((x) << 10)
#define TIPG_IPGR2(x) ((x) << 20)
// Receive Control register
#define RCTL_EN(x)    ((x) << 1)
#define RCTL_LPE(x)   ((x) << 5)
#define RCTL_LBM(x)   ((x) << 6)
#define RCTL_RDMTS(x) ((x) << 8)
#define RCTL_MO(x)    ((x) << 12)
#define RCTL_BAM(x)   ((x) << 15)
#define RCTL_BSIZE(x) ((x) << 16)
#define RCTL_SECRC(x) ((x) << 26)

#define TDESC_CMD_RS    (1 << 3)
#define TDESC_CMD_EOP   1
#define TDESC_STATUS_DD 1

#define NTXDESC 32
#define NRXDESC 256
#define TPACK_MAX_SIZE 1528
#define RPACK_MAX_SIZE 2048
#define MTA_SIZE (128 * 4)

// Legacy Transmit Descriptor format (spec section 3.3.3):
//
// 63            48 47   40 39   32 31   24 23   16 15             0
// +---------------------------------------------------------------+
// |                         Buffer address                        |
// +---------------+-------+-------+-------+-------+---------------+
// |    Special    |  CSS  | Status|  Cmd  |  CSO  |    Length     |
// +---------------+-------+-------+-------+-------+---------------+
struct tx_desc {
	uint64_t addr;
	uint16_t length;
	uint8_t cso;
	uint8_t cmd;
	uint8_t status;
	uint8_t css;
	uint16_t special;
} __attribute__((packed));

// Receive Descriptor format (spec section 3.2.3):
//
// 63            48 47   40 39   32 31           16 15             0
// +---------------------------------------------------------------+
// |                         Buffer address                        |
// +---------------+-------+-------+---------------+---------------+
// |    Special    | Errors| Status|Packet Checksum|    Length     |
// +---------------+-------+-------+---------------+---------------+
struct rx_desc {
	uint64_t addr;
	uint16_t length;
	uint16_t checksum;
	uint8_t status;
	uint8_t errors;
	uint16_t special;
} __attribute__((packed));


int pci_attach_82540em(struct pci_func *pcif);
int send_data_at(void *addr, uint16_t len);
int recv_data_at(void *addr, uint16_t len);

#endif	// JOS_KERN_E1000_H
