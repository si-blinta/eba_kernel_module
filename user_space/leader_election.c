/*------------------------------------------------------------------------------
    This can only work if we have the enqueue, dequeue and we need also to have asynchrounous functions !
    This version uses time outs, its not good !
    Todo : add the function: get_id by mac address, this way it could be implemented using acks .
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "eba_user.h"
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

    uint64_t get_smallest_mac(uint64_t *macs, int size)
    {   
        uint64_t smallest_mac = macs[0];
        for (int i = 1; i < size; i++) {
            if (macs[i] < smallest_mac) {
                smallest_mac = macs[i];
            }
        }
        return smallest_mac;
    }



#define EBA_SERVICE_LE 69
#define NEIGHBOURS 2

#define WAIT_TIMEOUT 3   // Wait 3 seconds for messages

int main()
{
    uint64_t le_id = eba_alloc(1024, 0, 0);
    if (le_id == 0) {
        fprintf(stderr, "Failed to allocate buffer\n");
        return EXIT_FAILURE;
    }
    if (eba_register_service(le_id, EBA_SERVICE_LE) < 0) {
        fprintf(stderr, "Failed to register service\n");
        return EXIT_FAILURE;
    }
    if (eba_register_queue(EBA_SERVICE_LE) < 0) {
        fprintf(stderr, "Failed to register queue\n");
        return EXIT_FAILURE;
    }

    uint64_t mac = get_mac_address();
    printf("My MAC address: %016lx\n", mac);
    printf("Tap to start the leader election\n");
    getchar();

    uint64_t leader_mac = 0;

    while (!leader_mac) {
        int received_msg = 0;
        // Allow enough space for candidates from all nodes.
        uint64_t candidate_macs[16];
        
        // First candidate is your own MAC.
        candidate_macs[received_msg++] = mac;

        printf("Broadcast Enqueuing MAC address: %016lx\n", mac);
        int ret = eba_remote_enqueue(EBA_SERVICE_LE, &mac, sizeof(mac), 0, 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to enqueue MAC address\n");
            return EXIT_FAILURE;
        }
        eba_wait_buffer(EBA_SERVICE_LE, 0);

        // Use timeout-based waiting.
        time_t start = time(NULL);
        while (time(NULL) - start < WAIT_TIMEOUT) {
            uint64_t neighbor_mac = 0;
            if (eba_dequeue(EBA_SERVICE_LE, &neighbor_mac, sizeof(neighbor_mac)) == 0) {
                candidate_macs[received_msg++] = neighbor_mac;
                printf("Received MAC address: %016lx\n", neighbor_mac);
            }
        }
        
        // Compute the smallest MAC among the candidates.
        if (received_msg > 0) {
            uint64_t smallest = candidate_macs[0];
            for (int i = 1; i < received_msg; i++) {
                if (candidate_macs[i] < smallest)
                    smallest = candidate_macs[i];
            }
            leader_mac = smallest;
        }
        
        if (!leader_mac) {
            printf("No leader computed, restarting election round...\n");
        }
    }

    printf("The leader is: %016lx\n", leader_mac);
    
    // Broadcast the leader MAC to all nodes.
    int ret_leader = eba_remote_enqueue(EBA_SERVICE_LE, &leader_mac, sizeof(leader_mac), 0, 0);
    if (ret_leader < 0) {
        fprintf(stderr, "Failed to enqueue leader MAC address\n");
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}