#pragma once

#if defined(_WIN64) || defined(_WIN32)
    #define SECURITY_WIN32
    #include <windows.h>
    #include <schannel.h>
    #include <security.h>

    #include <qnet/socket.h>

    typedef struct {
        qnet_socket sock;

        CredHandle cred;
        CtxtHandle ctx;
        BOOL handshake_complete;

        SecPkgContext_StreamSizes sizes;

        u8* enc_buffer;
        u8* dec_buffer;

        u8* enc_hold;
        i32 enc_hold_len;
        i32 enc_hold_cap;

        char hostname[256];
    } qnet_tls_socket;

    QNETAPI b8 qnet_tls_init(qnet_tls_socket* tls, qnet_socket sock, const char* hostname, i32 host_len);
    QNETAPI b8 qnet_tls_handshake(qnet_tls_socket* tls);
    QNETAPI i32 qnet_tls_send(qnet_tls_socket* tls, const void* data, i32 len);
    QNETAPI i32 qnet_tls_recv(qnet_tls_socket* tls, void* out, i32 max_len);
    QNETAPI void qnet_tls_close(qnet_tls_socket* tls);
#else
    #error "qnet tls requires Windows!"
#endif