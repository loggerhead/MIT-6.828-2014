#include "ns.h"

extern union Nsipc nsipcbuf;

#define NBUFF (PTSIZE / PGSIZE)

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	int r;
	int perm = PTE_P | PTE_W | PTE_U;
	char buffer[RPACK_MAX_SIZE];

#define SEND() do {                                                   \
	if ((r = sys_page_alloc(0, &nsipcbuf, perm)) < 0)             \
		panic("input failed: sys_page_alloc failed %e\n", r); \
	memmove(nsipcbuf.pkt.jp_data, buffer, res.nread);             \
	nsipcbuf.pkt.jp_len = res.nread;                              \
	ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, perm);             \
} while (0)

	for (;;) {
		struct recv_res res = { 0 };

		while (!res.is_eop) {
			while (sys_recv_data_at(buffer, PGSIZE, &res) < 0) {
				sys_yield();
			}

			if (res.nread == PGSIZE) {
				res.nread = 0;
				SEND();
			}
		}

		if (res.nread) {
			SEND();
		}
	}
}
