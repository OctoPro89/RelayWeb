#include <qnet/network.h>
#include "internal.h"

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>

QNETAPI b8 qnet_initialize() {
	net_error = qnet_success;

	WSADATA wsadata;
	int result = WSAStartup(MAKEWORD(2, 2), &wsadata);
	if (result != 0) {
		net_error = qnet_error_winsock_api_startup_failed;
		return false;
	}

	if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2) {
		net_error = qnet_error_winsock_api_invalid_version;
		return false;
	}

	return true;
}

QNETAPI void qnet_shutdown() {
	net_error = qnet_success;
	WSACleanup();
}