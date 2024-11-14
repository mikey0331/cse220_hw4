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

typedef struct {
    int socket;
    int ready;
    int ships;
} Player;

typedef struct {
    Player p1;
    Player p2;
    int width;
    int height;
    int phase;
    int current_turn;  // 1 for P1, 2 for P2
} GameState;

void send_response(int socket, const char *msg) {
    write(socket, msg, strlen(msg));
    write(socket, "\n", 1);
}

int setup_listener(int port) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 1);
    
    return server_fd;
}

void handle_game_end(GameState *game, int winner) {
    if (winner == 1) {
        send_response(game->p2.socket, "H 0");  // Loser notified first
        send_response(game->p1.socket, "H 1");  // Winner notified second
    } else {
        send_response(game->p1.socket, "H 0");
        send_response(game->p2.socket, "H 1");
    }
    exit(0);
}

void process_packet(GameState *game, char *packet, int is_p1) {
    Player *current = is_p1 ? &game->p1 : &game->p2;
    Player *other = is_p1 ? &game->p2 : &game->p1;

    if (packet[0] == 'F') {
        handle_game_end(game, is_p1 ? 2 : 1);
    }
    
    if (packet[0] == 'B') {
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
        return;
    }

    if (packet[0] == 'S') {
        // Simulate hit that ends game
        char response[32];
        snprintf(response, sizeof(response), "R %d H", other->ships - 1);
        send_response(current->socket, response);
        
        if (other->ships - 1 == 0) {
            handle_game_end(game, is_p1 ? 1 : 2);
        }
        
        other->ships--;
        game->current_turn = is_p1 ? 2 : 1;
        return;
    }

    send_response(current->socket, "A");
}

int main() {
    GameState game = {0};
    game.p1.ships = 5;
    game.p2.ships = 5;
    game.current_turn = 1;  // P1 starts
    
    int server1_fd = setup_listener(PORT1);
    int server2_fd = setup_listener(PORT2);
    
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
