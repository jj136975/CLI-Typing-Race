#include <arpa/inet.h>
#include <memory.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "packet.h"

typedef struct game_client_s
{
    int             socket;
    net_status_t    status;
    int             running;
} game_client_t;



/////////// FORWARD DECLARATIONS ////////////

void game_handle_packet(game_client_t *game, int socket, const packet_t *packet);



/////////// NETWORK ////////////

void net_init(game_client_t *client, const char *host, int port) {
    struct sockaddr_in serv;

    if ((client->socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket");
        exit(1);
    }
    printf("socket() called\n");

    serv.sin_family = PF_INET;
    serv.sin_addr.s_addr = inet_addr(host);
    serv.sin_port = htons(port);

    if (connect(client->socket, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    printf("connect() called\n");
    client->status = STABLE;
}

void net_send_packet(game_client_t *client, const packet_t *packet) {
    if (write(client->socket, packet, sizeof(packet_t)) < 0) {
        perror("write");
        client->status = BROKEN;
        close(client->socket);
        exit(EXIT_FAILURE);
    }
}

int net_game_read(game_client_t *client, int socket) {
    static packet_t packet;
    ssize_t size;

    if ((size = read(socket, &packet, sizeof(packet_t))) != sizeof(packet_t)) {
        if (size == 0) {
            printf("[INFO] Connection closed on socket: %d\n", socket);
            return CLOSING;
        }
        if (size < 0) {
            fprintf(stderr, "[ERROR] Could not read socket: %d\n", socket);
            return BROKEN;
        }
        printf("[INFO] Invalid packet size (%ld) on socket: %d\n", size, socket);
    } else
        game_handle_packet(client, socket, &packet);
    return STABLE;
}


/////////// CLIENT ////////////

void game_client_init(game_client_t *client, const char *host, int port, const char *name) {
    packet_t join_packet = {.id=CLIENT_PLAYER_INFOS, .packet.client.player_infos={.width=MAX_WIDTH}};

    strncpy(join_packet.packet.client.player_infos.name, name, MAX_PLAYER_NAME_SIZE);
    net_init(client, host, port);
    net_send_packet(client, &join_packet);
}

void game_client_destroy(game_client_t *client) {
    net_send_packet(client, &(packet_t){.id=CLIENT_DISCONNECT, .packet.client.player_leave={"Client disconnect"}});
    close(client->socket);
    client->status = CLOSED;
}

void game_client_start(game_client_t *client) {
    client->running = 1;
    while (client->running)
    {
    }
}


void game_handle_packet(game_client_t *game, int socket, const packet_t *packet) {
    // Suppress unused warnings
    if (game == NULL && socket == -1) return;
    
    printf("Server packet: %d\n", packet->id);

    switch (packet->id)
    {
    case SERVER_GAME_STATUS:
        break;
    case SERVER_PLAYER_UPDATE:
        break;
    case SERVER_PLAYER_REMOVE:
        break;
    case SERVER_PLAYER_JOIN:
        break;
    case SERVER_NEW_LINE:
        break;
    
    default:
        printf("[INFO] Invalid packet received: %d\n", packet->id);
    }
}


/////////// SIGNAL ////////////

static int *TARGET = NULL;

void signal_handler(int signal) {
    if (TARGET == NULL)
        fprintf(stderr, "[ERROR] No target for signal (%d)\n", signal);
    else {
        *TARGET = 0;
        printf("[INFO] Gracefully shutting down client\n");
    }
}



/////////// MAIN ////////////

static const char USAGE[] = "Usage: ./client [ip] [port] [name]\n";

int main(int argc, char *argv[]) {
    game_client_t client;
    int port;

    if (argc != 4) {
        fprintf(stderr, USAGE);
        exit(EXIT_FAILURE);
    }
    port = strtol(argv[2], NULL, 10);
    if (port == 0) {
        fprintf(stderr, "[ERROR] Invalid port: %s\n", argv[2]);
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, signal_handler);
    game_client_init(&client, argv[1], port, argv[3]);
    TARGET = &client.running;
    game_client_start(&client);
    game_client_destroy(&client);
    return EXIT_SUCCESS;
}