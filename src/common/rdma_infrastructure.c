#include "rdma_infrastructure.h"


int
rdma_get_port_info(struct ibv_context *context, int port, struct ibv_port_attr *attr)
{
	return ibv_query_port(context, port, attr);
}

void
wire_gid_to_gid(const char *wgid, union ibv_gid *gid)
{
	char tmp[9];
	__be32 v32;
	int i;
	uint32_t tmp_gid[4];

	for (tmp[8] = 0, i = 0; i < 4; ++i) {
		memcpy(tmp, wgid + i * 8, 8);
		sscanf(tmp, "%x", &v32);
		tmp_gid[i] = be32toh(v32);
	}
	memcpy(gid, tmp_gid, sizeof(*gid));
}

void
gid_to_wire_gid(const union ibv_gid *gid, char wgid[])
{
	uint32_t tmp_gid[4];
	int i;

	memcpy(tmp_gid, gid, sizeof(tmp_gid));
	for (i = 0; i < 4; ++i) {
		sprintf(&wgid[i * 8], "%08x", htobe32(tmp_gid[i]));
  }
}

void
set_timerfd(int fd, unsigned s, unsigned ns)
{
    struct itimerspec it;

    it.it_interval.tv_sec = s;
    it.it_interval.tv_nsec = ns;
    it.it_value.tv_sec = s;
    it.it_value.tv_nsec = ns;

    if (timerfd_settime(fd, 0, &it, NULL)) {
        printf("set_timerfd: timerfd_settime failed for fd %d. The timer will not fire.", fd);
        return;
    }
}

long int
get_current_timestamp_ns()
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        printf("get_current_timestamp_ns: clock_gettime failed");
        return -1;
    } else {
        return now.tv_nsec + now.tv_sec * 1E9;
    }
}

long int
get_current_timestamp_ns_thread_cpu()
{
    struct timespec now;

    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == -1) {
        printf("get_current_timestamp_ns: clock_gettime failed");
        return -1;
    } else {
        return now.tv_nsec + now.tv_sec * 1E9;
    }
}

char **
rdma_prepare(struct rdma_config *config, int role)
{
    int i;
    char **rdma_metadata;
    struct timespec now;

    config->dev_list = ibv_get_device_list(NULL);
    if (!config->dev_list) {
        fprintf(stderr, "rdma_prepare: Failed to get IB devices list\n");
        return NULL;
    }

    for (i = 0; config->dev_list[i]; ++i) {
        if (!strcmp(ibv_get_device_name(config->dev_list[i]), config->ib_devname)) {
            break;
        }
    }
    config->ib_dev = config->dev_list[i];
    if (!config->ib_dev) {
        fprintf(stderr, "rdma_prepare: IB device %s not found\n", config->ib_devname);
        return NULL;
    }

    config->rdma_ctx = rdma_init_ctx(config->ib_dev, config->message_count, config->message_size, config->buffer_size, config->remote_count, 1, role, config->function);
    if (!config->rdma_ctx) {
        fprintf(stderr, "rdma_prepare: Failed to create RDMA context\n");
        return NULL;
    }

    printf("rdma_prepare: buffer addr: %d\n", config->rdma_ctx->buf);

    if (rdma_get_port_info(config->rdma_ctx->context, 1, &config->rdma_ctx->portinfo)) {
        fprintf(stderr, "rdma_prepare: Couldn't get port info\n");
        return NULL;
    }
    
    rdma_metadata = (char **)calloc(config->remote_count, sizeof(char *));
    for (i=0; i < config->remote_count; i++) {
        (*(config->local_endpoint + i))->lid = config->rdma_ctx->portinfo.lid;
        if (config->rdma_ctx->portinfo.link_layer != IBV_LINK_LAYER_ETHERNET && !(*(config->local_endpoint + i))->lid) {
            fprintf(stderr, "rdma_prepare: Couldn't get local LID\n");
            return NULL;
        }
        
        if (config->gidx >= 0) {
            if (ibv_query_gid(config->rdma_ctx->context, 1, config->gidx, &(*(config->local_endpoint + i))->gid)) {
            fprintf(stderr, "rdma_prepare: Can't read sgid of index %d\n", config->gidx);
            return NULL;
            }
        } else {
            memset(&(*(config->local_endpoint + i))->gid, 0, sizeof((*(config->local_endpoint + i))->gid));
        }

        inet_ntop(AF_INET6, &(*(config->local_endpoint + i))->gid, (*(config->local_endpoint + i))->gid_string, sizeof((*(config->local_endpoint + i))->gid_string));
        gid_to_wire_gid(&(*(config->local_endpoint + i))->gid, (*(config->local_endpoint + i))->gid_string);

        if (clock_gettime(CLOCK_REALTIME, &now) == -1) {
            srand(time(NULL));
        } else {
            srand((int)now.tv_nsec);
        }
        (*(config->local_endpoint + i))->qpn = (*(config->rdma_ctx->qp + i))->qp_num;
        (*(config->local_endpoint + i))->psn = rand() & 0xffffff;
        // (*(config->local_endpoint + i))->psn = (rand() & 0xffffff) + i;
        // (*(config->local_endpoint + i))->psn = (rand() & 0xffffff) + i * 10000;

        if (config->function == RDMA_WRITE && role == RDMA_RECEIVER ||
            config->function == RDMA_READ && role == RDMA_SENDER) {
            (*(config->local_endpoint + i))->rkey = (*(config->rdma_ctx->mr + i))->rkey;
            (*(config->local_endpoint + i))->addr = (uint64_t)(*(config->rdma_ctx->mr + i))->addr;
            *(rdma_metadata + i) = (char *)malloc(78); // 4+1+6+1+6+1+8+1+16+1+32+1 (last one is the string terminator)
            memset(*(rdma_metadata + i), 0, 78);
            snprintf(*(rdma_metadata + i), 78, "%04x:%06x:%06x:%08x:%016lx:%s", (*(config->local_endpoint + i))->lid, (*(config->local_endpoint + i))->qpn, (*(config->local_endpoint + i))->psn, (*(config->local_endpoint + i))->rkey, (*(config->local_endpoint + i))->addr, (*(config->local_endpoint + i))->gid_string);
            debug_print("(RDMA_WRITE/RDMA_READ) local RDMA metadata for remote #%d: %s\n", i, *(rdma_metadata + i));
        } else {
            *(rdma_metadata + i) = (char *)malloc(52); // 4+1+6+1+6+1++32+1 (last one is the string terminator)
            memset(*(rdma_metadata + i), 0, 52);
            snprintf(*(rdma_metadata + i), 52, "%04x:%06x:%06x:%s", (*(config->local_endpoint + i))->lid, (*(config->local_endpoint + i))->qpn, (*(config->local_endpoint + i))->psn, (*(config->local_endpoint + i))->gid_string);
            debug_print("(RDMA_SEND) local RDMA metadata for remote #%d: %s\n", i, *(rdma_metadata + i));
        }
    }

    return(rdma_metadata);
}

struct rdma_context *
rdma_init_ctx(struct ibv_device *ib_dev, unsigned long *message_count, unsigned long *message_size, unsigned long *buffer_size, unsigned count, int port, int role, int function)
{
    struct rdma_context *ctx;
    int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
    
    // Add REMOTE_READ flag if using RDMA READ operations
    if (function == RDMA_READ) {
        access_flags |= IBV_ACCESS_REMOTE_READ;
    }
    
    int i, j;
    
    ctx = (struct rdma_context *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        fprintf(stderr, "rdma_init_ctx: Failed to create RDMA context.\n");
        return NULL;
    }

    ctx->size = (unsigned long *)calloc(count, sizeof(unsigned long));
    if (!ctx->size) {
        fprintf(stderr, "rdma_init_ctx: Failed to create work buffer size(s).\n");
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if (role == RDMA_SENDER) {
            printf("message_count: %d, message_size: %d\n", *(message_count + i), *(message_size + i));
            *(ctx->size + i) = *(message_count + i) * *(message_size + i);
        } else {
            printf("buffer_size: %d\n", *(buffer_size + i));
            *(ctx->size + i) = *(buffer_size + i);
        }
    }
    ctx->send_flags = IBV_SEND_SIGNALED;

    ctx->buf = (char **)malloc(count * sizeof(char *));
    if (!ctx->buf) {
        fprintf(stderr, "rdma_init_ctx: Couldn't allocate array of work buffers.\n");
        goto clean_ctx;
    }
    printf("rdma_init_ctx 1: buffer addr: %d\n", ctx->buf);
    for (i = 0; i < count; i++) {
        *(ctx->buf + i) = (char *)malloc(*(ctx->size + i));
        if (!*(ctx->buf + i)) {
            fprintf(stderr, "rdma_init_ctx: Couldn't allocate work buffer #%d out of %d.\n", i + 1, count);
            goto clean_ctx;
        } else {
            bzero(*(ctx->buf + i), *(ctx->size + i));
        }
    }

    for (i = 0; i < count; i++) {
        if (role == RDMA_SENDER) {
            for (j = 0; j < *(message_count + i); j++) {
                memset(*(ctx->buf + i) + j * *(message_size + i), j+43+i*(*(message_count + i)), *(message_size + i));
            }
        } else {
            memset(*(ctx->buf + i), 0x7b, *(ctx->size + i));
        }
    }
    // int k;
    // printf("ININTIALIZATION: Buffer data:\n");
    // for (k = 0; k < count; k++) {
    //     printf("ININTIALIZATION: client #%d:\n", k + 1);
    //     for (i = 0; i < *(message_count + k); i++) {
    //         for (j = 0; j < *(message_size + k); j++) {
    //             printf("%d:", *(*(ctx->buf + k) + i * *(message_size + k) + j));
    //         }
    //     }
    //     printf("\n");
    // }
    // printf("DONE\n");

    // Let the games begin!
    // open device
    ctx->context = ibv_open_device(ib_dev);
    if (!ctx->context) {
        fprintf(stderr, "rdma_init_ctx: Couldn't get context for %s\n", ibv_get_device_name(ib_dev));
        goto clean_buffer;
    }
    
    // create PD (Protection Domani) - a single one
    ctx->pd = ibv_alloc_pd(ctx->context);
    if (!ctx->pd) {
        fprintf(stderr, "rdma_init_ctx: Couldn't allocate PD\n");
    }
    
    // create MR (Memory Region) - one for each client
    ctx->mr = (struct ibv_mr **)calloc(count, sizeof(struct ibv_mr *));
    if (!ctx->mr) {
        fprintf(stderr, "rdma_init_ctx: Couldn't allocate MR array\n");
        goto clean_pd;
    }
    printf("rdma_init_ctx 2: buffer addr: %d\n", ctx->buf);
    printf("rdma_init_ctx 3: buffer addr: %d\n", *ctx->buf);
    for (i = 0; i < count; i++) {
        *(ctx->mr + i) = ibv_reg_mr(ctx->pd, *(ctx->buf + i), *(ctx->size + i), access_flags);
        fprintf(stderr, "rdma_init_ctx: MR addr: %d\n", (*(ctx->mr + i))->addr);
        fprintf(stderr, "rdma_init_ctx: buffer addr: %d\n", *(ctx->buf + i));
        if (!*(ctx->mr + i)) {
            fprintf(stderr, "rdma_init_ctx: Couldn't register MR #%d out of %d.\n", i + 1, count);
            goto clean_mr;
        }
    }
    
    // create CQ (Completion Queue) - one for each client
    ctx->cq = (struct ibv_cq **)calloc(count, sizeof(struct ibv_cq *));
    if (!ctx->cq) {
        fprintf(stderr, "rdma_init_ctx: Couldn't allocate CQ array\n");
        goto clean_mr;
    }
    for (i = 0; i < count; i++) {
        *(ctx->cq + i) = ibv_create_cq(ctx->context, RDMA_MAX_SEND_WR, NULL, NULL, 0);
        if (!*(ctx->cq + i)) {
            fprintf(stderr, "rdma_init_ctx: Couldn't create CQ #%d out of %d.\n", i + 1, count);
            goto clean_cq;
        }
    }

    // create QP (Queue Pair) - one for each client
    ctx->qp = (struct ibv_qp **)calloc(count, sizeof(struct ibv_qp *));
    if (!ctx->qp) {
        fprintf(stderr, "rdma_init_ctx: Couldn't allocate QP array\n");
        goto clean_cq;
    }
    for (i = 0; i < count; i++) {
        {
            struct ibv_qp_init_attr init_attr = {
                .send_cq = *(ctx->cq + i),
                .recv_cq = *(ctx->cq + i),
                .cap     = {
                    .max_send_wr  = RDMA_MAX_SEND_WR,
                    .max_recv_wr  = RDMA_MAX_RECV_WR,
                    .max_send_sge = 1,
                    .max_recv_sge = 1
                },
                .sq_sig_all = 1,
                .qp_type = IBV_QPT_RC
            };
            
            *(ctx->qp + i) = ibv_create_qp(ctx->pd, &init_attr);
            if (!*(ctx->qp + i))  {
                fprintf(stderr, "rdma_init_ctx: Couldn't create QP #%d out of %d.\n", i + 1, count);
                goto clean_qp;
            }
        }

        {
            struct ibv_qp_attr attr = {
                .qp_state        = IBV_QPS_INIT,
                .pkey_index      = 0,
                .port_num        = port,
                .qp_access_flags = access_flags
            };
            
            if (ibv_modify_qp(*(ctx->qp + i), &attr,
            IBV_QP_STATE              |
            IBV_QP_PKEY_INDEX         |
            IBV_QP_PORT               |
            IBV_QP_ACCESS_FLAGS)) {
                fprintf(stderr, "rdma_init_ctx: Failed to modify QP (#%d out of %d) to INIT.\n", i + 1, count);
                goto clean_qp;
            } else {
                fprintf(stdout, "rdma_init_ctx: QP (#%d out of %d) state set to INIT\n", i + 1, count);
            }
        }
    }

    return ctx;

    clean_qp:
    for (i = 0; i < count; i++) {
        if (*(ctx->qp + i)) {
            ibv_destroy_qp(*(ctx->qp + i));
        }
    }
    free(ctx->qp);

    clean_cq:
    for (i = 0; i < count; i++) {
        if (*(ctx->cq + i)) {
            ibv_destroy_cq(*(ctx->cq + i));
        }
    }
    free(ctx->cq);

    clean_mr:
    for (i = 0; i < count; i++) {
        if (*(ctx->mr + i)) {
            ibv_dereg_mr(*(ctx->mr + i));
        }
    }
    free(ctx->mr);

    clean_pd:
    ibv_dealloc_pd(ctx->pd);

    clean_buffer:
    for (i = 0; i < count; i++) {
        if (*(ctx->buf + i)) {
            free(*(ctx->buf + i));
        }
    }
    free(ctx->buf);

    clean_ctx:
    free(ctx);

    return NULL;
}

int
rdma_connect_ctx(struct rdma_context *ctx, int port, enum ibv_mtu mtu, struct rdma_endpoint **local_endpoint, struct rdma_endpoint **remote_endpoint, unsigned count, int sgid_idx, int role, int function)
{
    int i;

    for (i = 0; i < count; i++) {
        struct ibv_qp_attr attr = {
            .qp_state           = IBV_QPS_RTR,
            .path_mtu           = mtu,
            .dest_qp_num        = (*(remote_endpoint + i))->qpn,
            .rq_psn             = (*(remote_endpoint + i))->psn,
            .max_dest_rd_atomic	= MAX_RD_ATOMIC,
            .min_rnr_timer      = 12,
            .ah_attr			= {
                .is_global      = 0,
                .dlid           = (*(remote_endpoint + i))->lid,
                .sl             = 0,
                .src_path_bits  = 0,
                .port_num       = port
            }
        };

        if ((*(remote_endpoint + i))->gid.global.interface_id) {
            attr.ah_attr.is_global      = 1;
            attr.ah_attr.grh.hop_limit  = 1;
            attr.ah_attr.grh.dgid       = (*(remote_endpoint + i))->gid;
            attr.ah_attr.grh.sgid_index = sgid_idx;
        }

        if (ibv_modify_qp(*(ctx->qp + i), &attr,
                IBV_QP_STATE              |
                IBV_QP_AV                 |
                IBV_QP_PATH_MTU           |
                IBV_QP_DEST_QPN           |
                IBV_QP_RQ_PSN             |
                IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER)) {
            fprintf(stderr, "rdma_connect_ctx: Failed to modify QP (#%d out of %d) to RTR\n", i + 1, count);
            return 1;
        } else {
            fprintf(stdout, "rdma_connect_ctx: QP (#%d out of %d) state set to RTR\n", i + 1, count);
        }

        // For RDMA WRITE, only sender goes to RTS
        // For RDMA READ, both sender and receiver go to RTS (receiver initiates reads)
        if (role == RDMA_SENDER || (role == RDMA_RECEIVER && function == RDMA_READ)) {
            attr.qp_state       = IBV_QPS_RTS;
            attr.timeout        = 16;
            attr.retry_cnt      = 7;
            attr.rnr_retry      = 6;
            attr.sq_psn         = (*(local_endpoint + i))->psn;
            attr.max_rd_atomic  = MAX_RD_ATOMIC;

            if (ibv_modify_qp(*(ctx->qp + i), &attr,
                    IBV_QP_STATE              |
                    IBV_QP_TIMEOUT            |
                    IBV_QP_RETRY_CNT          |
                    IBV_QP_RNR_RETRY          |
                    IBV_QP_SQ_PSN             |
                    IBV_QP_MAX_QP_RD_ATOMIC)) {
                fprintf(stderr, "rdma_connect_ctx: Failed to modify QP (#%d out of %d) to RTS\n", i + 1, count);
                return 1;
            } else {
                fprintf(stdout, "rdma_connect_ctx: QP (#%d out of %d) state set to RTS\n", i + 1, count);
            }
        }
    }

	return 0;
}

int
rdma_close_ctx(struct rdma_context *ctx, unsigned count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (*(ctx->qp + i)) {
            if (ibv_destroy_qp(*(ctx->qp + i))) {
                fprintf(stderr, "rdma_close_ctx: Couldn't destroy QP (#%d out of %d)\n", i + 1, count);
                return 1;
            }
        }
    }
    free(ctx->qp);

    for (i = 0; i < count; i++) {
        if (*(ctx->cq + i)) {
            if (ibv_destroy_cq(*(ctx->cq + i))) {
                fprintf(stderr, "rdma_close_ctx: Couldn't destroy CQ (#%d out of %d)\n", i + 1, count);
                return 1;
            }
        }
    }
    free(ctx->cq);

    for (i = 0; i < count; i++) {
        if (*(ctx->mr + i)) {
            if (ibv_dereg_mr(*(ctx->mr + i))) {
                fprintf(stderr, "rdma_close_ctx: Couldn't deregister MR (#%d out of %d)\n", i + 1, count);
                return 1;
            }
        }
    }
    free(ctx->mr);

    if (ibv_dealloc_pd(ctx->pd)) {
        fprintf(stderr, "rdma_close_ctx: Couldn't deallocate PD\n");
        return 1;
    }

    if (ibv_close_device(ctx->context)) {
        fprintf(stderr, "rdma_close_ctx: Couldn't release context\n");
        return 1;
    }

    for (i = 0; i < count; i++) {
        if (*(ctx->buf + i)) {
            free(*(ctx->buf + i));
        }
    }
    free(ctx->buf);

    free(ctx);
    
    return 0;
}

