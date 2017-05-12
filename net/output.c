#include "ns.h"
#include "kern/e1000.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	for (;;) {
		int r = ipc_recv(NULL, &nsipcbuf, NULL);
		if (r < 0) {
			panic("output failed: ipc_recv failed with %e", r);
		}
		void *addr = nsipcbuf.pkt.jp_data;
		uint16_t len = nsipcbuf.pkt.jp_len;
		r = sys_send_data_at(addr, len);
		if (r < 0) {
			panic("output failed: sys_send_data_at failed with %e", r);
		}
	}
}
