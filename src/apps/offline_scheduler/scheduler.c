/*
 * scheduler.c
 *
 * Optimal one-to-one matching between clients and servers in a bipartite
 * graph using the Hungarian (Munkres) algorithm – O(n^3).
 *
 * The algorithm minimises the total assignment cost.
 * Pairs that have no edge in the input receive cost INF, so they are
 * naturally avoided unless no feasible perfect matching exists.
 *
 * If the number of clients and servers differ, the smaller side is
 * padded with dummy nodes (cost 0) so that a square cost matrix is
 * always produced; unmatched real nodes are reported at the end.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "utils.h"


// Hungarian algorithm – minimisation, square n×n cost matrix        *

/*
 * hungarian_solve
 *
 * n : dimension of the square matrix
 * cost   : cost[n][n] – caller-provided square matrix
 * assign : output array of size n; assign[i] = column matched to row i
 *
 * Returns the minimum total cost.
 *
 * - u[i]  : row potential
 * - v[j]  : column potential
 * - Invariant: u[i] + v[j] <= cost[i][j]  for all i,j
 */
static long long hungarian_solve(int n, int cost[][MAX_NODES], int assign[])
{
    /* potentials */
    long long *u = calloc(n + 1, sizeof(long long));
    long long *v = calloc(n + 1, sizeof(long long));

    /* p[j]   = row currently matched to column j  (1-indexed, 0 = unmatched) */
    int *p    = calloc(n + 1, sizeof(int));
    /* way[j] = previous column on the augmenting path to column j */
    int *way  = calloc(n + 1, sizeof(int));

    if (!u || !v || !p || !way) {
        fprintf(stderr, "hungarian_solve: out of memory\n");
        exit(EXIT_FAILURE);
    }

    /* minv[j]  = minimum (cost[cur_row][j] - u[cur_row] - v[j]) seen so far */
    long long *minv = malloc((n + 1) * sizeof(long long));
    int       *used = malloc((n + 1) * sizeof(int));
    if (!minv || !used) {
        fprintf(stderr, "hungarian_solve: out of memory\n");
        exit(EXIT_FAILURE);
    }

    /* Process each row (1-indexed internally) */
    for (int i = 1; i <= n; i++) {
        p[0] = i;               /* sentinel: row i tries to find a column */
        int j0 = 0;             /* start from dummy column 0              */

        for (int j = 0; j <= n; j++) {
            minv[j] = (long long)INF * 2;
            used[j] = 0;
        }

        do {
            used[j0] = 1;
            int    i0   = p[j0];
            long long delta = (long long)INF * 2;
            int    j1   = -1;

            for (int j = 1; j <= n; j++) {
                if (!used[j]) {
                    long long cur = (long long)cost[i0 - 1][j - 1] - u[i0] - v[j];
                    if (cur < minv[j]) {
                        minv[j] = cur;
                        way[j]  = j0;
                    }
                    if (minv[j] < delta) {
                        delta = minv[j];
                        j1    = j;
                    }
                }
            }

            /* update potentials */
            for (int j = 0; j <= n; j++) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j]    -= delta;
                } else {
                    minv[j] -= delta;
                }
            }

            j0 = j1;
        } while (p[j0] != 0);   /* until we reach an unmatched column */

        /* augment along the path */
        do {
            int j1 = way[j0];
            p[j0]  = p[j1];
            j0     = j1;
        } while (j0);
    }

    /* fill assignment array (0-indexed) */
    for (int j = 1; j <= n; j++)
        if (p[j] != 0)
            assign[p[j] - 1] = j - 1;

    /* compute total cost */
    long long total = 0;
    for (int i = 0; i < n; i++)
        total += cost[i][assign[i]];

    free(u); free(v); free(p); free(way); free(minv); free(used);
    return total;
}

int main(int argc, char *argv[])
{
    const char *input_file = "input.txt";
    if (argc >= 2)
        input_file = argv[1];

    /* --- parse input --- */
    Graph g;
    memset(&g, 0, sizeof(g));
    if (parse_input(input_file, &g) != 0)
        return EXIT_FAILURE;

    print_graph(&g);

    int nc = g.n_clients;
    int ns = g.n_servers;

    if (nc == 0 || ns == 0) {
        fprintf(stderr, "No clients or servers found.\n");
        return EXIT_FAILURE;
    }

    /* --- build square cost matrix, padded with 0-cost dummy rows/cols --- */
    int n = (nc > ns) ? nc : ns;   /* dimension of the square matrix */

    /* Allocate a local square matrix initialised to 0 (dummy cost) */
    int (*sq)[MAX_NODES] = malloc(n * sizeof(*sq));
    if (!sq) { perror("malloc"); return EXIT_FAILURE; }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            sq[i][j] = 0;

    /* Fill real edges; real rows = clients, real cols = servers */
    for (int i = 0; i < nc; i++)
        for (int j = 0; j < ns; j++)
            sq[i][j] = g.cost[i][j];   /* INF if no edge */

    /* --- run Hungarian algorithm --- */
    int *assign = malloc(n * sizeof(int));
    if (!assign) { perror("malloc"); free(sq); return EXIT_FAILURE; }

    long long total = hungarian_solve(n, sq, assign);

    /* --- print results --- */
    printf("\n=== Optimal assignment ===\n");

    long long real_cost = 0;
    int       matched   = 0;

    for (int i = 0; i < nc; i++) {
        int j = assign[i];
        if (j < ns) {
            if (g.cost[i][j] == INF) {
                printf("  Client %-6s  -->  Server %-6s  (no direct edge – forced)\n",
                       g.clients[i].name, g.servers[j].name);
            } else {
                printf("  Client %-6s  -->  Server %-6s  (cost %d)\n",
                       g.clients[i].name, g.servers[j].name, g.cost[i][j]);
                real_cost += g.cost[i][j];
                matched++;
            }
        } else {
            printf("  Client %-6s  -->  (unmatched - no server available)\n",
                   g.clients[i].name);
        }
    }

    /* report unmatched servers if ns > nc */
    if (ns > nc) {
        /* find which servers were not assigned */
        int *srv_used = calloc(ns, sizeof(int));
        for (int i = 0; i < nc; i++)
            if (assign[i] < ns)
                srv_used[assign[i]] = 1;
        for (int j = 0; j < ns; j++)
            if (!srv_used[j])
                printf("  Server %-6s  -->  (unmatched-no client available)\n",
                       g.servers[j].name);
        free(srv_used);
    }

    printf("\nTotal cost of matched pairs : %lld\n", real_cost);
    printf("Matched pairs              : %d\n", matched);
    (void)total;   /* suppress unused-variable warning */

    free(sq);
    free(assign);
    return EXIT_SUCCESS;
}
