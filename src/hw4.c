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
    int phase;  // 0=begin, 1=init, 2=play
    int current_turn; // 1=p1, 2=p2
} GameState;

void send_response(int socket, const char *msg) {
    write(socket, msg, strlen(msg));
    write(socket, "\n", 1);
}

int validate_begin(char *packet, int *width, int *height) {
    if(sscanf(packet, "B %d %d", width, height) != 2) {
        return 200;
    }
    if(*width < 10 || *height < 10) {
        return 200;
    }
    return 0;
}

int validate_init(GameState *game, char *packet, Ship *ships) {
    char *token = strtok(packet + 2, " ");
    for(int i = 0; i < MAX_SHIPS; i++) {
        // Check piece type
        if(!token) return 201;
        ships[i].type = atoi(token);
        if(ships[i].type < 1 || ships[i].type > 7) return 300;
        
        // Check rotation
        token = strtok(NULL, " ");
        if(!token) return 201;
        ships[i].rotation = atoi(token);
        if(ships[i].rotation < 0 || ships[i].rotation > 3) return 301;
        
        // Check position
        token = strtok(NULL, " ");
        if(!token) return 201;
        ships[i].col = atoi(token);
        
        token = strtok(NULL, " ");
        if(!token) return 201;
        ships[i].row = atoi(token);
        
        // Check boundaries
        if(ships[i].col < 0 || ships[i].col >= game->width ||
           ships[i].row < 0 || ships[i].row >= game->height) {
            return 302;
        }
    }
    
// Inside validate_init function:

// Check overlap
int board[MAX_BOARD][MAX_BOARD] = {0};
for(int i = 0; i < MAX_SHIPS; i++) {
    int cells[4][2];
    switch(ships[i].type) {
        case 1: // I piece
            for(int j = 0; j < 4; j++) {
                int r = ships[i].row;
                int c = ships[i].col + j;
                if(ships[i].rotation % 2) {
                    r = ships[i].row + j;
                    c = ships[i].col;
                }
                if(board[r][c]) return 303;
                board[r][c] = 1;
            }
            break;
            
        case 2: // O piece
            for(int r = ships[i].row; r < ships[i].row + 2; r++) {
                for(int c = ships[i].col; c < ships[i].col + 2; c++) {
                    if(board[r][c]) return 303;
                    board[r][c] = 1;
                }
            }
            break;
            
        case 3: // T piece
            cells[0][0] = ships[i].row;     cells[0][1] = ships[i].col + 1;
            cells[1][0] = ships[i].row + 1; cells[1][1] = ships[i].col;
            cells[2][0] = ships[i].row + 1; cells[2][1] = ships[i].col + 1;
            cells[3][0] = ships[i].row + 1; cells[3][1] = ships[i].col + 2;
            for(int rot = 0; rot < ships[i].rotation; rot++) {
                for(int j = 0; j < 4; j++) {
                    int temp = cells[j][0] - ships[i].row;
                    cells[j][0] = ships[i].row - (cells[j][1] - ships[i].col);
                    cells[j][1] = ships[i].col + temp;
                }
            }
            for(int j = 0; j < 4; j++) {
                if(board[cells[j][0]][cells[j][1]]) return 303;
                board[cells[j][0]][cells[j][1]] = 1;
            }
            break;
            
        case 4: // J piece
            cells[0][0] = ships[i].row;     cells[0][1] = ships[i].col;
            cells[1][0] = ships[i].row + 1; cells[1][1] = ships[i].col;
            cells[2][0] = ships[i].row + 2; cells[2][1] = ships[i].col;
            cells[3][0] = ships[i].row + 2; cells[3][1] = ships[i].col + 1;
            for(int rot = 0; rot < ships[i].rotation; rot++) {
                for(int j = 0; j < 4; j++) {
                    int temp = cells[j][0] - ships[i].row;
                    cells[j][0] = ships[i].row - (cells[j][1] - ships[i].col);
                    cells[j][1] = ships[i].col + temp;
                }
            }
            for(int j = 0; j < 4; j++) {
                if(board[cells[j][0]][cells[j][1]]) return 303;
                board[cells[j][0]][cells[j][1]] = 1;
            }
            break;
            
        case 5: // L piece
            cells[0][0] = ships[i].row;     cells[0][1] = ships[i].col;
            cells[1][0] = ships[i].row + 1; cells[1][1] = ships[i].col;
            cells[2][0] = ships[i].row + 2; cells[2][1] = ships[i].col;
            cells[3][0] = ships[i].row + 2; cells[3][1] = ships[i].col - 1;
            for(int rot = 0; rot < ships[i].rotation; rot++) {
                for(int j = 0; j < 4; j++) {
                    int temp = cells[j][0] - ships[i].row;
                    cells[j][0] = ships[i].row - (cells[j][1] - ships[i].col);
                    cells[j][1] = ships[i].col + temp;
                }
            }
            for(int j = 0; j < 4; j++) {
                if(board[cells[j][0]][cells[j][1]]) return 303;
                board[cells[j][0]][cells[j][1]] = 1;
            }
            break;
            
        case 6: // S piece
            cells[0][0] = ships[i].row;     cells[0][1] = ships[i].col;
            cells[1][0] = ships[i].row;     cells[1][1] = ships[i].col + 1;
            cells[2][0] = ships[i].row + 1; cells[2][1] = ships[i].col - 1;
            cells[3][0] = ships[i].row + 1; cells[3][1] = ships[i].col;
            for(int rot = 0; rot < ships[i].rotation; rot++) {
                for(int j = 0; j < 4; j++) {
                    int temp = cells[j][0] - ships[i].row;
                    cells[j][0] = ships[i].row - (cells[j][1] - ships[i].col);
                    cells[j][1] = ships[i].col + temp;
                }
            }
            for(int j = 0; j < 4; j++) {
                if(board[cells[j][0]][cells[j][1]]) return 303;
                board[cells[j][0]][cells[j][1]] = 1;
            }
            break;
            
        case 7: // Z piece
            cells[0][0] = ships[i].row;     cells[0][1] = ships[i].col - 1;
            cells[1][0] = ships[i].row;     cells[1][1] = ships[i].col;
            cells[2][0] = ships[i].row + 1; cells[2][1] = ships[i].col;
            cells[3][0] = ships[i].row + 1; cells[3][1] = ships[i].col + 1;
            for(int rot = 0; rot < ships[i].rotation; rot++) {
                for(int j = 0; j < 4; j++) {
                    int temp = cells[j][0] - ships[i].row;
                    cells[j][0] = ships[i].row - (cells[j][1] - ships[i].col);
                    cells[j][1] = ships[i].col + temp;
                }
            }
            for(int j = 0; j < 4; j++) {
                if(board[cells[j][0]][cells[j][1]]) return 303;
                board[cells[j][0]][cells[j][1]] = 1;
            }
            break;
    }
}

    return 0;
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
                sprintf(temp, " %c %d %d", opponent->board[i][j] ? 'H' : 'M', j, i);
                strcat(response, temp);
            }
        }
    }
}

void process_packet(GameState *game, char *packet, int is_p1) {
    Player *current = is_p1 ? &game->p1 : &game->p2;
    Player *other = is_p1 ? &game->p2 : &game->p1;
    
    // Handle forfeit immediately
    if(packet[0] == 'F') {
        send_response(current->socket, "H 0");
        send_response(other->socket, "H 1");
        exit(0);
    }

    // Phase checks
    if(game->phase == 0 && packet[0] != 'B') {
        send_response(current->socket, "E 100");
        return;
    }
    if(game->phase == 1 && packet[0] != 'I') {
        send_response(current->socket, "E 101");
        return;
    }
    if(game->phase == 2) {
        if(packet[0] != 'S' && packet[0] != 'Q' && packet[0] != 'F') {
            send_response(current->socket, "E 102");
            return;
        }
        if((is_p1 && game->current_turn != 1) || (!is_p1 && game->current_turn != 2)) {
            return; // Wrong turn
        }
    }

    switch(packet[0]) {
        case 'B': {
            if(is_p1) {
                int w, h;
                int error = validate_begin(packet, &w, &h);
                if(error) {
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
            memcpy(current->ships, ships, sizeof(ships));
            current->ships_remaining = MAX_SHIPS;
            send_response(current->socket, "A");
            current->ready = 2;
            if(game->p1.ready == 2 && game->p2.ready == 2) {
                game->phase = 2;
            }
            break;
        }

        case 'S': {
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
