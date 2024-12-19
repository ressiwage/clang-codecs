/**
 * Extract DC coefficients from H.264 video using FFmpeg/libav
 * 
 * Build with:
 * gcc -o extract_dc extract_dc.c -lavcodec -lavformat -lavutil
 * 
 * Note: Requires FFmpeg built with --enable-debug --disable-stripping
 */
#include <libavcodec/h264.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <stdio.h>
#include <libavcodec/h264_ps.h>  
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

// Scaling matrices for DC coefficients (from H.264 spec)
static const int dequant_coef[6][6] = {
    {10, 13, 16, 18, 23, 25},
    {11, 14, 18, 21, 25, 28},
    {13, 16, 20, 23, 29, 31},
    {14, 18, 23, 25, 31, 35},
    {16, 20, 25, 29, 35, 39},
    {18, 23, 29, 31, 39, 44}
};

// Function to get QP-based scaling factor for DC coefficients -- транспонирование матрицы
static int get_dc_scale_factor(int QP) {
    int qp_div6 = QP / 6;
    int qp_mod6 = QP % 6;
    return dequant_coef[qp_mod6][qp_div6];
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
            
            int mb_width = (video_dec_ctx->width + 15) >> 4; // x/2^4
            int mb_height = (video_dec_ctx->height + 15) >> 4; // x/2^4
            video_frame_count++;

            // Only process middle macroblock
            for (int mb_y =0; mb_y < mb_height-1; mb_y++) {
                for (int mb_x = 0; mb_x < mb_width-1; mb_x++) {
                    int mb_index = mb_y * mb_width + mb_x;

                    if (sl->intra16x16_pred_mode >= 0) {
                        // Get DC residual
                        int dc_residual = sl->mb_luma_dc[0][0];
                        
                        // Get QP for this macroblock
                        int qp = sl->qscale;
                        int scale_factor = get_dc_scale_factor(qp);
                        
                        // Inverse quantize the DC coefficient
                        int dc_scaled = dc_residual * scale_factor;
                        
                        // Get predicted value from intra prediction mode
                        int pred_dc = 0;
                        switch (sl->intra16x16_pred_mode) {
                            case 0: // Vertical
                                pred_dc = frame->data[0][(mb_y * 16 - 1) * frame->linesize[0] + mb_x * 16];
                                break;
                            case 1: // Horizontal
                                pred_dc = frame->data[0][mb_y * 16 * frame->linesize[0] + mb_x * 16 - 1];
                                break;
                            case 2: // DC
                                {
                                    int sum = 0;
                                    // Sum top and left samples
                                    for (int i = 0; i < 16; i++) {
                                        sum += frame->data[0][(mb_y * 16 - 1) * frame->linesize[0] + mb_x * 16 + i];
                                        sum += frame->data[0][(mb_y * 16 + i) * frame->linesize[0] + mb_x * 16 - 1];
                                    }
                                    pred_dc = (sum + 16) >> 5; // Average
                                }
                                break;
                            case 3: // Plane
                                // Simplified plane prediction
                                pred_dc = 128;
                                break;
                        }
                        
                        // Final DC value is prediction plus scaled residual
                        int final_dc = pred_dc + dc_scaled;
                        
                        printf("Frame %d, MB(%d,%d): DC_residual=%d QP=%d Scale=%d Pred=%d Final_DC=%d\n",
                               video_frame_count, mb_x, mb_y, dc_residual, qp, scale_factor, 
                               pred_dc, final_dc);
                        FILE *fptr;
                        fptr = fopen("out.txt", "a");
                        fprintf(fptr, "(%d;%d;%d;%d)", mb_x, mb_y,video_frame_count, final_dc);
                        fclose(fptr);
                    }
                }
            }
            
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
