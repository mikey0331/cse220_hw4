#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024
#define MAX_SHIPS 5
#define MAX_BOARD 20

typedef struct {
    int type;
    int rotation;
    int col;
    int row;
} Ship;

typedef struct {
    int socket;
    int ready;
    Ship ships[MAX_SHIPS];
    int num_ships;
    int board[MAX_BOARD][MAX_BOARD];
} Player;

typedef struct {
    Player p1;
    Player p2;
    int width;
    int height;
    int phase;
    int current_turn;
} GameState;

void send_response(int socket, const char *msg) {
    write(socket, msg, strlen(msg));
    write(socket, "\n", 1);
}

int check_init_errors(GameState *game, Ship *ships, int num_ships) {
    // Check piece types (E 300)
    for (int i = 0; i < num_ships; i++) {
        if (ships[i].type < 1 || ships[i].type > 7) {
            return 300;
        }
    }
    
    // Check rotations (E 301)
    for (int i = 0; i < num_ships; i++) {
        if (ships[i].rotation < 0 || ships[i].rotation > 3) {
            return 301;
        }
    }
    
    // Check boundaries (E 302)
    for (int i = 0; i < num_ships; i++) {
        if (ships[i].col < 0 || ships[i].col >= game->width ||
            ships[i].row < 0 || ships[i].row >= game->height) {
            return 302;
        }
    }
    
    // Check overlap (E 303)
    int board[MAX_BOARD][MAX_BOARD] = {0};
    for (int i = 0; i < num_ships; i++) {
        // Add ship placement logic here
        if (/* ship overlaps */) {
            return 303;
        }
    }
    
    return 0;
}

void process_packet(GameState *game, char *packet, int is_p1) {
    Player *current = is_p1 ? &game->p1 : &game->p2;
    Player *other = is_p1 ? &game->p2 : &game->p1;
    
    // Phase check (E 100, 101, 102)
    if (game->phase == 0 && packet[0] != 'B') {
        send_response(current->socket, "E 100");
        return;
    }
    if (game->phase == 1 && packet[0] != 'I') {
        send_response(current->socket, "E 101");
        return;
    }
    if (game->phase == 2 && packet[0] != 'S' && packet[0] != 'Q' && packet[0] != 'F') {
        send_response(current->socket, "E 102");
        return;
    }

    switch(packet[0]) {
        case 'F':
            send_response(current->socket, "H 0");
            send_response(other->socket, "H 1");
            exit(0);
            break;

        case 'B':
            if (is_p1) {
                int w, h;
                if (sscanf(packet, "B %d %d", &w, &h) != 2 || w < 10 || h < 10) {
                    send_response(current->socket, "E 200");
                    return;
                }
                game->width = w;
                game->height = h;
            }
            send_response(current->socket, "A");
            current->ready = 1;
            if (game->p1.ready && game->p2.ready) game->phase = 1;
            break;

        case 'I': {
            Ship ships[MAX_SHIPS];
            int error = check_init_errors(game, ships, MAX_SHIPS);
            if (error) {
                char error_msg[16];
                snprintf(error_msg, sizeof(error_msg), "E %d", error);
                send_response(current->socket, error_msg);
                return;
            }
            send_response(current->socket, "A");
            memcpy(current->ships, ships, sizeof(ships));
            current->ready = 2;
            if (game->p1.ready == 2 && game->p2.ready == 2) game->phase = 2;
            break;
        }

        case 'S': {
            int row, col;
            if (sscanf(packet, "S %d %d", &row, &col) != 2) {
                send_response(current->socket, "E 202");
                return;
            }
            if (row < 0 || row >= game->height || col < 0 || col >= game->width) {
                send_response(current->socket, "E 400");
                return;
            }
            if (other->board[row][col] != 0) {
                send_response(current->socket, "E 401");
                return;
            }
            send_response(current->socket, "A");
            break;
        }

        case 'Q':
            send_response(current->socket, "A");
            break;

        default:
            send_response(current->socket, "E 100");
    }
}

int main() {
    GameState game = {0};
    game.phase = 0;
    game.current_turn = 1;
    
    int server1_fd = socket(AF_INET, SOCK_STREAM, 0);
    int server2_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in addr1 = {0}, addr2 = {0};
    int opt = 1;
    
    setsockopt(server1_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server2_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    addr1.sin_family = AF_INET;
    addr1.sin_addr.s_addr = INADDR_ANY;
    addr1.sin_port = htons(PORT1);
    
    addr2.sin_family = AF_INET;
    addr2.sin_addr.s_addr = INADDR_ANY;
    addr2.sin_port = htons(PORT2);
    
    bind(server1_fd, (struct sockaddr *)&addr1, sizeof(addr1));
    bind(server2_fd, (struct sockaddr *)&addr2, sizeof(addr2));
    
    listen(server1_fd, 1);
    listen(server2_fd, 1);
    
    game.p1.socket = accept(server1_fd, NULL, NULL);
    game.p2.socket = accept(server2_fd, NULL, NULL);
    
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(game.p1.socket, &readfds);
        FD_SET(game.p2.socket, &readfds);
        
        int maxfd = (game.p1.socket > game.p2.socket) ? game.p1.socket : game.p2.socket;
        select(maxfd + 1, &readfds, NULL, NULL, NULL);
        
        if (FD_ISSET(game.p1.socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t bytes = read(game.p1.socket, buffer, BUFFER_SIZE-1);
            if (bytes <= 0) break;
            buffer[bytes] = '\0';
            buffer[strcspn(buffer, "\n")] = '\0';
            process_packet(&game, buffer, 1);
        }
        
        if (FD_ISSET(game.p2.socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t bytes = read(game.p2.socket, buffer, BUFFER_SIZE-1);
            if (bytes <= 0) break;
            buffer[bytes] = '\0';
            buffer[strcspn(buffer, "\n")] = '\0';
            process_packet(&game, buffer, 0);
        }
    }
    
    close(game.p1.socket);
    close(game.p2.socket);
    close(server1_fd);
    close(server2_fd);
    
    return 0;
}
