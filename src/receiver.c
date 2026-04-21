#include "host.h"
#include <assert.h>
#include "switch.h"


/* send a cumulative ACK back to the sender */
static void send_cumulative_ack(Host* host, uint8_t src_id, uint8_t ack_seq_num) {
    Frame* ack = calloc(1, sizeof(Frame));
    assert(ack);

    ack->remaining_msg_bytes = 0;
    ack->dst_id              = src_id;
    ack->src_id              = (uint8_t) host->id;
    ack->seq_num             = ack_seq_num;
    ack->frame_type          = 1;

    ack->crc8 = 0;
    char* buf = convert_frame_to_char(ack);
    ack->crc8 = compute_crc8(buf);
    free(buf);

    ll_append_node(&host->outgoing_frames_head, ack);
}
void handle_incoming_frames(Host* host) {
    int n = ll_get_length(host->incoming_frames_head);
    for (int iter = 0; iter < n; iter++) {
        LLnode* node  = ll_pop_node(&host->incoming_frames_head);
        Frame*  frame = (Frame*) node->value;
        free(node);

        /* drop corrupted frames */
        uint8_t saved_crc = frame->crc8;
        frame->crc8 = 0;
        char* buf = convert_frame_to_char(frame);
        uint8_t computed = compute_crc8(buf);
        free(buf);
        frame->crc8 = saved_crc;

        if (computed != saved_crc) {
            free(frame);
            continue;
        }

        /* ignore leftover ACK frames */
        if (frame->frame_type == 1) {
            free(frame);
            continue;
        }

        uint8_t src = frame->src_id;
        uint8_t seq = frame->seq_num;

        int diff = seq_num_diff(host->rcv_base[src], seq);

        /* duplicate: re-ACK so sender knows we already have it */
        if (diff < 0) {
            send_cumulative_ack(host, src, host->rcv_base[src]);
            free(frame);
            continue;
        }

        /* outside our window, drop it */
        if (diff >= glb_sysconfig.window_size) {
            free(frame);
            continue;
        }

        /* buffer it; skip if we already have this slot */
        int slot = seq % glb_sysconfig.window_size;
        if (host->rcv_window[src][slot] != NULL) {
            free(frame);
        } else {
            host->rcv_window[src][slot] = frame;
        }

        /* drain consecutive frames and build up the message */
        while (1) {
            int head_slot = host->rcv_base[src] % glb_sysconfig.window_size;
            Frame* f = host->rcv_window[src][head_slot];
            if (f == NULL) break;

            int chunk_len = (f->remaining_msg_bytes > 0) ? FRAME_PAYLOAD_SIZE : strlen(f->data);

            host->rcv_msg_buf[src] = realloc(host->rcv_msg_buf[src],
                                             host->rcv_msg_len[src] + chunk_len + 1);
            memcpy(host->rcv_msg_buf[src] + host->rcv_msg_len[src], f->data, chunk_len);
            host->rcv_msg_len[src] += chunk_len;
            host->rcv_msg_buf[src][host->rcv_msg_len[src]] = '\0';

            /* last frame: print full message and reset */
            if (f->remaining_msg_bytes == 0) {
                printf("<RECV_%d>:[%s]\n", host->id, host->rcv_msg_buf[src]);
                free(host->rcv_msg_buf[src]);
                host->rcv_msg_buf[src] = NULL;
                host->rcv_msg_len[src] = 0;
            }

            free(f);
            host->rcv_window[src][head_slot] = NULL;
            host->rcv_base[src]++;
        }

        send_cumulative_ack(host, src, host->rcv_base[src]);
    }
}

void run_receivers() {
    int recv_order[glb_num_hosts]; 
    get_rand_seq(glb_num_hosts, recv_order); 

    for (int i = 0; i < glb_num_hosts; i++) {
        int recv_id = recv_order[i]; 
        handle_incoming_frames(&glb_hosts_array[recv_id]); 
    }
}
