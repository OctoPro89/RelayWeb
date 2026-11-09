#include <qnet/qnet.h>

#ifdef QNET_PLATFORM_WINDOWS
	#pragma comment(lib, "ws2_32.lib")
#endif // QNET_PLATFORM_WINDOWS

qnet_error net_error;

QNETAPI qnet_error qnet_get_error() {
	return net_error;
}