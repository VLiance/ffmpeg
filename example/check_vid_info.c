#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <stdio.h>

int main() {
  setbuf(stdout, NULL); 
  setbuf(stderr, NULL); 
  
  printf("--------- stating --------\n");
    // Initialisation de FFmpeg
    avformat_network_init();  // Initialisation réseau (utile si vous utilisez des flux réseau)

    // Chemin vers la vidéo
    const char *filename = "Fire.mp4";

    AVFormatContext *formatContext = NULL;

    // Ouverture du fichier vidéo
    if (avformat_open_input(&formatContext, filename, NULL, NULL) != 0) {
        fprintf(stderr, "Erreur: impossible d'ouvrir le fichier vidéo.\n");
        return -1;
    }

    // Récupération des informations du format du fichier
    if (avformat_find_stream_info(formatContext, NULL) < 0) {
        fprintf(stderr, "Erreur: impossible de récupérer les informations du flux.\n");
        return -1;
    }

    // Affichage des informations sur la vidéo
    av_dump_format(formatContext, 0, filename, 0);

    // Libération des ressources
    avformat_close_input(&formatContext);
  printf("--------- end --------\n");
    return 0;
}
