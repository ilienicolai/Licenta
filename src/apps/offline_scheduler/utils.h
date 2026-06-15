#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

#define MAX_NAME_LEN  64
#define MAX_IP_LEN    64
#define MAX_NODES     128
#define INF           1000000000

/* ---------- data structures ---------- */

typedef struct {
    char     name[MAX_NAME_LEN];
    char     ip[MAX_IP_LEN];
    uint16_t port;
} Server;

typedef struct {
    char     name[MAX_NAME_LEN];
    char     ip[MAX_IP_LEN];
    uint16_t port;
} Client;

typedef struct {
    int n_servers;
    int n_clients;
    Server  servers[MAX_NODES];
    Client  clients[MAX_NODES];

    /*
     * cost[i][j]  = cost of assigning client i to server j.
     * INF         = no edge between client i and server j.
     */
    int cost[MAX_NODES][MAX_NODES];
} Graph;

/* ---------- function declarations ---------- */

/*
 * Parse the input file and fill the Graph structure.
 * Returns 0 on success, -1 on error.
 */
int parse_input(const char *filename, Graph *g);

/*
 * Print the parsed graph for debugging.
 */
void print_graph(const Graph *g);

#endif /* UTILS_H */
