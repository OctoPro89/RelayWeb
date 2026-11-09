#pragma once
#define QNET_RELIABLE_UDP_MAX_RETRIES 5
#define QNET_RELIABLE_UDP_TIMEOUT_MS 500

#include <qnet/common.h>
#include <qnet/socket.h>

typedef struct {
    u32 seq;      // Packet sequence number
    u8 ack;       // 1 if this is an ACK packet, 0 otherwise
} reliable_udp_header;

typedef struct {
    qnet_socket socket;
    u32 next_seq;
} reliable_udp_sender;

typedef struct {
    qnet_socket socket;
    u32 expected_seq;
} reliable_udp_receiver;

// Send reliable UDP packet
QNETAPI b8 reliable_udp_send_packet(reliable_udp_sender* sender, qnet_ip_endpoint* to, const void* data, i32 bytes);

// Receive reliable UDP packet
QNETAPI b8 reliable_udp_recv_packet(reliable_udp_receiver* receiver, void* out_data, i32* out_bytes, qnet_ip_endpoint* from);