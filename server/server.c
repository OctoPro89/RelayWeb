#define _CRT_SECURE_NO_WARNINGS
#include "qnet/network.h"
#include "qnet/http_server.h"
#include "qnet/https_client.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MYAPIKEY "APIKEYHERE"

qnet_https_client client;

void get_mylist_route(qnet_socket s, const char* method, const char* path, const char* body) {
    int out = 0;

    qnet_http_response resp = { 0 };
    qnet_http_response_init(&resp);
    if (qnet_https_client_get(&client, "/v1/api/torrents/mylist?offset=0&limit=1000&bypass_cache=false", &resp, true)) {
        const char* base =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "\r\n";

        char* buf = malloc(resp.body.length + 103);

        snprintf(buf, resp.body.length + 103, "%s%s", base, resp.body.data);
        buf[resp.body.length + 102] = ' '; // un-null terminate

        FILE* f = fopen("mylist.json", "w");
        fwrite(resp.body.data, resp.body.length, 1, f);
        fclose(f);

        qnet_socket_send(s, buf, resp.body.length + 103, &out, NULL);
        qnet_http_response_free(&resp);
    }
}

void request_dl_route(qnet_socket s, const char* method, const char* query, const char* body)
{
    char torrent_id[32] = { 0 };
    char file_id[32] = { 0 };

    qnet_http_get_param(query, "torrent_id", torrent_id, sizeof(torrent_id));
    qnet_http_get_param(query, "file_id", file_id, sizeof(file_id));

    char endpoint[512];

    snprintf(endpoint, sizeof(endpoint), "/v1/api/torrents/requestdl?token=" MYAPIKEY "&torrent_id=%s&file_id=%s&zip_link=false&torrent_file=false&redirect=false", torrent_id, file_id);

    qnet_http_response resp = { 0 };
    qnet_http_response_init(&resp);

    int out = 0;

    if (qnet_https_client_get(&client, endpoint, &resp, true))
    {
        const char* base =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "\r\n";

        char* buf = malloc(resp.body.length + strlen(base) + 1);
        snprintf(buf, resp.body.length + strlen(base) + 1, "%s%s", base, resp.body.data);

        qnet_socket_send(s, buf, resp.body.length + strlen(base), &out, NULL);

        free(buf);
    }

    qnet_http_response_free(&resp);
}

int main() {
    qnet_initialize();

    qnet_http_server server;

    qnet_http_server_init(&server, "0.0.0.0", 40000, "../../../frontend/");
    //qnet_http_server_init(&server, "0.0.0.0", 40000, "D:/Programming/JS/Relay/");
    qnet_http_register_route(&server, "/api/mylist", get_mylist_route);
    qnet_http_register_route(&server, "/api/requestdl", request_dl_route);

    if (!qnet_https_client_init(&client, "api.torbox.app", 443, true)) {
        printf("HTTPS init failed\n");
        return 1;
    }

    qnet_https_client_set_auth_header(&client, "Bearer", MYAPIKEY);

    while (1)
    {
        qnet_http_server_poll(&server);
    }

    qnet_http_server_shutdown(&server);

    qnet_shutdown();
}