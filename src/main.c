#include "common.h"
#include "input.h"
#include "util.h"
#include "run_main.h"

#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/********************************
* DO NOT CHANGE THE MAIN FUNCTION
*********************************/
int main(int argc, char* argv[]) {

    glb_exit_main = 0; 

    srand(time(NULL)); 

    // Prepare the glb_sysconfig object
    glb_sysconfig.window_size = 0; 
    glb_sysconfig.corrupt_prob = 0.00;
    glb_sysconfig.recv_accept_rate = 0; 
    glb_sysconfig.test_case_id = 0; 
    glb_sysconfig.host_send_cc_id = 0; 
    glb_sysconfig.host_recv_cc_id = 0; 
    strcpy(glb_sysconfig.config_path, "./test_suite/cc_basic.cfg"); 
    // Prepare other variables and seed the pseudo random number generator
    glb_num_hosts = -1; 

    // Parse out command line arguments 
    parse_args(argc, argv);

    // Read from the config file
    run_config();  
    
    // Initialize hosts, ports, and IO reader
    init(); 

    //Initialize testing fields
    init_test_setup(); 

    // Create the path for csv output
    struct stat st = {0};
    if (stat("./csv_files", &st) == -1) {
        mkdir("./csv_files", 0700);
    }    
   
    char cc_datafile_dir_name[1000]; 
    sprintf(cc_datafile_dir_name, "./csv_files/cc_test%d", glb_sysconfig.test_case_id); 

    if (stat(cc_datafile_dir_name, &st) == -1) {
        mkdir(cc_datafile_dir_name, 0700);
    } 

    char cc_diagnostics_name[1500]; 
    sprintf(cc_diagnostics_name, "%s/s%d_r%d_diagnostics.csv", cc_datafile_dir_name, glb_sysconfig.host_send_cc_id, glb_sysconfig.host_recv_cc_id); 

    cc_diagnostics = fopen(cc_diagnostics_name, "w+"); 
    fprintf(cc_diagnostics, "rtt,acks_received,dup_acks,state,cwnd,ssthresh,frames_sent,frames_dropped,frames_in_sender_window,timedout_frames\n"); 

    // Run the program until you get the exit signal
    while (1) {
        if (!glb_exit_main) {
            handle_input(); 
        }
        run_hosts(); 
        
        fflush(stdout); 

        graceful_exit(); 
    }

    return 0;
}