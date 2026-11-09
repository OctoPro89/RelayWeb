#pragma once

typedef enum {
	qnet_ip_ver_ipv4 = 0,
	qnet_ip_ver_ipv6
} qnet_ip_version;

typedef enum {
	qnet_socket_option_tcp_nodelay = 0, // if enabled disables nagle's algorithm, recommended for tcp sockets
	qnet_socket_option_ipv6_only, // only allow ipv6 connections
	qnet_socket_option_blocking, // set socket to block (default)
	qnet_socket_option_nonblocking // set socket to not block
} qnet_socket_option;

typedef enum {
	qnet_socket_type_tcp = 0,
	qnet_socket_type_udp
} qnet_socket_type;

typedef enum {
	qnet_socket_io_type_sendall_recvall, // safest manner to send data, used with TCP 
	qnet_socket_io_type_send_recv, // may drop bytes, used with TCP
	qnet_socket_io_type_sendto_recvfrom, // used with UDP sockets
} qnet_socket_io_type;

typedef enum {
	qnet_socket_poll_events_read_band = 0x200, // priority band data can be read without blocking
	qnet_socket_poll_events_read = 0x100, // normal data can be read without blocking
	qnet_socket_poll_events_write = 0x0010, // normal data can be written without blocking
	qnet_socket_poll_events_in = qnet_socket_poll_events_read | qnet_socket_poll_events_read_band,

	qnet_socket_poll_events_err = 0x0001, // error occurred
	qnet_socket_poll_events_hup = 0x0002, // a stream-oriented connection was disconnected or aborted
	qnet_socket_poll_events_nval = 0x0004, // an invalid socket was used
} qnet_socket_poll_events;

typedef enum {
	qnet_socket_poll_timeout_wait_indefinitely = -1,
	qnet_socket_poll_timeout_return_immediately = 0,
	qnet_socket_poll_timeout_wait_1ms = 1
} qnet_socket_poll_timeout;

typedef unsigned long long qnet_sock_handle;
#define qnet_invalid_socket (qnet_sock_handle)~0