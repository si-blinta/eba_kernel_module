#include <stdio.h>
#include "../include/eba_user.h"
#include <unistd.h>
int main(int argc, char *argv[])
 {
    uint64_t id1 = eba_alloc(512,10,0);
    const char mac[6]={0x00,0x00,0x00,0x00,0x00,0x02};
    eba_remote_alloc(512,100,id1,mac);
    uint64_t remote_id = 0;
    while(remote_id == 0)
    {
       sleep(1);
       eba_read(&remote_id,id1,0,8);
    }
    eba_remote_write(remote_id,0,sizeof("hello friend"),"hello friend",mac);
    uint64_t id2 = eba_alloc(512,10,0);
    eba_remote_read(id2,remote_id,0,0,sizeof("hello friend"),mac);
    char c[13] = {0};
    while(c[0] == 0)
    {
       sleep(1);
       eba_read(c,id2,0,13);
    }
    printf("read:\n");
    for(int i = 0 ; i < sizeof("hello friend"); i++)
      printf("%c",c[i]);
 }
 