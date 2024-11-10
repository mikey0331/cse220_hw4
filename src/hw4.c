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
#define MAX_SHOTS 1024
#define MAX_SHIPS 5
#define MIN_BOARD_SIZE 10

typedef struct {
    int row;
    int col;
    int hit;
} Shot;

typedef struct {
    int points[4][2];  // [point][row/col]
    int type;
    int rotation;
    int active;
} Ship;

typedef struct {
    int width;
    int height;
    int** board;
    Ship ships[MAX_SHIPS];
    int ships_remaining;
    int socket;
    int initialized;
    Shot shots[MAX_SHOTS];
    int shot_count;
    int ready;
} Player;

// Tetris piece definitions (each rotation)
const int TETRIS_PIECES[7][4][4][2] = {
    // I piece
    {{{0,0}, {0,1}, {0,2}, {0,3}},
     {{0,0}, {1,0}, {2,0}, {3,0}},
     {{0,0}, {0,1}, {0,2}, {0,3}},
     {{0,0}, {1,0}, {2,0}, {3,0}}},
    // J piece
    {{{0,0}, {1,0}, {1,1}, {1,2}},
     {{0,0}, {0,1}, {1,0}, {2,0}},
     {{0,0}, {0,1}, {0,2}, {1,2}},
     {{0,1}, {1,1}, {2,0}, {2,1}}},
    // L piece
    {{{0,2}, {1,0}, {1,1}, {1,2}},
     {{0,0}, {1,0}, {2,0}, {2,1}},
     {{0,0}, {0,1}, {0,2}, {1,0}},
     {{0,0}, {0,1}, {1,1}, {2,1}}},
    // O piece
    {{{0,0}, {0,1}, {1,0}, {1,1}},
     {{0,0}, {0,1}, {1,0}, {1,1}},
     {{0,0}, {0,1}, {1,0}, {1,1}},
     {{0,0}, {0,1}, {1,0}, {1,1}}},
    // S piece
    {{{0,1}, {0,2}, {1,0}, {1,1}},
     {{0,0}, {1,0}, {1,1}, {2,1}},
     {{0,1}, {0,2}, {1,0}, {1,1}},
     {{0,0}, {1,0}, {1,1}, {2,1}}},
    // T piece
    {{{0,1}, {1,0}, {1,1}, {1,2}},
     {{0,0}, {1,0}, {1,1}, {2,0}},
     {{0,0}, {0,1}, {0,2}, {1,1}},
     {{0,1}, {1,0}, {1,1}, {2,1}}},
    // Z piece
    {{{0,0}, {0,1}, {1,1}, {1,2}},
     {{0,1}, {1,0}, {1,1}, {2,0}},
     {{0,0}, {0,1}, {1,1}, {1,2}},
     {{0,1}, {1,0}, {1,1}, {2,0}}}
};

Player player1, player2;
int current_turn = 1;

void send_response(int socket, const char* response) {
    if (send(socket, response, strlen(response), 0) < 0) {
        fprintf(stderr, "Send failed: %s\n", strerror(errno));
    }
}

void cleanup_player(Player* p) {
    if (p->board) {
        for (int i = 0; i < p->height; i++) {
            free(p->board[i]);
        }
        free(p->board);
        p->board = NULL;
    }
    if (p->socket > 0) {
        close(p->socket);
    }
}

void init_player(Player* p) {
    memset(p, 0, sizeof(Player));
    p->ships_remaining = MAX_SHIPS;
    p->socket = -1;
}

int allocate_board(Player* p) {
    p->board = malloc(p->height * sizeof(int*));
    if (!p->board) return -1;
    
    for (int i = 0; i < p->height; i++) {
        p->board[i] = calloc(p->width, sizeof(int));
        if (!p->board[i]) {
            for (int j = 0; j < i; j++) free(p->board[j]);
            free(p->board);
            p->board = NULL;
            return -1;
        }
    }
    return 0;
}

int validate_ship_placement(Player* p, Ship* ship) {
    if (ship->type < 1 || ship->type > 7 || ship->rotation < 0 || ship->rotation > 3) {
        return 300;
    }

    const int (*piece)[2] = TETRIS_PIECES[ship->type - 1][ship->rotation];
    
    for (int i = 0; i < 4; i++) {
        int row = ship->points[i][0];
        int col = ship->points[i][1];
        
        // Check bounds
        if (row < 0 || row >= p->height || col < 0 || col >= p->width) {
            return 302;
        }
        
        // Check overlap
        if (p->board[row][col] != 0) {
            return 303;
        }
        
        // Validate shape matches tetris piece definition
        int valid_point = 0;
        for (int j = 0; j < 4; j++) {
            if (piece[j][0] + ship->points[0][0] == row && 
                piece[j][1] + ship->points[0][1] == col) {
                valid_point = 1;
                break;
            }
        }
        if (!valid_point) return 300;
    }
    return 0;
}

void place_ship(Player* p, Ship* ship) {
    for (int i = 0; i < 4; i++) {
        p->board[ship->points[i][0]][ship->points[i][1]] = ship->type;
    }
}

int compare_shots(const void* a, const void* b) {
    const Shot* shot_a = (const Shot*)a;
    const Shot* shot_b = (const Shot*)b;
    if (shot_a->row != shot_b->row)
        return shot_a->row - shot_b->row;
    return shot_a->col - shot_b->col;
}

void generate_query_response(Player* p, Player* opponent, char* response) {
    sprintf(response, "G %d", opponent->ships_remaining);
    
    // Sort shots in row-major order
    qsort(p->shots, p->shot_count, sizeof(Shot), compare_shots);
    
    for (int i = 0; i < p->shot_count; i++) {
        char shot_info[20];
        sprintf(shot_info, " %c %d %d", 
                p->shots[i].hit ? 'H' : 'M',
                p->shots[i].col,
                p->shots[i].row);
        strcat(response, shot_info);
    }
}

int handle_begin(char* packet, Player* p, int is_player1) {
    if (p->ready) return 100;
    
    if (is_player1) {
        int width, height;
        if (sscanf(packet, "B %d %d", &width, &height) != 2) {
            return 200;
        }
        if (width < MIN_BOARD_SIZE || height < MIN_BOARD_SIZE) {
            return 200;
        }
        p->width = width;
        p->height = height;
    } else {
        if (strlen(packet) != 1) {
            return 200;
        }
        p->width = player1.width;
        p->height = player1.height;
    }
    
    if (allocate_board(p) != 0) {
        return 200;
    }
    
    p->ready = 1;
    return 0;
}

// ... [Rest of the implementation with similar improvements]

int main() {
    int server_fd1, server_fd2;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    
    init_player(&player1);
    init_player(&player2);
    
    // Set up server sockets with error checking
    if ((server_fd1 = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket 1 creation failed");
        exit(EXIT_FAILURE);
    }
    
    if ((server_fd2 = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket 2 creation failed");
        close(server_fd1);
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_fd1, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd2, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // ... [Rest of the main function with improved error handling]
    
    // Cleanup
    cleanup_player(&player1);
    cleanup_player(&player2);
    close(server_fd1);
    close(server_fd2);
    
    return 0;
}
