
#pragma once

typedef enum packet_type_e {
    // Server -> Client
    SERVER_GAME_STATUS      =   0x01,
    SERVER_PLAYER_ACCEPT    =   0x02,
    SERVER_PLAYER_UPDATE    =   0x03,
    SERVER_PLAYER_REMOVE    =   0x04,
    SERVER_PLAYER_JOIN      =   0x05,
    SERVER_NEW_WORD         =   0x06,

    // Client -> Server
    CLIENT_PLAYER_INFOS     =   0x07,
    CLIENT_WORD_COMPLETE    =   0x08,
    CLIENT_DISCONNECT       =   0x09,
} packet_type_t;

typedef enum net_status_e {
    STABLE,
    BROKEN,
    CLOSING,
    CLOSED,
} net_status_t;

// Packet data types

#define     MAX_STRING_SIZE     32

typedef char packet_string_t[MAX_STRING_SIZE];

////////////// SERVER PACKETS ///////////////

// SERVER_GAME_STATUS

typedef enum game_state_e {
    WAITTING    =   0x01,
    RUNNING     =   0x02,
} game_state_t;

typedef struct server_game_status_s
{
    game_state_t    state;
    int             time_remain;
} server_game_status_t;

// SERVER_PLAYER_ACCEPT
typedef struct server_player_accept_s
{
    int     player_id;
} server_player_accept_t;

// SERVER_PLAYER_UPDATE
typedef enum player_type_e {
    PLAYER,
    SPECTATOR,
} player_type_t;

typedef struct player_info_s
{
    int             player_id;
    player_type_t   type;
    int             score;
} player_info_t;

// SERVER_PLAYER_REMOVE
typedef struct server_player_remove_s
{
    int     player_id;
} server_player_remove_t;

// SERVER_PLAYER_JOIN

#define     MAX_PLAYER_NAME_SIZE    16

typedef struct server_player_join_s
{
    player_info_t   info;
    char            name[MAX_PLAYER_NAME_SIZE];
} server_player_join_t;

// SERVER_NEW_WORD

typedef struct server_new_word_s
{
    int             count;
    packet_string_t word;
} server_new_word_t;



////////////// CLIENT PACKETS ///////////////

#define     MIN_START_WORDS     1
#define     MAX_START_WORDS     10

// CLIENT_PLAYER_INFOS
typedef struct client_player_infos_s
{
    int     start_words;
    char    name[MAX_PLAYER_NAME_SIZE];
} client_player_infos_t;

// CLIENT_WORD_COMPLETE
typedef struct client_word_complete_s
{
} client_word_complete_t;

// CLIENT_DISCONNECT
typedef struct client_disconnect_s
{
    packet_string_t reason;
} client_disconnect_t;



////////////// PACKET DATA ///////////////

// Packet definition
typedef struct packet_s
{
    packet_type_t   id;
    union
    {
        union
        {
            server_game_status_t    game_status;
            player_info_t           player_accept;
            player_info_t           player_update;
            server_player_remove_t  player_remove;
            server_player_join_t    player_join;
            server_new_word_t       new_line;
        }           server;
        union
        {
            client_player_infos_t   player_infos;
            client_disconnect_t     player_leave;
            client_word_complete_t  word_complete;
        }           client;
    } packet;
} packet_t;
