/*
 * Roq-Player by Andress Barajas
 * 
 * Include this header file in applications you want to 
 * have the ability to easily play a .ROQ file
 */

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

// Stores the above values
extern int player_errno;

// The library call this method after decoding a frame of video/audio.
// You can add controller input code to callback so you react to controller
// input while the video is playing.
typedef void (*frame_callback)();

typedef struct roq_player_t roq_player_t;

int player_init(void);
void player_shutdown(roq_player_t* player);

roq_player_t* player_create(const char* filename);
roq_player_t* player_create_file(FILE* f);
roq_player_t* player_create_memory(unsigned char* memory, const unsigned int length);

void player_play(roq_player_t* player, frame_callback frame_cb);
void player_pause(roq_player_t* player);
void player_stop(roq_player_t* player);
void player_volume(roq_player_t* player, int vol);
int player_isplaying(roq_player_t* player);
int player_get_loop(roq_player_t* player);
void player_set_loop(roq_player_t* player, int loop);
int player_has_ended(roq_player_t* player);

#ifdef __cplusplus
}
#endif

#endif
