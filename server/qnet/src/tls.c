#if defined(_WIN64) || defined(_WIN32)
    #include <qnet/tls.h>
#include <stdio.h>

    #pragma comment(lib, "Secur32.lib")

#define TLS_HANDSHAKE_RECV_BUF_SIZE (16 * 1024)
#define TLS_RECV_BUF_SIZE (128 * 1024)

QNETAPI b8 qnet_tls_init(qnet_tls_socket* tls, qnet_socket sock, const char* hostname, i32 host_len) {
    ZeroMemory(tls, sizeof(*tls));
    tls->sock = sock;

    if (host_len >= (i32)sizeof(tls->hostname))
        return false;

    memcpy(tls->hostname, hostname, host_len);
    tls->hostname[host_len] = 0;

    SCHANNEL_CRED cred = { 0 };
    cred.dwVersion = SCHANNEL_CRED_VERSION;
    cred.dwFlags =
        SCH_USE_STRONG_CRYPTO |
        SCH_CRED_NO_DEFAULT_CREDS |
        SCH_CRED_MANUAL_CRED_VALIDATION;
    cred.grbitEnabledProtocols =
        SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;

    TimeStamp expiry;
    SECURITY_STATUS sc = AcquireCredentialsHandleA(
        NULL,
        UNISP_NAME_A,
        SECPKG_CRED_OUTBOUND,
        NULL,
        &cred,
        NULL,
        NULL,
        &tls->cred,
        &expiry
    );

    tls->enc_hold = malloc(TLS_RECV_BUF_SIZE);
    tls->enc_hold_cap = TLS_RECV_BUF_SIZE;
    tls->enc_hold_len = 0;

    return sc == SEC_E_OK;
}


QNETAPI b8 qnet_tls_handshake(qnet_tls_socket* tls) {
    SECURITY_STATUS sc;
    DWORD flags =
        ISC_REQ_SEQUENCE_DETECT |
        ISC_REQ_REPLAY_DETECT |
        ISC_REQ_CONFIDENTIALITY |
        ISC_REQ_EXTENDED_ERROR |
        ISC_REQ_ALLOCATE_MEMORY |
        ISC_REQ_STREAM;

    BYTE* recv_buf = malloc(sizeof(BYTE) * TLS_HANDSHAKE_RECV_BUF_SIZE);
    DWORD recv_len = 0;
    BOOL have_context = FALSE;
    BOOL need_recv = FALSE;

    while (1) {
        /* If SChannel needs more data, read it */
        if (need_recv) {
            i32 received;
            if (!qnet_socket_recv(
                tls->sock,
                recv_buf + recv_len,
                TLS_HANDSHAKE_RECV_BUF_SIZE - recv_len,
                &received,
                NULL))
                return false;

            recv_len += received;
            need_recv = FALSE;
        }

        SecBuffer in_buf[2];
        SecBufferDesc in_desc;

        in_buf[0].BufferType = SECBUFFER_TOKEN;
        in_buf[0].pvBuffer = recv_buf;
        in_buf[0].cbBuffer = recv_len;

        in_buf[1].BufferType = SECBUFFER_EMPTY;
        in_buf[1].pvBuffer = NULL;
        in_buf[1].cbBuffer = 0;

        in_desc.ulVersion = SECBUFFER_VERSION;
        in_desc.cBuffers = 2;
        in_desc.pBuffers = in_buf;

        SecBuffer out_buf;
        SecBufferDesc out_desc;

        out_buf.BufferType = SECBUFFER_TOKEN;
        out_buf.pvBuffer = NULL;
        out_buf.cbBuffer = 0;

        out_desc.ulVersion = SECBUFFER_VERSION;
        out_desc.cBuffers = 1;
        out_desc.pBuffers = &out_buf;

        sc = InitializeSecurityContextA(
            &tls->cred,
            have_context ? &tls->ctx : NULL,
            tls->hostname,
            flags,
            0,
            SECURITY_NATIVE_DREP,
            recv_len ? &in_desc : NULL,
            0,
            have_context ? NULL : &tls->ctx,
            &out_desc,
            &flags,
            NULL
        );

        have_context = TRUE;

        /* Send handshake data if produced */
        if (out_buf.cbBuffer && out_buf.pvBuffer) {
            i32 sent;
            qnet_socket_send(
                tls->sock,
                out_buf.pvBuffer,
                out_buf.cbBuffer,
                &sent,
                NULL);
            FreeContextBuffer(out_buf.pvBuffer);
        }

        if (sc == SEC_E_OK)
            break;

        if (sc == SEC_E_INCOMPLETE_MESSAGE) {
            /* Need more network data */
            need_recv = TRUE;
            continue;
        }

        if (sc != SEC_I_CONTINUE_NEEDED)
            return false;

        /* Preserve extra bytes */
        if (in_buf[1].BufferType == SECBUFFER_EXTRA) {
            memmove(
                recv_buf,
                recv_buf + (recv_len - in_buf[1].cbBuffer),
                in_buf[1].cbBuffer
            );
            recv_len = in_buf[1].cbBuffer;
        }
        else {
            recv_len = 0;
        }

        /* We must receive more data before next ISC */
        need_recv = TRUE;
    }

    /* Query stream sizes */
    if (QueryContextAttributes(
        &tls->ctx,
        SECPKG_ATTR_STREAM_SIZES,
        &tls->sizes) != SEC_E_OK)
        return false;

    tls->enc_buffer = malloc(
        tls->sizes.cbHeader +
        tls->sizes.cbMaximumMessage +
        tls->sizes.cbTrailer
    );

    tls->dec_buffer = malloc(tls->sizes.cbMaximumMessage * 2);
    *(i32*)tls->dec_buffer = 0; // first 4 bytes = length

    tls->handshake_complete = TRUE;
    free(recv_buf);
    return true;
}


QNETAPI i32 qnet_tls_send(qnet_tls_socket* tls, const void* data, i32 len) {
    SecBuffer bufs[4];
    SecBufferDesc desc;

    memcpy(tls->enc_buffer + tls->sizes.cbHeader, data, len);

    bufs[0] = (SecBuffer){ tls->sizes.cbHeader,
                           SECBUFFER_STREAM_HEADER,
                           tls->enc_buffer };

    bufs[1] = (SecBuffer){ len,
                           SECBUFFER_DATA,
                           tls->enc_buffer + tls->sizes.cbHeader };

    bufs[2] = (SecBuffer){ tls->sizes.cbTrailer,
                           SECBUFFER_STREAM_TRAILER,
                           tls->enc_buffer + tls->sizes.cbHeader + len };

    bufs[3] = (SecBuffer){ 0, SECBUFFER_EMPTY, NULL };

    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers = 4;
    desc.pBuffers = bufs;

    if (EncryptMessage(&tls->ctx, 0, &desc, 0) != SEC_E_OK)
        return -1;

    i32 total =
        bufs[0].cbBuffer +
        bufs[1].cbBuffer +
        bufs[2].cbBuffer;

    i32 sent;
    qnet_socket_send(tls->sock, tls->enc_buffer, total, &sent, NULL);
    return sent;
}

QNETAPI i32 qnet_tls_recv(qnet_tls_socket* tls, void* out, i32 max_len) {
    if (!tls || !out || max_len <= 0) return -1;

    i32* used = (i32*)tls->dec_buffer;          // first 4 bytes store decrypted length
    BYTE* dec = tls->dec_buffer + sizeof(i32);  // pointer to plaintext

    // Serve any already decrypted data first
    if (*used > 0) {
        i32 n = (*used < max_len) ? *used : max_len;
        memcpy(out, dec, n);
        memmove(dec, dec + n, *used - n);
        *used -= n;
        return n;
    }

    // Ensure enc_hold exists
    if (!tls->enc_hold) {
        tls->enc_hold_cap = TLS_RECV_BUF_SIZE; // start with 128 KB
        tls->enc_hold_len = 0;
        tls->enc_hold = malloc(tls->enc_hold_cap);
        if (!tls->enc_hold) return -1;
    }

    // Keep reading until we decrypt at least some data
    while (1) {
        // Grow buffer if there is not enough free space for a minimum read
        while (tls->enc_hold_cap - tls->enc_hold_len < 16 * 1024) {
            tls->enc_hold_cap *= 2;
            tls->enc_hold = realloc(tls->enc_hold, tls->enc_hold_cap);
            if (!tls->enc_hold) return -1;
        }

        i32 received = 0;
        if (!qnet_socket_recv(
            tls->sock,
            tls->enc_hold + tls->enc_hold_len,
            tls->enc_hold_cap - tls->enc_hold_len,
            &received,
            NULL)) {
            return -1;
        }

        if (received <= 0) {
            if (tls->enc_hold_len == 0) return 0; // no data available
            // else: proceed to try decrypting leftover bytes
        }
        else {
            tls->enc_hold_len += received;
        }

        // Prepare SChannel buffers
        SecBuffer bufs[4] = { 0 };
        SecBufferDesc desc = { SECBUFFER_VERSION, 4, bufs };

        bufs[0].BufferType = SECBUFFER_DATA;
        bufs[0].cbBuffer = tls->enc_hold_len;
        bufs[0].pvBuffer = tls->enc_hold;

        bufs[1].BufferType = SECBUFFER_EMPTY;
        bufs[2].BufferType = SECBUFFER_EMPTY;
        bufs[3].BufferType = SECBUFFER_EMPTY;

        SECURITY_STATUS sc = DecryptMessage(&tls->ctx, &desc, 0, NULL);

        if (sc == SEC_E_INCOMPLETE_MESSAGE) {
            // Need more data: loop again
            continue;
        }

        if (sc != SEC_E_OK && sc != SEC_I_RENEGOTIATE) {
            return -1;
        }

        // Copy all decrypted data into dec_buffer
        i32 total_dec_len = 0;
        for (int i = 0; i < 4; ++i) {
            if (bufs[i].BufferType == SECBUFFER_DATA) {
                memcpy(dec + total_dec_len, bufs[i].pvBuffer, bufs[i].cbBuffer);
                total_dec_len += bufs[i].cbBuffer;
            }
        }
        *used = total_dec_len;

        // Preserve leftover encrypted bytes for next call
        tls->enc_hold_len = 0;
        for (int i = 0; i < 4; ++i) {
            if (bufs[i].BufferType == SECBUFFER_EXTRA) {
                memmove(tls->enc_hold, bufs[i].pvBuffer, bufs[i].cbBuffer);
                tls->enc_hold_len = bufs[i].cbBuffer;
                break;
            }
        }

        break; // successfully decrypted some data
    }

    if (*used == 0) return 0;

    // Serve up to max_len bytes to caller
    i32 n = (*used < max_len) ? *used : max_len;
    memcpy(out, dec, n);
    memmove(dec, dec + n, *used - n);
    *used -= n;

    return n;
}

void qnet_tls_close(qnet_tls_socket* tls) {
    if (tls->handshake_complete) {
        DeleteSecurityContext(&tls->ctx);
    }

    FreeCredentialsHandle(&tls->cred);
    free(tls->enc_hold);
    free(tls->enc_buffer);
    free(tls->dec_buffer);
}

#endif