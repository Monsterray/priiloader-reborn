/* stand-in for libogc's network.h: the socket calls HTTP_Parser makes. The fake net layer is in fakenet.cpp */
#ifndef STUB_NETWORK_H
#define STUB_NETWORK_H
#include "gctypes.h"

#define AF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_IP 0

typedef u32 socklen_t;

struct in_addr { u32 s_addr; };
struct sockaddr { u16 sa_family; char sa_data[14]; };
struct sockaddr_in {
	u8 sin_len;
	u8 sin_family;
	u16 sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};
struct hostent {
	char* h_name;
	char** h_aliases;
	u16 h_addrtype;
	u16 h_length;
	char** h_addr_list;
};

#ifdef __cplusplus
extern "C" {
#endif
s32 net_socket(u32 domain, u32 type, u32 protocol);
s32 net_connect(s32 s, struct sockaddr* addr, socklen_t addrlen);
s32 net_send(s32 s, const void* data, s32 size, u32 flags);
s32 net_recv(s32 s, void* mem, s32 len, u32 flags);
s32 net_close(s32 s);
struct hostent* net_gethostbyname(const char* addrString);
struct pollsd { s32 socket; u32 events; u32 revents; };
s32 net_poll(struct pollsd* sds, s32 nsds, s32 timeout);
#ifdef __cplusplus
}
#endif
#endif
