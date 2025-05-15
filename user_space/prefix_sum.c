/*------------------------------------------------------------------------------
 *  Distributed prefix sum,
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "eba_user.h"
#define NODES 3
#define DEPOT 20
#define SIZE 100
#define REMAINDER SIZE%NODES

static void prefix_sum(int* buf, uint64_t buffer_id, uint64_t offset, uint64_t size)
{
    printf("[DEBUG] Entering prefix_sum with size: %lu\n", size);
    int temp[SIZE] = {0};
    eba_read(temp, buffer_id, offset, size * sizeof(int));
    printf("[DEBUG] Initial buffer contents: ");
    for (uint64_t i = 0; i < size; i++) {
        printf("%d ", temp[i]);
    }
    printf("\n");
    
    // Copy to output buffer and compute prefix sum
    memcpy(buf, temp, size * sizeof(int));
    for (uint64_t i = 1; i < size; i++) {
        buf[i] += buf[i-1];
    }
    
    printf("[DEBUG] Final prefix sum result: ");
    for (uint64_t i = 0; i < size; i++) {
        printf("%d ", buf[i]);
    }
    printf("\n");
    
    eba_write(buf, buffer_id, offset, size * sizeof(int));
}

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
    
    printf("[DEBUG] MAC address read: %s\n", mac_str);

    uint64_t mac = 0;
    unsigned int mac_bytes[6];
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x",
               &mac_bytes[0], &mac_bytes[1], &mac_bytes[2],
               &mac_bytes[3], &mac_bytes[4], &mac_bytes[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            mac = (mac << 8) | mac_bytes[i];
        }
        printf("[DEBUG] Parsed MAC address value: %lu\n", mac);
    } else {
        fprintf(stderr, "Failed to parse MAC address\n");
        exit(EXIT_FAILURE);
    }
    return mac;
}

int array[SIZE];

int main()
{
    uint64_t node_id = get_mac_address();
    printf("[DEBUG] Node MAC id: %lu\n", node_id);

    
    if( node_id == (uint64_t) 1 )/*leader*/
    {
        printf("[DEBUG] Node %lu is leader\n", node_id);
                // initialize array
        for (int i = 0; i < SIZE; i++)
        {
            array[i] = i;
        }
        printf("[DEBUG] Array initialized\n");
        /*Register the 'DEPOT' buffer */
        uint64_t depot_id = eba_alloc(SIZE*sizeof(int),0,0);
        printf("[DEBUG] Allocated depot buffer with id: %lu\n", depot_id);
        eba_register_service(depot_id, DEPOT);
        printf("[DEBUG] Registered service for depot id: %lu with service number: %d\n", depot_id, DEPOT);
        // Send parts of the array to remote nodes
        int* node1_ptr = array + SIZE/NODES + REMAINDER;

        size_t segment_bytes = (SIZE/NODES) * sizeof(int);
        eba_remote_write(EBA_SERVICE_PREFIX_SUM, 0, segment_bytes, (const char*)node1_ptr, 1);

        // Calculate the pointer and size for node 2's segment
        int* node2_ptr = array + (2*SIZE)/NODES + REMAINDER;
        eba_remote_write(EBA_SERVICE_PREFIX_SUM, 0, segment_bytes, (const char*)node2_ptr, 2);
        uint64_t local =eba_alloc((SIZE/NODES + REMAINDER)*sizeof(int),0,0);
        eba_write(array,local,0,(SIZE/NODES + REMAINDER)*sizeof(int));
        int result[SIZE] = {0};
        prefix_sum(result,local,0,SIZE/NODES + REMAINDER);
        

        printf("[DEBUG] Waiting for depot buffer for first segment\n");
        eba_wait_buffer(DEPOT, 5000);
        uint64_t depot_offset = (SIZE/NODES + REMAINDER) * sizeof(int); 
        eba_read(result+(SIZE/NODES + REMAINDER), DEPOT, depot_offset, (SIZE/NODES)*sizeof(int));

        printf("[DEBUG] Waiting for depot buffer for second segment\n");
        eba_wait_buffer(DEPOT, 5000);
        depot_offset = (2*(SIZE/NODES) + REMAINDER) * sizeof(int);
        eba_read(result+(2*(SIZE/NODES) + REMAINDER), DEPOT, depot_offset, (SIZE/NODES)*sizeof(int));

        // Now i need to actually add the "offset to the array"
        int offset = result[SIZE/NODES];
        for(int i = SIZE/NODES + REMAINDER ; i < 2*(SIZE/NODES)+REMAINDER;i++ )
        {   
            result[i]+= offset;
        }
        offset = result[2*(SIZE/NODES)];
        for(int i = 2*(SIZE/NODES)+REMAINDER ; i < SIZE;i++ )
        {   
            result[i]+= offset;
        }
        for(int i = 0 ; i<SIZE;i++)
        {
            printf("%d ",result[i]);
        }
        printf("\n");


        
    }
    else 
    {
        printf("[DEBUG] Node %lu is a worker node\n", node_id);
        uint64_t service_id = eba_alloc(SIZE*sizeof(int), 0, 0);
        eba_register_service(service_id, EBA_SERVICE_PREFIX_SUM);
        eba_wait_buffer(EBA_SERVICE_PREFIX_SUM, 0); 

        // Compute prefix sum on our segment
        int result[SIZE/NODES];
        prefix_sum(result,EBA_SERVICE_PREFIX_SUM,0,SIZE/NODES);
        

        uint64_t depot_offset = ((node_id -1) * (SIZE/NODES) + REMAINDER) * sizeof(int); 
        int iid = eba_remote_write(DEPOT, depot_offset, (SIZE/NODES)*sizeof(int), (const char*)result, 0);
        if(iid == 0)
            printf("erreur eba_remote_write\n");

    }

    printf("[DEBUG] Program finished\n");
    return 0;
}