#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
#include "message.h"

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

void udp_phase(const char* server_ip, int udp_port, int payload_size)
{
    if (payload_size < 1 || payload_size > MAX_MSG_LEN) {
        fprintf(stderr, "Invalid message size: %d (must be between 1 and %d bytes)\n", payload_size, MAX_MSG_LEN);
        exit(1);
    }

    int udp_sockfd;
    struct sockaddr_in serv_udp_addr;
    Message msg;
    struct timeval start_time, end_time;

    udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sockfd < 0) error("Error creating UDP socket");

    memset(&serv_udp_addr, 0, sizeof(serv_udp_addr));
    serv_udp_addr.sin_family = AF_INET;
    serv_udp_addr.sin_port = htons(udp_port);
    serv_udp_addr.sin_addr.s_addr = inet_addr(server_ip);

    msg.msg_type = 3;
    memset(msg.msg, 'A', payload_size);  // Fill with 'A'
    msg.msg[payload_size] = '\0';
    msg.msg_length = payload_size;

    socklen_t serv_len = sizeof(serv_udp_addr);

    gettimeofday(&start_time, NULL);

    if (sendto(udp_sockfd, &msg, sizeof(int) * 2 + payload_size, 0,
               (struct sockaddr *)&serv_udp_addr, serv_len) < 0)
    {
        close(udp_sockfd);
        error("Error sending UDP message");
    }

    Message response;
    if (recvfrom(udp_sockfd, &response, sizeof(response), 0,
                 (struct sockaddr *)&serv_udp_addr, &serv_len) < 0)
    {
        close(udp_sockfd);
        error("Error receiving UDP response");
    }

    gettimeofday(&end_time, NULL);

    // Throughput and RTT calculations
    double rtt_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                    (end_time.tv_usec - start_time.tv_usec) / 1000.0;

    double throughput_kbps = (payload_size / 1024.0) / (rtt_ms / 1000.0);

    printf("Sent Type 3 UDP message (%d bytes)\n", payload_size);
    printf("Received Type 4 UDP response: %s\n", response.msg);
    printf("Round-trip time: %.3f ms\n", rtt_ms);
    printf("Throughput: %.3f KB/s\n", throughput_kbps);

    close(udp_sockfd);
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <server_ip> <tcp_port> <udp_message_size_bytes>\n", argv[0]);
        exit(1);
    }

    const char *server_ip = argv[1];
    int port_no = atoi(argv[2]);
    int udp_msg_size = atoi(argv[3]);

    int sockfd;
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("Error opening the socket");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port_no);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0)
        error("Invalid server address");

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("Connection failed");

    printf("Connected to the server at %s:%d\n", server_ip, port_no);

    // send Type 1 message
    Message request;
    request.msg_type = 1;
    snprintf(request.msg, MAX_MSG_LEN, "Hey, i would like to communicate. please give me the secret UDP port no.");
    request.msg_length = strlen(request.msg);
    send(sockfd, &request, sizeof(request), 0);

    // receive Type 2 response
    Message reply;
    memset(&reply, 0, sizeof(reply));
    if (recv(sockfd, &reply, sizeof(reply), 0) < 0)
        error("Error reading response from server");

    printf("Server replied [Type %d]: %s\n", reply.msg_type, reply.msg);

    int udp_port;
    if (sscanf(reply.msg, "UDP_PORT:%d", &udp_port) != 1) {
        fprintf(stderr, "Failed to parse UDP port.\n");
        exit(1);
    }

    printf("Parsed UDP port: %d\n", udp_port);

    // Now start UDP phase
    udp_phase(server_ip, udp_port, udp_msg_size);

    close(sockfd);
    return 0;
}



