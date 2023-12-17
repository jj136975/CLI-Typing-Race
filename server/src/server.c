#include <arpa/inet.h>
#include <memory.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "packet.h"

#define     MAX_PLAYERS         4

#define     GAME_WAITTING_TIME  15
#define     GAME_RUNNING_TIME   60

typedef struct linked_word_s
{
    int                     size;
    char                    word[MAX_STRING_SIZE];
    struct linked_word_s   *next;
} linked_word_t;

typedef struct player_s
{
    player_info_t   info;
    int             socket;
    net_status_t    status;
    int             start_words;
    char            name[MAX_PLAYER_NAME_SIZE];
    linked_word_t  *current;
} player_t;

#define     MAX_SCORE   50

#define     HAS_BROKEN_SOCK     0x1

typedef struct game_server_s
{
    game_state_t    state;
    int             time_remain;
    int             running;
    player_t        players[MAX_PLAYERS];
    int             player_count;
    fd_set          rd_set;
    int             socket;
    int             await;
    int             flags;
    linked_word_t  *words;
    linked_word_t  *last;
} game_server_t;

static inline int min(int a, int b) {
    return b > a ? b : a;
}



/////////// FORWARD DECLARATIONS ////////////

void game_server_destroy(game_server_t *game);
void game_player_remove(game_server_t *game, player_t *player);
void game_handle_packet(game_server_t *game, int socket, const packet_t *packet);



/////////// WORDS ////////////

#define     BUFFER_SIZE     2048

char BUFFER[BUFFER_SIZE + 1];

static const char DELIMITER[] = " ,.\n";

linked_word_t *word_list_create(const char *filename) {
    int fd = open(filename, O_RDONLY);
    ssize_t size;
    char *pch;
    linked_word_t list;
    linked_word_t *last = &list;

    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    do {
        if ((size = read(fd, BUFFER, BUFFER_SIZE)) < 0) {
            perror("read");
            exit(EXIT_FAILURE);
        }
        BUFFER[size] = 0;
        pch = strtok(BUFFER, DELIMITER);
        while (pch != NULL) {
            last = (last->next = malloc(sizeof(linked_word_t)));
            if (last == NULL) {
                close(fd);
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            last->size = min(strlen(pch), MAX_STRING_SIZE);
            strncpy(last->word, pch, last->size);
            pch = strtok(NULL, DELIMITER);
        }
    } while (size != 0);
    close(fd);
    last->next = list.next;
    return list.next;
}

void word_list_destroy(linked_word_t *list) {
    linked_word_t *actual = list;
    linked_word_t *next;

    if (list == NULL)
        return;
    while ((next = actual->next) != list) {
        free(actual);
        actual = next;
    }
    free(actual);
}



/////////// NETWORK ////////////

static int MAX_FD = -1;

void net_init(game_server_t *game, const char *host, int port) {
    int sockfd;
    struct sockaddr_in serv;

    if ((sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    serv.sin_family = PF_INET;
    serv.sin_port = htons(port);
    inet_aton(host, &serv.sin_addr);
    if (bind(sockfd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("bind");
        game_server_destroy(game);
        exit(EXIT_FAILURE);
    }
    if (listen(sockfd, SOMAXCONN) < 0) {
        perror("listen");
        game_server_destroy(game);
        exit(EXIT_FAILURE);
    }
    printf("[INFO] Server listening on %s:%d\n", host, port);
    game->socket = MAX_FD = sockfd;
}

void net_broadcast_packet(game_server_t *game, const packet_t *packet, int except_id) {
    printf("Broadcast packet %d to %d players except player %d\n", packet->id, game->player_count, except_id);
    for (int i = 0; i < game->player_count; ++i)
        if (game->players[i].info.player_id != except_id && game->players[i].status == STABLE
            && write(game->players[i].socket, packet, sizeof(packet_t)) < 0) {
                game->players[i].status = BROKEN;
                game->flags |= HAS_BROKEN_SOCK;
            }
            // game_handle_packet(game, game->players[i--].socket, &(packet_t){.id=CLIENT_DISCONNECT, .packet.client.player_leave={.reason="Lost connection"}});
}

void net_send_packet(game_server_t *game, const packet_t *packet, player_t *player) {
    printf("Sending packet %d to player %d\n", packet->id, player->info.player_id);
    if (player->status == STABLE && write(player->socket, packet, sizeof(packet_t)) < 0) {
        player->status = BROKEN;
        game->flags |= HAS_BROKEN_SOCK;
    }
            // game_handle_packet(game, player->socket, &(packet_t){.id=CLIENT_DISCONNECT, .packet.client.player_leave={.reason="Lost connection"}});
}

void net_client_accept(game_server_t *game) {
    int socket;
    struct sockaddr_in clnt;
    socklen_t sin_siz = sizeof(clnt);

    if ((socket = accept(game->socket, (struct sockaddr *)&clnt, &sin_siz)) < 0) {
        perror("accept");
        game_server_destroy(game);
        exit(EXIT_FAILURE);
    }
    if (game->await != -1) {
        close(game->await);
        FD_CLR(game->await, &game->rd_set);
        printf("[INFO] Awaitting client on socket %d has expired\n", game->await);
    }
    game->await = socket;
    if (socket > MAX_FD)
        MAX_FD = socket;
    printf("[INFO] Connection from %s:%d\n", inet_ntoa(clnt.sin_addr), ntohs(clnt.sin_port));
}

net_status_t net_client_read(game_server_t *game, int socket) {
    static packet_t packet;
    ssize_t size;

    if ((size = read(socket, &packet, sizeof(packet_t))) != sizeof(packet_t)) {
        if (size == 0) {
            printf("[INFO] Connection closed on socket: %d\n", socket);
            return CLOSING;
        }
        if (size < 0) {
            fprintf(stderr, "[ERROR] Could not read socket: %d\n", socket);
            game->flags |= HAS_BROKEN_SOCK;
            return BROKEN;
        }
        printf("[INFO] Invalid packet size (%ld) on socket: %d\n", size, socket);
    } else
        game_handle_packet(game, socket, &packet);
    return STABLE;
}

void net_loop(game_server_t *game) {
    static const struct timespec SELECT_TO = {.tv_sec=0, .tv_nsec=50000000};
    static sigset_t SIGSET;

    sigemptyset(&SIGSET);
    sigaddset(&SIGSET, SIGINT);

    FD_ZERO(&game->rd_set);
    FD_SET(game->socket, &game->rd_set);
    if (game->await != -1)
        FD_SET(game->await, &game->rd_set);
    for (int i = 0; i < game->player_count; ++i)
        FD_SET(game->players[i].socket, &game->rd_set);
    
    if (pselect(MAX_FD + 1, &game->rd_set, NULL, NULL, &SELECT_TO, &SIGSET) < 0) {
        if (!game->running)
            return;
        perror("select()");
        game_server_destroy(game);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < game->player_count; ++i)
        if (FD_ISSET(game->players[i].socket, &game->rd_set))
            game->players[i].status = net_client_read(game, game->players[i].socket);
    if (game->await != -1 && FD_ISSET(game->await, &game->rd_set) && net_client_read(game, game->await) != STABLE) {
        close(game->await);
        game->await = -1;
    }
    if (FD_ISSET(game->socket, &game->rd_set))
        net_client_accept(game);
}



/////////// PLAYER ////////////

void player_init(player_t *player, int socket, int start_words, const char name[MAX_PLAYER_NAME_SIZE]) {
    static int ID = 0;

    player->info = (player_info_t){.player_id=ID++, .score=0, .mode=SPECTATOR};
    player->socket = socket;
    player->status = STABLE;
    player->start_words = start_words;
    strncpy(player->name, name, MAX_PLAYER_NAME_SIZE);
    player->current = NULL;
}

void player_reset(player_t *player, linked_word_t *list) {
    player->info.score = 0;
    player->info.mode = SPECTATOR;
    player->current = list;
}

void player_destroy(player_t *player) {
    if (player->status != CLOSED) {
        close(player->socket);
        player->socket = -1;
        player->status = CLOSED;
    }
}

void player_send_word(game_server_t *game, player_t *player) {
    packet_t word_packet = {.id=SERVER_NEW_WORD};

    strncpy(word_packet.packet.server.new_word.word, player->current->word, MAX_STRING_SIZE);
    player->current = player->current->next;
    net_send_packet(game, &word_packet, player);
}

void player_send_update(game_server_t *game, player_t *player) {
    net_send_packet(game, &(packet_t){.id=SERVER_PLAYER_UPDATE, .packet.server.player_update=player->info}, player);
}

void player_send_join(game_server_t *game, player_t *player) {
    packet_t join_packet = {.id=SERVER_PLAYER_JOIN, .packet.server.player_join={.join_type=NEW_PLAYER, .info=player->info}};

    strncpy(join_packet.packet.server.player_join.name, player->name, MAX_PLAYER_NAME_SIZE);
    net_send_packet(game, &(packet_t){.id=SERVER_PLAYER_ACCEPT, .packet.server.player_accept=player->info}, player);
    net_broadcast_packet(game, &join_packet, player->info.player_id);
    join_packet.packet.server.player_join.join_type=OLD_PLAYER;
    for (int i = 0; i < game->player_count; ++i) {
        if (game->players + i == player)
            continue;
        strncpy(join_packet.packet.server.player_join.name, game->players[i].name, MAX_PLAYER_NAME_SIZE);
        join_packet.packet.server.player_join.info = game->players[i].info;
        net_send_packet(game, &join_packet, player);
    }
}



/////////// GAME ////////////

void game_server_init(game_server_t *game, const char *host, int port,  const char *filename) {
    game->state = WAITTING;
    game->time_remain = GAME_WAITTING_TIME;
    game->player_count = 0;
    FD_ZERO(&game->rd_set);
    game->flags = 0;
    game->words = word_list_create(filename);
    game->last = game->words;
    game->await = -1;
    net_init(game, host, port);
}

void game_server_destroy(game_server_t *game) {
    for (int i = 0; i < game->player_count; ++i)
        close(game->players[i].socket);
    close(game->socket);
    word_list_destroy(game->words);
}

void game_server_clean(game_server_t *game) {
    game->flags = 0;
    for (int i = 0; i < game->player_count; ++i) {
        switch (game->players[i].status)
        {
        case STABLE:
        case CLOSED:
            continue;
        
        case BROKEN:
        case CLOSING:
            game_player_remove(game, game->players + i);
        }
    }
}

void game_server_start(game_server_t *game) {
    game->running = 1;

    while (game->running)
    {
        net_loop(game);
        while (game->flags != 0)
            game_server_clean(game);
    }
}

player_t *game_find_player(game_server_t *game, int socket) {
    for (int i = 0; i < game->player_count; ++i)
        if (game->players[i].socket == socket)
            return &game->players[i];
    return NULL;
}

int game_find_player_idx(game_server_t *game, int id) {
    for (int i = 0; i < game->player_count; ++i)
        if (game->players[i].info.player_id == id)
            return i;
    return -1;
}

void game_update_all_players(game_server_t *game) {
    for (int i = 0; i < game->player_count; ++i)
        net_broadcast_packet(game, &(packet_t){
            .id=SERVER_PLAYER_UPDATE,
            .packet.server.player_update=game->players[i].info
        }, -1);
}

void game_start(game_server_t *game) {
    game->state = RUNNING;
    game->time_remain = GAME_RUNNING_TIME;
    for (int i = 0; i < game->player_count; ++i) {
        player_reset(game->players + i, game->last);
        game->players[i].info.mode = PLAYER;
        for (int i = 0; i < game->players[i].start_words && game->players[i].status == STABLE; ++i)
            player_send_word(game, game->players + i);
    }
    game_update_all_players(game);
    printf("[INFO] Game has started with %d players\n", game->player_count);
    net_broadcast_packet(game, &(packet_t){.id=SERVER_GAME_STATUS, .packet.server.game_status={.state=game->state, .time_remain=game->time_remain}}, -1);
}

void game_end(game_server_t *game, player_t *winner) {
    game->state = WAITTING;
    game->time_remain = GAME_WAITTING_TIME;

    if (winner == NULL)
        printf("[INFO] Game has ended without winner\n");
    else {
        game->last = winner->current;
        printf("[INFO] Game has ended won by: %.*s\n", MAX_PLAYER_NAME_SIZE, winner->name);
    }
    net_broadcast_packet(game, &(packet_t){.id=SERVER_GAME_STATUS, .packet.server.game_status={.state=game->state, .time_remain=game->time_remain}}, -1);
}

void game_player_add(game_server_t *game, int socket, const client_player_infos_t *packet) {
    player_t *player = game->players + game->player_count;

    if (packet->start_words < MIN_START_WORDS || packet->start_words > MAX_START_WORDS) {
        fprintf(stderr, "[ERROR] Player %.*s asked invalid start words: %d\n", MAX_PLAYER_NAME_SIZE, packet->name, packet->start_words);
        return;
    }
    player_init(player, socket, packet->start_words, packet->name);
    printf("[+] %.*s has joined\n", MAX_PLAYER_NAME_SIZE, packet->name);
    game->player_count++;
    game->await = -1;
    player_send_join(game, player);
}

void game_player_remove(game_server_t *game, player_t *player) {
    int idx = game_find_player_idx(game, player->info.player_id);

    if (idx == -1) {
        fprintf(stderr, "[ERROR] Player with id %d not found\n", player->info.player_id);
        return;
    }
    printf("[-] %.*s has left\n", MAX_PLAYER_NAME_SIZE, player->name);
    FD_CLR(player->socket, &game->rd_set);
    player_destroy(player);
    net_broadcast_packet(game, &(packet_t){.id=SERVER_PLAYER_REMOVE, .packet.server.player_remove={.player_id=player->info.player_id}}, player->info.player_id);
    game->player_count--;
    if (idx != game->player_count)
        game->players[idx] = game->players[game->player_count];
    if (game->player_count == 0)
        game_end(game, NULL);
}


void game_handle_packet(game_server_t *game, int socket, const packet_t *packet) {
    player_t *player = game_find_player(game, socket);

    printf("Client %d packet: %d\n", player != NULL ? player->info.player_id : -1, packet->id);

    if (player == NULL && packet->id != CLIENT_PLAYER_INFOS) {
        close(socket);
        return;
    }

    switch (packet->id)
    {
    case CLIENT_PLAYER_INFOS:
        if (player == NULL) {
            if (game->player_count == MAX_PLAYERS)
                close(socket);
            else
                game_player_add(game, socket, &packet->packet.client.player_infos);
        }
        break;
    
    case CLIENT_WORD_COMPLETE:
        if (game->state == RUNNING && player->current != NULL) {
            player->current = player->current->next;
            if (player->info.score++ >= MAX_SCORE)
                game_end(game, player);
        }
        break;
    
    case CLIENT_DISCONNECT:
        game_player_remove(game, player);
        break;
    
    default:
        printf("[INFO] Invalid packet received by player: %d (%.*s)\n", player->info.player_id, MAX_PLAYER_NAME_SIZE, player->name);
    }
}



/////////// SIGNAL ////////////

static int *TARGET = NULL;

void signal_handler(int signal) {
    if (TARGET == NULL)
        fprintf(stderr, "[ERROR] No target for signal (%d)\n", signal);
    else {
        *TARGET = 0;
        printf("[INFO] Gracefully shutting down server\n");
    }
}



/////////// MAIN ////////////

static const char USAGE[] = "./server [host] [port] [file]\n";

int main(int ac, char **av) {
    game_server_t game;
    int port;

    if (ac != 4) {
        fprintf(stderr, USAGE);
        exit(EXIT_FAILURE);
    }

    port = strtol(av[2], NULL, 10);
    if (port == 0) {
        fprintf(stderr, "[ERROR] Invalid port: %s\n", av[2]);
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, signal_handler);
    game_server_init(&game, av[1], port, av[3]);
    TARGET = &game.running;
    game_server_start(&game);
    game_server_destroy(&game);
    return EXIT_SUCCESS;
}