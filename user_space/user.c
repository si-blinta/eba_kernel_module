#include <stdio.h>
#include "../include/eba_user.h"

int main(int argc, char *argv[])
 {
   uint64_t id = eba_alloc(512,10,0);
   eba_write("hello from user\0",id,0,sizeof("hello from user\0"));
   char buf[1024];
   eba_read(buf,id,0,sizeof("hello from user\0"));
   printf("%s\n",buf);
 }
 