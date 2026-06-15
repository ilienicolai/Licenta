#include "rdma_infrastructure.h"

int
rdma_read_method(struct rdma_context *ctx, struct rdma_endpoint **remote_endpoint, unsigned long *message_count, unsigned long *message_size, unsigned long *mem_offset, unsigned count)
{
    struct ibv_send_wr *wr, **bad_wr;
    struct ibv_sge *list;

    int i, j, l, total, full_queue_count, remainder_queue_size, done, last, left, ne;
    struct ibv_wc wc[RDMA_MAX_SEND_WR];

    full_queue_count = *(message_count) / RDMA_MAX_SEND_WR;
    remainder_queue_size = *(message_count) % RDMA_MAX_SEND_WR;

    total = 0;
    
    // Process full batches
    for (j = 0; j < full_queue_count; j++) {
        wr = (struct ibv_send_wr *)malloc(RDMA_MAX_SEND_WR * sizeof(struct ibv_send_wr));
        bad_wr = (struct ibv_send_wr **)malloc(RDMA_MAX_SEND_WR * sizeof(struct ibv_send_wr *));
        list = (struct ibv_sge *)malloc(RDMA_MAX_SEND_WR * sizeof(struct ibv_sge));

        done = 0;
        last = 0;
        while (!done) {
            for (i = last; i < RDMA_MAX_SEND_WR; i++) {
                *(bad_wr + i) = NULL;
                memset(wr + i, 0, sizeof(struct ibv_send_wr));
                memset(list + i, 0, sizeof(struct ibv_sge));

                (wr + i)->wr_id = (j * RDMA_MAX_SEND_WR + i);
                
                // RDMA READ: Cannot chain work requests - must be NULL
                (wr + i)->next = NULL;
                
                // RDMA READ opcode
                (wr + i)->opcode = IBV_WR_RDMA_READ;
                (wr + i)->sg_list = list + i;
                (wr + i)->num_sge = 1;

                // RDMA READ: Must signal every completion
                (wr + i)->send_flags = IBV_SEND_SIGNALED;
                
                // Remote address: where to READ FROM
                (wr + i)->wr.rdma.remote_addr = (*remote_endpoint)->addr + *mem_offset + (j * RDMA_MAX_SEND_WR + i) * *message_size;
                (wr + i)->wr.rdma.rkey = (*remote_endpoint)->rkey;

                // Local buffer: where to WRITE TO
                (list + i)->length = *message_size;
                (list + i)->addr = (uint64_t)(*ctx->buf + (j * RDMA_MAX_SEND_WR + i) * *message_size);
                (list + i)->lkey = (*ctx->mr)->lkey;

                // RDMA READ: Must post individually (cannot chain)
                if (ibv_post_send(*ctx->qp, wr + i, bad_wr + i)) {
                    fprintf(stderr, "rdma_read_method: Couldn't post read #%d\n", i);
                    break;
                } else {
                    total++;
                }
            }

            debug_print("(client %d, loop %d) posted %d reads\n", 1, j, i);
            if (i < RDMA_MAX_SEND_WR) {
                debug_print("(client %d, loop %d) missing %d reads\n", 1, j, RDMA_MAX_SEND_WR - i);
                done = 0;
                last = i;
            } else {
                debug_print("(client %d, loop %d) all reads posted\n", 1, j);
                done = 1;
            }

            // Poll for completions
            left = i;  // Number of reads actually posted
            do {
                ne = ibv_poll_cq(*ctx->cq, left, wc);
                if (ne < 0) {
                    debug_print("(client %d, loop %d) poll CQ failed %d\n", 1, j, ne);
                } else {
                    left -= ne;
                    debug_print("(client %d, loop %d) ne=%d, left=%d\n", 1, j, ne, left);
                }
            } while (left > 0);
            debug_print("(client %d, loop %d) all completions received\n", 1, j);
        }

        free(wr);
        free(bad_wr);
        free(list);
    }

    // Process remainder batch
    if (remainder_queue_size > 0) {
        wr = (struct ibv_send_wr *)malloc(remainder_queue_size * sizeof(struct ibv_send_wr));
        bad_wr = (struct ibv_send_wr **)malloc(remainder_queue_size * sizeof(struct ibv_send_wr *));
        list = (struct ibv_sge *)malloc(remainder_queue_size * sizeof(struct ibv_sge));

        done = 0;
        last = 0;
        while (!done) {
            for (i = last; i < remainder_queue_size; i++) {
                *(bad_wr + i) = NULL;
                memset(wr + i, 0, sizeof(struct ibv_send_wr));
                memset(list + i, 0, sizeof(struct ibv_sge));

                (wr + i)->wr_id = (j * RDMA_MAX_SEND_WR + i);
                
                // RDMA READ: Cannot chain work requests - must be NULL
                (wr + i)->next = NULL;
                
                // RDMA READ opcode
                (wr + i)->opcode = IBV_WR_RDMA_READ;
                (wr + i)->sg_list = list + i;
                (wr + i)->num_sge = 1;

                // RDMA READ: Must signal every completion
                (wr + i)->send_flags = IBV_SEND_SIGNALED;
                
                // Remote address: where to READ FROM
                (wr + i)->wr.rdma.remote_addr = (*remote_endpoint)->addr + *mem_offset + (j * RDMA_MAX_SEND_WR + i) * *message_size;
                (wr + i)->wr.rdma.rkey = (*remote_endpoint)->rkey;

                // Local buffer: where to WRITE TO
                (list + i)->length = *message_size;
                (list + i)->addr = (uint64_t)(*ctx->buf + (j * RDMA_MAX_SEND_WR + i) * *message_size);
                (list + i)->lkey = (*ctx->mr)->lkey;

                // RDMA READ: Must post individually (cannot chain)
                if (ibv_post_send(*ctx->qp, wr + i, bad_wr + i)) {
                    fprintf(stderr, "rdma_read_method: Couldn't post read #%d\n", i);
                    break;
                } else {
                    total++;
                }
            }

            debug_print("(client %d, final loop) posted %d reads\n", 1, i);
            if (i < remainder_queue_size) {
                debug_print("(client %d, final loop) missing %d reads\n", 1, remainder_queue_size - i);
                done = 0;
                last = i;
            } else {
                debug_print("(client %d, final loop) all reads posted\n", 1);
                done = 1;
            }

            // Poll for completions
            left = i;  // Number of reads actually posted
            do {
                ne = ibv_poll_cq(*ctx->cq, left, wc);
                if (ne < 0) {
                    debug_print("(client %d, final loop) poll CQ failed %d\n", 1, ne);
                } else {
                    for (l = 0; l < ne; l++) {
                        if ((wc + l)->status != IBV_WC_SUCCESS) {
                            debug_print("(RDMA_READ) ibv_poll_cq failed status %s (%d) for wr_id %d\n", 
                                        ibv_wc_status_str((wc + l)->status), (wc + l)->status, (int)((wc + l)->wr_id));
                        } else {
                            debug_print("(RDMA_READ) ibv_poll_cq success status for wr_id %d\n", (int)((wc + l)->wr_id));
                        }
                    }
                    left -= ne;
                    debug_print("(client %d, final loop) ne=%d, left=%d\n", 1, ne, left);
                }
            } while (left > 0);
            debug_print("(client %d, final loop) all completions received\n", 1);
        }

            free(wr);
            free(bad_wr);
            free(list);
        }

    return 0;
}

int
rdma_read_stream_method(struct rdma_context *ctx, struct rdma_endpoint **remote_endpoint, unsigned long *message_count, unsigned long *message_size, unsigned long *mem_offset, unsigned count)
{
    struct ibv_send_wr *wr, **bad_wr;
    struct ibv_sge *list;

    int i, j, l, total, full_queue_count, remainder_queue_size, done, last, left, ne;
    struct ibv_wc wc[RDMA_MAX_SEND_WR];

    full_queue_count = *(message_count) / RDMA_MAX_SEND_WR;
    remainder_queue_size = *(message_count) % RDMA_MAX_SEND_WR;
    while (1) {
        total = 0;
        
        // Process full batches
        for (j = 0; j < full_queue_count; j++) {
            wr = (struct ibv_send_wr *)malloc(RDMA_MAX_SEND_WR * sizeof(struct ibv_send_wr));
            bad_wr = (struct ibv_send_wr **)malloc(RDMA_MAX_SEND_WR * sizeof(struct ibv_send_wr *));
            list = (struct ibv_sge *)malloc(RDMA_MAX_SEND_WR * sizeof(struct ibv_sge));

            done = 0;
            last = 0;
            while (!done) {
                for (i = last; i < RDMA_MAX_SEND_WR; i++) {
                    *(bad_wr + i) = NULL;
                    memset(wr + i, 0, sizeof(struct ibv_send_wr));
                    memset(list + i, 0, sizeof(struct ibv_sge));

                    (wr + i)->wr_id = (j * RDMA_MAX_SEND_WR + i);
                    
                    // RDMA READ: Cannot chain work requests - must be NULL
                    (wr + i)->next = NULL;
                    
                    // RDMA READ opcode
                    (wr + i)->opcode = IBV_WR_RDMA_READ;
                    (wr + i)->sg_list = list + i;
                    (wr + i)->num_sge = 1;

                    // RDMA READ: Must signal every completion
                    (wr + i)->send_flags = IBV_SEND_SIGNALED;
                    
                    // Remote address: where to READ FROM
                    (wr + i)->wr.rdma.remote_addr = (*remote_endpoint)->addr + *mem_offset + (j * RDMA_MAX_SEND_WR + i) * *message_size;
                    (wr + i)->wr.rdma.rkey = (*remote_endpoint)->rkey;

                    // Local buffer: where to WRITE TO
                    (list + i)->length = *message_size;
                    (list + i)->addr = (uint64_t)(*ctx->buf + (j * RDMA_MAX_SEND_WR + i) * *message_size);
                    (list + i)->lkey = (*ctx->mr)->lkey;

                    // RDMA READ: Must post individually (cannot chain)
                    if (ibv_post_send(*ctx->qp, wr + i, bad_wr + i)) {
                        fprintf(stderr, "rdma_read_method: Couldn't post read #%d\n", i);
                        break;
                    } else {
                        total++;
                    }
                }

                debug_print("(client %d, loop %d) posted %d reads\n", 1, j, i);
                if (i < RDMA_MAX_SEND_WR) {
                    debug_print("(client %d, loop %d) missing %d reads\n", 1, j, RDMA_MAX_SEND_WR - i);
                    done = 0;
                    last = i;
                } else {
                    debug_print("(client %d, loop %d) all reads posted\n", 1, j);
                    done = 1;
                }

                // Poll for completions
                left = i;  // Number of reads actually posted
                do {
                    ne = ibv_poll_cq(*ctx->cq, left, wc);
                    if (ne < 0) {
                        debug_print("(client %d, loop %d) poll CQ failed %d\n", 1, j, ne);
                    } else {
                        left -= ne;
                        debug_print("(client %d, loop %d) ne=%d, left=%d\n", 1, j, ne, left);
                    }
                } while (left > 0);
                debug_print("(client %d, loop %d) all completions received\n", 1, j);
            }

            free(wr);
            free(bad_wr);
            free(list);
        }

        // Process remainder batch
        if (remainder_queue_size > 0) {
            wr = (struct ibv_send_wr *)malloc(remainder_queue_size * sizeof(struct ibv_send_wr));
            bad_wr = (struct ibv_send_wr **)malloc(remainder_queue_size * sizeof(struct ibv_send_wr *));
            list = (struct ibv_sge *)malloc(remainder_queue_size * sizeof(struct ibv_sge));

            done = 0;
            last = 0;
            while (!done) {
                for (i = last; i < remainder_queue_size; i++) {
                    *(bad_wr + i) = NULL;
                    memset(wr + i, 0, sizeof(struct ibv_send_wr));
                    memset(list + i, 0, sizeof(struct ibv_sge));

                    (wr + i)->wr_id = (j * RDMA_MAX_SEND_WR + i);
                    
                    // RDMA READ: Cannot chain work requests - must be NULL
                    (wr + i)->next = NULL;
                    
                    // RDMA READ opcode
                    (wr + i)->opcode = IBV_WR_RDMA_READ;
                    (wr + i)->sg_list = list + i;
                    (wr + i)->num_sge = 1;

                    // RDMA READ: Must signal every completion
                    (wr + i)->send_flags = IBV_SEND_SIGNALED;
                    
                    // Remote address: where to READ FROM
                    (wr + i)->wr.rdma.remote_addr = (*remote_endpoint)->addr + *mem_offset + (j * RDMA_MAX_SEND_WR + i) * *message_size;
                    (wr + i)->wr.rdma.rkey = (*remote_endpoint)->rkey;

                    // Local buffer: where to WRITE TO
                    (list + i)->length = *message_size;
                    (list + i)->addr = (uint64_t)(*ctx->buf + (j * RDMA_MAX_SEND_WR + i) * *message_size);
                    (list + i)->lkey = (*ctx->mr)->lkey;

                    // RDMA READ: Must post individually (cannot chain)
                    if (ibv_post_send(*ctx->qp, wr + i, bad_wr + i)) {
                        fprintf(stderr, "rdma_read_method: Couldn't post read #%d\n", i);
                        break;
                    } else {
                        total++;
                    }
                }

                debug_print("(client %d, final loop) posted %d reads\n", 1, i);
                if (i < remainder_queue_size) {
                    debug_print("(client %d, final loop) missing %d reads\n", 1, remainder_queue_size - i);
                    done = 0;
                    last = i;
                } else {
                    debug_print("(client %d, final loop) all reads posted\n", 1);
                    done = 1;
                }

                // Poll for completions
                left = i;  // Number of reads actually posted
                do {
                    ne = ibv_poll_cq(*ctx->cq, left, wc);
                    if (ne < 0) {
                        debug_print("(client %d, final loop) poll CQ failed %d\n", 1, ne);
                    } else {
                        for (l = 0; l < ne; l++) {
                            if ((wc + l)->status != IBV_WC_SUCCESS) {
                                debug_print("(RDMA_READ) ibv_poll_cq failed status %s (%d) for wr_id %d\n", 
                                            ibv_wc_status_str((wc + l)->status), (wc + l)->status, (int)((wc + l)->wr_id));
                            } else {
                                debug_print("(RDMA_READ) ibv_poll_cq success status for wr_id %d\n", (int)((wc + l)->wr_id));
                            }
                        }
                        left -= ne;
                        debug_print("(client %d, final loop) ne=%d, left=%d\n", 1, ne, left);
                    }
                } while (left > 0);
                debug_print("(client %d, final loop) all completions received\n", 1);
            }

            free(wr);
            free(bad_wr);
            free(list);
        }
        sleep(5);
    }

    return 0;
}

// RDMA READ thread that posts read operations with backpressure control
void *
rdma_read_producer_thread(void *arg)
{
    struct rdma_thread_param *thread_args = (struct rdma_thread_param *)arg;
    struct ibv_send_wr *wr, **bad_wr;
    struct ibv_sge *list;
    struct rdma_context *ctx = thread_args->rdma_ctx;
    struct rdma_endpoint *remote_endpoint = thread_args->remote_endpoint;

    int i, j, l, total, full_queue_count, remainder_queue_size, done, last, left, ne;
    struct ibv_wc wc[RDMA_MAX_SEND_WR];
    unsigned long current_offset = 0;
    unsigned long reads_completed = 0;
    unsigned long total_reads = thread_args->message_count;
    unsigned long used_size;
    int paused = 0;
    unsigned long cycle = 0;

    full_queue_count = total_reads / RDMA_MAX_SEND_WR;
    remainder_queue_size = total_reads % RDMA_MAX_SEND_WR;
    
    // fprintf(stdout, "(RDMA_READ_PRODUCER) Starting continuous read mode with %lu reads per cycle, %d full batches, %d remainder\n",
    //         total_reads, full_queue_count, remainder_queue_size);

    // Continuous loop - keep reading forever
    while (1) {
        // fprintf(stdout, "(RDMA_READ_PRODUCER) Starting cycle %lu\n", cycle);
        reads_completed = 0;
        
        // Process full batches
        for (j = 0; j < full_queue_count; j++) {
            // Check backpressure before posting new batch
            pthread_mutex_lock(&(thread_args->cond_lock));
            used_size = thread_args->used_size;
            
            // Wait if buffer is too full
            while (used_size >= (thread_args->buffer_size * thread_args->backpressure_threshold_up / 100)) {
                if (!paused) {
                    // fprintf(stdout, "(RDMA_READ_PRODUCER) Backpressure activated: used=%lu, buffer=%lu, threshold=%u%%\n",
                    //         used_size, thread_args->buffer_size, thread_args->backpressure_threshold_up);
                    thread_args->backpressure = 1;
                    paused = 1;
                }
                pthread_mutex_unlock(&(thread_args->cond_lock));
                usleep(10000); // Sleep 10ms before checking again
                pthread_mutex_lock(&(thread_args->cond_lock));
                used_size = thread_args->used_size;
                
                // Check if we can resume
                if (used_size < (thread_args->buffer_size * thread_args->backpressure_threshold_down / 100)) {
                    // fprintf(stdout, "(RDMA_READ_PRODUCER) Backpressure released: used=%lu, buffer=%lu, threshold=%u%%\n",
                    //         used_size, thread_args->buffer_size, thread_args->backpressure_threshold_down);
                    thread_args->backpressure = 0;
                    paused = 0;
                    break;
                }
            }
            pthread_mutex_unlock(&(thread_args->cond_lock));

            wr = (struct ibv_send_wr *)malloc(RDMA_MAX_SEND_WR * sizeof(struct ibv_send_wr));
            bad_wr = (struct ibv_send_wr **)malloc(RDMA_MAX_SEND_WR * sizeof(struct ibv_send_wr *));
            list = (struct ibv_sge *)malloc(RDMA_MAX_SEND_WR * sizeof(struct ibv_sge));

            done = 0;
            last = 0;
            while (!done) {
                for (i = last; i < RDMA_MAX_SEND_WR; i++) {
                    unsigned long read_idx = j * RDMA_MAX_SEND_WR + i;
                    unsigned long buffer_offset = (thread_args->mem_offset_produce + i * thread_args->message_size) % thread_args->buffer_size;

                    *(bad_wr + i) = NULL;
                    memset(wr + i, 0, sizeof(struct ibv_send_wr));
                    memset(list + i, 0, sizeof(struct ibv_sge));

                    (wr + i)->wr_id = read_idx;
                    (wr + i)->next = NULL;
                    (wr + i)->opcode = IBV_WR_RDMA_READ;
                    (wr + i)->sg_list = list + i;
                    (wr + i)->num_sge = 1;
                    (wr + i)->send_flags = IBV_SEND_SIGNALED;
                    
                    // Remote address: where to READ FROM
                    (wr + i)->wr.rdma.remote_addr = remote_endpoint->addr + thread_args->mem_offset + read_idx * thread_args->message_size;
                    (wr + i)->wr.rdma.rkey = remote_endpoint->rkey;

                    // Local buffer: where to WRITE TO (circular buffer)
                    (list + i)->length = thread_args->message_size;
                    (list + i)->addr = (uint64_t)(*ctx->buf + buffer_offset);
                    (list + i)->lkey = (*ctx->mr)->lkey;

                    if (ibv_post_send(*ctx->qp, wr + i, bad_wr + i)) {
                        fprintf(stderr, "(RDMA_READ_PRODUCER) Couldn't post read #%d\n", i);
                        break;
                    } else {
                        total++;
                    }
                }

                debug_print("(RDMA_READ_PRODUCER, batch %d) posted %d reads\n", j, i);

                if (i < RDMA_MAX_SEND_WR) {
                    debug_print("(RDMA_READ_PRODUCER, batch %d) missing %d reads\n", j, RDMA_MAX_SEND_WR - i);
                    done = 0;
                    last = i;
                } else {
                    done = 1;
                }

                // Poll for completions
                left = i;
                do {
                    ne = ibv_poll_cq(*ctx->cq, left, wc);
                    if (ne < 0) {
                        fprintf(stderr, "(RDMA_READ_PRODUCER, batch %d) poll CQ failed %d\n", j, ne);
                    } else {
                        for (l = 0; l < ne; l++) {
                            if ((wc + l)->status != IBV_WC_SUCCESS) {
                                fprintf(stderr, "(RDMA_READ_PRODUCER) Read failed: %s (%d) for wr_id %d\n", 
                                        ibv_wc_status_str((wc + l)->status), (wc + l)->status, (int)((wc + l)->wr_id));
                            } else {
                                debug_print("(RDMA_READ_PRODUCER) Read completed for wr_id %d\n", (int)((wc + l)->wr_id));
                                
                                // Update buffer tracking
                                pthread_mutex_lock(&(thread_args->cond_lock));
                                thread_args->received_size_fifo[thread_args->tail] = thread_args->message_size;
                                thread_args->tail++;
                                if (thread_args->tail >= RECEIVED_FIFO_SIZE) {
                                    thread_args->tail = 0;
                                }
                                thread_args->mem_offset_produce = (thread_args->mem_offset_produce + thread_args->message_size) % thread_args->buffer_size;
                                thread_args->used_size += thread_args->message_size;
                                reads_completed++;
                                pthread_cond_signal(&(thread_args->start_work));
                                pthread_mutex_unlock(&(thread_args->cond_lock));
                            }
                        }
                        left -= ne;
                    }
                } while (left > 0);
            }

            free(wr);
            free(bad_wr);
            free(list);
        }

    // Process remainder batch
    if (remainder_queue_size > 0) {
        // Check backpressure
        pthread_mutex_lock(&(thread_args->cond_lock));
        used_size = thread_args->used_size;
        
        while (used_size >= (thread_args->buffer_size * thread_args->backpressure_threshold_up / 100)) {
            if (!paused) {
                // fprintf(stdout, "(RDMA_READ_PRODUCER) Backpressure activated: used=%lu, buffer=%lu, threshold=%u%%\n",
                //         used_size, thread_args->buffer_size, thread_args->backpressure_threshold_up);
                thread_args->backpressure = 1;
                paused = 1;
            }
            pthread_mutex_unlock(&(thread_args->cond_lock));
            usleep(10000);
            pthread_mutex_lock(&(thread_args->cond_lock));
            used_size = thread_args->used_size;
            
            if (used_size < (thread_args->buffer_size * thread_args->backpressure_threshold_down / 100)) {
                // fprintf(stdout, "(RDMA_READ_PRODUCER) Backpressure released: used=%lu, buffer=%lu, threshold=%u%%\n",
                //         used_size, thread_args->buffer_size, thread_args->backpressure_threshold_down);
                thread_args->backpressure = 0;
                paused = 0;
                break;
            }
        }
        pthread_mutex_unlock(&(thread_args->cond_lock));

        wr = (struct ibv_send_wr *)malloc(remainder_queue_size * sizeof(struct ibv_send_wr));
        bad_wr = (struct ibv_send_wr **)malloc(remainder_queue_size * sizeof(struct ibv_send_wr *));
        list = (struct ibv_sge *)malloc(remainder_queue_size * sizeof(struct ibv_sge));

        done = 0;
        last = 0;
        while (!done) {
            for (i = last; i < remainder_queue_size; i++) {
                unsigned long read_idx = full_queue_count * RDMA_MAX_SEND_WR + i;
                unsigned long buffer_offset = (thread_args->mem_offset_produce + i * thread_args->message_size) % thread_args->buffer_size;

                *(bad_wr + i) = NULL;
                memset(wr + i, 0, sizeof(struct ibv_send_wr));
                memset(list + i, 0, sizeof(struct ibv_sge));

                (wr + i)->wr_id = read_idx;
                (wr + i)->next = NULL;
                (wr + i)->opcode = IBV_WR_RDMA_READ;
                (wr + i)->sg_list = list + i;
                (wr + i)->num_sge = 1;
                (wr + i)->send_flags = IBV_SEND_SIGNALED;
                
                (wr + i)->wr.rdma.remote_addr = remote_endpoint->addr + thread_args->mem_offset + read_idx * thread_args->message_size;
                (wr + i)->wr.rdma.rkey = remote_endpoint->rkey;

                (list + i)->length = thread_args->message_size;
                (list + i)->addr = (uint64_t)(*ctx->buf + buffer_offset);
                (list + i)->lkey = (*ctx->mr)->lkey;

                if (ibv_post_send(*ctx->qp, wr + i, bad_wr + i)) {
                    fprintf(stderr, "(RDMA_READ_PRODUCER) Couldn't post read #%d\n", i);
                    break;
                } else {
                    total++;
                }
            }

            debug_print("(RDMA_READ_PRODUCER, final batch) posted %d reads\n", i);

            if (i < remainder_queue_size) {
                done = 0;
                last = i;
            } else {
                done = 1;
            }

            // Poll for completions
            left = i;
            do {
                ne = ibv_poll_cq(*ctx->cq, left, wc);
                if (ne < 0) {
                    fprintf(stderr, "(RDMA_READ_PRODUCER, final batch) poll CQ failed %d\n", ne);
                } else {
                    for (l = 0; l < ne; l++) {
                        if ((wc + l)->status != IBV_WC_SUCCESS) {
                            fprintf(stderr, "(RDMA_READ_PRODUCER) Read failed: %s (%d) for wr_id %d\n", 
                                    ibv_wc_status_str((wc + l)->status), (wc + l)->status, (int)((wc + l)->wr_id));
                        } else {
                            debug_print("(RDMA_READ_PRODUCER) Read completed for wr_id %d\n", (int)((wc + l)->wr_id));
                            
                            pthread_mutex_lock(&(thread_args->cond_lock));
                            thread_args->received_size_fifo[thread_args->tail] = thread_args->message_size;
                            thread_args->tail++;
                            if (thread_args->tail >= RECEIVED_FIFO_SIZE) {
                                thread_args->tail = 0;
                            }
                            thread_args->mem_offset_produce = (thread_args->mem_offset_produce + thread_args->message_size) % thread_args->buffer_size;
                            thread_args->used_size += thread_args->message_size;
                            reads_completed++;
                            pthread_cond_signal(&(thread_args->start_work));
                            pthread_mutex_unlock(&(thread_args->cond_lock));
                        }
                    }
                    left -= ne;
                }
            } while (left > 0);
        }

            free(wr);
            free(bad_wr);
            free(list);
        }

        // fprintf(stdout, "(RDMA_READ_PRODUCER) Cycle %lu completed: %lu reads\n", cycle, reads_completed);
        cycle++;
        
        // Small delay between cycles to avoid overwhelming the system
        usleep(100000); // 100ms delay between cycles
    }

    // This code is unreachable in continuous mode, but kept for completeness
    // fprintf(stdout, "(RDMA_READ_PRODUCER) Stopped after %lu cycles\n", cycle);
    
    // Signal all workers to finish
    pthread_mutex_lock(&(thread_args->cond_lock));
    thread_args->got_data = READY;
    pthread_cond_broadcast(&(thread_args->start_work));
    pthread_mutex_unlock(&(thread_args->cond_lock));

    return NULL;
}// Consumer thread for RDMA READ with backpressure
void *
rdma_read_consumer_thread(void *arg)
{
    char *devnull;
    unsigned long chunk_size, work_size, work_start_offset, work_end_offset;
    unsigned long new_mem_offset_circular;
    long int timestamp_ns;
    unsigned local_worker_id;

    struct rdma_thread_param *thread_args = (struct rdma_thread_param *)arg;

    pthread_mutex_lock(&(thread_args->cond_lock));
    local_worker_id = thread_args->worker_id++;
    pthread_mutex_unlock(&(thread_args->cond_lock));

    devnull = (char *)malloc(thread_args->message_count * thread_args->message_size);
    bzero(devnull, thread_args->message_count * thread_args->message_size);

    while (1) {
        timestamp_ns = get_current_timestamp_ns() - thread_args->start_ts;

        pthread_mutex_lock(&(thread_args->cond_lock));
        while(thread_args->mem_offset_produce == thread_args->mem_offset_consume && thread_args->got_data != READY) {
            pthread_cond_wait(&(thread_args->start_work), &(thread_args->cond_lock));
        }

        // Check if we're done
        if (thread_args->got_data == READY && thread_args->mem_offset_produce == thread_args->mem_offset_consume) {
            pthread_mutex_unlock(&(thread_args->cond_lock));
            break;
        }

        chunk_size = thread_args->received_size_fifo[thread_args->head];
        new_mem_offset_circular = (thread_args->mem_offset_consume + chunk_size) % thread_args->buffer_size;
        pthread_mutex_unlock(&(thread_args->cond_lock));

        // Divide work among workers
        work_size = chunk_size / thread_args->worker_count;
        work_start_offset = local_worker_id * work_size;
        work_end_offset = (local_worker_id + 1) * work_size;
        if (local_worker_id == thread_args->worker_count - 1) {
            if (work_end_offset < chunk_size) {
                work_end_offset = chunk_size;
                work_size = work_end_offset - work_start_offset;
            }
        }

        // Consume data (copy to devnull simulates processing)
        memcpy(devnull, (*thread_args->rdma_ctx->buf) + thread_args->mem_offset_consume + work_start_offset, work_size);

        pthread_barrier_wait(&(thread_args->workers_done_barrier));

        if (local_worker_id == 0) {
            unsigned long used_size;

            thread_args->head++;
            if (thread_args->head >= RECEIVED_FIFO_SIZE) {
                thread_args->head = 0;
            }

            pthread_mutex_lock(&(thread_args->cond_lock));
            thread_args->mem_offset_consume = new_mem_offset_circular;
            thread_args->used_size -= chunk_size;
            if (thread_args->mem_offset_produce >= thread_args->mem_offset_consume) {
                used_size = thread_args->mem_offset_produce - thread_args->mem_offset_consume;
            } else {
                used_size = thread_args->buffer_size + thread_args->mem_offset_produce - thread_args->mem_offset_consume;
            }
            pthread_mutex_unlock(&(thread_args->cond_lock));

            // printf("(RDMA_READ_CONSUMER) timestamp=%ld ms, used_size=%lu bytes (%.1f%%)\n",
            //        (timestamp_ns / 1000000), used_size, (100.0 * used_size / thread_args->buffer_size));
        }
    }

    free(devnull);
    // fprintf(stdout, "(RDMA_READ_CONSUMER) Worker %lu finished\n", local_worker_id);
    return NULL;
}

// Main function for RDMA READ with consumption and backpressure
int
rdma_read_consume(int control_socket, unsigned int backpressure_threshold_up, unsigned int backpressure_threshold_down, struct rdma_context *ctx, struct rdma_endpoint **remote_endpoint, unsigned long *message_count, unsigned long *message_size, unsigned long *buffer_size, unsigned long *mem_offset, unsigned worker_count)
{
    pthread_t producer_thread;
    pthread_t *worker_threads;
    struct rdma_thread_param *thread_args;

    worker_threads = (pthread_t *)malloc(worker_count * sizeof(pthread_t));
    thread_args = (struct rdma_thread_param *)malloc(sizeof(struct rdma_thread_param));

    thread_args->rdma_ctx = ctx;
    thread_args->remote_endpoint = *remote_endpoint;
    thread_args->message_count = *message_count;
    thread_args->message_size = *message_size;
    thread_args->buffer_size = *buffer_size;
    thread_args->mem_offset = *mem_offset;
    thread_args->mem_offset_produce = 0;
    thread_args->mem_offset_consume = 0;
    thread_args->used_size = 0;
    thread_args->received_size = 0;

    thread_args->control_socket = control_socket;
    thread_args->backpressure = 0;
    thread_args->backpressure_threshold_up = backpressure_threshold_up;
    thread_args->backpressure_threshold_down = backpressure_threshold_down;

    thread_args->start_ts = get_current_timestamp_ns();

    thread_args->worker_count = worker_count;
    thread_args->worker_id = 0;
    thread_args->got_data = 0;

    bzero(thread_args->received_size_fifo, RECEIVED_FIFO_SIZE * sizeof(unsigned int));
    thread_args->fifo_size = RECEIVED_FIFO_SIZE;
    thread_args->head = 0;
    thread_args->tail = 0;

    pthread_mutex_init(&(thread_args->cond_lock), NULL);
    pthread_cond_init(&(thread_args->start_work), NULL);
    pthread_barrier_init(&(thread_args->workers_done_barrier), NULL, worker_count);

    // fprintf(stdout, "(RDMA_READ_CONSUME) Starting with %u worker threads\n", worker_count);
    // fprintf(stdout, "(RDMA_READ_CONSUME) Buffer: %lu bytes, Messages: %lu x %lu bytes\n",
    //         *buffer_size, *message_count, *message_size);
    // fprintf(stdout, "(RDMA_READ_CONSUME) Backpressure thresholds: up=%u%%, down=%u%%\n",
    //         backpressure_threshold_up, backpressure_threshold_down);

    // Start worker threads
    for (int i = 0; i < worker_count; i++) {
        if (pthread_create(&(worker_threads[i]), NULL, rdma_read_consumer_thread, thread_args) != 0) {
            fprintf(stderr, "(RDMA_READ_CONSUME) pthread_create() error - worker_threads[%d]\n", i);
            return -1;
        }
    }

    // Start producer thread
    if (pthread_create(&producer_thread, NULL, rdma_read_producer_thread, thread_args) != 0) {
        fprintf(stderr, "(RDMA_READ_CONSUME) pthread_create() error - producer_thread\n");
        return -1;
    }

    // Wait for producer to finish
    if (pthread_join(producer_thread, NULL) != 0) {
        fprintf(stderr, "(RDMA_READ_CONSUME) pthread_join() error - producer_thread\n");
    }

    // Wait for all workers to finish
    for (int i = 0; i < worker_count; i++) {
        if (pthread_join(worker_threads[i], NULL) != 0) {
            fprintf(stderr, "(RDMA_READ_CONSUME) pthread_join() error - worker_threads[%d]\n", i);
        }
    }

    pthread_mutex_destroy(&(thread_args->cond_lock));
    pthread_cond_destroy(&(thread_args->start_work));
    pthread_barrier_destroy(&(thread_args->workers_done_barrier));

    free(worker_threads);
    free(thread_args);

    // fprintf(stdout, "(RDMA_READ_CONSUME) All operations completed successfully\n");

    return 0;
}

/*
 * Simple CRC32 implementation (IEEE 802.3 polynomial).
 * Used for data integrity checking over RDMA READ transfers.
 */
static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

static void
crc32_init_table(void)
{
    uint32_t i, j, crc;
    for (i = 0; i < 256; i++) {
        crc = i;
        for (j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc = crc >> 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = 1;
}

static uint32_t
crc32_compute(uint32_t crc, const void *data, size_t length)
{
    const unsigned char *buf = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < length; i++)
        crc = (crc >> 8) ^ crc32_table[(crc ^ buf[i]) & 0xFF];
    return crc;
}

uint32_t
rdma_crc32(const void *data, size_t length)
{
    if (!crc32_table_initialized)
        crc32_init_table();
    return crc32_compute(0xFFFFFFFF, data, length) ^ 0xFFFFFFFF;
}

/* Compute CRC32 over a region of a circular buffer, handling wrap-around. */
static uint32_t
crc32_of_circular(const char *buf, unsigned long start_offset, unsigned long total_len, unsigned long buf_size)
{
    uint32_t crc = 0xFFFFFFFF;
    unsigned long end_offset = start_offset + total_len;

    if (!crc32_table_initialized)
        crc32_init_table();

    if (end_offset <= buf_size) {
        /* No wrap-around: single contiguous region */
        crc = crc32_compute(crc, buf + start_offset, total_len);
    } else {
        /* Wrap-around: two parts */
        unsigned long part1 = buf_size - start_offset;
        unsigned long part2 = total_len - part1;
        crc = crc32_compute(crc, buf + start_offset, part1);
        crc = crc32_compute(crc, buf, part2);
    }
    return crc ^ 0xFFFFFFFF;
}

int
rdma_read_method_check(struct rdma_context *ctx, struct rdma_endpoint **remote_endpoint, unsigned long *message_count, unsigned long *message_size, unsigned long *mem_offset, unsigned count, int control_socket)
{
    struct ibv_send_wr *wr, **bad_wr;
    struct ibv_sge *list;

    int i, j, l, total, full_queue_count, remainder_queue_size, done, last, left, ne;
    int batch_size, batch_errors;
    struct ibv_wc wc[RDMA_MAX_SEND_WR];
    uint32_t remote_hash, local_hash;
    int total_batches;
    int hash_mismatches = 0;

    full_queue_count = *(message_count) / RDMA_MAX_SEND_WR;
    remainder_queue_size = *(message_count) % RDMA_MAX_SEND_WR;
    total_batches = full_queue_count + (remainder_queue_size > 0 ? 1 : 0);

    total = 0;
    
    // fprintf(stdout, "(RDMA_READ_CHECK) Starting integrity-checked RDMA READ: %lu messages, %lu bytes each, %d batches\n",
    //         *message_count, *message_size, total_batches);

    // Process all batches (full + remainder)
    for (j = 0; j < total_batches; j++) {
        // Determine batch size: full batch or remainder
        if (j < full_queue_count) {
            batch_size = RDMA_MAX_SEND_WR;
        } else {
            batch_size = remainder_queue_size;
        }

        // Step 1: Request hash from sender over TCP
        //   Send: "HASH:<batch_index>:<batch_offset>:<batch_data_length>"
        //   The sender will compute CRC32 over that region and send back the 4-byte hash
        {
            unsigned long batch_offset = *mem_offset + (unsigned long)j * RDMA_MAX_SEND_WR * *message_size;
            unsigned long batch_data_len = (unsigned long)batch_size * *message_size;
            char hash_req[128];

            snprintf(hash_req, sizeof(hash_req), "HASH:%d:%lu:%lu", j, batch_offset, batch_data_len);
            write(control_socket, hash_req, sizeof(hash_req));
            debug_print("(RDMA_READ_CHECK) Sent hash request for batch %d: offset=%lu, len=%lu\n", j, batch_offset, batch_data_len);

            // Receive the 4-byte CRC32 hash from sender
            memset(&remote_hash, 0, sizeof(remote_hash));
            read(control_socket, &remote_hash, sizeof(remote_hash));
            debug_print("(RDMA_READ_CHECK) Received remote hash for batch %d: 0x%08x\n", j, remote_hash);
        }

        // Step 2: Post RDMA READ work requests for this batch
        wr = (struct ibv_send_wr *)malloc(batch_size * sizeof(struct ibv_send_wr));
        bad_wr = (struct ibv_send_wr **)malloc(batch_size * sizeof(struct ibv_send_wr *));
        list = (struct ibv_sge *)malloc(batch_size * sizeof(struct ibv_sge));

        done = 0;
        last = 0;
        batch_errors = 0;
        while (!done) {
            for (i = last; i < batch_size; i++) {
                *(bad_wr + i) = NULL;
                memset(wr + i, 0, sizeof(struct ibv_send_wr));
                memset(list + i, 0, sizeof(struct ibv_sge));

                (wr + i)->wr_id = (j * RDMA_MAX_SEND_WR + i);
                
                // RDMA READ: Cannot chain work requests - must be NULL
                (wr + i)->next = NULL;
                
                // RDMA READ opcode
                (wr + i)->opcode = IBV_WR_RDMA_READ;
                (wr + i)->sg_list = list + i;
                (wr + i)->num_sge = 1;

                // RDMA READ: Must signal every completion
                (wr + i)->send_flags = IBV_SEND_SIGNALED;
                
                // Remote address: where to READ FROM
                (wr + i)->wr.rdma.remote_addr = (*remote_endpoint)->addr + *mem_offset + (j * RDMA_MAX_SEND_WR + i) * *message_size;
                (wr + i)->wr.rdma.rkey = (*remote_endpoint)->rkey;

                // Local buffer: where to WRITE TO
                (list + i)->length = *message_size;
                (list + i)->addr = (uint64_t)(*ctx->buf + (j * RDMA_MAX_SEND_WR + i) * *message_size);
                (list + i)->lkey = (*ctx->mr)->lkey;

                // RDMA READ: Must post individually (cannot chain)
                if (ibv_post_send(*ctx->qp, wr + i, bad_wr + i)) {
                    fprintf(stderr, "rdma_read_method_check: Couldn't post read #%d\n", i);
                    break;
                } else {
                    total++;
                }
            }

            debug_print("(RDMA_READ_CHECK) (batch %d) posted %d reads\n", j, i);
            if (i < batch_size) {
                debug_print("(RDMA_READ_CHECK) (batch %d) missing %d reads\n", j, batch_size - i);
                done = 0;
                last = i;
            } else {
                debug_print("(RDMA_READ_CHECK) (batch %d) all reads posted\n", j);
                done = 1;
            }

            // Step 3: Poll for completions
            left = i;  // Number of reads actually posted
            do {
                ne = ibv_poll_cq(*ctx->cq, left, wc);
                if (ne < 0) {
                    debug_print("(RDMA_READ_CHECK) (batch %d) poll CQ failed %d\n", j, ne);
                } else {
                    for (l = 0; l < ne; l++) {
                        if ((wc + l)->status != IBV_WC_SUCCESS) {
                            fprintf(stderr, "(RDMA_READ_CHECK) ibv_poll_cq failed status %s (%d) for wr_id %d\n", 
                                    ibv_wc_status_str((wc + l)->status), (wc + l)->status, (int)((wc + l)->wr_id));
                            batch_errors++;
                        } else {
                            debug_print("(RDMA_READ_CHECK) ibv_poll_cq success for wr_id %d\n", (int)((wc + l)->wr_id));
                        }
                    }
                    left -= ne;
                    debug_print("(RDMA_READ_CHECK) (batch %d) ne=%d, left=%d\n", j, ne, left);
                }
            } while (left > 0);
            debug_print("(RDMA_READ_CHECK) (batch %d) all completions received\n", j);
        }

        // Step 4: Compute local CRC32 over the received data and compare
        if (batch_errors == 0) {
            unsigned long local_offset = (unsigned long)j * RDMA_MAX_SEND_WR * *message_size;
            unsigned long batch_data_len = (unsigned long)batch_size * *message_size;

            local_hash = rdma_crc32(*ctx->buf + local_offset, batch_data_len);

            if (local_hash == remote_hash) {
                // fprintf(stdout, "(RDMA_READ_CHECK) Batch %d/%d: CRC32 OK (0x%08x), %d messages verified\n",
                //         j + 1, total_batches, local_hash, batch_size);
            } else {
                fprintf(stderr, "(RDMA_READ_CHECK) Batch %d/%d: CRC32 MISMATCH! remote=0x%08x local=0x%08x, %d messages\n",
                        j + 1, total_batches, remote_hash, local_hash, batch_size);
                hash_mismatches++;
            }
        } else {
            fprintf(stderr, "(RDMA_READ_CHECK) Batch %d/%d: SKIPPING hash check due to %d WC errors\n",
                    j + 1, total_batches, batch_errors);
            hash_mismatches++;
        }

        free(wr);
        free(bad_wr);
        free(list);
    }

    // fprintf(stdout, "(RDMA_READ_CHECK) All operations completed: %d total reads, %d/%d batches passed integrity check\n",
    //         total, total_batches - hash_mismatches, total_batches);

    return hash_mismatches > 0 ? -1 : 0;
}

/* Consumer thread for rdma_read_consume_check.
 * Identical to rdma_read_consumer_thread except it waits on
 * used_size == 0 instead of produce == consume.  This is necessary
 * because after a full cycle the produce pointer wraps back to exactly
 * equal the consume pointer, making the produce==consume test
 * ambiguous (empty vs. full circle), which deadlocks the original
 * consumer.  used_size is never ambiguous. */
void *
rdma_read_consume_check_consumer_thread(void *arg)
{
    char *devnull;
    unsigned long chunk_size, work_size, work_start_offset, work_end_offset;
    unsigned long new_mem_offset_circular;
    long int timestamp_ns;
    unsigned local_worker_id;

    struct rdma_thread_param *thread_args = (struct rdma_thread_param *)arg;

    pthread_mutex_lock(&(thread_args->cond_lock));
    local_worker_id = thread_args->worker_id++;
    pthread_mutex_unlock(&(thread_args->cond_lock));

    devnull = (char *)malloc(thread_args->message_count * thread_args->message_size);
    bzero(devnull, thread_args->message_count * thread_args->message_size);

    while (1) {
        timestamp_ns = get_current_timestamp_ns() - thread_args->start_ts;

        pthread_mutex_lock(&(thread_args->cond_lock));
        /* Wait on used_size == 0, NOT on produce == consume.
         * With a circular buffer whose size equals exactly one cycle's
         * worth of data, produce wraps to == consume after every cycle
         * even when the buffer is NOT empty. used_size has no such
         * ambiguity. */
        while (thread_args->used_size == 0 && thread_args->got_data != READY) {
            pthread_cond_wait(&(thread_args->start_work), &(thread_args->cond_lock));
        }

        if (thread_args->got_data == READY && thread_args->used_size == 0) {
            pthread_mutex_unlock(&(thread_args->cond_lock));
            break;
        }

        chunk_size = thread_args->received_size_fifo[thread_args->head];
        new_mem_offset_circular = (thread_args->mem_offset_consume + chunk_size) % thread_args->buffer_size;
        pthread_mutex_unlock(&(thread_args->cond_lock));

        work_size = chunk_size / thread_args->worker_count;
        work_start_offset = local_worker_id * work_size;
        work_end_offset = (local_worker_id + 1) * work_size;
        if (local_worker_id == thread_args->worker_count - 1) {
            if (work_end_offset < chunk_size) {
                work_end_offset = chunk_size;
                work_size = work_end_offset - work_start_offset;
            }
        }

        memcpy(devnull, (*thread_args->rdma_ctx->buf) + thread_args->mem_offset_consume + work_start_offset, work_size);

        pthread_barrier_wait(&(thread_args->workers_done_barrier));

        if (local_worker_id == 0) {
            unsigned long used_size;

            thread_args->head++;
            if (thread_args->head >= RECEIVED_FIFO_SIZE) {
                thread_args->head = 0;
            }

            pthread_mutex_lock(&(thread_args->cond_lock));
            thread_args->mem_offset_consume = new_mem_offset_circular;
            thread_args->used_size -= chunk_size;
            if (thread_args->mem_offset_produce >= thread_args->mem_offset_consume) {
                used_size = thread_args->mem_offset_produce - thread_args->mem_offset_consume;
            } else {
                used_size = thread_args->buffer_size + thread_args->mem_offset_produce - thread_args->mem_offset_consume;
            }
            pthread_mutex_unlock(&(thread_args->cond_lock));

            // printf("(RDMA_READ_CONSUME_CHECK_CONSUMER) timestamp=%ld ms, used_size=%lu bytes (%.1f%%)\n",
            //        (timestamp_ns / 1000000), used_size, (100.0 * used_size / thread_args->buffer_size));
        }
    }

    free(devnull);
    // fprintf(stdout, "(RDMA_READ_CONSUME_CHECK_CONSUMER) Worker %u finished\n", local_worker_id);
    return NULL;
}

/* Producer thread for rdma_read_consume_check:
 * Combines the circular-buffer / backpressure streaming loop from
 * rdma_read_producer_thread with per-batch CRC32 integrity checking
 * from rdma_read_method_check.  Runs indefinitely. */
void *
rdma_read_consume_check_producer_thread(void *arg)
{
    struct rdma_thread_param *thread_args = (struct rdma_thread_param *)arg;
    struct ibv_send_wr *wr, **bad_wr;
    struct ibv_sge *list;
    struct rdma_context *ctx = thread_args->rdma_ctx;
    struct rdma_endpoint *remote_endpoint = thread_args->remote_endpoint;

    int i, j, l, total, full_queue_count, remainder_queue_size, done, last, left, ne;
    int batch_size, batch_errors, total_batches;
    struct ibv_wc wc[RDMA_MAX_SEND_WR];
    uint32_t remote_hash, local_hash;
    unsigned long cycle = 0;
    int paused = 0;
    unsigned long used_size;
    unsigned long total_reads = thread_args->message_count;

    /* Bandwidth-measurement timing (mirrors the RDMA write client_thread).
     * t1 prints one sample per chunk (cycle); s1 prints while stalled on
     * backpressure. chunk_size is the bytes moved per cycle. */
    long int timestamp_ns, timestamp_ms;
    long int timestamp_ns_thread_cpu_start, timestamp_ns_thread_cpu_now;
    unsigned long chunk_size = total_reads * thread_args->message_size;

    full_queue_count  = total_reads / RDMA_MAX_SEND_WR;
    remainder_queue_size = total_reads % RDMA_MAX_SEND_WR;
    total_batches = full_queue_count + (remainder_queue_size > 0 ? 1 : 0);
    total = 0;

    // fprintf(stdout, "(RDMA_READ_CONSUME_CHECK) Starting stream+check mode: %lu messages, "
    //         "%d batches per cycle\n", total_reads, total_batches);

    while (1) {
        // fprintf(stdout, "(RDMA_READ_CONSUME_CHECK) Starting cycle %lu\n", cycle);

        /* Per-chunk timing start: wall-clock timestamp (since start_ts) and
         * thread-CPU baseline. CPU time naturally excludes any usleep() spent
         * waiting on backpressure, matching the write-side measurement. */
        timestamp_ns = get_current_timestamp_ns() - thread_args->start_ts;
        timestamp_ms = timestamp_ns / 1E6;
        timestamp_ns_thread_cpu_start = get_current_timestamp_ns_thread_cpu();

        for (j = 0; j < total_batches; j++) {
            batch_size = (j < full_queue_count) ? RDMA_MAX_SEND_WR : remainder_queue_size;

            /* ---- Backpressure check ---- */
            pthread_mutex_lock(&(thread_args->cond_lock));
            used_size = thread_args->used_size;
            while (used_size >= (thread_args->buffer_size * thread_args->backpressure_threshold_up / 100)) {
                if (!paused) {
                    // fprintf(stdout, "(RDMA_READ_CONSUME_CHECK) Backpressure activated: "
                    //         "used=%lu, buffer=%lu, threshold=%u%%\n",
                    //         used_size, thread_args->buffer_size,
                    //         thread_args->backpressure_threshold_up);
                    thread_args->backpressure = 1;
                    paused = 1;
                }
                pthread_mutex_unlock(&(thread_args->cond_lock));
                /* Backpressure stall sample (mirrors RDMA write s1 print) */
                timestamp_ns = get_current_timestamp_ns() - thread_args->start_ts;
                timestamp_ms = timestamp_ns / 1E6;
                printf("s1:%d:%ld:%ld\n", thread_args->client_id, timestamp_ns, timestamp_ms);
                usleep(10000);
                pthread_mutex_lock(&(thread_args->cond_lock));
                used_size = thread_args->used_size;
                if (used_size < (thread_args->buffer_size * thread_args->backpressure_threshold_down / 100)) {
                    // fprintf(stdout, "(RDMA_READ_CONSUME_CHECK) Backpressure released: "
                    //         "used=%lu, buffer=%lu, threshold=%u%%\n",
                    //         used_size, thread_args->buffer_size,
                    //         thread_args->backpressure_threshold_down);
                    thread_args->backpressure = 0;
                    paused = 0;
                    break;
                }
            }
            pthread_mutex_unlock(&(thread_args->cond_lock));

            /* ---- Step 1: request CRC32 hash from sender over TCP ---- */
            {
                unsigned long batch_offset   = thread_args->mem_offset +
                                               (unsigned long)j * RDMA_MAX_SEND_WR *
                                               thread_args->message_size;
                unsigned long batch_data_len = (unsigned long)batch_size *
                                               thread_args->message_size;
                char hash_req[128];

                snprintf(hash_req, sizeof(hash_req), "HASH:%d:%lu:%lu",
                         j, batch_offset, batch_data_len);
                write(thread_args->control_socket, hash_req, sizeof(hash_req));

                memset(&remote_hash, 0, sizeof(remote_hash));
                read(thread_args->control_socket, &remote_hash, sizeof(remote_hash));

                debug_print("(RDMA_READ_CONSUME_CHECK) Cycle %lu, Batch %d: "
                            "remote CRC32=0x%08x\n", cycle, j, remote_hash);
            }

            /* Snapshot circular-buffer produce pointer before posting this batch */
            unsigned long batch_buf_start;
            pthread_mutex_lock(&(thread_args->cond_lock));
            batch_buf_start = thread_args->mem_offset_produce;
            pthread_mutex_unlock(&(thread_args->cond_lock));

            /* ---- Step 2: post RDMA READ work requests into circular buffer ---- */
            wr     = (struct ibv_send_wr  *)malloc(batch_size * sizeof(struct ibv_send_wr));
            bad_wr = (struct ibv_send_wr **)malloc(batch_size * sizeof(struct ibv_send_wr *));
            list   = (struct ibv_sge      *)malloc(batch_size * sizeof(struct ibv_sge));

            done = 0;
            last = 0;
            batch_errors = 0;
            while (!done) {
                for (i = last; i < batch_size; i++) {
                    unsigned long read_idx     = (unsigned long)j * RDMA_MAX_SEND_WR + i;
                    unsigned long buffer_offset = (batch_buf_start +
                                                   (unsigned long)i * thread_args->message_size) %
                                                  thread_args->buffer_size;

                    *(bad_wr + i) = NULL;
                    memset(wr   + i, 0, sizeof(struct ibv_send_wr));
                    memset(list + i, 0, sizeof(struct ibv_sge));

                    (wr + i)->wr_id     = read_idx;
                    (wr + i)->next      = NULL;
                    (wr + i)->opcode    = IBV_WR_RDMA_READ;
                    (wr + i)->sg_list   = list + i;
                    (wr + i)->num_sge   = 1;
                    (wr + i)->send_flags = IBV_SEND_SIGNALED;

                    (wr + i)->wr.rdma.remote_addr = remote_endpoint->addr +
                                                    thread_args->mem_offset +
                                                    read_idx * thread_args->message_size;
                    (wr + i)->wr.rdma.rkey = remote_endpoint->rkey;

                    (list + i)->length = thread_args->message_size;
                    (list + i)->addr   = (uint64_t)(*ctx->buf + buffer_offset);
                    (list + i)->lkey   = (*ctx->mr)->lkey;

                    if (ibv_post_send(*ctx->qp, wr + i, bad_wr + i)) {
                        fprintf(stderr, "(RDMA_READ_CONSUME_CHECK) Couldn't post read #%d\n", i);
                        break;
                    } else {
                        total++;
                    }
                }

                done = (i >= batch_size) ? 1 : 0;
                if (!done) last = i;

            /* ---- Step 3: poll completions (no consumer signaling yet) ---- */
                left = i;
                do {
                    ne = ibv_poll_cq(*ctx->cq, left, wc);
                    if (ne < 0) {
                        fprintf(stderr, "(RDMA_READ_CONSUME_CHECK) poll CQ failed %d\n", ne);
                    } else {
                        for (l = 0; l < ne; l++) {
                            if ((wc + l)->status != IBV_WC_SUCCESS) {
                                fprintf(stderr, "(RDMA_READ_CONSUME_CHECK) Read failed: "
                                        "%s (%d) for wr_id %d\n",
                                        ibv_wc_status_str((wc + l)->status),
                                        (wc + l)->status, (int)((wc + l)->wr_id));
                                batch_errors++;
                            } else {
                                debug_print("(RDMA_READ_CONSUME_CHECK) Read completed "
                                            "for wr_id %d\n", (int)((wc + l)->wr_id));
                            }
                        }
                        left -= ne;
                    }
                } while (left > 0);
            }

            /* ---- Step 4: verify CRC32 (handles circular buffer wrap-around) ---- */
            {
                unsigned long batch_data_len = (unsigned long)batch_size *
                                               thread_args->message_size;
                if (batch_errors == 0) {
                    local_hash = crc32_of_circular(*ctx->buf, batch_buf_start,
                                                   batch_data_len,
                                                   thread_args->buffer_size);
                    if (local_hash == remote_hash) {
                        // fprintf(stdout, "(RDMA_READ_CONSUME_CHECK) Cycle %lu, Batch %d: "
                        //         "CRC32 OK (0x%08x), %d messages verified\n",
                        //         cycle, j, local_hash, batch_size);
                    } else {
                        fprintf(stderr, "(RDMA_READ_CONSUME_CHECK) Cycle %lu, Batch %d: "
                                "CRC32 MISMATCH! remote=0x%08x local=0x%08x\n",
                                cycle, j, remote_hash, local_hash);
                    }
                } else {
                    fprintf(stderr, "(RDMA_READ_CONSUME_CHECK) Cycle %lu, Batch %d: "
                            "SKIPPING hash check due to %d WC errors\n",
                            cycle, j, batch_errors);
                }
            }

            /* ---- Step 5: now that CRC is verified, update circular buffer
             * tracking and signal the consumer.  Doing this AFTER the CRC
             * check guarantees the data region is stable during hashing.
             * The consumer uses used_size == 0 as its wait condition (not
             * produce == consume), so the wrap-around ambiguity cannot
             * cause a deadlock here. ---- */
            pthread_mutex_lock(&(thread_args->cond_lock));
            for (i = 0; i < batch_size; i++) {
                thread_args->received_size_fifo[thread_args->tail] = thread_args->message_size;
                thread_args->tail++;
                if (thread_args->tail >= RECEIVED_FIFO_SIZE)
                    thread_args->tail = 0;
                thread_args->mem_offset_produce = (thread_args->mem_offset_produce +
                                                   thread_args->message_size) %
                                                  thread_args->buffer_size;
                thread_args->used_size += thread_args->message_size;
                pthread_cond_signal(&(thread_args->start_work));
            }
            pthread_mutex_unlock(&(thread_args->cond_lock));

            free(wr);
            free(bad_wr);
            free(list);
        }

        /* Per-chunk bandwidth sample (mirrors RDMA write t1 print):
         *   t1:client_id:wall_ns:cpu_delta_ns:chunk_size
         * wall_ns    - wall-clock ns since start_ts (captured at cycle start)
         * cpu_delta  - thread CPU ns spent issuing/polling this cycle's reads
         * chunk_size - bytes pulled this cycle (message_count * message_size).
         * Bandwidth = chunk_size / (delta of wall_ns between consecutive t1). */
        timestamp_ns_thread_cpu_now = get_current_timestamp_ns_thread_cpu();
        debug_print("t1:%d:%ld\n", thread_args->client_id, timestamp_ns);
        fprintf(stdout, "t1:%d:%ld:%ld:%lu\n", thread_args->client_id, timestamp_ns,
               timestamp_ns_thread_cpu_now - timestamp_ns_thread_cpu_start, chunk_size);

        // fprintf(stdout, "(RDMA_READ_CONSUME_CHECK) Cycle %lu completed\n", cycle);
        cycle++;
        /* No artificial inter-cycle throttle: the producer is rate-limited by
         * the real RDMA READ completions and by backpressure when the consumer
         * falls behind. Sleeping here would cap measured bandwidth at
         * chunk_size / sleep, defeating the throughput measurement. */
        /* usleep(100000); */
    }

    /* Unreachable in continuous mode */
    return NULL;
}

int
rdma_read_consume_check(int control_socket,
                        unsigned int backpressure_threshold_up,
                        unsigned int backpressure_threshold_down,
                        struct rdma_context *ctx,
                        struct rdma_endpoint **remote_endpoint,
                        unsigned long *message_count,
                        unsigned long *message_size,
                        unsigned long *buffer_size,
                        unsigned long *mem_offset,
                        unsigned worker_count)
{
    pthread_t producer_thread;
    pthread_t *worker_threads;
    struct rdma_thread_param *thread_args;

    worker_threads = (pthread_t *)malloc(worker_count * sizeof(pthread_t));
    thread_args    = (struct rdma_thread_param *)malloc(sizeof(struct rdma_thread_param));

    thread_args->rdma_ctx        = ctx;
    thread_args->remote_endpoint = *remote_endpoint;
    thread_args->message_count   = *message_count;
    thread_args->message_size    = *message_size;
    thread_args->buffer_size     = *buffer_size;
    thread_args->mem_offset      = *mem_offset;
    thread_args->mem_offset_produce = 0;
    thread_args->mem_offset_consume = 0;
    thread_args->used_size       = 0;
    thread_args->received_size   = 0;

    thread_args->control_socket           = control_socket;
    thread_args->backpressure             = 0;
    thread_args->backpressure_threshold_up   = backpressure_threshold_up;
    thread_args->backpressure_threshold_down = backpressure_threshold_down;

    thread_args->start_ts    = get_current_timestamp_ns();
    thread_args->worker_count = worker_count;
    thread_args->worker_id   = 0;
    thread_args->got_data    = 0;
    thread_args->client_id   = 0;

    bzero(thread_args->received_size_fifo, RECEIVED_FIFO_SIZE * sizeof(unsigned int));
    thread_args->fifo_size = RECEIVED_FIFO_SIZE;
    thread_args->head = 0;
    thread_args->tail = 0;

    pthread_mutex_init(&(thread_args->cond_lock), NULL);
    pthread_cond_init(&(thread_args->start_work), NULL);
    pthread_barrier_init(&(thread_args->workers_done_barrier), NULL, worker_count);

    // fprintf(stdout, "(RDMA_READ_CONSUME_CHECK) Starting with %u worker threads\n",
    //         worker_count);
    // fprintf(stdout, "(RDMA_READ_CONSUME_CHECK) Buffer: %lu bytes, Messages: %lu x %lu bytes\n",
    //         *buffer_size, *message_count, *message_size);
    // fprintf(stdout, "(RDMA_READ_CONSUME_CHECK) Backpressure thresholds: up=%u%%, down=%u%%\n",
    //         backpressure_threshold_up, backpressure_threshold_down);

    /* Start consumer worker threads (use the check-specific consumer
     * which waits on used_size == 0 instead of produce == consume) */
    for (int i = 0; i < (int)worker_count; i++) {
        if (pthread_create(&(worker_threads[i]), NULL, rdma_read_consume_check_consumer_thread,
                           thread_args) != 0) {
            fprintf(stderr, "(RDMA_READ_CONSUME_CHECK) pthread_create() error - "
                    "worker_threads[%d]\n", i);
            return -1;
        }
    }

    /* Start stream+check producer thread */
    if (pthread_create(&producer_thread, NULL,
                       rdma_read_consume_check_producer_thread, thread_args) != 0) {
        fprintf(stderr, "(RDMA_READ_CONSUME_CHECK) pthread_create() error - "
                "producer_thread\n");
        return -1;
    }

    /* Join producer (runs forever in continuous mode) */
    if (pthread_join(producer_thread, NULL) != 0)
        fprintf(stderr, "(RDMA_READ_CONSUME_CHECK) pthread_join() error - producer\n");

    for (int i = 0; i < (int)worker_count; i++) {
        if (pthread_join(worker_threads[i], NULL) != 0)
            fprintf(stderr, "(RDMA_READ_CONSUME_CHECK) pthread_join() error - worker %d\n", i);
    }

    pthread_mutex_destroy(&(thread_args->cond_lock));
    pthread_cond_destroy(&(thread_args->start_work));
    pthread_barrier_destroy(&(thread_args->workers_done_barrier));

    free(worker_threads);
    free(thread_args);

    return 0;
}
