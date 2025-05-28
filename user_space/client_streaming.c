#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <SDL2/SDL.h>
#include <jpeglib.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600
#define DEFAULT_PORT 12345
#define DEFAULT_SERVER "127.0.0.1"

// Get current time in ms
uint64_t get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Print usage/help
void usage(const char *prog) {
    printf("Usage: %s [server_ip port width height]\n", prog);
    printf("  server_ip: IP address of server (e.g. 192.168.1.100)\n");
    printf("  port: TCP port (e.g. 12345)\n");
    printf("  width/height: window size (e.g. 1280 720)\n");
    printf("Example: %s 192.168.1.100 12345 1280 720\n", prog);
}

// Initialize SDL window, renderer, and texture
int init_sdl(SDL_Window **win, SDL_Renderer **ren, SDL_Texture **tex, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    *win = SDL_CreateWindow("Screen Stream", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, 0);
    if (!*win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    *ren = SDL_CreateRenderer(*win, -1, 0);
    if (!*ren) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(*win); SDL_Quit();
        return -1;
    }
    *tex = SDL_CreateTexture(*ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!*tex) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(*ren); SDL_DestroyWindow(*win); SDL_Quit();
        return -1;
    }
    return 0;
}

// Receive exactly 'len' bytes from sockfd into buf
// Returns 0 on success, -1 on error or disconnect
int recv_all(int sockfd, void *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(sockfd, (char*)buf + got, len - got, 0);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

// Decode JPEG buffer and display using SDL
void show_frame(unsigned char *jpeg_buf, size_t jpeg_size, SDL_Renderer *renderer, SDL_Texture *texture, int width, int height) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpeg_buf, jpeg_size);
    if (jpeg_read_header(&cinfo, TRUE) != 1) {
        jpeg_destroy_decompress(&cinfo);
        return;
    }
    jpeg_start_decompress(&cinfo);

    unsigned char *rgb_buf = malloc(width * height * 3);
    if (!rgb_buf) {
        jpeg_destroy_decompress(&cinfo);
        return;
    }
    JSAMPROW row_pointer[1];
    while (cinfo.output_scanline < cinfo.output_height) {
        row_pointer[0] = &rgb_buf[cinfo.output_scanline * width * 3];
        jpeg_read_scanlines(&cinfo, row_pointer, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    SDL_UpdateTexture(texture, NULL, rgb_buf, width * 3);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    free(rgb_buf);
}

// Receive a frame (timestamp, size, JPEG) from the socket
// Returns 0 on success, -1 on error/disconnect
int receive_frame(int sockfd, uint64_t *server_ts, unsigned char **jpeg_buf, uint32_t *jpeg_size) {
    uint32_t sz_net;
    if (recv_all(sockfd, server_ts, sizeof(*server_ts)) != 0) return -1;
    if (recv_all(sockfd, &sz_net, sizeof(sz_net)) != 0) return -1;
    *jpeg_size = ntohl(sz_net);
    if (*jpeg_size == 0 || *jpeg_size > 10*1024*1024) return -1;
    *jpeg_buf = malloc(*jpeg_size);
    if (!*jpeg_buf) return -1;
    if (recv_all(sockfd, *jpeg_buf, *jpeg_size) != 0) {
        free(*jpeg_buf);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    char *server_ip = DEFAULT_SERVER;
    int port = DEFAULT_PORT, width = DEFAULT_WIDTH, height = DEFAULT_HEIGHT;

    // Parse arguments
    if (argc > 1) {
        if (argc < 5) { usage(argv[0]); return 1; }
        server_ip = argv[1];
        port = atoi(argv[2]);
        width = atoi(argv[3]);
        height = atoi(argv[4]);
        if (width <= 0 || height <= 0 || port <= 0) { usage(argv[0]); return 1; }
    }

    // TCP socket setup
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "ERROR: Invalid server IP address\n");
        close(sockfd); return 1;
    }
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    // SDL setup
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    SDL_Texture *tex = NULL;
    if (init_sdl(&win, &ren, &tex, width, height) != 0) {
        close(sockfd);
        return 1;
    }

    // Logging setup
    FILE *logfile = fopen("client_stats.csv", "w");
    if (!logfile) {
        perror("fopen");
        SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit(); close(sockfd); return 1;
    }
    fprintf(logfile, "timestamp,fps,avg_frame_size,bitrate_mbps,avg_latency_ms,startup_time_ms,frame_drops,play_length_s\n");

    unsigned long frame_count = 0, byte_count = 0, drop_count = 0;
    double total_latency = 0;
    uint64_t startup_time = 0;
    time_t start_time = time(NULL), last_report = start_time;
    int got_first = 0;

    // Main loop: receive, decode, display, log
    while (1) {
        uint64_t server_ts;
        unsigned char *jpeg_buf = NULL;
        uint32_t jpeg_size = 0;
        if (receive_frame(sockfd, &server_ts, &jpeg_buf, &jpeg_size) != 0) {
            drop_count++;
            break;
        }

        uint64_t now = get_time_ms();
        double latency = (double)(now - server_ts);
        total_latency += latency;
        if (!got_first) {
            startup_time = (uint64_t)latency;
            got_first = 1;
        }

        show_frame(jpeg_buf, jpeg_size, ren, tex, width, height);
        free(jpeg_buf);

        frame_count++;
        byte_count += jpeg_size;

        time_t tnow = time(NULL);
        if (tnow != last_report) {
            double fps_val = (tnow - last_report) ? (double)frame_count / (tnow - last_report) : 0.0;
            double avg_size = (frame_count > 0) ? (double)byte_count / frame_count : 0.0;
            double mbps = (tnow - last_report) ? (byte_count * 8.0) / (tnow - last_report) / 1e6 : 0.0;
            double avg_latency = (frame_count > 0) ? total_latency / frame_count : 0.0;
            double play_length = tnow - start_time;
            fprintf(logfile, "%ld,%.2f,%.0f,%.2f,%.1f,%" PRIu64 ",%lu,%.1f\n",
                    tnow, fps_val, avg_size, mbps, avg_latency, startup_time, drop_count, play_length);
            fflush(logfile);
            printf("[CLIENT] FPS: %.2f | Avg Frame: %.0f bytes | Bandwidth: %.2f Mbps | Avg Latency: %.1f ms | Drops: %lu\n",
                   fps_val, avg_size, mbps, avg_latency, drop_count);
            frame_count = 0;
            byte_count = 0;
            total_latency = 0;
            last_report = tnow;
        }

        SDL_Event e;
        if (SDL_PollEvent(&e) && e.type == SDL_QUIT) break;
    }

    fclose(logfile);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    close(sockfd);
    return 0;
}
