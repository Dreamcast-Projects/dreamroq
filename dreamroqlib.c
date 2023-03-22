/*
 * Dreamroq by Mike Melanson
 * Audio support by Josh Pearson
 * 
 * This is the main playback engine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "dreamroqlib.h"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define RoQ_INFO           0x1001
#define RoQ_QUAD_CODEBOOK  0x1002
#define RoQ_QUAD_VQ        0x1011
#define RoQ_JPEG           0x1012
#define RoQ_SOUND_MONO     0x1020
#define RoQ_SOUND_STEREO   0x1021
#define RoQ_PACKET         0x1030
#define RoQ_SIGNATURE      0x1084

#define CHUNK_HEADER_SIZE 8

#define LE_16(buf) (*buf | (*(buf+1) << 8))
#define LE_32(buf) (*buf | (*(buf+1) << 8) | (*(buf+2) << 16) | (*(buf+3) << 24))

#define ROQ_CODEBOOK_SIZE 256
#define SQR_ARRAY_SIZE 260
#define VQR_ARRAY_SIZE 256

typedef struct roq_buffer_t roq_buffer_t;
typedef struct roq_chunk_t roq_chunk_t;

int roq_errno = 0;

struct roq_t {
    int width;
    int height;
    int mb_width;
    int mb_height;
    int mb_count;

    unsigned short *frame[2];
    unsigned int frame_index;

    int stride;
    int framerate;
    int current_frame;
    int texture_height;
	
	int loop;
    int has_ended;

    roq_buffer_t *buffer;

    roq_video_decode_callback video_decode_callback;
	roq_audio_decode_callback audio_decode_callback;

    unsigned short cb2x2_rgb565[ROQ_CODEBOOK_SIZE][4];
    unsigned short cb4x4_rgb565[ROQ_CODEBOOK_SIZE][16];

    int channels;
    int pcm_samples;
    unsigned char pcm_sample[ROQ_BUFFER_DEFAULT_SIZE];

    // Sound LUT
    short int snd_sqr_array[SQR_ARRAY_SIZE];

    // Video LUT
    short int cr_r_lut[VQR_ARRAY_SIZE];
    short int cb_b_lut[VQR_ARRAY_SIZE];
    short int cr_g_lut[VQR_ARRAY_SIZE];
    short int cb_g_lut[VQR_ARRAY_SIZE];
    short int yy_lut[VQR_ARRAY_SIZE];
};

enum roq_buffer_mode {
	ROQ_BUFFER_MODE_FILE,
	ROQ_BUFFER_MODE_FIXED_MEM,
	ROQ_BUFFER_MODE_DYNAMIC_MEM
};

struct roq_buffer_t {
	FILE* fh;
    size_t start_index;
    size_t end_index;
    size_t capacity;
	unsigned char* bytes;

    int free_when_done;
	int close_when_done;

    enum roq_buffer_mode mode;
};

struct roq_chunk_t {
    short chunk_id;
    int chunk_size;
    short chunk_arg;
};

static roq_t* roq_create_with_buffer(roq_buffer_t* buffer);
static roq_buffer_t* roq_buffer_create_with_filename(const char* filename);
static roq_buffer_t* roq_buffer_create_with_file(FILE* fh, int close_when_done);
static roq_buffer_t* roq_buffer_create_with_memory(unsigned char* bytes, size_t capacity, int free_when_done);
static roq_buffer_t* roq_buffer_create_with_capacity(size_t capacity);

static int roq_buffer_read(roq_buffer_t* self, size_t count);
static int roq_buffer_has(roq_buffer_t* self, size_t count);
static unsigned char* roq_buffer_get_data(roq_buffer_t* self);
static void roq_buffer_set_offset(roq_buffer_t* self, off_t offset, int whence);
static void roq_buffer_destroy(roq_buffer_t* buffer);

static int roq_read_header_chunk(roq_buffer_t* self, roq_chunk_t* header);
static int roq_eof(roq_buffer_t* self);
static void roq_handle_end(roq_t* roq);

static int roq_unpack_quad_codebook(roq_t* roq, unsigned char* buf, int size, int arg);
static unsigned short* roq_unpack_vq(roq_t* roq, unsigned char* buf, int size, unsigned int arg);

roq_t* roq_create_with_filename(const char* filename) {
	roq_buffer_t *buffer = roq_buffer_create_with_filename(filename);
	if (!buffer) {
		return NULL;
	}
	return roq_create_with_buffer(buffer);
}

roq_t* roq_create_with_file(FILE* fh, int close_when_done) {
	roq_buffer_t *buffer = roq_buffer_create_with_file(fh, close_when_done);
    if (!buffer) {
		return NULL;
	}
	return roq_create_with_buffer(buffer);
}

roq_t* roq_create_with_memory(unsigned char* bytes, size_t capacity, int free_when_done) {
	roq_buffer_t *buffer = roq_buffer_create_with_memory(bytes, capacity, free_when_done);
    if (!buffer) {
		return NULL;
	}
	return roq_create_with_buffer(buffer);
}

void roq_set_video_decode_callback(roq_t* roq, roq_video_decode_callback fp) {
	roq->video_decode_callback = fp;
}

void roq_set_audio_decode_callback(roq_t* roq, roq_audio_decode_callback fp) {
	roq->audio_decode_callback = fp;
}

int roq_get_loop(roq_t* roq) {
	return roq->loop;
}

void roq_set_loop(roq_t* roq, int loop) {
	roq->loop = loop;
}

int roq_decode(roq_t* roq) {
	int decode_video = roq->video_decode_callback != NULL;
	int decode_audio = roq->audio_decode_callback != NULL;

	if (!decode_video && !decode_audio) {
		// Nothing to decode here
		return FALSE;
	}

    if(roq_eof(roq->buffer)) {
        roq_handle_end(roq);
    }

    if(roq->has_ended) {
        return FALSE;
    }

    roq_chunk_t header;
    int video_ended = FALSE;
    int audio_ended = FALSE;
    
	int video_decoded = FALSE;
	int audio_decoded = FALSE;
    
    do {
        if(!roq_eof(roq->buffer))
        {
            // Read the header
            if(!roq_read_header_chunk(roq->buffer, &header)) {
                roq_errno = ROQ_FILE_READ_FAILURE;
                return FALSE;
            }

            // Process chunk depending on ID
            switch(header.chunk_id) {
                case RoQ_INFO:
                    roq_buffer_set_offset(roq->buffer, header.chunk_size, SEEK_CUR);
                    break;
                case RoQ_PACKET:
                    printf("RoQ_PACKET\n\n");
                    roq_buffer_set_offset(roq->buffer, header.chunk_size, SEEK_CUR);
                    break;
                case RoQ_JPEG:
                    printf("RoQ_JPEG\n\n");
                    roq_buffer_set_offset(roq->buffer, header.chunk_size, SEEK_CUR);
                    break;
                case RoQ_QUAD_CODEBOOK:
                    fflush(stdout);
                    if(!decode_video) {
                        roq_buffer_set_offset(roq->buffer, header.chunk_size, SEEK_CUR);
                    }
                    else {
                        if(decode_audio && !audio_decoded && (video_decoded || video_ended)) {
                            audio_decoded = TRUE;
                            roq_buffer_set_offset(roq->buffer, -CHUNK_HEADER_SIZE, SEEK_CUR);
                            continue;
                        }

                        // Read the chunk
                        if(roq_buffer_read(roq->buffer, header.chunk_size) != header.chunk_size) {
                            roq_errno = ROQ_FILE_READ_FAILURE;
                            return FALSE;
                        }

                        unsigned char* read_buffer = roq_buffer_get_data(roq->buffer);

                        // Decode codebook
                        if(!roq_unpack_quad_codebook(roq, read_buffer, header.chunk_size, header.chunk_arg)) {
                            roq_errno = ROQ_BAD_CODEBOOK;
                            return FALSE;
                        }
                    }
                    
                    break;
                case RoQ_QUAD_VQ:
                    if(!decode_video) {
                        roq_buffer_set_offset(roq->buffer, header.chunk_size, SEEK_CUR);
                    }
                    else {
                        // Read the chunk
                        if(roq_buffer_read(roq->buffer, header.chunk_size) != header.chunk_size) {
                            roq_errno = ROQ_FILE_READ_FAILURE;
                            return FALSE;
                        }

                        unsigned char* read_buffer = roq_buffer_get_data(roq->buffer);

                        // Decode video
                        unsigned short* frame = roq_unpack_vq(roq, read_buffer, header.chunk_size, header.chunk_arg);
                        if(frame) {
                            video_decoded = TRUE;
                            roq->video_decode_callback(frame, roq->width, roq->height, roq->stride, roq->texture_height);
                        }
                        else {
                            roq_errno = ROQ_BAD_VQ_STREAM;
                            video_ended = TRUE;
                        }
                    }
                    break;
                case RoQ_SOUND_MONO:
                    if(!decode_audio) {
                        roq_buffer_set_offset(roq->buffer, header.chunk_size, SEEK_CUR);
                    }
                    else {
                        int i, snd_left;
                        // Read the chunk
                        if(roq_buffer_read(roq->buffer, header.chunk_size) != header.chunk_size) {
                            roq_errno = ROQ_FILE_READ_FAILURE;
                            return FALSE;
                        }

                        unsigned char* read_buffer = roq_buffer_get_data(roq->buffer);

                        // Decode audio
                        roq->channels = 1;
                        roq->pcm_samples = header.chunk_size*2;
                        snd_left = header.chunk_arg;
                        for(i = 0; i < header.chunk_size; i++) {
                            snd_left += roq->snd_sqr_array[read_buffer[i]];
                            roq->pcm_sample[i * 2] = snd_left & 0xff;
                            roq->pcm_sample[i * 2 + 1] = (snd_left & 0xff00) >> 8;
                        }
                        audio_decoded = TRUE;
                        roq->audio_decode_callback(roq->pcm_sample, header.chunk_size*2, 1);
                    }
                    break;
                case RoQ_SOUND_STEREO:
                    if(!decode_audio) {
                        roq_buffer_set_offset(roq->buffer, header.chunk_size, SEEK_CUR);
                    }
                    else {
                        int i, snd_left, snd_right;

                        // Read the chunk
                        if(roq_buffer_read(roq->buffer, header.chunk_size) != header.chunk_size) {
                            roq_errno = ROQ_FILE_READ_FAILURE;
                            return FALSE;
                        }
                        
                        unsigned char* read_buffer = roq_buffer_get_data(roq->buffer);

                        // Decode audio
                        roq->channels = 2;
                        roq->pcm_samples = header.chunk_size*2;
                        snd_left = (header.chunk_arg & 0xFF00);
                        snd_right = (header.chunk_arg & 0xFF) << 8;
                        for(i = 0; i < header.chunk_size; i += 2) {
                            snd_left  += roq->snd_sqr_array[read_buffer[i]];
                            snd_right += roq->snd_sqr_array[read_buffer[i+1]];
                            roq->pcm_sample[i * 2] = snd_left & 0xff;
                            roq->pcm_sample[i * 2 + 1] = (snd_left & 0xff00) >> 8;
                            roq->pcm_sample[i * 2 + 2] =  snd_right & 0xff;
                            roq->pcm_sample[i * 2 + 3] = (snd_right & 0xff00) >> 8;
                        }
                        audio_decoded = TRUE;
                        roq->audio_decode_callback(roq->pcm_sample, header.chunk_size*2, 2);
                    }
                    break;
                default:
                    break;
            }
        }
    } while ((decode_video && !video_decoded && !video_ended) || 
             (decode_audio && !audio_decoded && !audio_ended));
                
    // We wanted to decode something but failed -> the source must have ended
    if (video_ended || audio_ended) {
        roq_handle_end(roq);
        return FALSE;
    } 
    else {
        roq->current_frame += 1;
    }
    
    return TRUE;
}

int roq_get_framerate(roq_t* roq) {
	return roq->framerate;
}

int roq_has_ended(roq_t* roq) {
	return roq->has_ended;
}

int roq_get_width(roq_t* roq) {
    return roq->width;
}

int roq_get_height(roq_t* roq) {
    return roq->height;
}

void roq_destroy(roq_t* roq) {
    if(roq == NULL) {
        return;
    }
    
    if(roq->buffer != NULL) {
        roq_buffer_destroy(roq->buffer);
    }
	
    if(roq->frame[0] != NULL) {
        free (roq->frame[0]);
    }
    
    if(roq->frame[1] != NULL) {
        free (roq->frame[1]);
    }

	free(roq);
    roq = NULL;
}

static void roq_rewind(roq_t* roq) {
    roq->frame_index = 0;
    roq->current_frame = 0;
    roq->has_ended = FALSE;
    roq_buffer_set_offset(roq->buffer, CHUNK_HEADER_SIZE, SEEK_SET);
}

static void roq_handle_end(roq_t* roq) {
	if (roq->loop) {
		roq_rewind(roq);
	}
	else {
		roq->has_ended = TRUE;
	}
}

static roq_t* roq_create_with_buffer(roq_buffer_t* buffer) {
    int i = 0;
    roq_chunk_t header;
    unsigned char* read_buffer;
    roq_t* roq = (roq_t*)malloc(sizeof(roq_t));
    memset(roq, 0, sizeof(roq_t));

    roq->loop = FALSE;
    roq->buffer = buffer;
    roq->frame_index = 0;
    roq->current_frame = 0;

    // Initialize Audio SQRT Look-Up Table
    for(i = 0; i < 128; i++) {
        roq->snd_sqr_array[i] = i * i;
        roq->snd_sqr_array[i + 128] = -(roq->snd_sqr_array[i]);
    }

    // Initialize YUV420 -> RGB Math Look-Up Table
    for(i = 0; i < 256; i++) {
        roq->yy_lut[i] = 1.164 * (i - 16);
        roq->cr_r_lut[i] = 1.596 * (i - 128);
        roq->cb_b_lut[i] = 2.017 * (i - 128);
        roq->cr_g_lut[i] = -0.813 * (i - 128);
        roq->cb_g_lut[i] = -0.392 * (i - 128);
    }

    // Check if it has the ROQ signature header
    if(!roq_read_header_chunk(roq->buffer, &header)) {
        roq_destroy(roq);
        roq_errno = ROQ_FILE_READ_FAILURE;
        return NULL;
    }
    
    if(header.chunk_id != RoQ_SIGNATURE || header.chunk_size != 0xFFFFFFFF) {
        roq_destroy(roq);
        roq_errno = ROQ_FILE_READ_FAILURE;
        return NULL;
    }
    roq->framerate = header.chunk_arg;

    // Get RoQ_INFO
    do {
        if(!roq_read_header_chunk(roq->buffer, &header)) {
            roq_destroy(roq);
            roq_errno = ROQ_FILE_READ_FAILURE;
            return NULL;
        }
        
        if(header.chunk_id == RoQ_INFO) {
            if(roq_buffer_read(roq->buffer, header.chunk_size) != header.chunk_size) {
                roq_destroy(roq);
                roq_errno = ROQ_FILE_READ_FAILURE;
                return NULL;
            }

            read_buffer = roq_buffer_get_data(roq->buffer);
            roq->width = LE_16(&read_buffer[0]);
            roq->height = LE_16(&read_buffer[2]); 

            /* width and height each need to be divisible by 16 */
            if ((roq->width & 0xF) || (roq->height & 0xF)) {
                roq_destroy(roq);
                roq_errno = ROQ_INVALID_PIC_SIZE;
                return NULL;
            }

            if (roq->width < 8 || roq->width > 1024 ||
                roq->height < 8 || roq->height > 1024) {
                roq_destroy(roq);
                roq_errno = ROQ_INVALID_DIMENSION;
                return NULL;
            }

            roq->mb_width = roq->width >> 4;
            roq->mb_height = roq->height >> 4;
            roq->mb_count = roq->mb_width * roq->mb_height;
            
            roq->stride = 8;
            while (roq->stride < roq->width)
                roq->stride <<= 1;

            roq->texture_height = 8;
            while (roq->texture_height < roq->height)
                roq->texture_height <<= 1;

            printf("\tRoQ_INFO: dimensions = %dx%d, %dx%d; %d mbs, texture = %dx%d\n", 
                roq->width, roq->height, roq->mb_width, roq->mb_height,
                roq->mb_count, roq->stride, roq->texture_height);

            roq->frame[0] = (unsigned short*)memalign(32, roq->texture_height * roq->stride * sizeof(unsigned short));
            roq->frame[1] = (unsigned short*)memalign(32, roq->texture_height * roq->stride * sizeof(unsigned short));
            
            if (!roq->frame[0] || !roq->frame[1]) {
                roq_destroy(roq);
                roq_errno = ROQ_NO_MEMORY;
                return NULL;
            }

            memset(roq->frame[0], 0, roq->texture_height * roq->stride * sizeof(unsigned short));
            memset(roq->frame[1], 0, roq->texture_height * roq->stride * sizeof(unsigned short));
        } 
        else {
            roq_buffer_set_offset(roq->buffer, header.chunk_size, SEEK_CUR);
        }
    } while(header.chunk_id != RoQ_INFO);

    // Reset
    roq_buffer_set_offset(roq->buffer, CHUNK_HEADER_SIZE, SEEK_SET);

	return roq;
}

static roq_buffer_t* roq_buffer_create_with_filename(const char* filename) {
	FILE* fh = fopen(filename, "rb");
	if (!fh) {
        roq_errno = ROQ_FILE_OPEN_FAILURE;
		return NULL;
	}
	return roq_buffer_create_with_file(fh, TRUE);
}

static roq_buffer_t* roq_buffer_create_with_file(FILE* fh, int close_when_done) {
	roq_buffer_t* buffer = roq_buffer_create_with_capacity(ROQ_BUFFER_DEFAULT_SIZE);
	buffer->fh = fh;
	buffer->close_when_done = close_when_done;
    buffer->free_when_done = TRUE;
	buffer->mode = ROQ_BUFFER_MODE_FILE;
	return buffer;
}

static roq_buffer_t* roq_buffer_create_with_memory(unsigned char* bytes, size_t capacity, int free_when_done) {
	roq_buffer_t* buffer = (roq_buffer_t*)malloc(sizeof(roq_buffer_t));
	memset(buffer, 0, sizeof(roq_buffer_t));
    buffer->bytes = bytes;
	buffer->capacity = capacity;
	buffer->start_index = 0;
    buffer->end_index = 0;
	buffer->free_when_done = free_when_done;
	buffer->mode = ROQ_BUFFER_MODE_FIXED_MEM;
	return buffer;
}

static roq_buffer_t* roq_buffer_create_with_capacity(size_t capacity) {
	roq_buffer_t* buffer = (roq_buffer_t*)malloc(sizeof(roq_buffer_t));
	memset(buffer, 0, sizeof(roq_buffer_t));
	buffer->capacity = capacity;
	buffer->free_when_done = TRUE;
	buffer->bytes = (unsigned char*)malloc(capacity);
	buffer->mode = ROQ_BUFFER_MODE_DYNAMIC_MEM;
	return buffer;
}

static int roq_eof(roq_buffer_t* buffer) {
    if(buffer->mode == ROQ_BUFFER_MODE_FILE) {
        return feof(buffer->fh);
    } 
    else {
        return buffer->capacity == buffer->end_index;
    }
}

static void roq_buffer_set_offset(roq_buffer_t* buffer, off_t offset, int whence) {
    if(buffer->mode == ROQ_BUFFER_MODE_FILE) {
        fseek(buffer->fh, offset, whence);
    }
    else {
        if(whence == SEEK_SET) {
            buffer->end_index = offset;
            buffer->start_index = buffer->end_index;
        }
        else if(whence == SEEK_CUR) {
            buffer->end_index = buffer->start_index + offset;
            buffer->start_index = buffer->end_index;
        }
    }
}

static int roq_buffer_read(roq_buffer_t* buffer, size_t count) {
    if(buffer->mode == ROQ_BUFFER_MODE_FILE) {
        if(feof(buffer->fh) || fread(buffer->bytes, count, 1, buffer->fh) != 1) {
            return 0; 
        }
        return count;
    }
    else {
        if (!roq_buffer_has(buffer, count)) {
            return 0;
        }
        buffer->start_index = buffer->end_index;
        buffer->end_index += count;
        return count;
    }
}

static int roq_buffer_has(roq_buffer_t* buffer, size_t count) {
	size_t remaining = buffer->capacity - buffer->end_index;
	if (remaining >= count) {
		return TRUE;
	}
    return FALSE;
}

static unsigned char* roq_buffer_get_data(roq_buffer_t* buffer) {
    return buffer->bytes + buffer->start_index;
}

static void roq_buffer_destroy(roq_buffer_t* buffer) {
    if(buffer == NULL) {
        return;
    }

	if (buffer->fh && buffer->close_when_done) {
		fclose(buffer->fh);
	}

	if (buffer->free_when_done) {
		free(buffer->bytes);
	}

	free(buffer);
    buffer = NULL;
}

static int roq_read_header_chunk(roq_buffer_t* buffer, roq_chunk_t* header) {
    // Read the header section
    if(roq_buffer_read(buffer, CHUNK_HEADER_SIZE) != CHUNK_HEADER_SIZE) {
        roq_errno = ROQ_FILE_READ_FAILURE;
        return FALSE;
    }

    unsigned char* read_buffer = roq_buffer_get_data(buffer);
    header->chunk_id = LE_16(&read_buffer[0]);
    header->chunk_size = LE_32(&read_buffer[2]);
    header->chunk_arg = LE_16(&read_buffer[6]);

    // Check if size is too large
    if(header->chunk_size > ROQ_BUFFER_DEFAULT_SIZE) {
        roq_errno = ROQ_CHUNK_TOO_LARGE;
        return FALSE;
    }

    return TRUE;
}

static int roq_unpack_quad_codebook(roq_t* roq, unsigned char *buf, int size, int arg) {
    int y[4];
    int yp, u, v;
    int r, g, b;
    int count2x2;
    int count4x4;
    int i, j;
    unsigned short *v2x2;
    unsigned short *v4x4;

    count2x2 = (arg >> 8) & 0xFF;
    count4x4 =  arg       & 0xFF;

    if (!count2x2)
        count2x2 = ROQ_CODEBOOK_SIZE;
    /* 0x00 means 256 4x4 vectors if there is enough space in the chunk
     * after accounting for the 2x2 vectors */
    if (!count4x4 && count2x2 * 6 < size)
        count4x4 = ROQ_CODEBOOK_SIZE;

    /* size sanity check */
    if ((count2x2 * 6 + count4x4 * 4) != size) {
        return FALSE;
    }

    /* unpack the 2x2 vectors */
    for (i = 0; i < count2x2; i++) {
        /* unpack the YUV components from the bytestream */
        y[0] = *buf++;
        y[1] = *buf++;
        y[2] = *buf++;
        y[3] = *buf++;
        u  = *buf++;
        v  = *buf++;
        
        /* convert to RGB565 */
        for (j = 0; j < 4; j++) {
            yp = roq->yy_lut[y[j]];
            r = yp + roq->cr_r_lut[v];
            g = yp + roq->cr_g_lut[v] + roq->cb_g_lut[u];  
            b = yp + roq->cb_b_lut[u]; 

            r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
            g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
            b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

            roq->cb2x2_rgb565[i][j] = ((unsigned short)r & 0xf8) << 8 | 
                                      ((unsigned short)g & 0xfc) << 3 | 
                                      ((unsigned short)b & 0xf8) >> 3;
        }
    }

    /* unpack the 4x4 vectors */
    for (i = 0; i < count4x4; i++) {
        for (j = 0; j < 4; j++) {
            v2x2 = roq->cb2x2_rgb565[*buf++];
            v4x4 = roq->cb4x4_rgb565[i] + (j / 2) * 8 + (j % 2) * 2;
            v4x4[0] = v2x2[0];
            v4x4[1] = v2x2[1];
            v4x4[4] = v2x2[2];
            v4x4[5] = v2x2[3];
        }
    }

    return TRUE;
}

#define GET_BYTE(x) \
    if (index >= size) { \
        status = FALSE; \
        x = 0; \
    } else { \
        x = buf[index++]; \
    }

#define GET_MODE() \
    if (!mode_count) { \
        GET_BYTE(mode_lo); \
        GET_BYTE(mode_hi); \
        mode_set = (mode_hi << 8) | mode_lo; \
        mode_count = 16; \
    } \
    mode_count -= 2; \
    mode = (mode_set >> mode_count) & 0x03;

static unsigned short* roq_unpack_vq(roq_t* roq, unsigned char* buf, int size, unsigned int arg) {
    int status = TRUE;
    int mb_x, mb_y;
    int block;     /* 8x8 blocks */
    int subblock;  /* 4x4 blocks */
    int stride = roq->stride;
    int i;

    /* frame and pixel management */
    unsigned short *this_frame;
    unsigned short *last_frame;

    int line_offset;
    int mb_offset;
    int block_offset;
    int subblock_offset;

    unsigned short *this_ptr;
    unsigned int *this_ptr32;
    unsigned short *last_ptr;
    unsigned short *vector16;
    unsigned int *vector32;
    int stride32 = stride / 2;

    /* bytestream management */
    int index = 0;
    int mode_set = 0;
    int mode, mode_lo, mode_hi;
    int mode_count = 0;

    /* vectors */
    int mx, my;
    int motion_x, motion_y;
    unsigned char data_byte;

    mx = (signed char)(arg >> 8);
    my = (signed char)arg;

    if (roq->frame_index) {
        roq->frame_index = 0;
        this_frame = (unsigned short*)roq->frame[1];
        last_frame = (unsigned short*)roq->frame[0];
    }
    else {
        roq->frame_index = 1;
        this_frame = (unsigned short*)roq->frame[0];
        last_frame = (unsigned short*)roq->frame[1];
    }

    for (mb_y = 0; mb_y < roq->mb_height && status == TRUE; mb_y++) {
        line_offset = mb_y * 16 * stride;
        for (mb_x = 0; mb_x < roq->mb_width && status == TRUE; mb_x++) {
            mb_offset = line_offset + mb_x * 16;
            for (block = 0; block < 4 && status == TRUE; block++) {
                block_offset = mb_offset + (block / 2 * 8 * stride) + (block % 2 * 8);
                /* each 8x8 block gets a mode */
                GET_MODE();
                switch (mode) {
                case 0:  /* MOT: skip */
                    break;

                case 1:  /* FCC: motion compensation */
                    /* this needs to be done 16 bits at a time due to
                     * data alignment issues on the SH-4 */
                    GET_BYTE(data_byte);
                    motion_x = 8 - (data_byte >>  4) - mx;
                    motion_y = 8 - (data_byte & 0xF) - my;
                    last_ptr = last_frame + block_offset + 
                        (motion_y * stride) + motion_x;
                    this_ptr = this_frame + block_offset;
                    for (i = 0; i < 8; i++) {
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;

                        last_ptr += stride - 8;
                        this_ptr += stride - 8;
                    }
                    break;

                case 2:  /* SLD: upsample 4x4 vector */
                    GET_BYTE(data_byte);
                    vector16 = roq->cb4x4_rgb565[data_byte];
                    for (i = 0; i < 4*4; i++) {
                        this_ptr = this_frame + block_offset +
                            (i / 4 * 2 * stride) + (i % 4 * 2);
                        this_ptr[0] = *vector16;
                        this_ptr[1] = *vector16;
                        this_ptr[stride+0] = *vector16;
                        this_ptr[stride+1] = *vector16;
                        vector16++;
                    }
                    break;

                case 3:  /* CCC: subdivide into 4 subblocks */
                    for (subblock = 0; subblock < 4; subblock++) {
                        subblock_offset = block_offset + (subblock / 2 * 4 * stride) + (subblock % 2 * 4);

                        GET_MODE();
                        switch (mode)
                        {
                        case 0:  /* MOT: skip */
                            break;

                        case 1:  /* FCC: motion compensation */
                            GET_BYTE(data_byte);
                            motion_x = 8 - (data_byte >>  4) - mx;
                            motion_y = 8 - (data_byte & 0xF) - my;
                            last_ptr = last_frame + subblock_offset + 
                                (motion_y * stride) + motion_x;
                            this_ptr = this_frame + subblock_offset;
                            for (i = 0; i < 4; i++)
                            {
                                *this_ptr++ = *last_ptr++;
                                *this_ptr++ = *last_ptr++;
                                *this_ptr++ = *last_ptr++;
                                *this_ptr++ = *last_ptr++;

                                last_ptr += stride - 4;
                                this_ptr += stride - 4;
                            }
                            break;

                        case 2:  /* SLD: use 4x4 vector from codebook */
                            GET_BYTE(data_byte);
                            vector32 = (unsigned int*)roq->cb4x4_rgb565[data_byte];
                            this_ptr32 = (unsigned int*)this_frame;
                            this_ptr32 += subblock_offset / 2;
                            for (i = 0; i < 4; i++) {
                                *this_ptr32++ = *vector32++;
                                *this_ptr32++ = *vector32++;

                                this_ptr32 += stride32 - 2;
                            }
                            break;

                        case 3:  /* CCC: subdivide into 4 subblocks */
                            GET_BYTE(data_byte);
                            vector16 = roq->cb2x2_rgb565[data_byte];
                            this_ptr = this_frame + subblock_offset;
                            this_ptr[0] = vector16[0];
                            this_ptr[1] = vector16[1];
                            this_ptr[stride+0] = vector16[2];
                            this_ptr[stride+1] = vector16[3];

                            GET_BYTE(data_byte);
                            vector16 = roq->cb2x2_rgb565[data_byte];
                            this_ptr[2] = vector16[0];
                            this_ptr[3] = vector16[1];
                            this_ptr[stride+2] = vector16[2];
                            this_ptr[stride+3] = vector16[3];

                            this_ptr += stride * 2;

                            GET_BYTE(data_byte);
                            vector16 = roq->cb2x2_rgb565[data_byte];
                            this_ptr[0] = vector16[0];
                            this_ptr[1] = vector16[1];
                            this_ptr[stride+0] = vector16[2];
                            this_ptr[stride+1] = vector16[3];

                            GET_BYTE(data_byte);
                            vector16 = roq->cb2x2_rgb565[data_byte];
                            this_ptr[2] = vector16[0];
                            this_ptr[3] = vector16[1];
                            this_ptr[stride+2] = vector16[2];
                            this_ptr[stride+3] = vector16[3];

                            break;
                        }
                    }
                    break;
                }
            }
        }
    }

    /* sanity check to see if the stream was fully consumed */
    if (status == FALSE || (status == TRUE && index < size-2)) {
        return NULL;
    }

    return this_frame;
}