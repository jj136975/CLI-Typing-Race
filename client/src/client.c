#include <arpa/inet.h>
#include <memory.h>
#include <ncurses.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "packet.h"

#define MAX_PLAYER 4

typedef struct word_list_s
{
    struct word_list_s *next;
    char word[MAX_STRING_SIZE];
} word_list_t;

typedef struct scorboard_s {
    char name[MAX_PLAYER_NAME_SIZE];
    int player_id;
    int score;
} scorboard_t;

typedef struct game_client_s
{
    int                  socket;
    fd_set               set;
    player_info_t        info;
    net_status_t         status;
    int                  running;
    word_list_t          *words;
    server_game_status_t game_status;
    scorboard_t          scores[MAX_PLAYER];
    int                  player_count;
} game_client_t;

/////////// FORWARD DECLARATIONS ////////////

void game_client_destroy(game_client_t *client);
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

net_status_t net_game_read(game_client_t *client) {
    static packet_t packet;
    ssize_t size;

    if ((size = read(client->socket, &packet, sizeof(packet_t))) != sizeof(packet_t)) {
        if (size == 0) {
            printf("[INFO] Connection closed on socket: %d\n", client->socket);
            return CLOSING;
        }
        if (size < 0) {
            fprintf(stderr, "[ERROR] Could not read socket: %d\n", client->socket);
            return BROKEN;
        }
        printf("[INFO] Invalid packet size (%ld) on socket: %d\n", size, client->socket);
    } else
        game_handle_packet(client, client->socket, &packet);
    return STABLE;
}

void net_loop(game_client_t *client) {
    static const struct timespec SELECT_TO = {.tv_sec=0, .tv_nsec=50000000};
    static sigset_t SIGSET;

    sigemptyset(&SIGSET);
    sigaddset(&SIGSET, SIGINT);
    sigaddset(&SIGSET, SIGALRM);
    sigaddset(&SIGSET, SIGWINCH);

    FD_ZERO(&client->set);
    FD_SET(client->socket, &client->set);

    if (pselect(client->socket + 1, &client->set, NULL, NULL, &SELECT_TO, &SIGSET) < 0) {
        perror("select()");
        game_client_destroy(client);
        exit(EXIT_FAILURE);
    }
    if (FD_ISSET(client->socket, &client->set) && (client->status = net_game_read(client)) != STABLE) {
        game_client_destroy(client);
        exit(EXIT_FAILURE);
    }
}



/////////// CLIENT ////////////

void game_client_init(game_client_t *client, const char *host, int port, const char *name) {
    packet_t join_packet = {.id=CLIENT_PLAYER_INFOS, .packet.client.player_infos={.start_words=MAX_START_WORDS}};

    client->player_count = 0;
    client->words = 0;
    client->game_status.time_remain = -1;
    client->game_status.state = WAITTING;
    strncpy(join_packet.packet.client.player_infos.name, name, MAX_PLAYER_NAME_SIZE);
    strncpy(client->scores[0].name, name, MAX_PLAYER_NAME_SIZE);
    net_init(client, host, port);
    net_send_packet(client, &join_packet);
}

void game_client_destroy(game_client_t *client) {
    if (client->status == STABLE)
        net_send_packet(client, &(packet_t){.id=CLIENT_DISCONNECT, .packet.client.player_leave={"Client disconnect"}});
    close(client->socket);
    endwin();
    client->status = CLOSED;
}

void init_ncurse_win()
{
    initscr();
    start_color();
    noecho();
    curs_set(0);
    timeout(100);
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_WHITE, COLOR_BLACK);
    init_pair(3, COLOR_WHITE, COLOR_BLUE);
}

int invalid_terminal_size()
{
    if (LINES < 5 || COLS < 80) {
        mvprintw(0, 0, "Increase your terminal size to x>=5 and y >= 80 (x=%d and y=%d)", LINES, COLS);
        refresh();
        sleep(1000);
        return 1;
    }
    return 0;
}

void timer(int time)
{
    if (time < 0)
        mvprintw(0, COLS - 7, "PAUSED");
    else
        mvprintw(0, COLS - 4, "%- .3ds", time);
}

void scorboard(scorboard_t *players, int player_count)
{
    for (int i = 0; i < player_count; i++) {
        mvprintw(i + 2, COLS - MAX_PLAYER_NAME_SIZE - 4, "%.*s ", MAX_PLAYER_NAME_SIZE, players[i].name);
        mvprintw(i + 2, COLS - 4, "%.4d", players[i].score);
    }
}

void writing_screen(game_client_t *game, int *pos)
{
    char word[MAX_STRING_SIZE];

    if (!game->words) {
        return;
    }
    strncpy(word, game->words->word, MAX_STRING_SIZE);
    move(1, 1);
    attron(COLOR_PAIR(1));
    for (int i = 0; i < *pos; i++) {
        printw("%c", word[i]);
    }
    attroff(COLOR_PAIR(1));
    attron(COLOR_PAIR(3));
    printw("%c", word[*pos]);
    attroff(COLOR_PAIR(3));
    attron(COLOR_PAIR(2));
    for (int i = *pos + 1; i < 10; i++) {
        printw("%c", word[i]);
    }
    attroff(COLOR_PAIR(2));
    refresh();
    if (getch() == word[*pos]) {
        (*pos)++;
    }
    if (*pos == MAX_STRING_SIZE || word[*pos] == 0) {
        net_send_packet(game, &(packet_t){.id=CLIENT_WORD_COMPLETE});
        word_list_t *tmp = game->words;
        game->words = game->words->next;
        free(tmp);
        (*pos) = 0;
    }
}

void waiting_screen()
{
    mvprintw(0, 0, "Waiting for players...");
    refresh();
}

void game_client_start(game_client_t *client)
{
    int cursor_pos = 0;

    init_ncurse_win();
    client->running = 1;
    while (client->running)
    {
        net_loop(client);
        clear();
        if (invalid_terminal_size())
            continue;
        timer(client->game_status.time_remain);
        scorboard(client->scores, client->player_count);
        if (client->game_status.state == RUNNING)
            writing_screen(client, &cursor_pos);
        else
            waiting_screen();
    }
}


void game_handle_packet(game_client_t *game, int socket, const packet_t *packet) {
    // Suppress unused warnings
    if (game == NULL && socket == -1) return;

    printf("Server packet: %d\n", packet->id);

    switch (packet->id)
    {
    case SERVER_GAME_STATUS:
        if (packet->packet.server.game_status.state != game->game_status.state) {
            if (packet->packet.server.game_status.state == WAITTING) {
                while (game->words) {
                    word_list_t *list = game->words;
                    game->words = game->words->next;
                    free(list);
                }
            }
            game->game_status = packet->packet.server.game_status;
        }
        break;
    case SERVER_PLAYER_UPDATE:
        if (packet->packet.server.player_update.player_id != game->info.player_id) {
            break;
        }
        [[fallthrough]];
    case SERVER_PLAYER_ACCEPT:
        game->info = packet->packet.server.player_accept;
        game->scores[game->player_count].score = game->info.score;
        game->scores[game->player_count].player_id = game->info.player_id;
        game->player_count++;
        break;
    
    case SERVER_PLAYER_REMOVE:
        for (int i = 0; i < MAX_PLAYER; i++) {
            if (game->scores[i].player_id == packet->packet.server.player_remove.player_id) {
                game->player_count--;
                if (i != game->player_count)
                    game->scores[i] = game->scores[game->player_count];
            }
        }
        break;
    case SERVER_PLAYER_JOIN:
        server_player_join_t info = packet->packet.server.player_join;
        game->scores[game->player_count].score = info.info.score;
        game->scores[game->player_count].player_id = info.info.player_id;
        strncpy(game->scores[game->player_count].name, info.name, MAX_PLAYER_NAME_SIZE);
        game->player_count++;
        break;
    case SERVER_NEW_WORD:
        word_list_t *node = malloc(sizeof(node));
        word_list_t *it = game->words;

        node->next = 0;
        strncpy(node->word, packet->packet.server.new_word.word, MAX_STRING_SIZE);
        if (!game->words) {
            game->words = node;
            break;
        }
        while (it && it->next) {
            it = it->next;
        }
        it->next = node;
        break;
    default:
        printf("[INFO] Invalid packet received: %d\n", packet->id);
    }
}


/////////// SIGNAL ////////////

static int *TARGET = NULL;
static int *REMAIN = NULL;
static const struct itimerval TIMER = {
    .it_interval={.tv_sec=1, .tv_usec=0},
    .it_value={.tv_sec=1, .tv_usec=0}
};

void signal_handler(int signal) {
    if (TARGET == NULL)
        fprintf(stderr, "[ERROR] No target for signal (%d)\n", signal);
    else {
        *TARGET = 0;
        printf("[INFO] Gracefully shutting down client\n");
    }
}

void alarm_handler(int signal) {
    if (signal == SIGALRM && REMAIN != NULL && *REMAIN > 0) {
        *REMAIN -= TIMER.it_value.tv_sec;
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
    signal(SIGALRM, alarm_handler);
    setitimer(ITIMER_REAL, &TIMER, NULL);
    game_client_init(&client, argv[1], port, argv[3]);
    TARGET = &client.running;
    REMAIN = &client.game_status.time_remain;
    game_client_start(&client);
    game_client_destroy(&client);
    return EXIT_SUCCESS;
}
