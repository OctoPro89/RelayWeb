#include <qnet/socket.h>
#include <qnet/endpoint.h>
#include "internal.h"

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <WS2tcpip.h>

struct sockaddr_in ep_get_sockaddr_v4(qnet_ip_endpoint* ep);
struct sockaddr_in6 ep_get_sockaddr_v6(qnet_ip_endpoint* ep);
qnet_ip_endpoint ep_from_sockaddr(struct sockaddr* addr);

QNETAPI qnet_socket qnet_socket_create(qnet_ip_version ip_ver, qnet_socket_io_type io_type, qnet_socket_type type) {
	net_error = qnet_success;

	qnet_socket s;
	s.ip_ver = ip_ver;
	s.io_type = io_type;
	s.type = type;

	int family = (ip_ver == qnet_ip_ver_ipv4) ? AF_INET : AF_INET6;
	int sock_type = (type == qnet_socket_type_tcp) ? SOCK_STREAM : SOCK_DGRAM;
	int protocol = (type == qnet_socket_type_tcp) ? IPPROTO_TCP : IPPROTO_UDP;

	s.handle = socket(family, sock_type, protocol);

	if (s.handle == INVALID_SOCKET) {
		net_error = qnet_error_socket_creation_failed;
	}

	return s;
}

QNETAPI b8 qnet_socket_close(qnet_socket s) {
	net_error = qnet_success;

	if (s.handle == INVALID_SOCKET) {
		return false;
	}

	int result = closesocket(s.handle);
	if (result != 0) {
		net_error = qnet_error_socket_close_failed;
		return false;
	}

	return true;
}

QNETAPI b8 qnet_socket_set_option(qnet_socket s, qnet_socket_option option, b32 value) {
	net_error = qnet_success;

	int result = 0;
	switch (option) {
		case qnet_socket_option_tcp_nodelay: {
			result = setsockopt(s.handle, IPPROTO_TCP, TCP_NODELAY, (const char*)&value, sizeof(value));
			break;
		}
		case qnet_socket_option_ipv6_only: {
			result = setsockopt(s.handle, IPPROTO_TCP, IPV6_V6ONLY, (const char*)&value, sizeof(value));
			break;
		}
		case qnet_socket_option_nonblocking:
		case qnet_socket_option_blocking: {
			unsigned long non_blocking = 1;
			unsigned long blocking = 0;
			result = ioctlsocket(s.handle, FIONBIO, option == qnet_socket_option_blocking ? &blocking : &non_blocking);
			break;
		}
		default: {
			net_error = qnet_error_socket_option_invalid;
			break;
		}
	}

	if (result != 0) {
		net_error = qnet_error_socket_option_set_failed;
		return false;
	}

	return true;
}

QNETAPI b8 qnet_socket_bind(qnet_socket s, qnet_ip_endpoint* endpoint) {
	net_error = qnet_success;

	if (s.ip_ver != endpoint->ip_ver) {
		s.ip_ver = endpoint->ip_ver;
	}

	i32 result = 0;

	if (s.ip_ver == qnet_ip_ver_ipv4) {
		struct sockaddr_in addr = ep_get_sockaddr_v4(endpoint);
		result = bind(s.handle, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
	}
	else {
		struct sockaddr_in6 addr = ep_get_sockaddr_v6(endpoint);
		result = bind(s.handle, (struct sockaddr*)&addr, sizeof(struct sockaddr_in6));
	}

	if (result != 0) {
		net_error = qnet_error_socket_bind_failed;
		return false;
	}

	return true;
}

QNETAPI b8 qnet_socket_listen(qnet_socket s, qnet_ip_endpoint* endpoint, i32 backlog) {
	net_error = qnet_success;

	if (listen(s.handle, backlog) != 0) {
		net_error = qnet_error_socket_listen_failed_to_bind;
		return false;
	}

	return true;
}

QNETAPI b8 qnet_socket_accept(qnet_socket s, qnet_socket* out, qnet_ip_endpoint* endpoint) {
	net_error = qnet_success;

	if (s.ip_ver != endpoint->ip_ver) {
		s.ip_ver = endpoint->ip_ver;
	}

	if (s.ip_ver == qnet_ip_ver_ipv4) {
		struct sockaddr_in addr = { 0 };
		i32 len = sizeof(struct sockaddr_in);
		qnet_sock_handle accepted_handle = accept(s.handle, (struct sockaddr*)&addr, &len);
		if (accepted_handle == INVALID_SOCKET) {
			net_error = qnet_error_socket_accept_failed;
			return false;
		}

		if (endpoint != NULL) {
			*endpoint = ep_from_sockaddr((struct sockaddr*)&addr);
		}

		out->handle = accepted_handle;
		out->ip_ver = qnet_ip_ver_ipv4;
		out->io_type = s.io_type;
	}
	else { // IPv6
		struct sockaddr_in6 addr = { 0 };
		i32 len = sizeof(struct sockaddr_in6);
		qnet_sock_handle accepted_handle = accept(s.handle, (struct sockaddr*)&addr, &len);
		if (accepted_handle == INVALID_SOCKET) {
			net_error = qnet_error_socket_accept_failed;
			return false;
		}

		if (endpoint != NULL) {
			*endpoint = ep_from_sockaddr((struct sockaddr*)&addr);
		}

		out->handle = accepted_handle;
		out->ip_ver = qnet_ip_ver_ipv6;
		out->io_type = s.io_type;
		out->io_type = s.io_type;
	}

	return true;
}

QNETAPI b8 qnet_socket_connect(qnet_socket s, qnet_ip_endpoint* endpoint) {
	if (s.ip_ver != endpoint->ip_ver) {
		s.ip_ver = endpoint->ip_ver;
	}

	i32 result = 0;
	if (s.ip_ver == qnet_ip_ver_ipv4) {
		struct sockaddr_in addr = ep_get_sockaddr_v4(endpoint);
		result = connect(s.handle, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
	}
	else {
		struct sockaddr_in6 addr = ep_get_sockaddr_v6(endpoint);
		result = connect(s.handle, (struct sockaddr*)&addr, sizeof(struct sockaddr_in6));
	}

	return result == 0;
}

static b8 socket_send(qnet_socket s, const void* data, i32 number_of_bytes, i32* out_sent) {
	*out_sent = send(s.handle, (const char*)data, number_of_bytes, 0);
	return *out_sent != SOCKET_ERROR;
}

static b8 socket_recv(qnet_socket s, void* data, i32 number_of_bytes, i32* out_received) {
	*out_received = recv(s.handle, (char*)data, number_of_bytes, 0);
	return *out_received != SOCKET_ERROR;
}

static b8 socket_send_all(qnet_socket s, const void* data, i32 number_of_bytes) {
	i32 total_bytes_sent = 0;
	while (total_bytes_sent < number_of_bytes) {
		i32 bytes_remaining = number_of_bytes - total_bytes_sent;
		i32 bytes_sent = 0;
		char* buffer_offset = (char*)data + total_bytes_sent;
		if (!socket_send(s, buffer_offset, bytes_remaining, &bytes_sent)) {
			return false;
		}

		total_bytes_sent += bytes_sent;
	}

	return true;
}

static b8 socket_recv_all(qnet_socket s, void* data, i32 number_of_bytes) {
	i32 total_bytes_received = 0;
	while (total_bytes_received < number_of_bytes) {
		i32 bytes_remaining = number_of_bytes - total_bytes_received;
		i32 bytes_received = 0;
		char* buffer_offset = (char*)data + total_bytes_received;
		if (!socket_recv(s, buffer_offset, bytes_remaining, &bytes_received)) {
			return false;
		}

		total_bytes_received += bytes_received;
	}

	return true;
}

static b8 socket_send_to(qnet_socket s, qnet_ip_endpoint* to, const void* data, i32 number_of_bytes, i32* sent_bytes) {
	struct sockaddr_storage addr = { 0 };
	socklen_t addr_len = 0;

	if (to->ip_ver == qnet_ip_ver_ipv4) {
		struct sockaddr_in addr4 = ep_get_sockaddr_v4(to);
		memcpy(&addr, &addr4, sizeof(addr4));
		addr_len = sizeof(addr4);
	}
	else {
		struct sockaddr_in6 addr6 = ep_get_sockaddr_v6(to);
		memcpy(&addr, &addr6, sizeof(addr6));
		addr_len = sizeof(addr6);
	}

	*sent_bytes = sendto(s.handle, (const char*)data, number_of_bytes, 0, (struct sockaddr*)&addr, addr_len);

	return *sent_bytes != SOCKET_ERROR;
}

static b8 socket_recv_from(qnet_socket s, qnet_ip_endpoint* from, void* data, i32 number_of_bytes, i32* received_bytes) {
	struct sockaddr_storage addr = { 0 };
	socklen_t addr_len = sizeof(addr);

	*received_bytes = recvfrom(s.handle, (char*)data, number_of_bytes, 0, (struct sockaddr*)&addr, &addr_len);

	if (*received_bytes == SOCKET_ERROR) {
		return false;
	}

	if (from != NULL) {
		*from = ep_from_sockaddr((struct sockaddr*)&addr);
	}

	return true;
}

// do results
QNETAPI b8 qnet_socket_send(qnet_socket s, const void* data, i32 bytes, i32* out_sent, qnet_ip_endpoint* to) {
	net_error = qnet_success;
	b8 result = false;

	switch (s.io_type) {
		case qnet_socket_io_type_send_recv: {
			result = socket_send(s, data, bytes, out_sent);
			break;
		}
		case qnet_socket_io_type_sendall_recvall: {
			result = socket_send_all(s, data, bytes);
			if (out_sent) { *out_sent = bytes; }
			break;
		}
		case qnet_socket_io_type_sendto_recvfrom: {
			result = socket_send_to(s, to, data, bytes, out_sent);
			break;
		}
		default: {
			result = false;
			net_error = qnet_error_socket_io_type_invalid;
			break;
		}
	}

	if (!result) {
		net_error = qnet_error_socket_send_failed;
	}

	return result;
}

QNETAPI b8 qnet_socket_recv(qnet_socket s, void* data, i32 bytes, i32* out_received, qnet_ip_endpoint* from) {
	net_error = qnet_success;
	b8 result = false;

	switch (s.io_type) {
		case qnet_socket_io_type_send_recv: {
			result = socket_recv(s, data, bytes, out_received);
			break;
		}
		case qnet_socket_io_type_sendall_recvall: {
			result = socket_recv_all(s, data, bytes);
			if (out_received) { *out_received = bytes; }
			break;
		}
		case qnet_socket_io_type_sendto_recvfrom: {
			result = socket_recv_from(s, from, data, bytes, out_received);
			break;
		}
		default: {
			net_error = qnet_error_socket_io_type_invalid;
			result = false;
			break;
		}
	}

	if (!result) {
		net_error = qnet_error_socket_recv_failed;
	}

	return result;
}

QNETAPI b8 qnet_socket_send_packet(qnet_socket s, const void* data, i32 bytes, qnet_ip_endpoint* to) {
	net_error = qnet_success;
	u32 encoded_packet_size = htonl((u64)bytes);
	i32 sent = 0;
	b8 result = qnet_socket_send(s, &encoded_packet_size, sizeof(u32), &sent, to);
	if (!result) {
		net_error = qnet_error_socket_send_packet_failed;
		return false;
	}

	result = qnet_socket_send(s, data, bytes, &sent, to);
	if (!result) {
		net_error = qnet_error_socket_send_packet_failed;
		return false;
	}

	return true;
}

QNETAPI b8 qnet_socket_recv_packet(qnet_socket s, void* data, i32* out_bytes, qnet_ip_endpoint* from) {
	net_error = qnet_success;
	u32 encoded_packet_size = 0;
	i32 received = 0;
	b8 result = qnet_socket_recv(s, &encoded_packet_size, sizeof(u32), &received, from);
	if (!result) {
		net_error = qnet_error_socket_recv_packet_failed;
		return false;
	}

	u32 packet_size = ntohl(encoded_packet_size);

	result = qnet_socket_recv(s, data, packet_size, &received, from);
	if (!result) {
		net_error = qnet_error_socket_send_packet_failed;
		return false;
	}

	*out_bytes = (i32)packet_size;

	return true;
}

static WSAPOLLFD pollfds[QNET_MAX_POLLABLE_SOCKETS];
static qnet_socket_poll_events pollreturn[QNET_MAX_POLLABLE_SOCKETS];

QNETAPI i32 qnet_socket_poll(qnet_socket* sockets, i32 num_sockets, qnet_socket_poll_timeout timeout, qnet_socket_poll_events in_events, qnet_socket_poll_events* out_return_events) {
	net_error = qnet_success;
	
	for (i32 i = 0; i < num_sockets; ++i) {
		pollfds[i].fd = sockets[i].handle;
		pollfds[i].events = (i16)in_events;
		pollfds[i].revents = 0;
	}

	i32 result = WSAPoll(&pollfds[0], num_sockets, (i32)timeout);

	switch (result) {
		case 0: {
			net_error = qnet_error_socket_poll_timed_out;
			return 0;
		}
		case SOCKET_ERROR: {
			switch (WSAGetLastError()) {
				case WSAENETDOWN: {
					net_error = qnet_error_socket_poll_subsystem_failed;
					return -1;
				}
				case WSAEFAULT: {
					net_error = qnet_error_socket_poll_exception_occurred;
					return -1;
				}
				case WSAEINVAL: {
					net_error = qnet_error_socket_poll_invalid_parameter;
					return -1;
				}
				case WSAENOBUFS: {
					net_error = qnet_error_socket_poll_memory_allocation_failed;
					return -1;
				}
			}
			break;
		}
		default: {
			for (i32 i = 0; i < num_sockets; ++i) {
				pollreturn[i] = pollfds[i].revents;
			}

			*out_return_events = pollreturn[0];
		}
	}

	return result;
}