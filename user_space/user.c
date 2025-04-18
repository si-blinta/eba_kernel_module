#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "eba_user.h"

#define MAX_LINE 512
#define MAX_TOKENS 16

/* Reads a line from stdin, returns NULL on EOF */
static char *read_line(char *buf, size_t sz) {
    if (!fgets(buf, sz, stdin))
        return NULL;
    size_t l = strlen(buf);
    if (l && buf[l-1]=='\n') buf[l-1]='\0';
    return buf;
}

/* Splits line into tokens (whitespace delimited), returns count */
static int tokenize(char *line, char *tokens[], int max) {
    int n = 0;
    char *p = strtok(line, " \t");
    while (p && n < max) {
        tokens[n++] = p;
        p = strtok(NULL, " \t");
    }
    return n;
}

int main(void) {
    char line[MAX_LINE];
    char *tok[MAX_TOKENS];
    int  ntoks;

    printf("Welcome to eba-shell! Type 'help' for commands, 'exit' to quit.\n");
    while (1) {
        printf("eba> ");
        if (!read_line(line, sizeof(line)))
            break;

        ntoks = tokenize(line, tok, MAX_TOKENS);
        if (ntoks == 0) 
            continue;

        if (strcmp(tok[0], "exit")==0 || strcmp(tok[0], "quit")==0) {
            break;
        }
        else if (strcmp(tok[0], "help")==0) {
            puts(
            "Commands:\n"
            "  alloc <size> <lifetime>\n"
            "  write <buf_id> <offset> <string>\n"
            "  read  <buf_id> <offset> <size>\n"
            "  remote_alloc <node_id> <local_id> <size> <lifetime>\n"
            "  remote_write <node_id> <buf_id> <offset> <string>\n"
            "  remote_read  <node_id> <dst_id> <src_id> <dst_off> <src_off> <size>\n"
            "  discover\n"
            "  export\n"
            "  get_node_infos\n"
            "  exit\n"
            );
        }
        else if (strcmp(tok[0], "alloc")==0 && ntoks==3) {
            uint64_t size     = strtoull(tok[1], NULL, 0);
            uint64_t lifetime = strtoull(tok[2], NULL, 0);
            printf("Allocating %llu @ lifetime %llu…\n",
                   (unsigned long long)size, (unsigned long long)lifetime);
            uint64_t bid = eba_alloc(size, lifetime, 0);
            printf("  -> buffer_id = %llu\n", (unsigned long long)bid);
        }
        else if (strcmp(tok[0], "write")==0 && ntoks>=4) {
            uint64_t buf_id = strtoull(tok[1], NULL, 0);
            uint64_t offset = strtoull(tok[2], NULL, 0);
            /* reconstruct string from tokens[3..] */
            char *str = tok[3];
            for (int i=4;i<ntoks;i++){
                strcat(str, " ");
                strcat(str, tok[i]);
            }
            size_t len = strlen(str);
            printf("Writing '%s' (%zu bytes) to buf %llu@%llu\n",
                   str, len,
                   (unsigned long long)buf_id,
                   (unsigned long long)offset);
            eba_write(str, buf_id, offset, len);
        }
        else if (strcmp(tok[0], "read")==0 && ntoks==4) {
            uint64_t buf_id = strtoull(tok[1], NULL, 0);
            uint64_t offset = strtoull(tok[2], NULL, 0);
            uint64_t size   = strtoull(tok[3], NULL, 0);
            char out[1024]  = {0};
            printf("Reading %llu bytes from buf %llu@%llu\n",(unsigned long long)size,(unsigned long long)buf_id,(unsigned long long)offset);
            eba_read(out,buf_id,offset,size);
            printf(" Hex:");
            for (uint64_t i=0;i<size && i<256;i++)
                printf(" %02x", (unsigned char)out[i]);
            printf("\n");
            printf(" Txt:\"%.*s\"\n", (int)size, out);
            uint64_t addr;
            memcpy(&addr,out,8);
            printf("Uint64_t: %lu\n",addr);
        }
        else if (strcmp(tok[0], "remote_alloc")==0 && ntoks==5) {
            uint16_t node_id = (uint16_t)strtoul(tok[1], NULL, 0);
            uint64_t local_id= strtoull(tok[2], NULL, 0);
            uint64_t size    = strtoull(tok[3], NULL, 0);
            uint64_t lifetime= strtoull(tok[4], NULL, 0);
            printf("Remote alloc on node %u: local_id=%llu size=%llu lifetime=%llu\n",
                   node_id,
                   (unsigned long long)local_id,
                   (unsigned long long)size,
                   (unsigned long long)lifetime);
            eba_remote_alloc(size,lifetime,local_id,node_id);
        }
        else if (strcmp(tok[0], "remote_write")==0 && ntoks>=5) {
            uint16_t node_id = (uint16_t)strtoul(tok[1], NULL, 0);
            uint64_t buf_id  = strtoull(tok[2], NULL, 0);
            uint64_t offset  = strtoull(tok[3], NULL, 0);
            char *str = tok[4];
            for (int i=5;i<ntoks;i++){
                strcat(str, " ");
                strcat(str, tok[i]);
            }
            size_t len = strlen(str);
            printf("Remote write to node %u buf %llu@%llu '%s' (%zu bytes)\n",
                   node_id,
                   (unsigned long long)buf_id,
                   (unsigned long long)offset,
                   str, len);
            eba_remote_write(buf_id,offset,len,str,node_id);
        }
        else if (strcmp(tok[0], "remote_read")==0 && ntoks==7) {
            uint16_t node_id = (uint16_t)strtoul(tok[1], NULL, 0);
            uint64_t dst_id  = strtoull(tok[2], NULL, 0);
            uint64_t src_id  = strtoull(tok[3], NULL, 0);
            uint64_t dst_off = strtoull(tok[4], NULL, 0);
            uint64_t src_off = strtoull(tok[5], NULL, 0);
            uint64_t size    = strtoull(tok[6], NULL, 0);
            printf("Remote read node %u src %llu@%llu → dst %llu@%llu size=%llu\n",
                   node_id,
                   (unsigned long long)src_id,
                   (unsigned long long)src_off,
                   (unsigned long long)dst_id,
                   (unsigned long long)dst_off,
                   (unsigned long long)size);
            eba_remote_read(dst_id,src_id,dst_off,src_off,size,node_id);
        }
        else if (strcmp(tok[0], "discover")==0) {
            eba_discover();
            puts("Sent discover broadcast.");
        }
        else if (strcmp(tok[0], "export")==0) {
            eba_export_node_specs();
            puts("Exported node specs.");
        }
        else if (strcmp(tok[0], "get_node_infos")==0) {
            struct eba_node_info infos[MAX_NODE_COUNT];
            uint64_t count = 0;
            if (eba_get_node_infos(infos, &count) < 0) {
                fprintf(stderr,"Failed to get node infos\n");
                continue;
            }
            printf("Known %llu node(s):\n", (unsigned long long)count);
            for (uint64_t i=0;i<count;i++) {
                unsigned char *m = infos[i].mac;
                printf("  NodeID=%u MTU=%u MAC=%02x:%02x:%02x:%02x:%02x:%02x specs=%llu\n",
                       infos[i].id, infos[i].mtu,
                       m[0],m[1],m[2],m[3],m[4],m[5],
                       (unsigned long long)infos[i].node_specs);
            }
        }
        else {
            printf("Unknown or malformed command. Type 'help' for list.\n");
        }
    }

    printf("Goodbye.\n");
    return 0;
}
