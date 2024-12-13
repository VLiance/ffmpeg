#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <stdio.h>

void save_frame_as_bmp(AVFrame *frame, int width, int height, int frame_number) {
    char filename[64];
    snprintf(filename, sizeof(filename), "frame_%03d.bmp", frame_number);

    FILE *file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Erreur: impossible de créer le fichier %s.\n", filename);
        return;
    }

    unsigned char bmp_header[54] = {
        0x42, 0x4D, 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 24, 0
    };

    int padding = (4 - (width * 3) % 4) % 4;
    int row_size = width * 3 + padding;
    int image_size = row_size * height;
    int file_size = 54 + image_size;

    bmp_header[2] = (unsigned char)(file_size & 0xFF);
    bmp_header[3] = (unsigned char)((file_size >> 8) & 0xFF);
    bmp_header[4] = (unsigned char)((file_size >> 16) & 0xFF);
    bmp_header[5] = (unsigned char)((file_size >> 24) & 0xFF);
    bmp_header[18] = (unsigned char)(width & 0xFF);
    bmp_header[19] = (unsigned char)((width >> 8) & 0xFF);
    bmp_header[20] = (unsigned char)((width >> 16) & 0xFF);
    bmp_header[21] = (unsigned char)((width >> 24) & 0xFF);
    bmp_header[22] = (unsigned char)(height & 0xFF);
    bmp_header[23] = (unsigned char)((height >> 8) & 0xFF);
    bmp_header[24] = (unsigned char)((height >> 16) & 0xFF);
    bmp_header[25] = (unsigned char)((height >> 24) & 0xFF);
    bmp_header[34] = (unsigned char)(image_size & 0xFF);
    bmp_header[35] = (unsigned char)((image_size >> 8) & 0xFF);
    bmp_header[36] = (unsigned char)((image_size >> 16) & 0xFF);
    bmp_header[37] = (unsigned char)((image_size >> 24) & 0xFF);

    fwrite(bmp_header, 1, 54, file);

    for (int y = height - 1; y >= 0; y--) {
        fwrite(frame->data[0] + y * frame->linesize[0], 1, width * 3, file);
        for (int p = 0; p < padding; p++) {
            fputc(0, file);
        }
    }

    fclose(file);
}

void extract_frames(AVFormatContext *formatContext, const char *filename) {
    int videoStreamIndex = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }

    if (videoStreamIndex == -1) {
        fprintf(stderr, "Erreur: aucun flux vidéo trouvé.\n");
        return;
    }

    AVCodecParameters *codecParameters = formatContext->streams[videoStreamIndex]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecParameters->codec_id);
    if (!codec) {
        fprintf(stderr, "Erreur: codec non trouvé.\n");
        return;
    }

    AVCodecContext *codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        fprintf(stderr, "Erreur: impossible d'allouer le contexte du codec.\n");
        return;
    }

    if (avcodec_parameters_to_context(codecContext, codecParameters) < 0) {
        fprintf(stderr, "Erreur: impossible de copier les paramètres du codec.\n");
        avcodec_free_context(&codecContext);
        return;
    }

    if (avcodec_open2(codecContext, codec, NULL) < 0) {
        fprintf(stderr, "Erreur: impossible d'ouvrir le codec.\n");
        avcodec_free_context(&codecContext);
        return;
    }

    printf("Dimensions de la vidéo : %dx%d\n", codecContext->width, codecContext->height);
    printf("Format de pixel : %s\n", av_get_pix_fmt_name(codecContext->pix_fmt));

    AVFrame *frame = av_frame_alloc();
    AVFrame *frameRGB = av_frame_alloc();
    if (!frame || !frameRGB) {
        fprintf(stderr, "Erreur: impossible d'allouer les frames.\n");
        av_frame_free(&frame);
        av_frame_free(&frameRGB);
        avcodec_free_context(&codecContext);
        return;
    }

    struct SwsContext *swsContext = sws_getContext(codecContext->width, codecContext->height, codecContext->pix_fmt,
                                                   codecContext->width, codecContext->height, AV_PIX_FMT_BGR24,
                                                   SWS_BICUBIC, NULL, NULL, NULL);

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, codecContext->width, codecContext->height, 1);
    uint8_t *buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(frameRGB->data, frameRGB->linesize, buffer, AV_PIX_FMT_BGR24,
                         codecContext->width, codecContext->height, 1);

    AVPacket packet;
    int frameCount = 0;

    while (av_read_frame(formatContext, &packet) >= 0 && frameCount < 5) {
        if (packet.stream_index == videoStreamIndex) {
            if (avcodec_send_packet(codecContext, &packet) == 0) {
                while (avcodec_receive_frame(codecContext, frame) == 0) {
                    sws_scale(swsContext, (uint8_t const * const *)frame->data,
                              frame->linesize, 0, codecContext->height,
                              frameRGB->data, frameRGB->linesize);

                    save_frame_as_bmp(frameRGB, codecContext->width, codecContext->height, frameCount++);
                }
            }
        }
        av_packet_unref(&packet);
    }

    av_free(buffer);
    av_frame_free(&frame);
    av_frame_free(&frameRGB);
    avcodec_free_context(&codecContext);
    sws_freeContext(swsContext);
}

AVFormatContext* open_video_file(const char *filename) {
    AVFormatContext *formatContext = NULL;
    if (avformat_open_input(&formatContext, filename, NULL, NULL) != 0) {
        fprintf(stderr, "Erreur: impossible d'ouvrir le fichier vidéo.\n");
        return NULL;
    }
    return formatContext;
}

void print_video_info(AVFormatContext *formatContext, const char *filename) {
    if (avformat_find_stream_info(formatContext, NULL) < 0) {
        fprintf(stderr, "Erreur: impossible de récupérer les informations du flux.\n");
        return;
    }
    av_dump_format(formatContext, 0, filename, 0);
}

int main() {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("--------- starting --------\n");

    avformat_network_init();

    const char *filename = "Fire.mp4";

    AVFormatContext *formatContext = open_video_file(filename);
    if (!formatContext) {
        return -1;
    }

    print_video_info(formatContext, filename);

    extract_frames(formatContext, filename);

    avformat_close_input(&formatContext);

    printf("--------- end --------\n");
    return 0;
}
