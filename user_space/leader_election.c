/*------------------------------------------------------------------------------
 *  Distributed prefix sum, this is the master node, its waaay to slow because i need to add the offsets on this node 
 Also there is no prefix sum operation on remote nodes so i end up sending a lot of invocations .
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "eba_user.h"
enum INVOKE_STATUS {
    INVOKE_QUEUED = 0,    
    INVOKE_COMPLETED,      
    INVOKE_FAILED,
    INVOKE_DEFAULT     
};

uint64_t get_mac_address() {
    const char *interface = "enp0s8";
    char path[256], mac_str[18];
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", interface);
    FILE *fp = fopen(path, "r");
    if (!fp || !fgets(mac_str, sizeof(mac_str), fp)) {
        perror("Error reading MAC address");
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    uint64_t mac = 0;
    unsigned int mac_bytes[6];
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x",
               &mac_bytes[0], &mac_bytes[1], &mac_bytes[2],
               &mac_bytes[3], &mac_bytes[4], &mac_bytes[5]) == 6) {
        for (int i = 0; i < 6; i++) mac = (mac << 8) | mac_bytes[i];
    } else {
        fprintf(stderr, "Failed to parse MAC address\n");
        exit(EXIT_FAILURE);
    }
    return mac;
}



int main()
{
    
    /* i want to do a leader election, we have 3 nodes, it means that each node will receive 2 mac addresseses, the problem is :
    each node will wait to receive the mac addresses on the same buffer, nodes can overwrite other nodes buffers, whats the solution ?
    imagine: node A and node B sends their mac addresses to node C, node C will only receive the last one because the buffer is overwritten.
    the solution is to use a buffer for each node, so node A will send its mac address to node C and node B will send its mac address to node C, then node C will have 2 buffers, one for each node.
    but this is bad, i want to have only one buffer, whats the solution ?
    
    */





    
}