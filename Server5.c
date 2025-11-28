#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>


#include "message.h"

// Client request struct
typedef struct {
    int client_sockfd;
    struct sockaddr_in client_addr;
} threadargs;

// Queue Node
typedef struct node {
    threadargs* data;
    struct node* next;
} node;
void* handle_client(void* arg);
// Queue with head and tail pointers
node* queue_head = NULL;
node* queue_tail = NULL;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

// Mutex for round-robin scheduling
pthread_mutex_t rr_lock = PTHREAD_MUTEX_INITIALIZER;
// Mutex for FCFS scheduler
pthread_mutex_t fcfs_lock = PTHREAD_MUTEX_INITIALIZER;

// Function to enqueue a client to the queue with FCFS synchronization
void enqueue(threadargs* args) {
    node* new_node = malloc(sizeof(node));
    new_node->data = args;
    new_node->next = NULL;

    // Locking queue for FCFS synchronization
    pthread_mutex_lock(&queue_lock);
    if (queue_tail == NULL) {
        queue_head = new_node;
        queue_tail = new_node;
    } else {
        queue_tail->next = new_node;
        queue_tail = new_node;
    }
    pthread_mutex_unlock(&queue_lock);
}

// Function to dequeue a client from the queue
threadargs* dequeue() {
    pthread_mutex_lock(&queue_lock);
    if (queue_head == NULL) {
        pthread_mutex_unlock(&queue_lock);
        return NULL;
    }

    node* temp = queue_head;
    threadargs* client = temp->data;
    queue_head = queue_head->next;

    if (queue_head == NULL) {
        queue_tail = NULL;
    }

    free(temp);
    pthread_mutex_unlock(&queue_lock);
    return client;
}

// FCFS Scheduler Logic (Fixed)
void* fcfs_scheduler(void* arg) {
    while (1) {
        threadargs* client = dequeue();
        if (client != NULL) {
            // Create a new thread to handle the client
            pthread_t tid;
            if (pthread_create(&tid, NULL, handle_client, (void*)client) != 0) {
                close(client->client_sockfd);
                free(client);
                perror("Failed to create thread");
            } else {
                pthread_detach(tid); // auto-cleans up after thread finishes
            }
        }
        usleep(100); // Small delay to avoid busy-waiting
    }
}

void* rr_scheduler_func(void* arg) {
    while (1) {
        pthread_mutex_lock(&queue_lock);
        if (client_count == 0) {
            pthread_mutex_unlock(&queue_lock);
            sleep(1);
            continue;
        }

        for (int i = 0; i < MAX_CLIENTS; ++i) {
            int idx = (queue_front + i) % MAX_CLIENTS;
            if (i >= client_count) break;
            ClientArgs* args = client_queue[idx];
            if (args && args->phase != DONE) {
                printf("[Scheduler] Resuming client %d\n", args->id);
                resume_thread(args->id);
                sleep(TIME_SLICE);
                pause_thread(args->id);
            }
        }
        pthread_mutex_unlock(&queue_lock);
    }
    return NULL;
}



void error(const char *msg) {
    perror(msg);
    exit(1);
}

void udp_phase(int udp_port) {
    int udp_sockfd;
    struct sockaddr_in serv_udp_addr, cli_udp_addr;
    socklen_t cli_len = sizeof(cli_udp_addr);
    Message msg;

    udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sockfd < 0) error("UDP socket creation failed.");

    memset(&serv_udp_addr, 0, sizeof(serv_udp_addr));

    serv_udp_addr.sin_family = AF_INET;
    serv_udp_addr.sin_addr.s_addr = INADDR_ANY;
    serv_udp_addr.sin_port = htons(udp_port);

    if (bind(udp_sockfd, (struct sockaddr *)&serv_udp_addr, sizeof(serv_udp_addr)) < 0) {
        close(udp_sockfd);
        error("UDP binding error.");
    }

    printf("\nUDP waiting for data message on port %d....\n", udp_port);

    if (recvfrom(udp_sockfd, &msg, sizeof(msg), 0, (struct sockaddr *)&cli_udp_addr, &cli_len) < 0) {
        close(udp_sockfd);
        error("Error receiving UDP response.");
    }

    printf("UDP received Type %d message.\n", msg.msg_type);

    // Send type 4 msg
    Message response;
    response.msg_type = 4;
    strcpy(response.msg, "DATA received successfully!");
    response.msg_length = strlen(response.msg);

    if (sendto(udp_sockfd, &response, sizeof(response), 0, (struct sockaddr *)&cli_udp_addr, cli_len) < 0) {
        close(udp_sockfd);
        error("Error sending udp response.");
    } else {
        printf("UDP sent acknowledgement (type 4).\n");
    }

    close(udp_sockfd);
}

void* handle_client(void* arg) {
    threadargs* args = (threadargs*)arg;
    int newsocketfd = args->client_sockfd;
    struct sockaddr_in cli_addr = args->client_addr;

    // Generate a dynamic UDP port (use a base port and increment for each new client)
    static int udp_port_counter = 6900; // Starting port
    int udp_port = udp_port_counter++;
    
    // Avoid overflow by resetting after reaching a limit
    if (udp_port_counter > 7000) {
        udp_port_counter = 6900;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    printf("Connection established with client %s:%d\n", client_ip, ntohs(cli_addr.sin_port));

    // Process the client
    Message msg;
    memset(&msg, 0, sizeof(msg));
    int n = recv(newsocketfd, &msg, sizeof(msg), 0);
    if (n < 0) {
        close(newsocketfd);
        free(args);
        error("Error reading message from client.");
        pthread_exit(NULL);
    }
    printf("Received message [Type %d]: %s\n", msg.msg_type, msg.msg);

    // Send dynamic UDP port to client
    Message reply;
    reply.msg_type = 2;
    snprintf(reply.msg, MAX_MSG_LEN, "UDP_PORT:%d", udp_port);
    reply.msg_length = strlen(reply.msg);

    send(newsocketfd, &reply, sizeof(reply), 0);
    close(newsocketfd);
    printf("TCP phase completed. Assigned UDP port: %d\n", udp_port);

    // Enter UDP phase with the assigned port
    udp_phase(udp_port);

    free(args);
    pthread_exit(NULL);
}



int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Port number not provided. connection terminated.");
        exit(1);
    }

    int server_fd, newsocketfd, n;
    struct sockaddr_in serv_addr, cli_addr;
    int port_no = atoi(argv[1]);
    socklen_t clilen;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        error("Error opening socket.\n");
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_no);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) error("Error binding.");
    listen(server_fd, 3);

    printf("Waiting for connection request on port %d....\n", port_no);
    clilen = sizeof(cli_addr);

    // Choose scheduling type (FCFS or RR)
    char scheduling_type[4];
    printf("Enter scheduling type (FCFS/RR): ");
    scanf("%s", scheduling_type);

    pthread_t scheduler_tid;
    if (strcmp(scheduling_type, "FCFS") == 0) {
        pthread_create(&scheduler_tid, NULL, fcfs_scheduler, NULL);
    } else if (strcmp(scheduling_type, "RR") == 0) {
        pthread_create(&scheduler_tid, NULL, rr_scheduler, NULL);
    } else {
        fprintf(stderr, "Invalid scheduling type.\n");
        exit(1);
    }
    pthread_detach(scheduler_tid);

    while (1) {
        newsocketfd = accept(server_fd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsocketfd < 0) {
            error("Error accepting client");
            continue;
        }

        threadargs* args = malloc(sizeof(threadargs));
        args->client_sockfd = newsocketfd;
        args->client_addr = cli_addr;

        // Add client to the queue
        enqueue(args);
    }

    close(server_fd);
    return 0;
}
