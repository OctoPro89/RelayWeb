#pragma once

#include <qnet/common.h>
#include <qnet/netdefs.h>

typedef struct _qnet_ip_endpoint {
	qnet_ip_version ip_ver;
	char hostname[128];
	char ip_str[128];

	union {
		union {
			u8 ipv6[16];
		};
		union {
			u8 ipv4[4];
			u8 __pad[12];
		};
	} ip_bytes;

	u16 port;
} qnet_ip_endpoint;

QNETAPI qnet_ip_endpoint qnet_ip_endpoint_create(const char* ip, u16 port);