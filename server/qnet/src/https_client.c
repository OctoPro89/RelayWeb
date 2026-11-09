#include <qnet/https_client.h>
#include <qnet/endpoint.h>
#include <stdio.h>

#pragma region http_buf

static void http_buf_init(qnet_http_buffer* b) {
    b->capacity = 8192;
    b->length = 0;
    b->data = malloc(b->capacity);
}

static void http_buf_append(qnet_http_buffer* b, const void* data, i32 len) {
    if (b->length + len > b->capacity) {
        while (b->length + len > b->capacity)
            b->capacity *= 2;
        b->data = realloc(b->data, b->capacity);
    }

    memcpy(b->data + b->length, data, len);
    b->length += len;
}

#pragma endregion

#pragma region helpers

static const char* qnet_http_response_get_header(qnet_http_response* resp, const char* key) {
    for (int i = 0; i < resp->header_count; ++i) {
        if (_stricmp(resp->headers[i].key, key) == 0) {
            return resp->headers[i].value;
        }
    }
    return NULL;
}

static int parse_hex(const char* str) {
    int val = 0;

    while (*str) {
        char c = *str++;
        val *= 16;
        if (c >= '0' && c <= '9') { val += c - '0'; }
        else if (c >= 'A' && c <= 'F') { val += c - 'A' + 10; }
        else if (c >= 'a' && c <= 'f') { val += c - 'a' + 10; }
        else break;
    }

    return val;
}

static void read_chunked_body(qnet_tls_socket* tls, qnet_http_buffer* out, const char* remainder, int remainder_len) {
    int pos = 0;
    char buf[4096];

    while (1) {
        // Read chunk size line
        char line[32];
        int line_len = 0;

        while (1) {
            char c;
            if (pos < remainder_len) {
                c = remainder[pos++];
            }
            else {
                int r = qnet_tls_recv(tls, &c, 1);
                if (r <= 0) { return; }
            }

            if (line_len >= sizeof(line) - 1) { break; }
            line[line_len++] = c;

            if (line_len >= 2 && line[line_len - 2] == '\r' && line[line_len - 1] == '\n') {
                line[line_len - 2] = 0; // terminate string before \r\n
                break;
            }
        }

        int chunk_size = parse_hex(line);
        if (chunk_size == 0) { break; }

        // Read chunk data
        int remaining = chunk_size;
        while (remaining > 0) {
            int to_read = remaining > sizeof(buf) ? sizeof(buf) : remaining;
            int r = 0;

            if (pos < remainder_len) {
                r = remainder_len - pos;
                if (r > to_read) { r = to_read; }
                http_buf_append(out, remainder + pos, r);
                pos += r;
            }
            else {
                r = qnet_tls_recv(tls, buf, to_read);
                if (r <= 0) { return; }
                http_buf_append(out, buf, r);
            }

            remaining -= r;
        }

        // Skip trailing \r\n after chunk
        for (int i = 0; i < 2; ++i) {
            if (pos < remainder_len) pos++;
            else qnet_tls_recv(tls, buf, 1);
        }
    }
}

static void read_body(qnet_tls_socket* tls, qnet_http_response* resp, const char* remainder, int remainder_len) {
    const char* cl = qnet_http_response_get_header(resp, "Content-Length");
    const char* te = qnet_http_response_get_header(resp, "Transfer-Encoding");

    http_buf_init(&resp->body);

    if (te && _stricmp(te, "chunked") == 0) {
        read_chunked_body(tls, &resp->body, remainder, remainder_len);
    }
    else if (cl) {
        int content_len = atoi(cl);

        if (remainder_len > 0) {
            int n = remainder_len > content_len ? content_len : remainder_len;
            http_buf_append(&resp->body, remainder, n);
            content_len -= n;
        }

        char buf[4096];

        while (content_len > 0) {
            int to_read = content_len > sizeof(buf) ? sizeof(buf) : content_len;
            int r = qnet_tls_recv(tls, buf, to_read);
            if (r <= 0) break;
            http_buf_append(&resp->body, buf, r);
            content_len -= r;
        }
    }
    else {
        // read until close
        char buf[4096];
        if (remainder_len > 0) http_buf_append(&resp->body, remainder, remainder_len);

        int r = 0;
        while ((r = qnet_tls_recv(tls, buf, sizeof(buf))) > 0) {
            http_buf_append(&resp->body, buf, r);
        }
    }
}

#pragma endregion

#pragma region http_response_helpers

static void qnet_http_response_add_header(qnet_http_response* resp, const char* key, const char* value) {
    if (resp->header_count >= resp->headers_capacity) {
        int new_cap = resp->headers_capacity * 2;
        resp->headers = realloc(resp->headers, sizeof(qnet_http_header) * new_cap);
        for (int i = resp->headers_capacity; i < new_cap; ++i) {
            resp->headers[i].key = NULL;
            resp->headers[i].value = NULL;
        }
        resp->headers_capacity = new_cap;
    }

    int idx = resp->header_count++;
    resp->headers[idx].key = _strdup(key);
    resp->headers[idx].value = _strdup(value);
}

static void parse_status_and_headers(qnet_http_response* resp, const char* buf, int len, int* header_end_index) {
    // Find end of headers (\r\n\r\n)
    const char* ptr = buf;
    const char* end = buf + len;
    *header_end_index = 0;

    // Status line
    const char* line_end = strstr(ptr, "\r\n");
    if (!line_end) { return; }

    char line[256];
    int line_len = (int)(line_end - ptr);
    if (line_len >= sizeof(line)) { line_len = sizeof(line) - 1; }
    memcpy(line, ptr, line_len);
    line[line_len] = 0;

    sscanf(line, "HTTP/%*s %d %[^\r\n]", &resp->status_code, resp->status_text);

    ptr = line_end + 2;

    // Parse headers
    while (ptr < end && !(ptr[0] == '\r' && ptr[1] == '\n')) {
        const char* colon = strchr(ptr, ':');
        if (!colon) { break; }

        char key[128], value[1024];
        int key_len = (int)(colon - ptr);
        if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
        memcpy(key, ptr, key_len);
        key[key_len] = 0;

        const char* val_start = colon + 1;
        while (*val_start == ' ') { val_start++; }
        const char* line_end2 = strstr(val_start, "\r\n");
        if (!line_end2) { break; }
        int val_len = (int)(line_end2 - val_start);
        if (val_len >= sizeof(value)) { val_len = sizeof(value) - 1; }
        memcpy(value, val_start, val_len);
        value[val_len] = 0;

        qnet_http_response_add_header(resp, key, value);

        ptr = line_end2 + 2;
    }

    *header_end_index = (int)(ptr - buf) + 2; // point after \r\n\r\n
}

#pragma endregion

QNETAPI b8 qnet_https_client_init(qnet_https_client* c, const char* host, u16 port, b8 follow_redirects) {
    memset(c, 0, sizeof(qnet_https_client));

    strncpy(c->host, host, sizeof(c->host) - 1);
    c->port = port;
    c->follow_redirects = follow_redirects;

    /* Resolve */
    qnet_ip_endpoint ep = qnet_ip_endpoint_create(host, port);

    qnet_socket sock = qnet_socket_create(
        ep.ip_ver,
        qnet_socket_io_type_send_recv,
        qnet_socket_type_tcp
    );

    if (!qnet_socket_connect(sock, &ep)) {
        return false;
    }

    if (!qnet_tls_init(&c->tls, sock, host, (i32)strlen(host))) {
        return false;
    }

    if (!qnet_tls_handshake(&c->tls)) {
        return false;
    }

    /* Default headers */
    c->default_headers_len = snprintf(
        c->default_headers,
        sizeof(c->default_headers),
        "Host: %s\r\n"
        "User-Agent: qnet/0.1\r\n"
        "Accept: */*\r\n",
        host
    );

    c->connected = true;
    c->has_resources = true;
    return true;
}

QNETAPI void qnet_https_client_set_header(
    qnet_https_client* c,
    const char* key,
    const char* value
) {
    c->default_headers_len += snprintf(
        c->default_headers + c->default_headers_len,
        sizeof(c->default_headers) - c->default_headers_len,
        "%s: %s\r\n",
        key,
        value
    );
}

QNETAPI void qnet_https_client_set_auth_header(qnet_https_client* c, const char* scheme, const char* parameter) {
    char buffer[1024];
    int len = snprintf(
        buffer,
        sizeof(buffer),
        "%s %s",
        scheme,
        parameter
    );

    if (len <= 0 || len >= (int)sizeof(buffer)) {
        return; // Truncated or encoding error
    }

    qnet_https_client_set_header(c, "Authorization", buffer);
}

QNETAPI void qnet_http_buffer_free(qnet_http_buffer* b);

QNETAPI b8 qnet_https_client_get(qnet_https_client* c, const char* path, qnet_http_response* resp, b8 auto_reconnect_on_close) {
    if (!c->connected) {
        if (!auto_reconnect_on_close || !qnet_https_client_reconnect(c)) {
            return false;
        }
    }

    int redirect_count = 0;
    char current_path[1024];
    strncpy(current_path, path, sizeof(current_path) - 1);

    while (redirect_count < 5) {
        qnet_http_buffer buf;
        http_buf_init(&buf);

        // Build request
        char req[4096];
        int req_len = snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\n"
            "%.*s"
            "Connection: close\r\n\r\n",
            current_path,
            c->default_headers_len,
            c->default_headers
        );
        qnet_tls_send(&c->tls, req, req_len);

        // Read headers + body
        int header_end = 0;
        int bytes_read = 0;
        while (header_end == 0) {
            char tmp[4096];
            int r = qnet_tls_recv(&c->tls, tmp, sizeof(tmp));
            if (r <= 0) break;
            http_buf_append(&buf, tmp, r);

            // check for \r\n\r\n
            for (int i = 0; i < buf.length - 3; ++i) {
                if (buf.data[i] == '\r' && buf.data[i + 1] == '\n' && buf.data[i + 2] == '\r' && buf.data[i + 3] == '\n') {
                    header_end = i + 4;
                    break;
                }
            }
        }

        qnet_http_response_init(resp);
        parse_status_and_headers(resp, buf.data, buf.length, &header_end);
        read_body(&c->tls, resp, buf.data + header_end, buf.length - header_end);

        const char* conn = qnet_http_response_get_header(resp, "Connection");
        if (conn && _stricmp(conn, "close") == 0) {
            c->connected = false;
        }

        // handle redirect
        const char* loc = qnet_http_response_get_header(resp, "Location");
        if ((resp->status_code / 100) == 3 && loc && c->follow_redirects) {
            redirect_count++;
            strncpy(current_path, loc, sizeof(current_path) - 1);
            qnet_http_response_free(resp);
            qnet_http_buffer_free(&buf);
            continue;
        }

        qnet_http_buffer_free(&buf);

        break;
    }

    return 1;
}

QNETAPI void qnet_https_client_close(qnet_https_client* c) {
    if (c->has_resources) {
        qnet_tls_close(&c->tls);
        c->connected = false;
        c->has_resources = false;
    }
}

QNETAPI b8 qnet_https_client_reconnect(qnet_https_client* c) {
    qnet_https_client_close(c);

    /* Resolve */
    qnet_ip_endpoint ep = qnet_ip_endpoint_create(c->host, c->port);

    qnet_socket sock = qnet_socket_create(
        ep.ip_ver,
        qnet_socket_io_type_send_recv,
        qnet_socket_type_tcp
    );

    if (!qnet_socket_connect(sock, &ep)) {
        return false;
    }

    if (!qnet_tls_init(&c->tls, sock, c->host, (i32)strlen(c->host))) {
        return false;
    }

    if (!qnet_tls_handshake(&c->tls)) {
        return false;
    }

    c->connected = true;
    c->has_resources = true;

    return true;
}

QNETAPI void qnet_http_buffer_free(qnet_http_buffer* b) {
    free(b->data);
    b->data = NULL;
    b->length = b->capacity = 0;
}

QNETAPI void qnet_http_response_init(qnet_http_response* resp) {
    resp->status_code = 0;
    resp->status_text[0] = '\0';
    resp->header_count = 0;
    resp->headers_capacity = 16;
    resp->headers = malloc(sizeof(qnet_http_header) * resp->headers_capacity);

    for (int i = 0; i < resp->headers_capacity; ++i) {
        resp->headers[i].key = NULL;
        resp->headers[i].value = NULL;
    }

    resp->body.data = NULL;
    resp->body.length = resp->body.capacity = 0;
}

QNETAPI void qnet_http_response_free(qnet_http_response* resp) {
    for (int i = 0; i < resp->header_count; ++i) {
        free(resp->headers[i].key);
        free(resp->headers[i].value);
    }

    free(resp->headers);
    qnet_http_buffer_free(&resp->body);

    resp->headers = NULL;
    resp->header_count = resp->headers_capacity = 0;
}