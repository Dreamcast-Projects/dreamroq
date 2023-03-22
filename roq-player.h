#ifndef ROQPLAYER_H
#define ROQPLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#define ERROR                 0x00
#define SUCCESS               0x01
#define OUT_OF_MEMORY         0x02
#define OUT_OF_VID_MEMORY     0x03
#define SND_INIT_FAILURE      0x04
#define FORMAT_INIT_FAILURE   0x05
#define SOURCE_ERROR          0x06

extern int player_errno;

/* The library call this method after decoding a frame of video/audio.
*  You can add controller input code to callback so you react to controller
*  input while the video is playing. */
typedef void (*frame_callback)();

typedef struct format_player_t format_player_t;

int player_init();
void player_shutdown(format_player_t* format_player);

format_player_t* player_create(const char* filename);
format_player_t* player_create_fd(FILE* f);
format_player_t* player_create_buf(unsigned char* buf, const unsigned int length);

//void player_decode(format_player_t* format_player);
void player_play(format_player_t* format_player, frame_callback frame_cb);
void player_pause(format_player_t* format_player);
void player_stop(format_player_t* format_player);
void player_volume(format_player_t* format_player, int vol);
int player_isplaying(format_player_t* format_player);
int player_get_loop(format_player_t* format_player);
void player_set_loop(format_player_t* format_player, int loop);
int player_has_ended(format_player_t* format_player);

#ifdef __cplusplus
}
#endif

#endif
