#include "host.h"
#include <assert.h>
#include "switch.h"

void handle_incoming_frames(Host* host) {
    // TODO: Suggested steps for handling incoming frames
    //    1) Dequeue the Frame from the host->incoming_frames_head
    //    2) Compute CRC of incoming frame to know whether it is corrupted
    //    3) If frame is corrupted, drop it and move on.
    //    4) Implement logic to check if the expected frame has come
    //    5) Implement logic to combine payload received from all frames belonging to a message
    //       and print the final message when all frames belonging to a message have been received.
    //    6) Implement the cumulative acknowledgement part of the sliding window protocol
    //    7) Append acknowledgement frames to the outgoing_frames_head queue
    int incoming_frames_length = ll_get_length(host->incoming_frames_head);
    while (incoming_frames_length > 0) {
        // Pop a node off the front of the link list and update the count
        LLnode* ll_inmsg_node = ll_pop_node(&host->incoming_frames_head);
        incoming_frames_length = ll_get_length(host->incoming_frames_head);

        Frame* inframe = ll_inmsg_node->value;
        uint8_t sender_id = inframe->src_id;

        // Drop corrupted frames
        char* frame_char = convert_frame_to_char(inframe);   // use actual helper name
        uint8_t crc = compute_crc8(frame_char);
        free(frame_char);
        if (crc != 0) {
            free(inframe);
            free(ll_inmsg_node);
            continue;
        }

        uint8_t expected = host->expected_seq_num[sender_id];
        int is_expected = (inframe->seq_num == expected);

        if (is_expected) {
            int payload_len;

            if (inframe->remaining_msg_bytes > 0) {
                payload_len = FRAME_PAYLOAD_SIZE;
            } else {
                payload_len = strlen(inframe->data);
            }

            // Start a new reassembly buffer if needed
            if (host->recv_message_buffer[sender_id] == NULL) {
                host->recv_message_capacity[sender_id] =
                    payload_len + inframe->remaining_msg_bytes + 1;
                host->recv_message_buffer[sender_id] =
                    calloc(host->recv_message_capacity[sender_id], sizeof(char));
                assert(host->recv_message_buffer[sender_id] != NULL);
                host->recv_message_offset[sender_id] = 0;
            }

            memcpy(host->recv_message_buffer[sender_id] +
                       host->recv_message_offset[sender_id],
                   inframe->data,
                   payload_len);

            host->recv_message_offset[sender_id] += payload_len;
            host->expected_seq_num[sender_id] =
                (uint8_t)((host->expected_seq_num[sender_id] + 1) % (MAX_SEQ_NUM + 1));

            // If this is the last frame of the message, print the full reassembled message
            if (inframe->remaining_msg_bytes == 0) {
                host->recv_message_buffer[sender_id][host->recv_message_offset[sender_id]] = '\0';
                printf("<RECV_%d>:[%s]\n", host->id, host->recv_message_buffer[sender_id]);

                free(host->recv_message_buffer[sender_id]);
                host->recv_message_buffer[sender_id] = NULL;
                host->recv_message_offset[sender_id] = 0;
                host->recv_message_capacity[sender_id] = 0;
            }
        }

        // Send cumulative ACK
        Frame* ack_frame = malloc(sizeof(Frame));
        assert(ack_frame != NULL);
        memset(ack_frame, 0, sizeof(Frame));

        ack_frame->src_id = host->id;
        ack_frame->dst_id = sender_id;

        if (is_expected) {
            ack_frame->seq_num = inframe->seq_num;
        } else {
            ack_frame->seq_num =
                (uint8_t)((host->expected_seq_num[sender_id] + MAX_SEQ_NUM) % (MAX_SEQ_NUM + 1));
        }

        ack_frame->crc_8 = 0;
        char* ack_char = convert_frame_to_char(ack_frame);   // use actual helper name
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