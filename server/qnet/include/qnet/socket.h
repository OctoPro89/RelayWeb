#pragma once

#include <qnet/common.h>
#include <qnet/netdefs.h>

typedef struct {
	qnet_ip_version ip_ver;
	qnet_sock_handle handle;
	qnet_socket_type type;
	qnet_socket_io_type io_type;
} qnet_socket;

struct _qnet_ip_endpoint;
typedef struct _qnet_ip_endpoint qnet_ip_endpoint;

QNETAPI qnet_socket qnet_socket_create(qnet_ip_version ip_ver, qnet_socket_io_type io_type, qnet_socket_type type);
QNETAPI b8 qnet_socket_close(qnet_socket s);
QNETAPI b8 qnet_socket_set_option(qnet_socket s, qnet_socket_option option, b32 value);
QNETAPI b8 qnet_socket_bind(qnet_socket s, qnet_ip_endpoint* endpoint);
QNETAPI b8 qnet_socket_listen(qnet_socket s, qnet_ip_endpoint* endpoint, i32 backlog);
QNETAPI b8 qnet_socket_accept(qnet_socket s, qnet_socket* out, qnet_ip_endpoint* endpoint);
QNETAPI b8 qnet_socket_connect(qnet_socket s, qnet_ip_endpoint* endpoint);
QNETAPI b8 qnet_socket_send(qnet_socket s, const void* data, i32 bytes, i32* out_sent, qnet_ip_endpoint* to);
QNETAPI b8 qnet_socket_recv(qnet_socket s, void* data, i32 bytes, i32* out_received, qnet_ip_endpoint* from);
QNETAPI b8 qnet_socket_send_packet(qnet_socket s, const void* data, i32 bytes, qnet_ip_endpoint* to);
QNETAPI b8 qnet_socket_recv_packet(qnet_socket s, void* data, i32* out_bytes, qnet_ip_endpoint* from);
QNETAPI i32 qnet_socket_poll(qnet_socket* sockets, i32 num_sockets, qnet_socket_poll_timeout timeout, qnet_socket_poll_events in_events, qnet_socket_poll_events* out_return_events);