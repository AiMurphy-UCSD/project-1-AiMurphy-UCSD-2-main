#include "host.h"
#include <assert.h>
#include "switch.h"

void handle_incoming_frames(Host* host) {
    int incoming_frames_length = ll_get_length(host->incoming_frames_head);

    while (incoming_frames_length > 0) {
        LLnode* ll_inmsg_node = ll_pop_node(&host->incoming_frames_head);
        incoming_frames_length = ll_get_length(host->incoming_frames_head);

        Frame* inframe = ll_inmsg_node->value;
        uint8_t sender_id = inframe->src_id;

        char* frame_char = convert_frame_to_char(inframe);
        uint8_t crc = compute_crc8(frame_char);
        free(frame_char);

        if (crc != 0) {
            free(inframe);
            free(ll_inmsg_node);
            continue;
        }

        uint8_t expected = host->expected_seq_num[sender_id];
        int diff = seq_num_diff(expected, inframe->seq_num);
        int in_window = (diff >= 0 && diff < glb_sysconfig.window_size);

        if (in_window) {
            int slot = diff;

            if (!host->recv_window_present[sender_id][slot]) {
                Frame* stored = malloc(sizeof(Frame));
                assert(stored != NULL);
                memcpy(stored, inframe, sizeof(Frame));

                host->recv_window[sender_id][slot] = stored;
                host->recv_window_present[sender_id][slot] = 1;
            }

            while (host->recv_window_present[sender_id][0]) {
                Frame* deliver = host->recv_window[sender_id][0];
                int payload_len;

                if (deliver->remaining_msg_bytes > 0) {
                    payload_len = FRAME_PAYLOAD_SIZE;
                } else {
                    payload_len = strlen(deliver->data);
                }

                if (host->recv_message_buffer[sender_id] == NULL) {
                    host->recv_message_capacity[sender_id] =
                        payload_len + deliver->remaining_msg_bytes + 1;
                    host->recv_message_buffer[sender_id] =
                        calloc(host->recv_message_capacity[sender_id], sizeof(char));
                    assert(host->recv_message_buffer[sender_id] != NULL);
                    host->recv_message_offset[sender_id] = 0;
                }

                memcpy(host->recv_message_buffer[sender_id] +
                           host->recv_message_offset[sender_id],
                       deliver->data,
                       payload_len);

                host->recv_message_offset[sender_id] += payload_len;

                if (deliver->remaining_msg_bytes == 0) {
                    host->recv_message_buffer[sender_id][host->recv_message_offset[sender_id]] = '\0';
                    printf("<RECV_%d>:[%s]\n", host->id, host->recv_message_buffer[sender_id]);

                    free(host->recv_message_buffer[sender_id]);
                    host->recv_message_buffer[sender_id] = NULL;
                    host->recv_message_offset[sender_id] = 0;
                    host->recv_message_capacity[sender_id] = 0;
                }

                free(deliver);

                for (int j = 0; j < glb_sysconfig.window_size - 1; j++) {
                    host->recv_window[sender_id][j] = host->recv_window[sender_id][j + 1];
                    host->recv_window_present[sender_id][j] =
                        host->recv_window_present[sender_id][j + 1];
                }
                host->recv_window[sender_id][glb_sysconfig.window_size - 1] = NULL;
                host->recv_window_present[sender_id][glb_sysconfig.window_size - 1] = 0;

                host->expected_seq_num[sender_id] =
                    (uint8_t)((host->expected_seq_num[sender_id] + 1) % (MAX_SEQ_NUM + 1));
            }
        }

        /* Selective ACK: ACK the received frame's sequence number */
        Frame* ack_frame = malloc(sizeof(Frame));
        assert(ack_frame != NULL);
        memset(ack_frame, 0, sizeof(Frame));

        ack_frame->src_id = host->id;
        ack_frame->dst_id = sender_id;
        ack_frame->remaining_msg_bytes = 0;
        ack_frame->seq_num = inframe->seq_num;

        ack_frame->crc_8 = 0;
        char* ack_char = convert_frame_to_char(ack_frame);
        ack_frame->crc_8 = compute_crc8(ack_char);
        free(ack_char);

        ll_append_node(&host->outgoing_frames_head, ack_frame);

        free(inframe);
        free(ll_inmsg_node);
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