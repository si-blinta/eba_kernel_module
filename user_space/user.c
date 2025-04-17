#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include "../include/eba_user.h"
char mac[6] = {0x00,0x00,0x00,0x00,0x00,0x02};
//if node 2
//char mac[6] = {0x00,0x00,0x00,0x00,0x00,0x01};
int main(void)
{
   char command[32];

   printf("Enter command (alloc, write, read, remote_alloc, remote_write, remote_read, discover, export ): ");
   if (scanf("%31s", command) != 1)
   {
      fprintf(stderr, "Error reading command.\n");
      return 1;
   }

   if (strcmp(command, "alloc") == 0)
   {
      // "alloc" takes no arguments, just print a message.
      uint64_t size = 0;
      printf("Enter size: ");
      if (scanf("%" SCNu64, &size) != 1)
      {
         fprintf(stderr, "Error reading 64-bit unsigned integer.\n");
         return 1;
      }
      uint64_t lifetime = 0;
      printf("Enter lifetime: ");
      if (scanf("%" SCNu64, &lifetime) != 1)
      {
         fprintf(stderr, "Error reading 64-bit unsigned integer.\n");
         return 1;
      }
      printf("Parameters read: size = %" PRIu64 ", lifetime = %" PRIu64 "\n", size, lifetime);
      uint64_t local_id = eba_alloc(size, lifetime, 0);
      printf("Returned buffer id = %" PRIu64 "\n", local_id);
   }
   else if (strcmp(command, "write") == 0)
   {
      uint64_t local_id, offset;
      char userString[256];
      printf("Enter local_id and offset: ");
      if (scanf("%" SCNu64 " %" SCNu64, &local_id, &offset) != 2)
      {
         fprintf(stderr, "Error reading write numeric parameters.\n");
         return 1;
      }
      
      printf("Enter string: ");
      if (scanf(" %[^\n]", userString) != 1)
      {
         fprintf(stderr, "Error reading string.\n");
         return 1;
      }
      size_t len = strlen(userString);
      printf("Parameters read: local_id = %" PRIu64 ", offset = %" PRIu64 ", string = \"%s\", length = %zu\n",  local_id, offset, userString, len);
      eba_write(userString, local_id, offset, len);
   }
   else if (strcmp(command, "read") == 0)
   {
      uint64_t local_id, offset,size;
      printf("Enter local_id and offset and size: ");
      if (scanf("%" SCNu64 " %" SCNu64 " %" SCNu64, &local_id, &offset,&size) != 3)
      {
         fprintf(stderr, "Error reading remote_write numeric parameters.\n");
         return 1;
      }
      char read[256];
      printf("Parameters read: local_id = %" PRIu64 ", offset = %" PRIu64 ", size = %" PRIu64 "\n", local_id, offset, size);
      eba_read(read,local_id,offset,size);
      
      printf("Read (hex dump): ");
      for (size_t i = 0; i < size; i++) {
         printf("%02x ", (unsigned char)read[i]);
      }
      printf("\n");
      printf("Read (as chars): ");
      for (size_t i = 0; i < size; i++) {
         printf("%c",read[i]);
      }
      printf("\n");
      if(size == 8) {
         uint64_t value;
         memcpy(&value, read, sizeof(uint64_t));
         printf("Read (uint64_t): %" PRIu64 "\n", value);
      }   
      printf("\n");
   }
   else if (strcmp(command, "remote_alloc") == 0)
   {
      uint64_t id, size, life_time;
      printf("Enter local id, size and life_time (each as a 64-bit unsigned integer): ");
      if (scanf("%" SCNu64 " %" SCNu64 " %" SCNu64, &id, &size, &life_time) != 3)
      {
         fprintf(stderr, "Error reading remote_alloc parameters.\n");
         return 1;
      }
      printf("Parameters read: local_id = %" PRIu64 ", size = %" PRIu64 ", life_time = %" PRIu64 "\n",id, size, life_time);
      eba_remote_alloc(size,life_time,id,mac);
   }
   else if (strcmp(command, "remote_write") == 0)
   {
      uint64_t remote_id, offset;
      char userString[256];
      printf("Enter remote_id and offset (each as a 64-bit unsigned integer): ");
      if (scanf("%" SCNu64 " %" SCNu64, &remote_id, &offset) != 2)
      {
         fprintf(stderr, "Error reading remote_write numeric parameters.\n");
         return 1;
      }
      printf("Enter string: ");
      if (scanf(" %[^\n]", userString) != 1)
      {
         fprintf(stderr, "Error reading remote_write string.\n");
         return 1;
      }
      size_t len = strlen(userString);
      printf("Parameters read: remote_id = %" PRIu64 ", offset = %" PRIu64 ", string = \"%s\", length = %zu\n",remote_id, offset, userString, len);
      eba_remote_write(remote_id,offset,len,userString,mac);
         
   }
   else if (strcmp(command, "remote_read") == 0)
   {
      uint64_t local_id, remote_id, local_offset, remote_offset, size;
      printf("Enter local_id, remote_id, local_offset, remote_offset and size (each as a 64-bit unsigned integer): ");
      if (scanf("%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                &local_id, &remote_id, &local_offset, &remote_offset, &size) != 5)
      {
         fprintf(stderr, "Error reading remote_read parameters.\n");
         return 1;
      }
      printf("Parameters read: local_id = %" PRIu64 ", remote_id = %" PRIu64 ", local_offset = %" PRIu64 ", remote_offset = %" PRIu64 ", size = %" PRIu64 "\n",local_id, remote_id, local_offset, remote_offset, size);
      eba_remote_read(local_id,remote_id,local_offset,remote_offset,size,mac);
   }
   else if (strcmp(command, "discover") == 0)
   {  
      eba_discover();
      printf("Broadcasted discover message\n");
   }
   else if (strcmp(command, "export") == 0)
   {  
      eba_export_node_specs();
      printf("Exported node specs\n");
   }
   else
   {
      printf("Unknown command.\n");
   }

   return 0;
}
