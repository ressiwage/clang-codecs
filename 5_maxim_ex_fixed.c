/**
 * Extract DC coefficients from H.264 video using FFmpeg/libav
 * 
 * Build with:
 * gcc -o extract_dc extract_dc.c -lavcodec -lavformat -lavutil
 * 
 * Note: Requires FFmpeg built with --enable-debug --disable-stripping
 */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <stdio.h>

// Include internal H.264 header for accessing coefficient data
#include <libavcodec/h264.h>
#include <libavcodec/h264_ps.h>  // For H264Context definition
#include <libavcodec/internal.h>  // For internal FFmpeg structures
#include <libavcodec/h264dec.h>   // For H.264 specific decoding structures
//
static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL;
static AVStream *video_stream = NULL;
static const char *src_filename = NULL;
static int video_stream_idx = -1;
static AVFrame *frame = NULL;
static int video_frame_count = 0;

static const int dequant_coef[6][6] = {
    {10, 13, 16, 18, 23, 25},
    {11, 14, 18, 21, 25, 28},
    {13, 16, 20, 23, 29, 31},
    {14, 18, 23, 25, 31, 35},
    {16, 20, 25, 29, 35, 39},
    {18, 23, 29, 31, 39, 44}
};

static int get_dc_scale_factor(int QP) {
    if (QP < 0 || QP > 51) { // H.264 QP range check
        fprintf(stderr, "Invalid QP value: %d\n", QP);
        return 0;
    }
    int qp_div6 = QP / 6;
    int qp_mod6 = QP % 6;
    return dequant_coef[qp_mod6][qp_div6];
}

// Helper function to check if reference samples are available
static int are_ref_samples_available(int mb_x, int mb_y, int mb_width, int mb_height) {
    // Check frame boundaries
    if (mb_x == 0 || mb_y == 0 || mb_x >= mb_width || mb_y >= mb_height) {
        return 0;
    }
    return 1;
}

// Helper function to get predicted DC value
static int get_predicted_dc(const AVFrame *frame, int mb_x, int mb_y, 
                          int pred_mode, int mb_width, int mb_height) {
    int pred_dc = 128; // Default value for unavailable samples
    
    if (!frame || !frame->data[0]) {
        return pred_dc;
    }

    // Check if reference samples are available
    if (are_ref_samples_available(mb_x, mb_y, mb_width, mb_height)) {
        switch (pred_mode) {
            case 0: // Vertical
                pred_dc = frame->data[0][(mb_y * 16 - 1) * frame->linesize[0] + mb_x * 16];
                break;
            case 1: // Horizontal
                pred_dc = frame->data[0][mb_y * 16 * frame->linesize[0] + mb_x * 16 - 1];
                break;
            case 2: // DC
    
                {
                    
                    int sum = 0;
                    int available_samples = 0;
                    // Sum available top and left samples
                    if (mb_y > 0) {
                        for (int i = 0; i < 16; i++) {
                            sum += frame->data[0][(mb_y * 16 - 1) * frame->linesize[0] + mb_x * 16 + i];
                            available_samples++;
                        }
                    }
                    if (mb_x > 0) {
                        for (int i = 0; i < 16; i++) {
                            sum += frame->data[0][(mb_y * 16 + i) * frame->linesize[0] + mb_x * 16 - 1];
                            available_samples++;
                        }
                    }
                    pred_dc = available_samples > 0 ? (sum + (available_samples >> 1)) / available_samples : 128;
                }
                break;
            case 3: // Plane
                // Implement full plane prediction if needed
                pred_dc = 128;
                break;
        }
    }
    
    return pred_dc;
}


static uint8_t *dc_frame_buffer = NULL;
static int dc_frame_width = 0;
static int dc_frame_height = 0;

static int init_dc_frame_buffer(int width, int height) {
    dc_frame_width = (width + 15) >> 4;  // Number of macroblocks horizontally
    dc_frame_height = (height + 15) >> 4; // Number of macroblocks vertically
    
    // Allocate buffer for DC values
    dc_frame_buffer = (uint8_t*)av_malloc(dc_frame_width * dc_frame_height);
    if (!dc_frame_buffer) {
        fprintf(stderr, "Could not allocate DC frame buffer\n");
        return -1;
    }
    
    return 0;
}

// Function to save DC frame as PGM (grayscale image)
static void save_dc_frame(int frame_number) {
    char filename[1024];
    snprintf(filename, sizeof(filename), "temp/dc_frame_%d.pgm", frame_number);
    
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s for writing\n", filename);
        return;
    }
    
    // Write PGM header
    fprintf(f, "P5\n%d %d\n255\n", dc_frame_width, dc_frame_height);
    
    // Write pixel data
    fwrite(dc_frame_buffer, 1, dc_frame_width * dc_frame_height, f);
    
    fclose(f);
}


static int decode_packet(const AVPacket *pkt) {
    int ret = avcodec_send_packet(video_dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending packet to decoder: %s\n", av_err2str(ret));
        return ret;
    }
    while (ret >= 0) {
        ret = avcodec_receive_frame(video_dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            fprintf(stderr, "Error receiving frame from decoder: %s\n", av_err2str(ret));
            return ret;
        }

                if (ret >= 0) {
            H264Context *h = (H264Context *)video_dec_ctx->priv_data;
            H264SliceContext *sl = &h->slice_ctx[0];
            
            int mb_width = (video_dec_ctx->width + 15) >> 4;
            int mb_height = (video_dec_ctx->height + 15) >> 4;
            
            // Initialize DC frame buffer if not done yet
            if (!dc_frame_buffer) {
                if (init_dc_frame_buffer(video_dec_ctx->width, video_dec_ctx->height) < 0) {
                    return -1;
                }
            }

            for (int mb_y = 0; mb_y < mb_height; mb_y++) {
                for (int mb_x = 0; mb_x < mb_width; mb_x++) {
                    int final_dc = 0;
                    if (sl->intra16x16_pred_mode >= 0) {
                        // Intra 16x16 mode handling
                        int dc_residual = sl->mb_luma_dc[0][0];
                        int qp = sl->qscale;
                        int scale_factor = get_dc_scale_factor(qp);
                        
                        if (scale_factor == 0) continue;
                        
                        int dc_scaled = dc_residual * scale_factor;
                        int pred_dc = get_predicted_dc(frame, mb_x, mb_y, 
                                                     sl->intra16x16_pred_mode,
                                                     mb_width, mb_height);
                        
                        final_dc = pred_dc + dc_scaled;
                    } else {
                        // Intra 4x4 mode handling
                        int sum_dc = 0;
                        int valid_blocks = 0;
                        
                        for (int block = 0; block < 16; block++) {
                            int dc_residual = sl->mb[block * 16];
                            int qp = sl->qscale;
                            int scale_factor = get_dc_scale_factor(qp);
                            
                            if (scale_factor > 0) {
                                sum_dc += dc_residual * scale_factor;
                                valid_blocks++;
                            }
                        }
                        
                        if (valid_blocks > 0) {
                            final_dc = sum_dc / valid_blocks;
                        }
                    }

                    // Clamp value to valid range
                    final_dc = av_clip_uint8(final_dc);
                    
                    // Store in our DC frame buffer
                    dc_frame_buffer[mb_y * dc_frame_width + mb_x] = final_dc;
                }
            }
            
            // Save the DC frame
            save_dc_frame(video_frame_count);
            
            video_frame_count++;
            av_frame_unref(frame);
        }
    }
    return 0;
}


static int open_codec_context(AVFormatContext *fmt_ctx, enum AVMediaType type) {
    int ret;
    int stream_idx;
    AVStream *st;
    const AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    stream_idx = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (stream_idx < 0) {
        fprintf(stderr, "Could not find %s stream\n", av_get_media_type_string(type));
        return stream_idx;
    }

    printf("%d %d\n", stream_idx, fmt_ctx->streams[stream_idx]->codecpar->codec_id);
    st = fmt_ctx->streams[stream_idx];
    dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        fprintf(stderr, "Failed to find codec\n");
        return AVERROR(EINVAL);
    }

    video_dec_ctx = avcodec_alloc_context3(dec);
    if (!video_dec_ctx) {
        fprintf(stderr, "Failed to allocate codec context\n");
        return AVERROR(ENOMEM);
    }

    ret = avcodec_parameters_to_context(video_dec_ctx, st->codecpar);
    if (ret < 0) {
        fprintf(stderr, "Failed to copy codec parameters\n");
        return ret;
    }

    // Enable necessary debug options
    av_dict_set(&opts, "flags2", "+export_mvs", 0);
    
    ret = avcodec_open2(video_dec_ctx, dec, &opts);
    if (ret < 0) {
        fprintf(stderr, "Failed to open codec\n");
        return ret;
    }

    video_stream_idx = stream_idx;
    video_stream = fmt_ctx->streams[video_stream_idx];

    av_dict_free(&opts);
    return 0;
}

int main(int argc, char **argv) {
    int ret = 0;
    AVPacket *pkt = NULL;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <video>\n", argv[0]);
        exit(1);
    }
    src_filename = argv[1];

    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    if (open_codec_context(fmt_ctx, AVMEDIA_TYPE_VIDEO) < 0) {
        goto end;
    }

    if (!video_stream) {
        fprintf(stderr, "Could not find video stream\n");
        ret = 1;
        goto end;
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate packet\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    printf("Starting DC coefficient extraction...\n");
    printf("Video dimensions: %dx%d\n", video_dec_ctx->width, video_dec_ctx->height);

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            ret = decode_packet(pkt);
        }
        av_packet_unref(pkt);
        if (ret < 0)
            break;
    }

    decode_packet(NULL);

end:
    avcodec_free_context(&video_dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return ret < 0;
}
