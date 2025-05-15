/*------------------------------------------------------------------------------
    This can only work if we have the enqueue, dequeue and we need also to have asynchrounous functions !
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
#define EBA_SERVICE_LE 69


int main()
{
    
    uint64_t le_id = eba_alloc(1024, 0, 0);
    if (le_id == 0) {
        fprintf(stderr, "Failed to allocate buffer\n");
        return EXIT_FAILURE;
    }
    if(eba_register_service(le_id,EBA_SERVICE_LE) <0){
        fprintf(stderr, "Failed to register service\n");
        return EXIT_FAILURE;
    }
    if(eba_register_queue(EBA_SERVICE_LE) <0){
        fprintf(stderr, "Failed to register queue\n");
        return EXIT_FAILURE;
    }
    uint64_t mac = get_mac_address();
    /*Broadcast enqueue the mac address */
    printf("tap to start the leader election\n");
    getchar();
    printf("Broadcast Enqueuing MAC address: %016lx\n", mac);
    int iid = eba_remote_enqueue(EBA_SERVICE_LE, &mac, sizeof(mac), 0);
    if (iid < 0) {
        fprintf(stderr, "Failed to enqueue MAC address\n");
        return EXIT_FAILURE;
    }
    eba_wait_buffer(EBA_SERVICE_LE, 0);
    uint64_t neighbor_mac[2];
    eba_dequeue(EBA_SERVICE_LE, &neighbor_mac[0], sizeof(neighbor_mac[0]));
    printf("Received MAC address: %016lx\n", neighbor_mac[0]);
    if(neighbor_mac[0] > mac){
        printf("I am not the leader\n");
        return EXIT_SUCCESS;
    }
    printf("I am the leader\n");










    
}