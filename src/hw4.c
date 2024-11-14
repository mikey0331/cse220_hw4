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
    // I-piece (1)
    {{0,0}, {0,1}, {0,2}, {0,3}},
    // O-piece (2)
    {{0,0}, {0,1}, {1,0}, {1,1}},
    // T-piece (3)
    {{0,1}, {1,0}, {1,1}, {1,2}},
    // J-piece (4)
    {{0,0}, {1,0}, {2,0}, {2,1}},
    // L-piece (5)
    {{0,0}, {1,0}, {2,0}, {2,-1}},
    // S-piece (6)
    {{0,0}, {0,1}, {1,-1}, {1,0}},
    // Z-piece (7)
    {{0,-1}, {0,0}, {1,0}, {1,1}}
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
        
        // Apply rotation
        rotate_point(&row, &col, ship.rotation);
        
        // Apply ship position
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
            exit(0);
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

    switch(packet[0]) {
        case 'B': {
            if(is_p1) {
                int w, h;
                if(sscanf(packet, "B %d %d", &w, &h) != 2 || w < 10 || h < 10) {
                    send_response(current->socket, "E 200");
                    return;
                }
                game->width = w;
                game->height = h;
            }
            send_response(current->socket, "A");
            current->ready = 1;
            if(game->p1.ready && game->p2.ready) {
                game->phase = 1;
            }
            break;
        }

        case 'I': {
            Ship ships[MAX_SHIPS];
            int error = validate_init(game, packet, ships);
            if(error) {
                char error_msg[16];
                snprintf(error_msg, sizeof(error_msg), "E %d", error);
                send_response(current->socket, error_msg);
                return;
            }
            place_ships(game, current, ships);
            send_response(current->socket, "A");
            current->ready = 2;
            if(game->p1.ready == 2 && game->p2.ready == 2) {
                game->phase = 2;
            }
            break;
        }

        case 'S': {
            if((is_p1 && game->current_turn != 1) || (!is_p1 && game->current_turn != 2)) {
                return;
            }
            int row, col;
            if(sscanf(packet, "S %d %d", &row, &col) != 2) {
                send_response(current->socket, "E 202");
                return;
            }
            if(row < 0 || row >= game->height || col < 0 || col >= game->width) {
                send_response(current->socket, "E 400");
                return;
            }
            if(current->shots[row][col]) {
                send_response(current->socket, "E 401");
                return;
            }
            process_shot(game, current, other, row, col);
            break;
        }

        case 'Q': {
            if((is_p1 && game->current_turn != 1) || (!is_p1 && game->current_turn != 2)) {
                return;
            }
            char response[BUFFER_SIZE];
            build_query_response(game, current, other, response);
            send_response(current->socket, response);
            break;
        }
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
    
    while(1) {
        FD_ZERO(&readfds);
        FD_SET(game.p1.socket, &readfds);
        FD_SET(game.p2.socket, &readfds);
        
        int maxfd = (game.p1.socket > game.p2.socket) ? game.p1.socket : game.p2.socket;
        select(maxfd + 1, &readfds, NULL, NULL, NULL);
        
        if(FD_ISSET(game.p1.socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t bytes = read(game.p1.socket, buffer, BUFFER_SIZE-1);
            if(bytes <= 0) break;
            buffer[bytes] = '\0';
            buffer[strcspn(buffer, "\n")] = '\0';
            process_packet(&game, buffer, 1);
        }
        
        if(FD_ISSET(game.p2.socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t bytes = read(game.p2.socket, buffer, BUFFER_SIZE-1);
            if(bytes <= 0) break;
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
