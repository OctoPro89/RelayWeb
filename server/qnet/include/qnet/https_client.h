#pragma once

#include <qnet/tls.h>

typedef struct {
    char* data;
    i32   length;
    i32   capacity;
} qnet_http_buffer;

typedef struct {
    char* key;
    char* value;
} qnet_http_header;

typedef struct {
    int status_code;
    char status_text[128];

    qnet_http_header* headers;
    int header_count;
    int headers_capacity;

    qnet_http_buffer body;
} qnet_http_response;

typedef struct {
    qnet_tls_socket tls;
    char host[256];
    u16  port;

    char default_headers[2048];
    i32  default_headers_len;

    b8   connected;
    b8   follow_redirects;
    b8   has_resources;
} qnet_https_client;

QNETAPI b8 qnet_https_client_init(qnet_https_client* c, const char* host, u16 port, b8 follow_redirects);
QNETAPI void qnet_https_client_close(qnet_https_client* c);
QNETAPI b8 qnet_https_client_reconnect(qnet_https_client* c);

QNETAPI void qnet_https_client_set_header(qnet_https_client* c, const char* key, const char* value);
QNETAPI void qnet_https_client_set_auth_header(qnet_https_client* c, const char* scheme, const char* parameter);
QNETAPI b8 qnet_https_client_get(qnet_https_client* c, const char* path, qnet_http_response* resp, b8 auto_reconnect_on_close);

QNETAPI void qnet_http_response_init(qnet_http_response* resp);
QNETAPI void qnet_http_response_free(qnet_http_response* resp);