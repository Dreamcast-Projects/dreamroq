/*
 * Dreamroq by Mike Melanson
 *
 * This is the header file to be included in the programs wishing to
 * use the Dreamroq playback engine.
 */

#ifndef DREAMROQ_H
#define DREAMROQ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#define ROQ_SUCCESS           0
#define ROQ_FILE_OPEN_FAILURE 1
#define ROQ_FILE_READ_FAILURE 2
#define ROQ_CHUNK_TOO_LARGE   3
#define ROQ_BAD_CODEBOOK      4
#define ROQ_INVALID_PIC_SIZE  5
#define ROQ_NO_MEMORY         6
#define ROQ_BAD_VQ_STREAM     7
#define ROQ_INVALID_DIMENSION 8
#define ROQ_RENDER_PROBLEM    9
#define ROQ_CLIENT_PROBLEM    10

#define ROQ_BUFFER_DEFAULT_SIZE (64 * 1024)

extern int roq_errno;

typedef struct roq_t roq_t;

// Create a roq instance with a filename. Returns NULL if the file could not
// be opened.

roq_t* roq_create_with_filename(const char* filename);

// Create a roq instance with file handle. Pass TRUE to close_when_done
// to let roq call fclose() on the handle when roq_destroy() is 
// called.

roq_t* roq_create_with_file(FILE* fh, int close_when_done);

// Create a roq_t instance with pointer to memory as source. This assumes the
// whole file is in memory. Pass TRUE to free_when_done to let roq call
// free() on the pointer when roq_destroy() is called.

roq_t* roq_create_with_memory(unsigned char* bytes, size_t length, int free_when_done);

// The library calls this function when it has a frame ready for display.
typedef void(*roq_video_decode_callback)
	(unsigned short *frame_data, int width, int height, int stride, int texture_height);
void roq_set_video_decode_callback(roq_t *roq, roq_video_decode_callback fp);

// The library calls this function when it has pcm samples ready for output.
typedef void(*roq_audio_decode_callback)
	(unsigned char *audio_frame_data, int size, int channels);
void roq_set_audio_decode_callback(roq_t *roq, roq_audio_decode_callback fp);

int roq_get_loop(roq_t* roq);

void roq_set_loop(roq_t* roq, int loop);

int roq_decode(roq_t* roq);

int roq_get_framerate(roq_t* roq);

int roq_get_width(roq_t* roq);

int roq_get_height(roq_t* roq);

int roq_has_ended(roq_t* roq);

void roq_destroy(roq_t* roq);

void roq_seek(roq_t* roq);

#ifdef __cplusplus
}
#endif

#endif  /* DREAMROQ_H */
