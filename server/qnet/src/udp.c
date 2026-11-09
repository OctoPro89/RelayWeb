#include <qnet/udp.h>
#include <qnet/endpoint.h>

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>

#include <stdlib.h>

// Send reliable UDP packet
QNETAPI b8 reliable_udp_send(reliable_udp_sender* sender, qnet_ip_endpoint* to, const void* data, i32 bytes) {
    u32 seq = sender->next_seq++;
    char packet[1500];
    reliable_udp_header* header = (reliable_udp_header*)packet;
    header->seq = htonl(seq);
    header->ack = 0;
    memcpy(packet + sizeof(reliable_udp_header), data, bytes);

    for (int attempt = 0; attempt < QNET_RELIABLE_UDP_MAX_RETRIES; ++attempt) {
        qnet_socket_send_packet(sender->socket, packet, bytes + sizeof(reliable_udp_header), to);

        // Wait for ACK
        char ack_packet[sizeof(reliable_udp_header)];
        qnet_ip_endpoint from;
        i32 received = 0;
        qnet_socket_poll_events ev;
        qnet_socket_poll(&sender->socket, 1, QNET_RELIABLE_UDP_TIMEOUT_MS, qnet_socket_poll_events_read, &ev);

        if (qnet_socket_recv_packet(sender->socket, ack_packet, &received, &from)) {
            reliable_udp_header* ack_header = (reliable_udp_header*)ack_packet;
            if (ack_header->ack && ntohl(ack_header->seq) == seq) {
                return true; // ACK received
            }
        }
    }

    return false; // Failed after retries
}

// Receive reliable UDP packet
QNETAPI b8 reliable_udp_recv(reliable_udp_receiver* receiver, void* out_data, i32* out_bytes, qnet_ip_endpoint* from) {
    char packet[1500];
    i32 received = 0;

    if (!qnet_socket_recv_packet(receiver->socket, packet, &received, from)) {
        return false;
    }

    reliable_udp_header* header = (reliable_udp_header*)packet;

    // Send ACK
    reliable_udp_header ack_header = { 0 };
    ack_header.seq = header->seq;
    ack_header.ack = 1;
    qnet_socket_send_packet(receiver->socket, &ack_header, sizeof(ack_header), from);

    // Only accept in-order packets
    u32 seq = ntohl(header->seq);
    if (seq == receiver->expected_seq) {
        *out_bytes = received - sizeof(reliable_udp_header);
        memcpy(out_data, packet + sizeof(reliable_udp_header), *out_bytes);
        receiver->expected_seq++;
        return true;
    }

    return false; // Out-of-order packet, ignore
}
