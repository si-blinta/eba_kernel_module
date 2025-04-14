#include <stdio.h>
#include "../include/eba_user.h"

int main(int argc, char *argv[])
 {
    uint64_t id = eba_alloc(512,10,0);
    const char mac[6]={0x00,0x00,0x00,0x00,0x00,0x02};
    eba_remote_alloc(512,100,id,mac);
    /*uint64_t remote_id = 0;
    while(remote_id == 0)
    {
       eba_read(&remote_id,id,0,8);
    }
    eba_remote_write(remote_id,0,sizeof("hello friend"),"hello friend",mac);*/
 }
 