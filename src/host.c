#include "host.h"
#include "sender.h"
#include "receiver.h"
#include "switch.h"
#include <assert.h>

void init_host(Host* host, int id) {
    host->id = id;
    host->active = 0; 
    host->awaiting_ack = 0; 
    host->round_trip_num = 0; 
    host->csv_out = 0; 
    
    host->input_cmdlist_head = NULL;
    host->incoming_frames_head = NULL; 
    host->buffered_outframes_head = NULL; 
    host->outgoing_frames_head = NULL; 
    host->send_window = calloc(glb_sysconfig.window_size, sizeof(struct send_window_slot)); 
    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        host->send_window[i].frame = NULL;
        host->send_window[i].timeout = NULL;
    }
    host->latest_timeout = malloc(sizeof(struct timeval));
    gettimeofday(host->latest_timeout, NULL);

    // TODO: You should fill in this function as necessary to initialize variables
    host->next_seq_num = 0;

    host->expected_seq_num = calloc(glb_num_hosts, sizeof(uint8_t));
    assert(host->expected_seq_num);

    host->recv_message_buffer = calloc(glb_num_hosts, sizeof(char*));
    assert(host->recv_message_buffer);

    host->recv_message_offset = calloc(glb_num_hosts, sizeof(uint16_t));
    assert(host->recv_message_offset);

    host->recv_message_capacity = calloc(glb_num_hosts, sizeof(uint16_t));
    assert(host->recv_message_capacity);

    for (int i = 0; i < glb_num_hosts; i++) {
        host->recv_message_capacity[i] = 0;
    }

    for (int i = 0; i < glb_num_hosts; i++) {
        host->expected_seq_num[i] = 0;
        host->recv_message_buffer[i] = NULL;
        host->recv_message_offset[i] = 0;
    }

    // *********** PA1b ONLY ***********
    host->cc = calloc(glb_num_hosts, sizeof(CongestionControl));
    for (int i = 0; i < glb_num_hosts; i++) {
        host->cc[i].cwnd = 1.0; 
        host->cc[i].ssthresh = (double)glb_sysconfig.window_size; 
        host->cc[i].dup_acks = 0; 
        host->cc[i].state = cc_SS; 
    }
}

void run_hosts() {
    run_senders(); 
    send_data_frames(); 
    run_receivers(); 
    send_ack_frames(); 
}