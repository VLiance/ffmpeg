#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#include <libavutil/opt.h>
#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

void custom_log_callback(void *ptr, int level, const char *fmt, va_list vargs) {
    vfprintf(stdout, fmt, vargs);
}
/*
void list_dshow_devices() {
    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *options = NULL;
    const char *device_type = "dshow";

    printf("Début de la liste des périphériques...\n");
    av_dict_set(&options, "list_devices", "true", 0);

    printf("Liste des périphériques disponibles :\n");
    int ret = avformat_open_input(&fmt_ctx, "dummy", av_find_input_format(device_type), &options);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("Erreur lors de l'ouverture de l'entrée : %s\n", errbuf);
    }

    av_dict_free(&options);
    avformat_close_input(&fmt_ctx);
    printf("Fin de la liste des périphériques.\n\n");
}
*/
static int save_frame_as_bmp(AVFrame *frame, int width, int height, int frame_number) {
    char filename[32];
    snprintf(filename, sizeof(filename), "frame_%08d.bmp", frame_number);

   // FILE *file = fopen(filename, "wb");
    FILE *file = fopen("output.bmp", "wb");
    if (!file) {
        fprintf(stderr, "Impossible d'ouvrir le fichier %s\n", filename);
        return -1;
    }

    int stride = (width * 3 + 3) & ~3;
    int filesize = 54 + stride * height;

    unsigned char bmpfileheader[14] = {'B','M', 0,0,0,0, 0,0, 0,0, 54,0,0,0};
    unsigned char bmpinfoheader[40] = {40,0,0,0, 0,0,0,0, 0,0,0,0, 1,0, 24,0};

    bmpfileheader[ 2] = (unsigned char)(filesize    );
    bmpfileheader[ 3] = (unsigned char)(filesize>> 8);
    bmpfileheader[ 4] = (unsigned char)(filesize>>16);
    bmpfileheader[ 5] = (unsigned char)(filesize>>24);

    bmpinfoheader[ 4] = (unsigned char)(width    );
    bmpinfoheader[ 5] = (unsigned char)(width>> 8);
    bmpinfoheader[ 6] = (unsigned char)(width>>16);
    bmpinfoheader[ 7] = (unsigned char)(width>>24);
    bmpinfoheader[ 8] = (unsigned char)(height    );
    bmpinfoheader[ 9] = (unsigned char)(height>> 8);
    bmpinfoheader[10] = (unsigned char)(height>>16);
    bmpinfoheader[11] = (unsigned char)(height>>24);

    fwrite(bmpfileheader, 1, 14, file);
    fwrite(bmpinfoheader, 1, 40, file);

    uint8_t *buffer = malloc(stride * height);
    struct SwsContext *sws_ctx = sws_getContext(
        width, height, frame->format,
        width, height, AV_PIX_FMT_BGR24,
        SWS_BILINEAR, NULL, NULL, NULL);

    uint8_t *dst[4] = {buffer, NULL, NULL, NULL};
    int dst_linesize[4] = {stride, 0, 0, 0};
    sws_scale(sws_ctx, (const uint8_t * const*)frame->data, frame->linesize, 0, height, dst, dst_linesize);

    for (int y = height - 1; y >= 0; y--) {
        fwrite(buffer + y * stride, 1, stride, file);
    }

    fclose(file);
    free(buffer);
    sws_freeContext(sws_ctx);
    printf("Image sauvegardée : %s\n", filename);
    return 0;
}

int capture_frames_continuously(const char *device_name) {
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    AVFrame *frame = NULL;
    AVPacket packet;
    int ret = 0;
    int frame_number = 0;

    printf("Début de la capture à partir de : %s\n", device_name);

    avdevice_register_all();

    AVDictionary *options = NULL;
    av_dict_set(&options, "video_size", "640x480", 0);
    av_dict_set(&options, "framerate", "30", 0);

    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        fprintf(stderr, "Impossible d'allouer le contexte de format\n");
        return -1;
    }

    printf("Ouverture du périphérique...\n");
    if ((ret = avformat_open_input(&fmt_ctx, device_name, av_find_input_format("dshow"), &options)) < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Impossible d'ouvrir le périphérique : %s\n", errbuf);
        return -1;
    }

    printf("Recherche des informations du flux...\n");
    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        fprintf(stderr, "Impossible de trouver les informations du flux\n");
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    printf("Recherche du meilleur flux vidéo...\n");
    int video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (video_stream_index < 0) {
        fprintf(stderr, "Flux vidéo non trouvé\n");
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Impossible d'allouer le contexte du codec\n");
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);
    if ((ret = avcodec_open2(codec_ctx, codec, NULL)) < 0) {
        fprintf(stderr, "Impossible d'ouvrir le codec\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Impossible d'allouer la frame\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    printf("Début de la boucle de capture...\n");
    //while (frame_number < 999) {
      bool alive =true;
    while (alive) {
        if (av_read_frame(fmt_ctx, &packet) < 0) {
            printf("Fin du flux ou erreur de lecture\n");
            break;
        }

        if (packet.stream_index == video_stream_index) {
            if ((ret = avcodec_send_packet(codec_ctx, &packet)) < 0) {
                fprintf(stderr, "Erreur lors de l'envoi du paquet au décodeur\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    fprintf(stderr, "Erreur lors de la réception de la frame\n");
                    goto end;
                }

                if (ret >= 0) {
                    printf("Traitement de la frame %d\n", frame_number);
                    save_frame_as_bmp(frame, codec_ctx->width, codec_ctx->height, frame_number++);
                }
            }
        }

        av_packet_unref(&packet);
        Sleep(10); 
    }

end:
    printf("Fin de la capture\n");
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return frame_number > 0 ? 0 : -1;  // Retourne 0 si au moins une frame a été capturée
}



void list_dshow_devices(const char *device_type) {
    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *options = NULL;

    av_dict_set(&options, "list_devices", "true", 0);

    if (avformat_open_input(&fmt_ctx, "dummy", av_find_input_format("dshow"), &options) < 0) {
        fprintf(stdout, "Impossible de lister les périphériques %s.\n", device_type);
        av_dict_free(&options);
        return;
    }

    av_dict_free(&options);
    avformat_close_input(&fmt_ctx);
}


int main() {
   setbuf(stderr, 0);
   setbuf(stdout, 0);

    av_log_set_callback(custom_log_callback);

    printf("Début du programme\n");
    
   // list_dshow_devices();
    
   // printf("Liste des périphériques vidéo (Webcams) :\n");
   // list_dshow_devices("video");
   //
   // printf("\nListe des périphériques audio :\n");
   // list_dshow_devices("audio");
    
    /////////////////

    const char *device_name = "video=SPCA2281 Web Camera ";
    printf("Tentative de capture à partir de : %s\n", device_name);
    int ret = capture_frames_continuously(device_name);

    if (ret < 0) {
        fprintf(stderr, "Échec de la capture. Essayez un autre périphérique.\n");
    } else {
        printf("Capture réussie. %d frames ont été sauvegardées.\n", ret);
    }

    printf("Fin du programme\n");
    return ret;
}
