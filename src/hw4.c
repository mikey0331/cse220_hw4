#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024
#define MIN_BOARD_SIZE 10
#define MAX_SHIPS 5

typedef struct {
    int row;
    int col;
    char result;  // 'H' for hit, 'M' for miss
} Shot;

typedef struct {
    int socket;
    int ships_remaining;
    int board[20][20];
    Shot shots[400];  // Maximum possible shots
    int shot_count;
    int initialized;
} Player;

typedef struct {
    int width;
    int height;
    Player player1;
    Player player2;
    int current_turn;  // 1 or 2
    int game_phase;    // 0=begin, 1=init, 2=play
} GameState;

void send_error(int socket, int code) {
    char response[32];
    sprintf(response, "E %d\n", code);
    send(socket, response, strlen(response), 0);
}

void send_ack(int socket) {
    char response[] = "A\n";
    send(socket, response, strlen(response), 0);
}

void send_halt(int socket, int win) {
    char response[8];
    sprintf(response, "H %d\n", win);
    send(socket, response, strlen(response), 0);
}

void handle_begin(GameState *state, char *packet, int player) {
    if (state->game_phase != 0) {
        send_error(player == 1 ? state->player1.socket : state->player2.socket, 100);
        return;
    }

    if (player == 1) {
        int width, height;
        if (sscanf(packet, "B %d %d", &width, &height) != 2) {
            send_error(state->player1.socket, 200);
            return;
        }
        if (width < MIN_BOARD_SIZE || height < MIN_BOARD_SIZE) {
            send_error(state->player1.socket, 200);
            return;
        }
        state->width = width;
        state->height = height;
        send_ack(state->player1.socket);
    } else {
        if (strlen(packet) > 2) {  // Just "B\n"
            send_error(state->player2.socket, 200);
            return;
        }
        send_ack(state->player2.socket);
        state->game_phase = 1;  // Move to initialization phase
    }
}

void send_shot_response(int socket, int ships_remaining, char result) {
    char response[32];
    sprintf(response, "R %d %c\n", ships_remaining, result);
    send(socket, response, strlen(response), 0);
}

void send_query_response(Player *player) {
    char response[1024] = "";
    sprintf(response, "G %d", player->ships_remaining);
    
    for (int i = 0; i < player->shot_count; i++) {
        char shot[32];
        sprintf(shot, " %c %d %d", player->shots[i].result, 
                player->shots[i].col, player->shots[i].row);
        strcat(response, shot);
    }
    strcat(response, "\n");
    send(player->socket, response, strlen(response), 0);
}

int main() {
    GameState state = {0};
    state.player1.ships_remaining = MAX_SHIPS;
    state.player2.ships_remaining = MAX_SHIPS;
    
    // Socket setup code here
    
    while (1) {
        char buffer[BUFFER_SIZE] = {0};
        int current_socket = state.current_turn == 1 ? 
                           state.player1.socket : state.player2.socket;
        
        int bytes_read = read(current_socket, buffer, BUFFER_SIZE);
        if (bytes_read <= 0) continue;
        
        switch (buffer[0]) {
            case 'B':
                handle_begin(&state, buffer, state.current_turn);
                break;
            case 'I':
                // Handle initialization
                break;
            case 'S':
                // Handle shots
                break;
            case 'Q':
                if (state.game_phase != 2) {
                    send_error(current_socket, 102);
                    break;
                }
                send_query_response(state.current_turn == 1 ? 
                                  &state.player1 : &state.player2);
                break;
            case 'F':
                if (state.game_phase != 2) {
                    send_error(current_socket, 102);
                    break;
                }
                send_halt(state.player1.socket, state.current_turn == 2);
                send_halt(state.player2.socket, state.current_turn == 1);
                return 0;
            default:
                send_error(current_socket, 100);
        }
    }
    
    return 0;
}
