/*Simple distributed reduction 
Given an array of integers, the program computes the sum of all elements in the array.
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "eba_user.h"
#define EBA_SERVICE_REDUCTION 66
#define DEPOT 50
#define ARRAY_SIZE 1024
#define NODES 3
int init_array(int *array, int size)
{
    for (int i = 0; i < size; i++) {
        array[i] = rand() % 100; // Random values between 0 and 99
    }
    return 0;
}

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
int local_sum(int *array, int size) {
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum += array[i];
    }
    return sum;
}

int main()
{
    eba_discover();
    if(get_mac_address() == (uint64_t )0x1) {
        
        int array[ARRAY_SIZE];
        init_array(array, ARRAY_SIZE);
        printf("Array initialized: ");
        for (int i = 0; i < ARRAY_SIZE; i++) {
            printf("%d ", array[i]);
        }
        printf("\n");
        /*Allocate a queue in which the nodes will enqueue their sums */
        uint64_t depot_id = eba_alloc((NODES-1)*sizeof(int), 0, 0);
        if (depot_id == 0) {
            fprintf(stderr, "Failed to allocate depot buffer\n");
            return EXIT_FAILURE;
        }
        eba_register_service(depot_id, DEPOT);
        /*Register the depot as a queue */
        eba_register_queue(DEPOT);
        int remainder = ARRAY_SIZE % NODES;
        eba_remote_write(EBA_SERVICE_REDUCTION, 0, sizeof(array)/NODES, (const char*)(array+remainder+ARRAY_SIZE/NODES), 1, 0);
        eba_remote_write(EBA_SERVICE_REDUCTION, 0, sizeof(array)/NODES, (const char*)(array+remainder+2*ARRAY_SIZE/NODES), 2, 0);
        int sum_count_received = 0;
        eba_wait_buffer(DEPOT, 0);
        int sum = 0;
        int tmp = 0;
        
        while( eba_dequeue(DEPOT, &tmp, sizeof(int)) == 0) {
            sum += tmp;
            printf("Received sum: %d\n", tmp);
            sum_count_received++;
        }
        if(sum_count_received != NODES-1) {
            eba_wait_buffer(DEPOT, 0);
            eba_dequeue(DEPOT, &tmp, sizeof(int));
            sum += tmp;
            printf("Received sum: %d\n", tmp);
        }
        sum += local_sum(array, ARRAY_SIZE/NODES+remainder);
        printf("Final sum: %d\n", sum);
        
        printf("Sanity check: ");
        printf("Sum of all elements: %d\n", local_sum(array, ARRAY_SIZE));
    } 
    
    else {
        
        uint64_t reduction_id = eba_alloc(sizeof(int)*ARRAY_SIZE/NODES, 0, 0);
        if (reduction_id == 0) {
            fprintf(stderr, "Failed to allocate buffer\n");
            return EXIT_FAILURE;
        }
        if (eba_register_service(reduction_id, EBA_SERVICE_REDUCTION) < 0) {
            fprintf(stderr, "Failed to register service\n");
            return EXIT_FAILURE;
        }
        int chunk[ARRAY_SIZE/NODES];
        eba_wait_buffer(EBA_SERVICE_REDUCTION, 0);
        eba_read(chunk, EBA_SERVICE_REDUCTION, 0, sizeof(chunk));
        for (int i = 0; i < ARRAY_SIZE/NODES; i++) {
            printf("%d ", chunk[i]);
        }
        printf("\n");
        int sum = local_sum(chunk, sizeof(chunk)/sizeof(int));
        printf("Local sum: %d\n", sum);
        eba_remote_enqueue(DEPOT, &sum, sizeof(int), 0, 0);


        
    }


    


    








    return EXIT_SUCCESS;
}