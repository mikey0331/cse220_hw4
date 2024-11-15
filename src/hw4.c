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
    int row;
    int col;
    int hits;
} Ship;

typedef struct {
    int socket;
    int ready;
    Ship ships[MAX_SHIPS];
    int num_ships;
    int board[MAX_BOARD][MAX_BOARD];
    int shots[MAX_BOARD][MAX_BOARD];
    int ships_remaining;
} Player;

typedef struct {
    Player p1;
    Player p2;
    int width;
    int height;
    int phase;
    int current_turn;
} GameState;

// Tetris piece definitions (row, col offsets from anchor point)
const int TETRIS_PIECES[7][4][2] = {
    {{0,0}, {0,1}, {0,2}, {0,3}},  // I-piece
    {{0,0}, {0,1}, {1,0}, {1,1}},  // O-piece
    {{0,1}, {1,0}, {1,1}, {1,2}},  // T-piece
    {{0,0}, {1,0}, {2,0}, {2,1}},  // J-piece
    {{0,0}, {1,0}, {2,0}, {2,-1}}, // L-piece
    {{0,0}, {0,1}, {1,-1}, {1,0}}, // S-piece
    {{0,-1}, {0,0}, {1,0}, {1,1}}  // Z-piece
};

void send_response(int socket, const char *msg) {
    write(socket, msg, strlen(msg));
    write(socket, "\n", 1);
}

void rotate_point(int *row, int *col, int rotation) {
    int temp;
    for(int i = 0; i < rotation; i++) {
        temp = *row;
        *row = -*col;
        *col = temp;
    }
}

int validate_ship_placement(GameState *game, Ship ship, int board[MAX_BOARD][MAX_BOARD]) {
    int piece_idx = ship.type - 1;
    
    for(int i = 0; i < 4; i++) {
        int row = TETRIS_PIECES[piece_idx][i][0];
        int col = TETRIS_PIECES[piece_idx][i][1];
        
        rotate_point(&row, &col, ship.rotation);
        
        row += ship.row;
        col += ship.col;
        
        // Check boundaries
        if(row < 0 || row >= game->height || col < 0 || col >= game->width) {
            return 302;  // Ship doesn't fit
        }
        
        // Check overlap
        if(board[row][col]) {
            return 303;  // Ships overlap
        }
        
        board[row][col] = 1;
    }
    return 0;
}

int validate_init(GameState *game, char *packet, Ship *ships) {
    char *token = strtok(packet + 2, " ");
    for(int i = 0; i < MAX_SHIPS; i++) {
        // Parse type
        if(!token) return 201;
        ships[i].type = atoi(token);
        if(ships[i].type < 1 || ships[i].type > 7) return 300;
        
        // Parse rotation
        token = strtok(NULL, " ");
        if(!token) return 201;
        ships[i].rotation = atoi(token);
        if(ships[i].rotation < 0 || ships[i].rotation > 3) return 301;
        
        // Parse row
        token = strtok(NULL, " ");
        if(!token) return 201;
        ships[i].row = atoi(token);
        
        // Parse column
        token = strtok(NULL, " ");
        if(!token) return 201;
        ships[i].col = atoi(token);
        
        token = strtok(NULL, " ");
    }

    // Validate all ship placements
    int board[MAX_BOARD][MAX_BOARD] = {0};
    for(int i = 0; i < MAX_SHIPS; i++) {
        int result = validate_ship_placement(game, ships[i], board);
        if(result != 0) return result;
    }
    
    return 0;
}

void place_ships(GameState *game, Player *player, Ship *ships) {
    memset(player->board, 0, sizeof(player->board));
    for(int i = 0; i < MAX_SHIPS; i++) {
        int piece_idx = ships[i].type - 1;
        for(int j = 0; j < 4; j++) {
            int row = TETRIS_PIECES[piece_idx][j][0];
            int col = TETRIS_PIECES[piece_idx][j][1];
            rotate_point(&row, &col, ships[i].rotation);
            row += ships[i].row;
            col += ships[i].col;
            player->board[row][col] = 1;
        }
    }
    player->ships_remaining = MAX_SHIPS * 4;  // Each ship has 4 cells
}

void process_shot(GameState *game, Player *shooter, Player *target, int row, int col) {
    if(target->board[row][col]) {
        target->ships_remaining--;
        char response[32];
        snprintf(response, sizeof(response), "R %d H", target->ships_remaining);
        send_response(shooter->socket, response);
        
        if(target->ships_remaining == 0) {
            send_response(target->socket, "H 0");
            send_response(shooter->socket, "H 1");
            exit(0);  // Game Over
        }
    } else {
        char response[32];
        snprintf(response, sizeof(response), "R %d M", target->ships_remaining);
        send_response(shooter->socket, response);
    }
    shooter->shots[row][col] = 1;
    game->current_turn = (game->current_turn == 1) ? 2 : 1;
}

void build_query_response(GameState *game, Player *player, Player *opponent, char *response) {
    sprintf(response, "G %d", opponent->ships_remaining);
    for(int i = 0; i < game->height; i++) {
        for(int j = 0; j < game->width; j++) {
            if(player->shots[i][j]) {
                char temp[32];
                sprintf(temp, " %c %d %d", opponent->board[i][j] ? 'H' : 'M', i, j);
                strcat(response, temp);
            }
        }
    }
}

void process_packet(GameState *game, char *packet, int is_p1) {
    Player *current = is_p1 ? &game->p1 : &game->p2;
    Player *other = is_p1 ? &game->p2 : &game->p1;

    if(packet[0] == 'F') {
        send_response(current->socket, "H 0");
        send_response(other->socket, "H 1");
        exit(0);
    }

    if(game->phase == 0 && packet[0] != 'B') {
        send_response(current->socket, "E 100");
        return;
    }
    if(game->phase == 1 && packet[0] != 'I') {
        send_response(current->socket, "E 101");
        return;
    }
    if(game->phase == 2 && packet[0] != 'S' && packet[0] != 'Q') {
        send_response(current->socket, "E 102");
        return;
    }

    // Process initial setup phase
    if(packet[0] == 'B' && game->phase == 0) {
        Ship ships[MAX_SHIPS];
        int result = validate_init(game, packet, ships);
        if(result == 0) {
            place_ships(game, current, ships);
            game->phase++;
            send_response(current->socket, "I");
            if(game->p1.ready && game->p2.ready) {
                game->phase++;
                send_response(current->socket, "S");
            }
        } else {
            send_response(current->socket, "E %d", result);
        }
    }

    if(packet[0] == 'S' && game->phase == 2) {
        int row = packet[1] - '0';
        int col = packet[2] - '0';
        process_shot(game, current, other, row, col);
    }

    if(packet[0] == 'Q') {
        char response[BUFFER_SIZE] = {0};
        build_query_response(game, current, other, response);
        send_response(current->socket, response);
    }
}

int main() {
    GameState game;
    memset(&game, 0, sizeof(game));
    game.width = 10;
    game.height = 10;
    game.phase = 0;
    
    int server_fd, client_fd1, client_fd2;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);

    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT1);

    if(bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        return -1;
    }

    if(listen(server_fd, 2) < 0) {
        perror("listen failed");
        return -1;
    }

    printf("Waiting for players to connect...\n");

    // Wait for both players
    client_fd1 = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
    printf("Player 1 connected\n");

    client_fd2 = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
    printf("Player 2 connected\n");

    // Initialize players
    game.p1.socket = client_fd1;
    game.p2.socket = client_fd2;

    while(1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(game.p1.socket, &read_fds);
        FD_SET(game.p2.socket, &read_fds);
        
        int max_sd = (game.p1.socket > game.p2.socket) ? game.p1.socket : game.p2.socket;
        
        if(select(max_sd + 1, &read_fds, NULL, NULL, NULL) > 0) {
            if(FD_ISSET(game.p1.socket, &read_fds)) {
                char packet[BUFFER_SIZE];
                memset(packet, 0, sizeof(packet));
                read(game.p1.socket, packet, sizeof(packet));
                process_packet(&game, packet, 1);
            }
            if(FD_ISSET(game.p2.socket, &read_fds)) {
                char packet[BUFFER_SIZE];
                memset(packet, 0, sizeof(packet));
                read(game.p2.socket, packet, sizeof(packet));
                process_packet(&game, packet, 0);
            }
        }
    }

    close(client_fd1);
    close(client_fd2);
    close(server_fd);

    return 0;
}
