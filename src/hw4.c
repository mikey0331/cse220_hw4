#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ctype.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024
#define MAX_PIECES 5
#define BOARD_MAX_SIZE 20

// Piece shapes and rotations based on Tetris pieces
const int PIECE_SHAPES[7][4][4][4] = {
    // I piece
    {{{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}},
     {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}},
    // Rest of piece definitions...
};

typedef struct {
    int type;
    int rotation;
    int col;
    int row;
    int hits;
    int size;
    int sunk;
} Piece;

typedef struct {
    int socket;
    int board[BOARD_MAX_SIZE][BOARD_MAX_SIZE];
    int shot_board[BOARD_MAX_SIZE][BOARD_MAX_SIZE];
    Piece pieces[MAX_PIECES];
    int num_pieces;
    int ships_remaining;
} Player;

typedef struct {
    int width;
    int height;
    Player player1;
    Player player2;
    int current_turn;  // 1 for player1, 2 for player2
    int game_started;
    int game_over;
} GameState;

// Function prototypes
void init_game(GameState *game);
int validate_piece_placement(GameState *game, int player_num, Piece piece);
void place_piece(GameState *game, int player_num, Piece piece);
int process_shot(GameState *game, int player_num, int row, int col);
void send_response(int socket, const char *response);
void broadcast_response(GameState *game, const char *response);
void handle_begin(GameState *game, char *packet, int player_num);
void handle_initialize(GameState *game, char *packet, int player_num);
void handle_shoot(GameState *game, char *packet, int player_num);
void handle_query(GameState *game, int player_num);
void handle_forfeit(GameState *game, int player_num);

// Implementation of all functions...
void init_game(GameState *game) {
    memset(game, 0, sizeof(GameState));
    game->player1.ships_remaining = MAX_PIECES;
    game->player2.ships_remaining = MAX_PIECES;
    game->current_turn = 1;
}

void send_response(int socket, const char *response) {
    send(socket, response, strlen(response), 0);
}

void broadcast_response(GameState *game, const char *response) {
    send_response(game->player1.socket, response);
    send_response(game->player2.socket, response);
}

void handle_begin(GameState *game, char *packet, int player_num) {
    if (player_num == 1) {
        int width, height;
        sscanf(packet, "B %d %d", &width, &height);
        if (width > 0 && width <= BOARD_MAX_SIZE && 
            height > 0 && height <= BOARD_MAX_SIZE) {
            game->width = width;
            game->height = height;
            send_response(game->player1.socket, "OK");
        } else {
            send_response(game->player1.socket, "ERROR invalid board dimensions");
        }
    } else {
        if (game->width > 0 && game->height > 0) {
            send_response(game->player2.socket, "OK");
            game->game_started = 1;
        } else {
            send_response(game->player2.socket, "ERROR waiting for player 1");
        }
    }
}

// Add all remaining function implementations...

int main() {
    GameState game;
    init_game(&game);
    
    int server_fd1, server_fd2;
    struct sockaddr_in address1, address2;
    int opt = 1;
    int addrlen = sizeof(address1);
    
    // Socket creation and setup code...
    
    // Main game loop
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    
    while (!game.game_over) {
        FD_ZERO(&readfds);
        FD_SET(game.player1.socket, &readfds);
        FD_SET(game.player2.socket, &readfds);
        
        int max_sd = (game.player1.socket > game.player2.socket) ? 
                     game.player1.socket : game.player2.socket;
        
        select(max_sd + 1, &readfds, NULL, NULL, NULL);
        
        // Handle player input
        if (FD_ISSET(game.player1.socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            if (read(game.player1.socket, buffer, BUFFER_SIZE) <= 0) {
                handle_forfeit(&game, 1);
                break;
            }
            process_input(&game, buffer, 1);
        }
        
        if (FD_ISSET(game.player2.socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            if (read(game.player2.socket, buffer, BUFFER_SIZE) <= 0) {
                handle_forfeit(&game, 2);
                break;
            }
            process_input(&game, buffer, 2);
        }
    }
    
    // Cleanup
    close(game.player1.socket);
    close(game.player2.socket);
    
    return 0;
}
