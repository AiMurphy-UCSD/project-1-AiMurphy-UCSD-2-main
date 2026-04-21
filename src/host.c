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

    
    /* Sender state: one entry per destination host */
    host->snd_base = calloc(glb_num_hosts, sizeof(uint8_t));
    host->snd_next = calloc(glb_num_hosts, sizeof(uint8_t));

    /* Receiver state: one entry per source host */
    host->rcv_base   = calloc(glb_num_hosts, sizeof(uint8_t));
    host->rcv_window = calloc(glb_num_hosts, sizeof(Frame **));
    host->rcv_msg_buf = calloc(glb_num_hosts, sizeof(char *));
    host->rcv_msg_len = calloc(glb_num_hosts, sizeof(int));
    for (int i = 0; i < glb_num_hosts; i++) {
        host->rcv_window[i] = calloc(glb_sysconfig.window_size, sizeof(Frame *));
        host->rcv_msg_buf[i] = NULL;
        host->rcv_msg_len[i] = 0;
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