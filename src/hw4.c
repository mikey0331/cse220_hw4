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

int get_ship_cells(Ship ship, int cells[4][2], int width, int height) {
    int base_cells[7][4][2] = {
        {{0,0}, {0,1}, {0,2}, {0,3}},  // I
        {{0,0}, {0,1}, {1,0}, {1,1}},  // O
        {{0,1}, {1,0}, {1,1}, {1,2}},  // T
        {{0,0}, {1,0}, {2,0}, {2,1}},  // J
        {{0,0}, {1,0}, {2,0}, {2,-1}}, // L
        {{0,0}, {0,1}, {1,-1}, {1,0}}, // S
        {{0,-1}, {0,0}, {1,0}, {1,1}}  // Z
    };
    
    memcpy(cells, base_cells[ship.type-1], sizeof(int) * 8);
    
    // Apply rotation
    for(int r = 0; r < ship.rotation; r++) {
        for(int i = 0; i < 4; i++) {
            int temp = cells[i][0];
            cells[i][0] = -cells[i][1];
            cells[i][1] = temp;
        }
    }
    
    // Apply position and check bounds
    for(int i = 0; i < 4; i++) {
        cells[i][0] += ship.row;
        cells[i][1] += ship.col;
        if(cells[i][0] < 0 || cells[i][0] >= height ||
           cells[i][1] < 0 || cells[i][1] >= width) {
            return 0;
        }
    }
    return 1;
}

int validate_init(GameState *game, char *packet, Ship *ships) {
    char *token = strtok(packet + 2, " ");
    for(int i = 0; i < MAX_SHIPS; i++) {
        if(!token) return 201;
        ships[i].type = atoi(token);
        if(ships[i].type < 1 || ships[i].type > 7) return 300;
        
        token = strtok(NULL, " ");
        if(!token) return 201;
        ships[i].rotation = atoi(token);
        if(ships[i].rotation < 0 || ships[i].rotation > 3) return 301;
        
        token = strtok(NULL, " ");
        if(!token) return 201;
        ships[i].row = atoi(token);
        
        token = strtok(NULL, " ");
        if(!token) return 201;
        ships[i].col = atoi(token);
        
        ships[i].hits = 0;
    }
    
    int board[MAX_BOARD][MAX_BOARD] = {0};
    for(int i = 0; i < MAX_SHIPS; i++) {
        int cells[4][2];
        if(!get_ship_cells(ships[i], cells, game->width, game->height)) {
            return 302;
        }
        
        for(int j = 0; j < 4; j++) {
            if(board[cells[j][0]][cells[j][1]]) {
                return 303;
            }
            board[cells[j][0]][cells[j][1]] = 1;
        }
    }
    return 0;
}

void place_ships(Player *player, Ship *ships, GameState *game) {
    memcpy(player->ships, ships, sizeof(Ship) * MAX_SHIPS);
    player->num_ships = MAX_SHIPS;
    player->ships_remaining = MAX_SHIPS * 4;  // Each ship has 4 cells
    
    for(int i = 0; i < MAX_SHIPS; i++) {
        int cells[4][2];
        get_ship_cells(ships[i], cells, game->width, game->height);
        for(int j = 0; j < 4; j++) {
            player->board[cells[j][0]][cells[j][1]] = 1;
        }
    }
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
            place_ships(current, ships, game);
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
