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
#define MAX_BOARD 20

typedef struct {
    int socket;
    int board[MAX_BOARD][MAX_BOARD];
    int shots[MAX_BOARD][MAX_BOARD];
    int ships_remaining;
    int initialized;
} Player;

typedef struct {
    int width;
    int height;
    Player p1;
    Player p2;
    int phase;  // 0=setup, 1=play
} GameState;

void send_packet(int socket, const char *msg) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s\n", msg);
    send(socket, buffer, strlen(buffer), 0);
}

int setup_listener(int port) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;
    
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) return -1;
    if (listen(server_fd, 1) < 0) return -1;
    
    return server_fd;
}

void handle_begin(GameState *game, char *packet, int player_socket, int is_player1) {
    if (is_player1) {
        int w, h;
        if (sscanf(packet, "B %d %d", &w, &h) != 2 || w < 10 || h < 10) {
            send_packet(player_socket, "E 200");
            return;
        }
        game->width = w;
        game->height = h;
    } else if (strlen(packet) > 2) {
        send_packet(player_socket, "E 200");
        return;
    }
    send_packet(player_socket, "A");
}

int main() {
    GameState game = {0};
    int server1_fd = setup_listener(PORT1);
    int server2_fd = setup_listener(PORT2);
    
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    game.p1.socket = accept(server1_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    game.p2.socket = accept(server2_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(game.p1.socket, &readfds);
        FD_SET(game.p2.socket, &readfds);
        
        int max_sd = (game.p1.socket > game.p2.socket) ? game.p1.socket : game.p2.socket;
        
        select(max_sd + 1, &readfds, NULL, NULL, NULL);
        
        if (FD_ISSET(game.p1.socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            if (read(game.p1.socket, buffer, BUFFER_SIZE) <= 0) {
                send_packet(game.p2.socket, "H 1");
                send_packet(game.p1.socket, "H 0");
                break;
            }
            buffer[strcspn(buffer, "\n")] = 0;
            handle_begin(&game, buffer, game.p1.socket, 1);
        }
        
        if (FD_ISSET(game.p2.socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            if (read(game.p2.socket, buffer, BUFFER_SIZE) <= 0) {
                send_packet(game.p1.socket, "H 1");
                send_packet(game.p2.socket, "H 0");
                break;
            }
            buffer[strcspn(buffer, "\n")] = 0;
            handle_begin(&game, buffer, game.p2.socket, 0);
        }
    }
    
    close(game.p1.socket);
    close(game.p2.socket);
    close(server1_fd);
    close(server2_fd);
    
    return 0;
}
