#pragma once

#include <qnet/qnet.h>
#include <qnet/socket.h>
#include <qnet/endpoint.h>

#define QNET_MAX_ROUTES 64

typedef void (*qnet_http_route_callback)(qnet_socket client, const char* method, const char* query, const char* body, const char* client_buffer);

typedef struct {
    const char* path;
    qnet_http_route_callback callback;
} qnet_http_route;

typedef struct
{
    qnet_socket listen_socket;
    qnet_ip_endpoint endpoint;
    char root[512];

    qnet_http_route routes[QNET_MAX_ROUTES];
    int route_count;
} qnet_http_server;

QNETAPI b8 qnet_http_server_init(qnet_http_server* server, const char* ip, u16 port, const char* public_dir);
QNETAPI void qnet_http_server_poll(qnet_http_server* server);
QNETAPI void qnet_http_server_shutdown(qnet_http_server* server);
b8 qnet_http_register_route(qnet_http_server* server, const char* path, qnet_http_route_callback callback);
b8 qnet_http_get_param(const char* query, const char* key, char* out, int max);
b8 qnet_http_send_all(qnet_socket sock, const void* data, size_t bytes);

// TODO: move
b8 qnet_http_parse_range(const char* request, __int64 filesize, __int64* start, __int64* end);
const char* qnet_http_mime(const char* path);
void qnet_http_send_file(qnet_socket sock, const char* path, const char* request);