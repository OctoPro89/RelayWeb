#include <qnet/http_server.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define FILESEND_BUFFER_SIZE (64 * 1024)
char* filesend_buffer = NULL;

b8 qnet_http_parse_range(const char* request, __int64 filesize, __int64* start, __int64* end)
{
    const char* range = strstr(request, "Range:");
    if (!range)
        return false;

    range = strstr(range, "bytes=");
    if (!range)
        return false;

    range += 6;

    const char* dash = strchr(range, '-');
    if (!dash)
        return false;

    // bytes=-500
    if (dash == range)
    {
        __int64 suffix = _strtoi64(dash + 1, NULL, 10);

        if (suffix <= 0)
            return false;

        if (suffix > filesize)
            suffix = filesize;

        *start = filesize - suffix;
        *end = filesize - 1;

        return true;
    }

    // bytes=123-
    // bytes=123-456
    *start = _strtoi64(range, NULL, 10);

    if (*start >= filesize)
        return false;

    const char* p = dash + 1;

    while (*p == ' ') { p++; }

    // empty end?
    if (*p == '\r' || *p == '\n' || *p == '\0')
    {
        *end = filesize - 1;
    }
    else
    {
        *end = _strtoi64(p, NULL, 10);

        if (*end >= filesize)
            *end = filesize - 1;

        if (*end < *start)
            return false;
    }

    return true;
}

static void qnet_http_handle_api(qnet_http_server* server, qnet_socket client, const char* method, const char* path) {
    int out = 0;

    // Example GET endpoint
    if (strcmp(path, "/api/mylist") == 0 && strcmp(method, "GET") == 0) {
        // TODO: make a server-side request to Torbox API
        const char* proxy_response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":true,\"data\":[]}";

        qnet_socket_send(client, proxy_response, strlen(proxy_response), &out, NULL);
        return;
    }

    // Example POST endpoint
    if (strcmp(path, "/api/submit") == 0 && strcmp(method, "POST") == 0) {
        // Here you can read body from socket and respond
        char buf[4096];
        int received = 0;
        if (qnet_socket_recv(client, buf, sizeof(buf) - 1, &received, NULL)) {
            buf[received] = 0;
            printf("[http] POST body: %s\n", buf);
        }

        const char* resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":true}";

        qnet_socket_send(client, resp, strlen(resp), &out, NULL);
        return;
    }

    // fallback 404
    const char* resp404 =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length:0\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";

    qnet_socket_send(client, resp404, strlen(resp404), &out, NULL);
}

const char* qnet_http_mime(const char* path) {
    const char* ext = strrchr(path, '.');

    if (!ext) return "text/plain";

    if (!strcmp(ext, ".html")) return "text/html";
    if (!strcmp(ext, ".css")) return "text/css";
    if (!strcmp(ext, ".js")) return "application/javascript";
    if (!strcmp(ext, ".json")) return "application/json";
    if (!strcmp(ext, ".png")) return "image/png";
    if (!strcmp(ext, ".jpg")) return "image/jpeg";
    if (!strcmp(ext, ".svg")) return "image/svg+xml";
    if (!strcmp(ext, ".mp4")) return "video/mp4";
    if (!strcmp(ext, ".mkv")) return "video/x-matroska";
    if (!strcmp(ext, ".mov")) return "video/quicktime";

    return "application/octet-stream";
}

/*
static void qnet_http_send_file(qnet_socket sock, const char* path) {
    FILE* f = fopen(path, "rb");

    if (!f) {
        const char* resp =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length:0\r\n\r\n";

        i32 out = 0;
        qnet_socket_send(sock, resp, (i32)strlen(resp), &out, NULL);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char header[512];

    int len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %ld\r\n"
        "Content-Type: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        size,
        qnet_http_mime(path)
    );

    i32 out = 0;
    qnet_socket_send(sock, header, len, &out, NULL);

    char buf[4096];
    size_t r = 0;

    while ((r = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        qnet_socket_send(sock, buf, (i32)r, &out, NULL);
    }

    fclose(f);
}
*/

void qnet_http_send_file(qnet_socket sock, const char* path, const char* request) {
    FILE* f = fopen(path, "rb");

    if (!f) {
        const char* resp =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length:0\r\n\r\n";

        i32 out = 0;
        qnet_socket_send(sock, resp, (i32)strlen(resp), &out, NULL);
        return;
    }

    _fseeki64(f, 0, SEEK_END);

    __int64 size = _ftelli64(f);

    _fseeki64(f, 0, SEEK_SET);

    __int64 start = 0;
    __int64 end = size - 1;

    b8 partial = qnet_http_parse_range(request, size, &start, &end);

    __int64 content_length = end - start + 1;

    char header[512];

    int len;

    if (partial)
    {
        len = snprintf(
            header,
            sizeof(header),
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Length: %lld\r\n"
            "Content-Range: bytes %lld-%lld/%lld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Content-Type: %s\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "\r\n",
            content_length,
            start,
            end,
            size,
            qnet_http_mime(path)
        );
    }
    else
    {
        len = snprintf(
            header,
            sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: %lld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Content-Type: %s\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "\r\n",
            size,
            qnet_http_mime(path)
        );
    }

    i32 out = 0;
    qnet_socket_send(sock, header, len, &out, NULL);

    printf("Responding with:\n%s", header);

    size_t r = 0;

    _fseeki64(f, start, SEEK_SET);

    __int64 remaining = content_length;

    while (remaining > 0)
    {
        size_t chunk = remaining > FILESEND_BUFFER_SIZE ? FILESEND_BUFFER_SIZE : (size_t)remaining;

        size_t r = fread(filesend_buffer, 1, chunk, f);

        if (r == 0)
            break;

        printf("sending %lld bytes\n", content_length);
        printf("remaining=%lld\n", remaining);

        if (!qnet_http_send_all(sock, filesend_buffer, r))
        {
            fclose(f);
            return;
        }

        remaining -= (__int64)r;
    }

    printf("finished sending\n");

    fclose(f);
}

static void qnet_http_handle_connection(qnet_http_server* server, qnet_socket client) {
    char buf[4096];
    int received = 0;

    while (received == 0) {
        qnet_socket_poll_events revents = 0;

        qnet_socket_poll(
            &client,
            1,
            qnet_socket_poll_timeout_wait_1ms,
            qnet_socket_poll_events_read,
            &revents
        );

        if (revents & qnet_socket_poll_events_read)
        {
            if (!qnet_socket_recv(client, buf, sizeof(buf) - 1, &received, NULL))
                return;
        }
    }

    buf[received] = 0;

    printf("[http] request:\n%s\n", buf);

    char method[8];
    char path[1024];
    char plain_path[1024];
    char* query = NULL;

    sscanf(buf, "%7s %1023s", method, path);

    strcpy(plain_path, path);

    char* q = strchr(plain_path, '?');
    if (q) {
        *q = '\0'; // terminate path
        query = q + 1; // start of query parameters
    }

    for (int i = 0; i < server->route_count; i++) {
        if (strcmp(plain_path, server->routes[i].path) == 0) {
            char body[4096] = { 0 };
            if (strcmp(method, "POST") == 0) {
                // TODO: read body from socket
            }
            server->routes[i].callback(client, method, query, body, buf);
            return;
        }
    }

    // fallback to static file
    char file_path[1024];
    if (!strcmp(path, "/")) {
        sprintf(file_path, "%s/index.html", server->root);
    }
    else {
        sprintf(file_path, "%s%s", server->root, path);
    }

    qnet_http_send_file(client, file_path, buf);
}

b8 qnet_http_server_init(qnet_http_server* server, const char* ip, u16 port, const char* public_dir) {
    memset(server, 0, sizeof(*server));

    server->endpoint = qnet_ip_endpoint_create(ip, port);

    strncpy(server->root, public_dir, sizeof(server->root) - 1);

    server->listen_socket = qnet_socket_create(qnet_ip_ver_ipv4, qnet_socket_io_type_send_recv, qnet_socket_type_tcp);

    qnet_socket_set_option(server->listen_socket, qnet_socket_option_nonblocking, 1);

    if (!qnet_socket_bind(server->listen_socket, &server->endpoint)) { return false; }
    if (!qnet_socket_listen(server->listen_socket, NULL, 32)) { return false; }

    filesend_buffer = malloc(FILESEND_BUFFER_SIZE);

    return true;
}

void qnet_http_server_poll(qnet_http_server* server) {
    qnet_socket_poll_events revents = 0;

    qnet_socket_poll(
        &server->listen_socket,
        1,
        qnet_socket_poll_timeout_wait_1ms,
        qnet_socket_poll_events_read,
        &revents
    );

    if (!(revents & qnet_socket_poll_events_read)) { return; }

    qnet_socket client;
    qnet_ip_endpoint client_ep;

    if (!qnet_socket_accept(server->listen_socket, &client, &client_ep)) { return; }

    qnet_socket_set_option(client, qnet_socket_option_nonblocking, 1); // IMPORTANT NOT TO STALL

    printf("[http] connection from %s:%d\n", client_ep.ip_str, client_ep.port);

    qnet_http_handle_connection(server, client);

    qnet_socket_close(client);
}

void qnet_http_server_shutdown(qnet_http_server* server) {
    if (filesend_buffer) { free(filesend_buffer); }
    qnet_socket_close(server->listen_socket);
}

b8 qnet_http_register_route(qnet_http_server* server, const char* path, qnet_http_route_callback callback) {
    if (server->route_count >= QNET_MAX_ROUTES) return false;
    server->routes[server->route_count].path = path;
    server->routes[server->route_count].callback = callback;
    server->route_count++;
    return true;
}

b8 qnet_http_get_param(const char* query, const char* key, char* out, int max){
    if (!query) return false;

    const char* p = query;

    while (*p) {

        const char* eq = strchr(p, '=');
        if (!eq) break;

        u64 key_len = eq - p;

        if (strncmp(p, key, key_len) == 0 && strlen(key) == key_len) {

            const char* val = eq + 1;
            const char* end = strchr(val, '&');

            u64 len = end ? (end - val) : strlen(val);
            if (len >= max) len = max - 1;

            memcpy(out, val, len);
            out[len] = 0;

            return true;
        }

        p = strchr(p, '&');
        if (!p) break;
        p++;
    }

    return false;
}

b8 qnet_http_send_all(qnet_socket sock, const void* data, size_t bytes)
{
    const char* p = (const char*)data;
    size_t total = 0;

    while (total < bytes)
    {
        i32 sent = 0;

        if (!qnet_socket_send(sock, p + total, (i32)(bytes - total), &sent, NULL))
        {
            return false;
        }

        // Connection closed or no progress.
        if (sent <= 0)
        {
            return false;
        }

        total += (size_t)sent;
    }

    return true;
}