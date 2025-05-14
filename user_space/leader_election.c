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
    






    
}