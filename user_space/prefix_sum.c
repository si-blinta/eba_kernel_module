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
#define SIZE 999
static int prefix_sum(uint64_t buffer_id, uint64_t offset, uint64_t size)
{
    int x1 = 0;
    int x2 = 0;
    for (uint64_t i = 1; i < size; i++)
    {
        eba_read(&x1, buffer_id, offset + (i-1) * sizeof(int), sizeof(int));
        eba_read(&x2, buffer_id, offset + i * sizeof(int), sizeof(int));
        x2 += x1;
        eba_write(&x2, buffer_id, offset + i * sizeof(int), sizeof(int));
    }
    return 0;
}

enum INVOKE_STATUS {
    INVOKE_QUEUED = 0,    
    INVOKE_COMPLETED,      
    INVOKE_FAILED,
    INVOKE_DEFAULT     
};
static int remote_prefix_sum(uint64_t buffer_id, uint64_t offset, uint64_t size,int node_id)
{
    uint64_t x1_x2 = eba_alloc(sizeof(int)*2,0,0);
    int x1 = 0;
    int x2 = 0; 
    for (uint64_t i = 1; i < size; i++)
    {
        int iid;
        iid = eba_remote_read(x1_x2,buffer_id,0,offset + (i-1) * sizeof(int),sizeof(int),node_id);
        eba_wait_iid(iid,INVOKE_COMPLETED,1000);  
        iid = eba_remote_read(x1_x2,buffer_id,sizeof(int),offset + i * sizeof(int),sizeof(int),node_id);
        eba_wait_iid(iid,INVOKE_COMPLETED,1000);  
        
        eba_read(&x1, x1_x2, 0, sizeof(int));
        eba_read(&x2, x1_x2, sizeof(int), sizeof(int));
        x2 += x1;
        iid= eba_remote_write(buffer_id,offset+ i * sizeof(int),sizeof(int),(const char*)&x2,node_id);
        eba_wait_iid(iid,INVOKE_COMPLETED,1000);  
    }
    return 0;
}
int array[SIZE];
int main()
{
    
    for (int i = 0; i < SIZE; i++)
    {
        array[i] = i;
    }
    printf("original array:\n");
    for (int i = 0; i < SIZE; i++)
    {
        printf("%d ", array[i]);
    }   
    printf("\n");

    uint64_t buffer_id = eba_alloc(sizeof(int)*SIZE/3,0,0);
    eba_write(array, buffer_id, 0, sizeof(int)*SIZE/3);

    uint64_t node_1_buffer_id_holder = eba_alloc(8,0,0);
    uint64_t node_2_buffer_id_holder = eba_alloc(8,0,0);

    int iid = eba_remote_alloc(sizeof(int)*SIZE/3,0,node_1_buffer_id_holder,1);
    eba_wait_iid(iid,INVOKE_COMPLETED,1000);
    iid = eba_remote_alloc(sizeof(int)*SIZE/3,0,node_2_buffer_id_holder,2);
    eba_wait_iid(iid,INVOKE_COMPLETED,1000);

    uint64_t node_1_buffer_id;
    uint64_t node_2_buffer_id;
    eba_read(&node_1_buffer_id, node_1_buffer_id_holder, 0, 8);
    eba_read(&node_2_buffer_id, node_2_buffer_id_holder, 0, 8);
    printf("node_1_buffer_id: %llu\n", node_1_buffer_id);
    printf("node_2_buffer_id: %llu\n", node_2_buffer_id);
    

    iid = eba_remote_write(node_1_buffer_id,0, sizeof(int)*SIZE/3,(const char*)array+sizeof(int)*SIZE/3,1);
    printf("sent this array to node 1:\n");
    for (int i = 0; i < SIZE/3; i++)
    {
        printf("%d ", *((int*)((char*)array + sizeof(int) * (SIZE/3 + i))));
    }
    printf("\n");
    eba_wait_iid(iid,INVOKE_COMPLETED,1000);


    iid = eba_remote_write(node_2_buffer_id,0,sizeof(int)*SIZE/3,(const char*)array+2*sizeof(int)*SIZE/3,2);
    printf("sent this array to node 2:\n");
    for (int i = 0; i < SIZE/3; i++)
    {
        printf("%d ", *((int*)((char*)array + 2*sizeof(int)*SIZE/3 + sizeof(int) * i)));
    }
    printf("\n");
    eba_wait_iid(iid,INVOKE_COMPLETED,1000);
    remote_prefix_sum(node_1_buffer_id,0,SIZE/3,1);
    remote_prefix_sum(node_2_buffer_id,0,SIZE/3,2);
    prefix_sum( buffer_id,0,SIZE/3);


    uint64_t result_buf = eba_alloc(sizeof(int)*SIZE,0,0);
    int buf[SIZE/3];
    eba_read(buf, buffer_id, 0, sizeof(int)*SIZE/3);
    eba_write((const char*)buf,result_buf, 0, sizeof(int)*SIZE/3);
    iid = eba_remote_read(result_buf,node_1_buffer_id,sizeof(int)*SIZE/3,0,sizeof(int)*SIZE/3,1);
    eba_wait_iid(iid,INVOKE_COMPLETED,1000);
    iid = eba_remote_read(result_buf,node_2_buffer_id,2*sizeof(int)*SIZE/3,0,sizeof(int)*SIZE/3,2);
    eba_wait_iid(iid,INVOKE_COMPLETED,1000);

    /*Add the offsets to the result buff */
    int offset = 0;
    eba_read(&offset, result_buf, sizeof(int)*SIZE/3 - sizeof(int), sizeof(int));
    eba_read(buf, result_buf, sizeof(int)*SIZE/3,sizeof(int)*SIZE/3);
    for (int i = 0; i < SIZE/3; i++)
    {
        //debug 
        printf("offset: %d\n", offset);
        printf("buf[%d]: %d\n", i, buf[i]);
        buf[i] += offset;

    }
    eba_write((const char*)buf,result_buf, sizeof(int)*SIZE/3, SIZE/3*sizeof(int));
    eba_read(&offset, result_buf, 2*sizeof(int)*SIZE/3 - sizeof(int), sizeof(int));
    eba_read(buf, result_buf, 2*sizeof(int)*SIZE/3,sizeof(int)*SIZE/3);
    for (int i = 0; i < SIZE/3; i++)
    {
        //debug 
        printf("offset: %d\n", offset);
        printf("buf[%d]: %d\n", i, buf[i]);
        buf[i] += offset;

    }
    eba_write((const char*)buf,result_buf, 2*sizeof(int)*SIZE/3, SIZE/3*sizeof(int));

    printf("result array:\n");
    int result[SIZE];
    eba_read(result, result_buf, 0, sizeof(int)*SIZE);
    for (int i = 0; i < SIZE; i++)
    {
        printf("%d ", result[i]);
    }
    printf("\n");
    
}