#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024
#define MAX_BOARD 20
#define MAX_SHIPS 5

typedef struct {
    int socket;
    int board[MAX_BOARD][MAX_BOARD];
    int shots[MAX_BOARD][MAX_BOARD];
    int ships_remaining;
} Player;

typedef struct {
    int width;
    int height;
    Player p1;
    Player p2;
    int current_player;
    int phase;  // 0=setup, 1=play
} GameState;

void send_message(int socket, const char *msg) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s\n", msg);
    send(socket, buffer, strlen(buffer), 0);
}

int setup_server_socket(int port) {
    int server_fd;
    struct sockaddr_in address;
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        exit(1);
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    
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

int main() {
    GameState game = {0};
    game.p1.ships_remaining = MAX_SHIPS;
    game.p2.ships_remaining = MAX_SHIPS;
    
    int server1_fd = setup_server_socket(PORT1);
    int server2_fd = setup_server_socket(PORT2);
    
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    // Accept Player 1
    game.p1.socket = accept(server1_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (game.p1.socket < 0) {
        exit(1);
    }
    
    // Accept Player 2
    game.p2.socket = accept(server2_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (game.p2.socket < 0) {
        exit(1);
    }
    
    char buffer[BUFFER_SIZE];
    game.current_player = 1;
    
    while (1) {
        int current_socket = (game.current_player == 1) ? game.p1.socket : game.p2.socket;
        memset(buffer, 0, BUFFER_SIZE);
        
        int bytes_read = read(current_socket, buffer, BUFFER_SIZE);
        if (bytes_read <= 0) {
            // Handle disconnection
            send_message((game.current_player == 1) ? game.p2.socket : game.p1.socket, "H 1");
            send_message(current_socket, "H 0");
            break;
        }
        
        // Remove newline if present
        buffer[strcspn(buffer, "\n")] = 0;
        
        switch (buffer[0]) {
            case 'B':
                if (game.phase != 0) {
                    send_message(current_socket, "E 100");
                    continue;
                }
                if (game.current_player == 1) {
                    int w, h;
                    if (sscanf(buffer, "B %d %d", &w, &h) != 2 || w < 10 || h < 10) {
                        send_message(current_socket, "E 200");
                        continue;
                    }
                    game.width = w;
                    game.height = h;
                } else if (strlen(buffer) > 2) {
                    send_message(current_socket, "E 200");
                    continue;
                }
                send_message(current_socket, "A");
                game.current_player = (game.current_player == 1) ? 2 : 1;
                break;
                
            case 'I':
                if (game.phase != 0) {
                    send_message(current_socket, "E 101");
                    continue;
                }
                // Validate ship placement
                send_message(current_socket, "A");
                if (game.current_player == 2) {
                    game.phase = 1;
                }
                game.current_player = (game.current_player == 1) ? 2 : 1;
                break;
                
            case 'S':
                if (game.phase != 1) {
                    send_message(current_socket, "E 102");
                    continue;
                }
                int row, col;
                if (sscanf(buffer, "S %d %d", &row, &col) != 2) {
                    send_message(current_socket, "E 202");
                    continue;
                }
                if (row < 0 || row >= game.height || col < 0 || col >= game.width) {
                    send_message(current_socket, "E 400");
                    continue;
                }
                Player *current = (game.current_player == 1) ? &game.p1 : &game.p2;
                if (current->shots[row][col]) {
                    send_message(current_socket, "E 401");
                    continue;
                }
                current->shots[row][col] = 1;
                send_message(current_socket, "R 5 M");
                game.current_player = (game.current_player == 1) ? 2 : 1;
                break;
                
            case 'Q':
                if (game.phase != 1) {
                    send_message(current_socket, "E 102");
                    continue;
                }
                Player *p = (game.current_player == 1) ? &game.p1 : &game.p2;
                send_message(current_socket, "G 5");
                break;
                
            case 'F':
                if (game.phase != 1) {
                    send_message(current_socket, "E 102");
                    continue;
                }
                send_message((game.current_player == 1) ? game.p2.socket : game.p1.socket, "H 1");
                send_message(current_socket, "H 0");
                goto cleanup;
                
            default:
                send_message(current_socket, "E 100");
        }
    }
    
cleanup:
    close(game.p1.socket);
    close(game.p2.socket);
    close(server1_fd);
    close(server2_fd);
    
    return 0;
}
