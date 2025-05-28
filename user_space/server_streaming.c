#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <jpeglib.h>
#include <inttypes.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#define DEFAULT_PORT   12345
#define DEFAULT_QUALITY 100

typedef struct {
    AVFormatContext *fmt_ctx;
    AVCodecContext *codec_ctx;
    struct SwsContext *sws_ctx;
    int video_stream;
    int width;
    int height;
    double fps;
    AVFrame *frame;
    AVFrame *rgb_frame;
    AVPacket *packet;
    uint8_t *rgb_buf;
} VideoState;

// Utility: Get current time in ms
uint64_t get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Print usage/help
void usage(const char *prog) {
    printf("Usage: %s <video_file> [port jpeg_quality]\n", prog);
    printf("  video_file: path to video file to stream\n");
    printf("  port: TCP port to listen on (default %d)\n", DEFAULT_PORT);
    printf("  jpeg_quality: 1-100 (default %d)\n", DEFAULT_QUALITY);
    printf("Example: %s sample.mp4 12345 80\n", prog);
}

// Open video and prepare decoding/scaling
int open_video(const char *path, VideoState *vs) {
    vs->fmt_ctx = NULL;
    if (avformat_open_input(&vs->fmt_ctx, path, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open video file\n");
        return -1;
    }
    if (avformat_find_stream_info(vs->fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream info\n");
        avformat_close_input(&vs->fmt_ctx);
        return -1;
    }
    vs->video_stream = -1;
    for (unsigned i = 0; i < vs->fmt_ctx->nb_streams; i++) {
        if (vs->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vs->video_stream = i;
            break;
        }
    }
    if (vs->video_stream == -1) {
        fprintf(stderr, "No video stream found\n");
        avformat_close_input(&vs->fmt_ctx);
        return -1;
    }
    AVCodecParameters *codecpar = vs->fmt_ctx->streams[vs->video_stream]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Unsupported codec\n");
        avformat_close_input(&vs->fmt_ctx);
        return -1;
    }
    vs->codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(vs->codec_ctx, codecpar);
    if (avcodec_open2(vs->codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        avcodec_free_context(&vs->codec_ctx);
        avformat_close_input(&vs->fmt_ctx);
        return -1;
    }
    vs->width = vs->codec_ctx->width;
    vs->height = vs->codec_ctx->height;
    AVStream *stream = vs->fmt_ctx->streams[vs->video_stream];
    AVRational fr = stream->avg_frame_rate;
    vs->fps = av_q2d(fr);
    if (vs->fps < 1.0 || vs->fps > 240.0) vs->fps = 25.0; // fallback for bogus values
    // Prepare scaling to RGB24
    vs->sws_ctx = sws_getContext(
        vs->width, vs->height, vs->codec_ctx->pix_fmt,
        vs->width, vs->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL);
    int rgb_bufsize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, vs->width, vs->height, 1);
    vs->rgb_buf = av_malloc(rgb_bufsize);
    vs->frame = av_frame_alloc();
    vs->rgb_frame = av_frame_alloc();
    av_image_fill_arrays(vs->rgb_frame->data, vs->rgb_frame->linesize, vs->rgb_buf, AV_PIX_FMT_RGB24, vs->width, vs->height, 1);
    vs->packet = av_packet_alloc();
    return 0;
}

// Clean up all video resources
void close_video(VideoState *vs) {
    av_frame_free(&vs->frame);
    av_frame_free(&vs->rgb_frame);
    av_packet_free(&vs->packet);
    av_free(vs->rgb_buf);
    sws_freeContext(vs->sws_ctx);
    avcodec_free_context(&vs->codec_ctx);
    avformat_close_input(&vs->fmt_ctx);
}

// Read and decode the next frame, convert to RGB24
// Returns 0 on success, <0 on error or EOF (loops video)
int get_next_rgb_frame(VideoState *vs) {
    int ret;
    while ((ret = av_read_frame(vs->fmt_ctx, vs->packet)) >= 0) {
        if (vs->packet->stream_index == vs->video_stream) {
            if (avcodec_send_packet(vs->codec_ctx, vs->packet) == 0) {
                if (avcodec_receive_frame(vs->codec_ctx, vs->frame) == 0) {
                    sws_scale(vs->sws_ctx,
                              (const uint8_t * const*)vs->frame->data, vs->frame->linesize,
                              0, vs->height,
                              vs->rgb_frame->data, vs->rgb_frame->linesize);
                    av_packet_unref(vs->packet);
                    return 0;
                }
            }
        }
        av_packet_unref(vs->packet);
    }
    // End of file or error: loop video
    av_seek_frame(vs->fmt_ctx, vs->video_stream, 0, AVSEEK_FLAG_BACKWARD);
    return -1;
}

// Encode an RGB24 buffer to JPEG
int encode_jpeg(uint8_t *rgb, int width, int height, int quality, unsigned char **jpeg_buf, unsigned long *jpeg_size) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    jpeg_mem_dest(&cinfo, jpeg_buf, jpeg_size);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    JSAMPROW row_pointer[1];
    for (int y = 0; y < height; y++) {
        row_pointer[0] = rgb + y * width * 3;
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    return 0;
}

// Setup TCP server socket, return listening fd
int setup_tcp_server(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return -1; }
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sockfd); return -1;
    }
    if (listen(sockfd, 1) < 0) {
        perror("listen"); close(sockfd); return -1;
    }
    return sockfd;
}

// Send a frame (timestamp, size, JPEG) over the socket
int send_frame(int sockfd, uint64_t ts, unsigned char *jpeg_buf, uint32_t jpeg_size) {
    uint32_t sz_net = htonl(jpeg_size);
    ssize_t sent;
    if ((sent = send(sockfd, &ts, sizeof(ts), 0)) != (ssize_t)sizeof(ts)) return -1;
    if ((sent = send(sockfd, &sz_net, sizeof(sz_net), 0)) != (ssize_t)sizeof(sz_net)) return -1;
    if ((sent = send(sockfd, jpeg_buf, jpeg_size, 0)) != (ssize_t)jpeg_size) return -1;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }
    const char *video_path = argv[1];
    int port = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;
    int jpeg_quality = (argc > 3) ? atoi(argv[3]) : DEFAULT_QUALITY;
    if (port <= 0) port = DEFAULT_PORT;
    if (jpeg_quality < 1 || jpeg_quality > 100) jpeg_quality = DEFAULT_QUALITY;

    // --- Open video and print info before waiting for client ---
    VideoState vs;
    if (open_video(video_path, &vs) != 0) return 1;

    printf("Video info: width=%d height=%d fps=%.3f\n", vs.width, vs.height, vs.fps);
    printf("Use these values for your client:\n");
    printf("  ./client <server_ip> %d %d %d\n", port, vs.width, vs.height);

    // --- Setup TCP server and wait for client ---
    int sockfd = setup_tcp_server(port);
    if (sockfd < 0) { close_video(&vs); return 1; }
    printf("Waiting for client on port %d...\n", port);
    int clientfd = accept(sockfd, NULL, NULL);
    if (clientfd < 0) {
        perror("accept"); close(sockfd); close_video(&vs); return 1;
    }
    printf("Client connected!\n");

    FILE *logfile = fopen("server_stats.csv", "w");
    if (!logfile) {
        perror("fopen"); close(clientfd); close(sockfd); close_video(&vs); return 1;
    }
    fprintf(logfile, "timestamp,fps,avg_frame_size,bitrate_mbps,frame_drops,play_length_s\n");

    unsigned long frame_count = 0, byte_count = 0, drop_count = 0;
    time_t start_time = time(NULL), last_report = start_time;

    // --- Synchronize to video PTS for perfect playback timing ---
    int64_t start_pts = -1;
    uint64_t stream_start_time = 0;
    AVRational time_base = vs.fmt_ctx->streams[vs.video_stream]->time_base;

    while (1) {
        // --- Decode next frame and convert to RGB ---
        if (get_next_rgb_frame(&vs) != 0) continue; // loops video on EOF

        // --- Get frame PTS in ms ---
        int64_t pts = vs.frame->pts;
        if (pts == AV_NOPTS_VALUE) pts = 0;
        int64_t pts_ms = av_rescale_q(pts, time_base, (AVRational){1,1000});

        if (start_pts < 0) {
            start_pts = pts_ms;
            stream_start_time = get_time_ms();
        }

        uint64_t target_time = stream_start_time + (pts_ms - start_pts);
        uint64_t now = get_time_ms();
        if (target_time > now)
            usleep((useconds_t)((target_time - now) * 1000));

        // --- Encode as JPEG ---
        unsigned char *jpeg_buf = NULL;
        unsigned long jpeg_size = 0;
        if (encode_jpeg(vs.rgb_frame->data[0], vs.width, vs.height, jpeg_quality, &jpeg_buf, &jpeg_size) != 0) {
            fprintf(stderr, "JPEG encode failed\n");
            drop_count++;
            continue;
        }

        // --- Send to client ---
        uint64_t ts = get_time_ms();
        if (send_frame(clientfd, ts, jpeg_buf, (uint32_t)jpeg_size) != 0) {
            fprintf(stderr, "Send failed: %s\n", strerror(errno));
            free(jpeg_buf);
            drop_count++;
            break;
        }
        free(jpeg_buf);

        frame_count++;
        byte_count += jpeg_size;

        // --- Logging and stats ---
        time_t now_sec = time(NULL);
        if (now_sec != last_report) {
            double fps_val = (now_sec - last_report) ? (double)frame_count / (now_sec - last_report) : 0.0;
            double avg_size = (frame_count > 0) ? (double)byte_count / frame_count : 0.0;
            double mbps = (now_sec - last_report) ? (byte_count * 8.0) / (now_sec - last_report) / 1e6 : 0.0;
            double play_length = now_sec - start_time;
            fprintf(logfile, "%ld,%.2f,%.0f,%.2f,%lu,%.1f\n",
                    now_sec, fps_val, avg_size, mbps, drop_count, play_length);
            fflush(logfile);
            printf("[SERVER] FPS: %.2f | Avg Frame: %.0f bytes | Bandwidth: %.2f Mbps | Drops: %lu\n",
                   fps_val, avg_size, mbps, drop_count);
            frame_count = 0;
            byte_count = 0;
            last_report = now_sec;
        }
    }

    fclose(logfile);
    close(clientfd);
    close(sockfd);
    close_video(&vs);
    return 0;
}
