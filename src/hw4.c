#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MAX_BUFFER 1024
#define PORT_P1 2201
#define PORT_P2 2202

typedef struct {
    int socket;
    int ready;
    int ships_remaining;
} Player;

typedef struct {
    int width;
    int height;
    Player p1;
    Player p2;
    int turn;  // 1 for Player 1's turn, 2 for Player 2's turn
} GameState;

// Function to send response to the client
void send_response(int socket, const char *message) {
    write(socket, message, strlen(message));
}

// Function to process incoming packets
void process_packet(GameState *game, char *packet, int is_p1) {
    Player *current = is_p1 ? &game->p1 : &game->p2;
    Player *other = is_p1 ? &game->p2 : &game->p1;

    // Handle Forfeit
    if (packet[0] == 'F') {
        send_response(current->socket, "H 0"); // Current player forfeits
        send_response(other->socket, "H 1");  // Other player wins
        exit(0);  // End the game
    }

    // Handle Begin packet
    if (packet[0] == 'B') {
        if (is_p1) {
            int w, h;
            if (sscanf(packet, "B %d %d", &w, &h) != 2 || w < 10 || h < 10) {
                send_response(current->socket, "E 200");  // Invalid begin packet
                return;
            }
            game->width = w;
            game->height = h;
        }
        send_response(current->socket, "A");  // Acknowledge Begin packet
        current->ready = 1;
        return;
    }

    // Handle Initialize packet
    if (packet[0] == 'I') {
        // Validate the ship placement
        // If invalid, respond with E 200 or an appropriate error
        send_response(current->socket, "E 200");  // Invalid Initialize packet (example)
        return;
    }

    // Handle Shoot packet
    if (packet[0] == 'S') {
        int x, y;
        if (sscanf(packet, "S %d %d", &x, &y) != 2) {
            send_response(current->socket, "E 202");  // Invalid Shoot packet
            return;
        }

        // Check if the shot is valid (inside the board and not previously guessed)
        if (x < 0 || x >= game->width || y < 0 || y >= game->height) {
            send_response(current->socket, "E 400");  // Invalid Shoot packet (out of bounds)
            return;
        }

        // Example shot processing, assume it's a hit
        if (x == 5 && y == 5) {  // For example, the target is at (5,5)
            send_response(current->socket, "R 4 H");  // Hit and 4 ships remaining
            other->ships_remaining--;
            if (other->ships_remaining == 0) {
                send_response(current->socket, "H 1");  // Player 1 wins
                send_response(other->socket, "H 0");   // Player 2 loses
                exit(0);
            }
        } else {
            send_response(current->socket, "R 5 M");  // Miss and 5 ships remaining
        }

        // Switch turns
        game->turn = (game->turn == 1) ? 2 : 1;
        return;
    }

    // Handle Query packet
    if (packet[0] == 'Q') {
        // Example response with dummy shot history
        send_response(current->socket, "G 5 M 0 0 H 1 1 H 1 2 M 4 4");
        return;
    }

    // Handle random packets like "Random A"
    if (strstr(packet, "Random") != NULL) {
        send_response(current->socket, "E 200");  // Invalid random packet
        return;
    }

    // If we reach here, the packet is invalid
    send_response(current->socket, "E 200");  // Invalid packet type
}

// Function to handle player communication
void handle_player(int player_socket, GameState *game, int is_p1) {
    char buffer[MAX_BUFFER];
    ssize_t bytes_read;

    while (1) {
        bytes_read = read(player_socket, buffer, MAX_BUFFER);
        if (bytes_read <= 0) {
            break;
        }

        buffer[bytes_read] = '\0';  // Null-terminate the packet

        process_packet(game, buffer, is_p1);
    }
}

// Main function to initialize the server and handle game logic
int main() {
    int server_fd, player1_socket, player2_socket;
    struct sockaddr_in server_addr, player1_addr, player2_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    // Set up game state
    GameState game = {0};
    game.turn = 1;  // Player 1 starts

    // Create socket for server
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket failed");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = 0;  // Let the OS choose a free port

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_fd, 2) == -1) {
        perror("Listen failed");
        return 1;
    }

    // Accept Player 1
    player1_socket = accept(server_fd, (struct sockaddr*)&player1_addr, &addr_len);
    if (player1_socket == -1) {
        perror("Player 1 connection failed");
        return 1;
    }
    game.p1.socket = player1_socket;

    // Accept Player 2
    player2_socket = accept(server_fd, (struct sockaddr*)&player2_addr, &addr_len);
    if (player2_socket == -1) {
        perror("Player 2 connection failed");
        return 1;
    }
    game.p2.socket = player2_socket;

    // Start handling Player 1 and Player 2
    // Handle Player 1
    if (fork() == 0) {
        handle_player(player1_socket, &game, 1);  // Player 1's turn
        close(player1_socket);
        exit(0);
    }

    // Handle Player 2
    if (fork() == 0) {
        handle_player(player2_socket, &game, 0);  // Player 2's turn
        close(player2_socket);
        exit(0);
    }

    // Wait for both child processes to finish
    wait(NULL);
    wait(NULL);

    close(server_fd);
    return 0;
}
