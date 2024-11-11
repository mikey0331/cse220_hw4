#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024
#define MAX_BOARD 20

typedef struct {
    int socket;
    int board[MAX_BOARD][MAX_BOARD];
    int shots[MAX_BOARD][MAX_BOARD];
    int ships_remaining;
    int ready;
} Player;

typedef struct {
    int width;
    int height;
    Player p1;
    Player p2;
    int phase;
} GameState;

void send_packet(int socket, const char *msg) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s\n", msg);
    write(socket, buffer, strlen(buffer));
}

int setup_socket(int port) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        exit(1);
    }
    
    if (listen(server_fd, 1) < 0) {
        exit(1);
    }
    
    return server_fd;
}

void handle_packet(GameState *game, char *packet, int player_num) {
    Player *current = (player_num == 1) ? &game->p1 : &game->p2;
    Player *other = (player_num == 1) ? &game->p2 : &game->p1;

    switch(packet[0]) {
        case 'F':
            if (game->phase < 2) {
                send_packet(current->socket, "E 102");
                return;
            }
            send_packet(current->socket, "H 0");
            send_packet(other->socket, "H 1");
            exit(0);
            break;

        case 'B':
            if (game->phase != 0) {
                send_packet(current->socket, "E 100");
                return;
            }
            if (player_num == 1) {
                int w, h;
                if (sscanf(packet, "B %d %d", &w, &h) != 2 || w < 10 || h < 10) {
                    send_packet(current->socket, "E 200");
                    return;
                }
                game->width = w;
                game->height = h;
            }
            current->ready = 1;
            send_packet(current->socket, "A");
            if (game->p1.ready && game->p2.ready) {
                game->phase = 1;
            }
            break;

        case 'I':
            if (game->phase != 1) {
                send_packet(current->socket, "E 101");
                return;
            }
            // Add ship placement validation here
            send_packet(current->socket, "A");
            current->ready = 2;
            if (game->p1.ready == 2 && game->p2.ready == 2) {
                game->phase = 2;
            }
            break;

        case 'S':
            if (game->phase != 2) {
                send_packet(current->socket, "E 102");
                return;
            }
            int row, col;
            if (sscanf(packet, "S %d %d", &row, &col) != 2) {
                send_packet(current->socket, "E 202");
                return;
            }
            if (row < 0 || row >= game->height || col < 0 || col >= game->width) {
                send_packet(current->socket, "E 400");
                return;
            }
            send_packet(current->socket, "A");
            break;

        case 'Q':
            if (game->phase != 2) {
                send_packet(current->socket, "E 102");
                return;
            }
            send_packet(current->socket, "A");
            break;

        default:
            send_packet(current->socket, "E 100");
    }
}

int main() {
    GameState game = {0};
    game.p1.ships_remaining = 5;
    game.p2.ships_remaining = 5;
    
    int server1_fd = setup_socket(PORT1);
    int server2_fd = setup_socket(PORT2);
    
    game.p1.socket = accept(server1_fd, NULL, NULL);
    game.p2.socket = accept(server2_fd, NULL, NULL);
    
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(game.p1.socket, &readfds);
        FD_SET(game.p2.socket, &readfds);
        
        int maxfd = (game.p1.socket > game.p2.socket) ? game.p1.socket : game.p2.socket;
        
        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            continue;
        }
        
        if (FD_ISSET(game.p1.socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t bytes = read(game.p1.socket, buffer, BUFFER_SIZE - 1);
            if (bytes <= 0) {
                send_packet(game.p2.socket, "H 1");
                break;
            }
            buffer[bytes] = '\0';
            buffer[strcspn(buffer, "\n")] = '\0';
            handle_packet(&game, buffer, 1);
        }
        
        if (FD_ISSET(game.p2.socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t bytes = read(game.p2.socket, buffer, BUFFER_SIZE - 1);
            if (bytes <= 0) {
                send_packet(game.p1.socket, "H 1");
                break;
            }
            buffer[bytes] = '\0';
            buffer[strcspn(buffer, "\n")] = '\0';
            handle_packet(&game, buffer, 2);
        }
    }
    
    close(game.p1.socket);
    close(game.p2.socket);
    close(server1_fd);
    close(server2_fd);
    
    return 0;
}
