#include <qnet/endpoint.h>
#include "internal.h"

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <ws2tcpip.h>

QNETAPI qnet_ip_endpoint qnet_ip_endpoint_create(const char* ip, u16 port) {
	net_error = qnet_success;

	qnet_ip_endpoint ep;
	ep.port = port;

	// IPv4
	struct in_addr addr;
	int result = inet_pton(AF_INET, ip, &addr);

	if (result == 1) {
		if (addr.S_un.S_addr != INADDR_NONE) {
			size_t len = strnlen(ip, 127);

			strncpy(&ep.ip_str[0], ip, len);
			ep.ip_str[len + 1] = '\0';

			strncpy(&ep.hostname[0], ip, len);
			ep.hostname[len + 1] = '\0';

			memcpy(&ep.ip_bytes.ipv4[0], &addr.S_un.S_addr, 4);

			ep.ip_ver = qnet_ip_ver_ipv4;
			return ep;
		}
	}

	// Attempt to resolve hostname to IPv4 address
	struct addrinfo hints = { 0 }; // This filters back result
	hints.ai_family = AF_INET; // IPv4 addresses only
	struct addrinfo* hostinfo = NULL;
	result = getaddrinfo(ip, NULL, &hints, &hostinfo);

	if (result == 0) {
		struct sockaddr_in* host_addr = (struct sockaddr_in*)hostinfo->ai_addr;

		inet_ntop(AF_INET, &host_addr->sin_addr, &ep.ip_str[0], 16);
		ep.ip_str[17] = '\0';

		size_t len = strnlen(ip, 127);
		strncpy(&ep.hostname[0], ip, len);
		ep.hostname[len + 1] = '\0';

		ULONG ip_long = host_addr->sin_addr.S_un.S_addr; // Get IP address as unsigned long
		memcpy(&ep.ip_bytes.ipv4[0], &ip_long, sizeof(ULONG));

		ep.ip_ver = qnet_ip_ver_ipv4;

		freeaddrinfo(hostinfo);
		return ep;
	}

	// IPv6
	struct in6_addr addr6 = { 0 };
	result = inet_pton(AF_INET6, ip, &addr6);

	if (result == 1) {
		size_t len = strnlen(ip, 127);

		strncpy(&ep.ip_str[0], ip, len);
		ep.ip_str[len + 1] = '\0';

		strncpy(&ep.hostname[0], ip, len);
		ep.hostname[len + 1] = '\0';

		memcpy(&ep.ip_bytes.ipv6[0], &addr6.u, 16);

		ep.ip_ver = qnet_ip_ver_ipv6;
		return ep;
	}

	// Attempt to resolve hostname to IPv6 address
	struct addrinfo hintsv6 = { 0 };
	hints.ai_family = AF_INET6; // IPv6 addresses only
	struct addrinfo* hostinfov6 = NULL;
	result = getaddrinfo(ip, NULL, &hintsv6, &hostinfov6);
	if (result == 0) {
		struct sockaddr_in6* host_addr = (struct sockaddr_in6*)hostinfov6->ai_addr;
		inet_ntop(AF_INET6, &host_addr->sin6_addr, &ep.ip_str[0], 46);

		size_t len = strnlen(ip, 127);
		strncpy(&ep.hostname[0], ip, len);
		ep.hostname[len + 1] = '\0';

		memcpy(&ep.ip_bytes.ipv6[0], &host_addr->sin6_addr, 16);

		ep.ip_ver = qnet_ip_ver_ipv6;

		freeaddrinfo(hostinfov6);
		return ep;
	}

	// error
	net_error = qnet_error_endpoint_failed_to_resolve;
	return ep;
}

struct sockaddr_in ep_get_sockaddr_v4(qnet_ip_endpoint* ep) {
	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	memcpy(&addr.sin_addr, &ep->ip_bytes.ipv4[0], 4);
	addr.sin_port = htons(ep->port);
	return addr;
}

struct sockaddr_in6 ep_get_sockaddr_v6(qnet_ip_endpoint* ep) {
	struct sockaddr_in6 addr = { 0 };
	addr.sin6_family = AF_INET6;
	memcpy(&addr.sin6_addr, &ep->ip_bytes.ipv6[0], 16);
	addr.sin6_port = htons(ep->port);
	return addr;
}

qnet_ip_endpoint ep_from_sockaddr(struct sockaddr* addr) {
	if (!(addr->sa_family == AF_INET || addr->sa_family == AF_INET6)) {
		return (qnet_ip_endpoint){0};
	}

	qnet_ip_endpoint ep = { 0 };

	if (addr->sa_family == AF_INET) { // IPv4
		struct sockaddr_in* v4 = (struct sockaddr_in*)addr;
		inet_ntop(AF_INET, &v4->sin_addr, &ep.ip_str[0], 16);
		ep.ip_str[17] = '\0';

		ep.port = ntohs(v4->sin_port);

		strncpy(&ep.hostname[0], &ep.ip_str[0], 127);

		ULONG ip_long = v4->sin_addr.S_un.S_addr; // Get IP address as unsigned long
		memcpy(&ep.ip_bytes.ipv4[0], &ip_long, sizeof(ULONG));

		ep.ip_ver = qnet_ip_ver_ipv4;
	}
	else { // IPv6
		struct sockaddr_in6* host_addr = (struct sockaddr_in6*)addr;
		inet_ntop(AF_INET6, &host_addr->sin6_addr, &ep.ip_str[0], 46);

		ep.port = ntohs(host_addr->sin6_port);

		strncpy(&ep.hostname[0], &ep.ip_str[0], 127);

		memcpy(&ep.ip_bytes.ipv6[0], &host_addr->sin6_addr, 16);

		ep.ip_ver = qnet_ip_ver_ipv6;
	}

	return ep;
}