#include "rdma_infrastructure.h"


struct rdma_config config;
unsigned int backpressure_threshold_up = 90;
unsigned int backpressure_threshold_down = 75;

/* Options. */
static struct argp_option options[] = {
    {"worker-count", 'w', "WORKERS", 0, "Number of worker threads used for consuming incoming data"},
    {"ib-device", 'd', "IBDEV", 0, "IB device (e.g. mlx5_0)"},
    {"ib-gid-index", 'i', "IBGIDX", 0, "IB GID index (e.g. 5)"},
    {"message-count", 'M', "MCOUNT", 0, "RDMA message count to be received"},
    {"message-size", 'S', "MSIZE", 0, "RDMA message size to be received"},
    {"buffer-size", 'B', "BSIZE", 0, "Size of the memory buffer which will store the received RDMA messages"},
    {"backpressure-threshold-up", 's', "UPTHR", 0, "Threshold(%) of buffer being in use for enabling backpressure"},
    {"backpressure-threshold-down", 'j', "DOWNTHR", 0, "Threshold(%) of buffer being in use for disabling backpressure"},
    { 0 }
};

/* Options' parsing function. */
static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    struct rdma_config *cfg = state->input;
    char* end;

    switch (key) {
    case 'w':
        cfg->worker_count = strtol(arg, &end, 0);
        if (end == arg) argp_error(state, "'%s' is not a number", arg);
        break;

    case 'd':
        cfg->ib_devname = strdup(arg);
        break;

    case 'i':
        cfg->gidx = strtol(arg, &end, 0);
        if (end == arg) argp_error(state, "'%s' is not a number", arg);
        break;

    case 'M':
        *(cfg->message_count) = strtol(arg, &end, 0);
        if (end == arg) argp_error(state, "'%s' is not a number", arg);
        break;

    case 'S':
        *(cfg->message_size) = strtol(arg, &end, 0);
        if (end == arg) argp_error(state, "'%s' is not a number", arg);
        break;

    case 'B':
        *(cfg->buffer_size) = strtol(arg, &end, 0);
        if (end == arg) argp_error(state, "'%s' is not a number", arg);
        break;

    case 's':
        backpressure_threshold_up = strtol(arg, &end, 0);
        if (end == arg) argp_error(state, "'%s' is not a number", arg);
        break;

    case 'j':
        backpressure_threshold_down = strtol(arg, &end, 0);
        if (end == arg) argp_error(state, "'%s' is not a number", arg);
        break;

    case ARGP_KEY_ARG:
        switch(state->arg_num) {
        case 0: // <local-ip-address>
            cfg->local_hostname = strdup(arg);
            break;
        case 1: { // <local-port>
            long p = strtol(arg, &end, 0);
            if (end == arg) argp_error(state, "'%s' is not a number", arg);
            cfg->local_port = (int)p;
            break;
        }
        case 2: { // <scheduler-ip>
            cfg->remote_hostname = strdup(arg);
            break;
        }
        case 3: { // <scheduler-port>
            long p = strtol(arg, &end, 0);
            if (end == arg) argp_error(state, "'%s' is not a number", arg);
            cfg->remote_port = (int)p;
            break;
        }
        default:
            argp_usage(state);
            break;
        }
        break;

    case ARGP_KEY_END:
        if (state->arg_num != 4) argp_usage(state);
        break;

    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

/* A description of the arguments we accept. */
static char args_doc[] = "<local-ip-address> <local-port> <scheduler-ip> <scheduler-port>";

/* Program documentation. */
static char doc[] = "RDMA receiver (scheduled): waits for scheduler assignment then performs RDMA READ";

/* Our argp parser. */
static struct argp argp = { options, parse_opt, args_doc, doc };

void
cli_parse(int argc, char **argv, struct rdma_config* config)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) == -1) {
        srand(time(NULL));
    } else {
        srand((int)now.tv_nsec);
    }

    // set default values
    config->function = RDMA_READ;

    config->local_hostname = "";
    config->local_port = 0;
    config->remote_hostname = "";  /* overridden by <scheduler-ip> CLI arg  */
    config->remote_port = 52000;   /* overridden by <scheduler-port> CLI arg */

    config->worker_count = 1;
    config->ib_devname = "mlx5_0";
    config->gidx = 5;
    config->mtu = IBV_MTU_1024;

    config->remote_count = 1;

    config->local_endpoint = (struct rdma_endpoint **)calloc(config->remote_count, sizeof(struct rdma_endpoint *));
    *(config->local_endpoint) = (struct rdma_endpoint *)malloc(sizeof(struct rdma_endpoint));
    config->remote_endpoint = (struct rdma_endpoint **)calloc(config->remote_count, sizeof(struct rdma_endpoint *));
    *(config->remote_endpoint) = (struct rdma_endpoint *)malloc(sizeof(struct rdma_endpoint));

    config->message_count = (unsigned long *)calloc(config->remote_count, sizeof(unsigned long));
    *(config->message_count) = 10;
    config->message_size = (unsigned long *)calloc(config->remote_count, sizeof(unsigned long));
    *(config->message_size) = 1024;
    config->buffer_size = (unsigned long *)calloc(config->remote_count, sizeof(unsigned long));
    *(config->buffer_size) = *(config->message_count) * *(config->message_size);
    config->mem_offset = (unsigned long *)calloc(config->remote_count, sizeof(unsigned long));
    *(config->mem_offset) = 0;

    // parse arguments
    argp_parse(&argp, argc, argv, 0, 0, config);
}

int
main(int argc, char** argv)
{
    char **local_receiver_rdma_metadata;
    char *remote_sender_rdma_metadata;

    cli_parse(argc, argv, &config);

    /* ------------------------------------------------------------------ *
     * Phase 1: Connect to the scheduler to receive the assignment.        *
     *          Connect to <scheduler-ip>:<scheduler-port>, send           *
     *          "RECEIVER <local-ip>\n", receive                           *
     *          "CONNECT_TO <sender_ip> <rdma_port>\n".                   *
     * ------------------------------------------------------------------ */
    {
        int flag = 1;
        int sched_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sched_fd < 0) { perror("socket"); exit(1); }
        setsockopt(sched_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        struct sockaddr_in sa;
        bzero(&sa, sizeof(sa));
        sa.sin_family      = AF_INET;
        sa.sin_addr.s_addr = inet_addr(config.remote_hostname);
        sa.sin_port        = htons(config.remote_port);

        fprintf(stdout, "(RECEIVER) Connecting to scheduler at %s:%d ...\n",
                config.remote_hostname, config.remote_port);

        if (connect(sched_fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
            fprintf(stderr, "main: cannot connect to scheduler – %m\n");
            exit(1);
        }

        /* identify ourselves by IP and port */
        char ident[128];
        snprintf(ident, sizeof(ident), "RECEIVER %s %d\n",
                 config.local_hostname, config.local_port);
        write(sched_fd, ident, strlen(ident));

        /* receive assignment */
        char msg[128];
        memset(msg, 0, sizeof(msg));
        read(sched_fd, msg, sizeof(msg) - 1);
        close(sched_fd);

        char sender_ip[64];
        int  rdma_port;
        if (sscanf(msg, "CONNECT_TO %63s %d", sender_ip, &rdma_port) != 2) {
            fprintf(stderr, "main: unexpected scheduler message: '%s'\n", msg);
            exit(1);
        }

        /* store the assigned sender address – overwrite remote_* with RDMA peer */
        config.remote_hostname = strdup(sender_ip);
        config.remote_port     = rdma_port;

        fprintf(stdout, "(RECEIVER) Scheduler assigned: connect to sender %s:%d\n",
                sender_ip, rdma_port);
    }

    /* ------------------------------------------------------------------ *
     * Phase 2: Initialise RDMA resources.                                 *
     * ------------------------------------------------------------------ */
    local_receiver_rdma_metadata = rdma_prepare(&config, RDMA_RECEIVER);
    if (local_receiver_rdma_metadata == NULL) {
        fprintf(stderr, "main: Failed to initialize RDMA and get the receiver RDMA metadata.\n");
        exit(1);
    }
    fprintf(stdout, "(RDMA_RECEIVER) local RDMA metadata: %s\n", *local_receiver_rdma_metadata);

    /* ------------------------------------------------------------------ *
     * Phase 3: Connect to the sender and exchange RDMA metadata.          *
     * ------------------------------------------------------------------ */
    int s, flag = 1;
    struct sockaddr_in s_in;

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "main: Socket initialization failed.\n");
        exit(1);
    }

    if (-1 == setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag))) {
        fprintf(stderr, "main: setsockopt TCP_NODELAY failed.\n");
        exit(1);
    }

    bzero(&s_in, sizeof(s_in));
    s_in.sin_family = AF_INET;
    s_in.sin_addr.s_addr = inet_addr(config.remote_hostname);
    s_in.sin_port = htons(config.remote_port);

    if (connect(s, (struct sockaddr *)&s_in, sizeof(s_in)) != 0) {
        fprintf(stderr, "main: Connection with the sender failed.\n");
        exit(1);
    } else {
        fprintf(stdout, "main: Connected to the sender.\n");
    }

    /* exchange metadata */
    if (config.function == RDMA_WRITE) {
        write(s, *local_receiver_rdma_metadata, 78);
    } else {
        write(s, *local_receiver_rdma_metadata, 52);
    }

    remote_sender_rdma_metadata = (char *)malloc(78);
    memset(remote_sender_rdma_metadata, 0, 78);

    read(s, remote_sender_rdma_metadata, 78);
    sscanf(remote_sender_rdma_metadata, "%0lx:%0lx:%0lx:%08x:%016lx:%s",
           &((*(config.remote_endpoint))->lid), &((*(config.remote_endpoint))->qpn),
           &((*(config.remote_endpoint))->psn), &((*(config.remote_endpoint))->rkey),
           &((*(config.remote_endpoint))->addr), &((*(config.remote_endpoint))->gid_string));
    wire_gid_to_gid((*(config.remote_endpoint))->gid_string, &((*(config.remote_endpoint))->gid));

    fprintf(stdout, "(RDMA_RECEIVER) [SECOND] remote RDMA metadata: %s\n", remote_sender_rdma_metadata);

    if (rdma_connect_ctx(config.rdma_ctx, 1, config.mtu, config.local_endpoint, config.remote_endpoint, config.remote_count, config.gidx, RDMA_RECEIVER, config.function)) {
        fprintf(stderr, "main:  Failed to connect to remote RDMA endpoint (provider).\n");
        exit(1);
    }

    char buf[32];
    do {
        bzero(buf, 32);
        read(s, buf, 32);
    } while (strcmp(buf, "GO") != 0);

    fprintf(stdout, "(RDMA_RECEIVER_CHECK) Starting integrity-checked streaming RDMA READ operations...\n");

    if (rdma_read_consume_check(s, backpressure_threshold_up, backpressure_threshold_down, config.rdma_ctx, config.remote_endpoint, config.message_count, config.message_size, config.buffer_size, config.mem_offset, config.worker_count) < 0) {
        fprintf(stderr, "main: RDMA READ stream+check failed.\n");
        exit(1);
    }

    /* rdma_read_consume_check runs indefinitely; unreachable below */
    fprintf(stdout, "(RDMA_RECEIVER_CHECK) RDMA READ stream+check stopped.\n");

    if (rdma_close_ctx(config.rdma_ctx, config.remote_count)) {
        fprintf(stderr, "main: Failed to clean up before exiting.\n");
    }
}
