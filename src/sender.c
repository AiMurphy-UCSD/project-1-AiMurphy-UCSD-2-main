#include "host.h"
#include <assert.h>
#include "switch.h"

struct timeval* host_get_next_expiring_timeval(Host* host) {
    // TODO: You should fill in this function so that it returns the 
    // timeval when next timeout should occur
    // 
    // 1) Check your send_window for the timeouts of the frames. 
    // 2) Return the timeout of a single frame. 
    // HINT: It's not the frame with the furtherst/latest timeout. 
    struct timeval* earliest = NULL;

    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        struct timeval* t = host->send_window[i].timeout;
        if (t == NULL) {
            continue;
        }

        if (earliest == NULL || timeval_usecdiff(earliest, t) > 0) {
            earliest = t;
        }
    }

    return earliest;
}

void handle_incoming_acks(Host* host, struct timeval curr_timeval) {
    (void) curr_timeval;

    uint8_t num_acks_received[glb_num_hosts]; 
    memset(num_acks_received, 0, glb_num_hosts); 

    uint8_t num_dup_acks_for_this_rtt[glb_num_hosts];
    memset(num_dup_acks_for_this_rtt, 0, glb_num_hosts); 

    int incoming_frames_length = ll_get_length(host->incoming_frames_head);

    while (incoming_frames_length > 0) {
        LLnode* ll_inmsg_node = ll_pop_node(&host->incoming_frames_head);
        incoming_frames_length = ll_get_length(host->incoming_frames_head);

        Frame* ack_frame = ll_inmsg_node->value;

        char* ack_char = convert_frame_to_char(ack_frame);
        uint8_t crc = compute_crc8(ack_char);
        free(ack_char);

        if (crc != 0) {
            free(ack_frame);
            free(ll_inmsg_node);
            continue;
        }

        if (ack_frame->dst_id != host->id) {
            free(ack_frame);
            free(ll_inmsg_node);
            continue;
        }

        uint8_t ack_sender = ack_frame->src_id;
        uint8_t ack_seq = ack_frame->seq_num;
        num_acks_received[ack_sender]++;

        for (int i = 0; i < glb_sysconfig.window_size; i++) {
            Frame* win_frame = host->send_window[i].frame;
            if (win_frame == NULL) {
                continue;
            }

            if (win_frame->dst_id == ack_sender &&
                win_frame->src_id == host->id &&
                win_frame->seq_num == ack_seq) {

                if (host->send_window[i].acked == 1) {
                    num_dup_acks_for_this_rtt[ack_sender]++;
                } else {
                    host->send_window[i].acked = 1;
                    if (host->send_window[i].timeout != NULL) {
                        free(host->send_window[i].timeout);
                        host->send_window[i].timeout = NULL;
                    }
                }
                break;
            }
        }

        free(ack_frame);
        free(ll_inmsg_node);
    }

    /* Slide sender window only from the front */
    while (host->send_window[0].frame != NULL && host->send_window[0].acked == 1) {
        free(host->send_window[0].frame);

        for (int i = 0; i < glb_sysconfig.window_size - 1; i++) {
            host->send_window[i] = host->send_window[i + 1];
        }

        host->send_window[glb_sysconfig.window_size - 1].frame = NULL;
        host->send_window[glb_sysconfig.window_size - 1].timeout = NULL;
        host->send_window[glb_sysconfig.window_size - 1].acked = 0;
    }

    if (host->id == glb_sysconfig.host_send_cc_id) {
        fprintf(cc_diagnostics,"%d,%d,%d,",host->round_trip_num,
                num_acks_received[glb_sysconfig.host_recv_cc_id],
                num_dup_acks_for_this_rtt[glb_sysconfig.host_recv_cc_id]); 
    }
}

void handle_input_cmds(Host* host, struct timeval curr_timeval) {
    (void) curr_timeval;

    /* Only start a new command if sender is otherwise idle */
    if (ll_get_length(host->buffered_outframes_head) > 0) {
        return;
    }

    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].frame != NULL) {
            return;
        }
    }

    if (ll_get_length(host->input_cmdlist_head) == 0) {
        return;
    }

    LLnode* ll_input_cmd_node = ll_pop_node(&host->input_cmdlist_head);
    Cmd* outgoing_cmd = (Cmd*) ll_input_cmd_node->value;
    free(ll_input_cmd_node);

    char* msg = outgoing_cmd->message;
    int msg_length = strlen(msg);
    int bytes_sent = 0;

    while (bytes_sent < msg_length || (msg_length == 0 && bytes_sent == 0)) {
        Frame* outgoing_frame = malloc(sizeof(Frame));
        assert(outgoing_frame);
        memset(outgoing_frame, 0, sizeof(Frame));

        int chunk_size = FRAME_PAYLOAD_SIZE;
        if (msg_length - bytes_sent < FRAME_PAYLOAD_SIZE) {
            chunk_size = msg_length - bytes_sent;
        }

        if (msg_length == 0) {
            chunk_size = 0;
        }

        outgoing_frame->src_id = (uint8_t) outgoing_cmd->src_id;
        outgoing_frame->dst_id = (uint8_t) outgoing_cmd->dst_id;
        outgoing_frame->seq_num = host->next_seq_num;

        if (chunk_size > 0) {
            memcpy(outgoing_frame->data, msg + bytes_sent, chunk_size);
        }

        outgoing_frame->remaining_msg_bytes =
            (uint16_t)(msg_length - bytes_sent - chunk_size);

        outgoing_frame->crc_8 = 0;
        char* frame_char = convert_frame_to_char(outgoing_frame);
        outgoing_frame->crc_8 = compute_crc8(frame_char);
        free(frame_char);

        ll_append_node(&host->buffered_outframes_head, outgoing_frame);

        host->next_seq_num = (uint8_t)((host->next_seq_num + 1) % (MAX_SEQ_NUM + 1));
        bytes_sent += chunk_size;

        if (msg_length == 0) {
            break;
        }
    }

    free(outgoing_cmd->message);
    free(outgoing_cmd);
}

void handle_timedout_frames(Host* host, struct timeval curr_timeval) {
    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].frame != NULL &&
            host->send_window[i].acked == 0 &&
            host->send_window[i].timeout != NULL) {

            if (timeval_usecdiff(host->send_window[i].timeout, &curr_timeval) <= 0) {
                free(host->send_window[i].timeout);
                host->send_window[i].timeout = NULL;
            }
        }
    }
}

void handle_outgoing_frames(Host* host, struct timeval curr_timeval) {
    long additional_ts = 0; 

    if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
        memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    }

    /* Retransmit only timed-out, unacked frames */
    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].frame != NULL &&
            host->send_window[i].acked == 0 &&
            host->send_window[i].timeout == NULL) {

            Frame* resend_copy = malloc(sizeof(Frame));
            assert(resend_copy != NULL);
            memcpy(resend_copy, host->send_window[i].frame, sizeof(Frame));
            ll_append_node(&host->outgoing_frames_head, resend_copy);

            struct timeval* next_timeout = malloc(sizeof(struct timeval));
            assert(next_timeout);
            memcpy(next_timeout, &curr_timeval, sizeof(struct timeval));
            timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);

            host->send_window[i].timeout = next_timeout;
            additional_ts += 10000;
        }
    }

    /* Fill empty slots with new frames */
    for (int i = 0; i < glb_sysconfig.window_size &&
                    ll_get_length(host->buffered_outframes_head) > 0; i++) {
        if (host->send_window[i].frame == NULL) {
            LLnode* ll_outframe_node = ll_pop_node(&host->buffered_outframes_head);
            Frame* outgoing_frame = ll_outframe_node->value;

            host->send_window[i].frame = outgoing_frame;
            host->send_window[i].acked = 0;

            Frame* send_copy = malloc(sizeof(Frame));
            assert(send_copy != NULL);
            memcpy(send_copy, outgoing_frame, sizeof(Frame));
            ll_append_node(&host->outgoing_frames_head, send_copy);

            struct timeval* next_timeout = malloc(sizeof(struct timeval));
            assert(next_timeout);
            memcpy(next_timeout, &curr_timeval, sizeof(struct timeval));
            timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);

            host->send_window[i].timeout = next_timeout;
            additional_ts += 10000;

            free(ll_outframe_node);
        }
    }

    memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
    timeval_usecplus(host->latest_timeout, additional_ts);
}

// WE HIGHLY RECOMMEND TO NOT MODIFY THIS FUNCTION
void run_senders() {
    int sender_order[glb_num_hosts]; 
    get_rand_seq(glb_num_hosts, sender_order); 

    for (int i = 0; i < glb_num_hosts; i++) {
        int sender_id = sender_order[i]; 
        struct timeval curr_timeval;

        gettimeofday(&curr_timeval, NULL);

        Host* host = &glb_hosts_array[sender_id]; 

        // Check whether anything has arrived
        int input_cmd_length = ll_get_length(host->input_cmdlist_head);
        int inframe_queue_length = ll_get_length(host->incoming_frames_head);
        struct timeval* next_timeout = host_get_next_expiring_timeval(host); 
        
        // Conditions to "wake up" the host:
        //    1) Acknowledgement or new command
        //    2) Timeout      
        int incoming_frames_cmds = (input_cmd_length != 0) | (inframe_queue_length != 0); 
        long reached_timeout = (next_timeout != NULL) && (timeval_usecdiff(&curr_timeval, next_timeout) <= 0);

        host->awaiting_ack = 0; 
        host->active = 0; 
        host->csv_out = 0; 

        if (incoming_frames_cmds || reached_timeout) {
            host->round_trip_num += 1; 
            host->csv_out = 1; 
            
            // Implement this
            handle_input_cmds(host, curr_timeval); 
            // Implement this
            handle_incoming_acks(host, curr_timeval);
            // Implement this
            handle_timedout_frames(host, curr_timeval);
            // Implement this
            handle_outgoing_frames(host, curr_timeval); 
        }

        //Check if we are waiting for acks
        for (int j = 0; j < glb_sysconfig.window_size; j++) {
            if (host->send_window[j].frame != NULL) {
                host->awaiting_ack = 1; 
                break; 
            }
        }

        //Condition to indicate that the host is active 
        if (host->awaiting_ack || ll_get_length(host->buffered_outframes_head) > 0) {
            host->active = 1; 
        }
    }
}