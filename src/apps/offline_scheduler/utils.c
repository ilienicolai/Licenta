#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

int parse_input(const char *filename, Graph *g)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    /* Initialise the cost matrix: no edge = INF */
    for (int i = 0; i < MAX_NODES; i++)
        for (int j = 0; j < MAX_NODES; j++)
            g->cost[i][j] = INF;

    /* --- read servers --- */
    if (fscanf(fp, "%d", &g->n_servers) != 1) {
        fprintf(stderr, "Error reading number of servers\n");
        fclose(fp);
        return -1;
    }
    for (int i = 0; i < g->n_servers; i++) {
        if (fscanf(fp, "%63s %63s %hu",
                   g->servers[i].name,
                   g->servers[i].ip,
                   &g->servers[i].port) != 3) {
            fprintf(stderr, "Error reading server %d\n", i);
            fclose(fp);
            return -1;
        }
    }

    /* --- read clients --- */
    if (fscanf(fp, "%d", &g->n_clients) != 1) {
        fprintf(stderr, "Error reading number of clients\n");
        fclose(fp);
        return -1;
    }
    for (int i = 0; i < g->n_clients; i++) {
        if (fscanf(fp, "%63s %63s %hu",
                   g->clients[i].name,
                   g->clients[i].ip,
                   &g->clients[i].port) != 3) {
            fprintf(stderr, "Error reading client %d\n", i);
            fclose(fp);
            return -1;
        }
    }

    /* --- read edges: <client_name> <server_name> <cost> --- */
    char cname[MAX_NAME_LEN], sname[MAX_NAME_LEN];
    int  cost;
    while (fscanf(fp, "%63s %63s %d", cname, sname, &cost) == 3) {
        /* find client index */
        int ci = -1;
        for (int i = 0; i < g->n_clients; i++)
            if (strcmp(g->clients[i].name, cname) == 0) { ci = i; break; }

        /* find server index */
        int si = -1;
        for (int i = 0; i < g->n_servers; i++)
            if (strcmp(g->servers[i].name, sname) == 0) { si = i; break; }

        if (ci == -1) { fprintf(stderr, "Unknown client '%s'\n", cname); continue; }
        if (si == -1) { fprintf(stderr, "Unknown server '%s'\n", sname); continue; }

        g->cost[ci][si] = cost;
    }

    fclose(fp);
    return 0;
}

void print_graph(const Graph *g)
{
    printf("=== Servers (%d) ===\n", g->n_servers);
    for (int i = 0; i < g->n_servers; i++)
        printf("  [%d] %s  %s:%u\n",
               i, g->servers[i].name, g->servers[i].ip, g->servers[i].port);

    printf("=== Clients (%d) ===\n", g->n_clients);
    for (int i = 0; i < g->n_clients; i++)
        printf("  [%d] %s  %s:%u\n",
               i, g->clients[i].name, g->clients[i].ip, g->clients[i].port);

    printf("=== Cost matrix ===\n");
    printf("       ");
    for (int j = 0; j < g->n_servers; j++)
        printf("%8s", g->servers[j].name);
    printf("\n");
    for (int i = 0; i < g->n_clients; i++) {
        printf("%6s ", g->clients[i].name);
        for (int j = 0; j < g->n_servers; j++) {
            if (g->cost[i][j] == INF)
                printf("%8s", "INF");
            else
                printf("%8d", g->cost[i][j]);
        }
        printf("\n");
    }
}
