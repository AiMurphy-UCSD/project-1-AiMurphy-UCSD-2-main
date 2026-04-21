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
        if (host->send_window[i].frame != NULL &&
            host->send_window[i].timeout != NULL) {
            if (earliest == NULL) {
                earliest = host->send_window[i].timeout;
            } else if (timeval_usecdiff(host->send_window[i].timeout, earliest) > 0) {
                earliest = host->send_window[i].timeout;
            }
        }
    }
    return earliest;
}

void handle_incoming_acks(Host* host, struct timeval curr_timeval) {

    uint8_t num_acks_received[glb_num_hosts];
    memset(num_acks_received, 0, glb_num_hosts);

    uint8_t num_dup_acks_for_this_rtt[glb_num_hosts];   /* PA1b */
    memset(num_dup_acks_for_this_rtt, 0, glb_num_hosts);

    int n = ll_get_length(host->incoming_frames_head);
    for (int iter = 0; iter < n; iter++) {
        LLnode* node  = ll_pop_node(&host->incoming_frames_head);
        Frame*  frame = (Frame*) node->value;
        free(node);

        /* check CRC, drop if corrupted */
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

        if (frame->frame_type == 1 && frame->dst_id == (uint8_t) host->id) {
            uint8_t receiver_id = frame->src_id;
            uint8_t ack_num     = frame->seq_num;

            /* slide window forward for each newly ACKed frame */
            while (seq_num_diff(host->snd_base[receiver_id], ack_num) > 0) {
                int slot = host->snd_base[receiver_id] % glb_sysconfig.window_size;
                if (host->send_window[slot].frame != NULL) {
                    free(host->send_window[slot].frame);
                    host->send_window[slot].frame = NULL;
                }
                if (host->send_window[slot].timeout != NULL) {
                    free(host->send_window[slot].timeout);
                    host->send_window[slot].timeout = NULL;
                }
                host->snd_base[receiver_id]++;
                num_acks_received[receiver_id]++;
            }
            free(frame);
        } else {
            /* not an ACK for us, put it back for the receiver */
            ll_append_node(&host->incoming_frames_head, frame);
        }
    }

    if (host->id == glb_sysconfig.host_send_cc_id) {
        fprintf(cc_diagnostics, "%d,%d,%d,",
                host->round_trip_num,
                num_acks_received[glb_sysconfig.host_recv_cc_id],
                num_dup_acks_for_this_rtt[glb_sysconfig.host_recv_cc_id]);
    }
}

void handle_input_cmds(Host* host, struct timeval curr_timeval) {
    int input_cmd_length = ll_get_length(host->input_cmdlist_head);

    while (input_cmd_length > 0) {
        LLnode* ll_input_cmd_node = ll_pop_node(&host->input_cmdlist_head);
        input_cmd_length = ll_get_length(host->input_cmdlist_head);

        Cmd* outgoing_cmd = (Cmd*) ll_input_cmd_node->value;
        free(ll_input_cmd_node);

        /* strip trailing \r in case input came from a Windows file */
        int msg_length = strlen(outgoing_cmd->message);
        if (msg_length > 0 && outgoing_cmd->message[msg_length - 1] == '\r') {
            outgoing_cmd->message[--msg_length] = '\0';
        }

        char* msg_ptr  = outgoing_cmd->message;
        int   remaining = msg_length;

        /* edge case: empty message still needs one frame */
        if (remaining == 0) {
            Frame* f = calloc(1, sizeof(Frame));
            assert(f);
            f->src_id              = (uint8_t) outgoing_cmd->src_id;
            f->dst_id              = (uint8_t) outgoing_cmd->dst_id;
            f->remaining_msg_bytes = 0;
            f->frame_type          = 0;
            ll_append_node(&host->buffered_outframes_head, f);
        }

        while (remaining > 0) {
            Frame* f = calloc(1, sizeof(Frame));
            assert(f);

            int chunk = (remaining > FRAME_PAYLOAD_SIZE) ? FRAME_PAYLOAD_SIZE : remaining;
            memcpy(f->data, msg_ptr, chunk);
            msg_ptr  += chunk;
            remaining -= chunk;

            f->src_id              = (uint8_t) outgoing_cmd->src_id;
            f->dst_id              = (uint8_t) outgoing_cmd->dst_id;
            f->remaining_msg_bytes = (uint16_t) remaining;
            f->frame_type          = 0;
            /* seq_num and crc8 filled in later when frame is sent */

            ll_append_node(&host->buffered_outframes_head, f);
        }

        free(outgoing_cmd->message);
        free(outgoing_cmd);
    }
}

void handle_timedout_frames(Host* host, struct timeval curr_timeval) {
    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].frame  != NULL &&
            host->send_window[i].timeout != NULL) {
            if (timeval_usecdiff(&curr_timeval, host->send_window[i].timeout) <= 0) {
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

    /* pass 1: resend anything that timed out */
    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].frame  != NULL &&
            host->send_window[i].timeout == NULL) {

            Frame* copy = malloc(sizeof(Frame));
            assert(copy);
            memcpy(copy, host->send_window[i].frame, sizeof(Frame));
            ll_append_node(&host->outgoing_frames_head, copy);

            struct timeval* next_timeout = malloc(sizeof(struct timeval));
            memcpy(next_timeout, &curr_timeval, sizeof(struct timeval));
            timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
            additional_ts += 10000; /* 10 ms gap per frame */
            host->send_window[i].timeout = next_timeout;
        }
    }

    /* pass 2: send new frames as long as the window isn't full */
    while (ll_get_length(host->buffered_outframes_head) > 0) {
        Frame* peek = (Frame*) ll_peek_node(host->buffered_outframes_head);
        uint8_t dst = peek->dst_id;

        int in_flight = seq_num_diff(host->snd_base[dst], host->snd_next[dst]);
        if (in_flight < 0) { in_flight = 0; }
        if (in_flight >= glb_sysconfig.window_size) {
            break;
        }

        LLnode* ll_outframe_node = ll_pop_node(&host->buffered_outframes_head);
        Frame*  outgoing_frame   = (Frame*) ll_outframe_node->value;
        free(ll_outframe_node);

        /* assign seq num and compute CRC before sending */
        outgoing_frame->seq_num = host->snd_next[dst];
        host->snd_next[dst]++;

        outgoing_frame->crc8 = 0;
        char* buf = convert_frame_to_char(outgoing_frame);
        outgoing_frame->crc8 = compute_crc8(buf);
        free(buf);

        int slot = outgoing_frame->seq_num % glb_sysconfig.window_size;
        host->send_window[slot].frame = outgoing_frame;

        struct timeval* next_timeout = malloc(sizeof(struct timeval));
        memcpy(next_timeout, &curr_timeval, sizeof(struct timeval));
        timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
        additional_ts += 10000;
        host->send_window[slot].timeout = next_timeout;

        /* send a copy; keep the original in the window for retransmission */
        Frame* copy = malloc(sizeof(Frame));
        assert(copy);
        memcpy(copy, outgoing_frame, sizeof(Frame));
        ll_append_node(&host->outgoing_frames_head, copy);
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