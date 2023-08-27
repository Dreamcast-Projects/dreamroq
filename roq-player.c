/*
 * Roq-Player by Andress Barajas
 * 
 * This is the playback engine. Handles displaying video 
 * and playing audio that is decoded by the decoder engine.
 */

#include <kos/thread.h>
#include <dc/sound/stream.h>
#include <dc/pvr.h>
#include <arch/timer.h>
#include <arch/cache.h>

#include <stdlib.h>
#include <string.h>

#include "dreamroqlib.h"
#include "roq-player.h"

#define PLAYER_STATUS_NULL         0x00
#define PLAYER_STATUS_READY        0x01
#define PLAYER_STATUS_STREAMING    0x02
#define PLAYER_STATUS_PAUSING      0x03
#define PLAYER_STATUS_STOPPING     0x04
#define PLAYER_STATUS_RESUMING     0x05
#define PLAYER_STATUS_DONE         0x06
#define PLAYER_STATUS_ERROR        0x07

#define ROQ_SAMPLE_RATE    22050

int player_errno = 0;

struct roq_player_t {
    roq_t* decoder;
    int paused;
    int initialized_format;
};

typedef struct {
    unsigned char *buffer;
    int head;
    int tail;
    int size;
    int capacity;
} ring_buffer;

#define AUDIO_BUFFER_SIZE 1024*80
#define AUDIO_DECODE_BUFFER_SIZE 1024*1024
typedef int snd_stream_hnd_t;

typedef struct {
    int initialized;
    snd_stream_hnd_t shnd; 
    volatile int status;
    unsigned int vol;
    unsigned int rate;
    unsigned int channels;
    mutex_t decode_buffer_mut;
    ring_buffer decode_buffer;
    unsigned char pcm_buffer[AUDIO_BUFFER_SIZE];
} sound_hndlr;

// Target frame time in milliseconds
#define TARGET_FRAME_TIME    1000 / 30

typedef struct {
    int framerate;
    int initialized;
    int load_frame_index;
    int last_frame_time;
    volatile int status;
    semaphore_t frames_semaphore;
    semaphore_t load_semaphore;
    mutex_t decode_render_mut;
    int render_frame_index;
    int texture_byte_length;
    pvr_ptr_t textures[2];
    pvr_poly_hdr_t hdr[2];
    pvr_vertex_t vert[4];
} video_hndlr;

static void* player_snd_thread();
static void* aica_callback(snd_stream_hnd_t hnd, int req, int* done);

static void* player_rndr_thread();

static void roq_loop_cb(void* user_data);
static void roq_video_cb(unsigned short *buf, int width, int height, int stride, int texture_height, void* user_data);
static void roq_audio_cb(unsigned char *buf, int size, int channels, void* user_data);

static void initialize_defaults(roq_player_t* player, int index);
static int initialize_graphics(roq_player_t* player);
static int initialize_audio(int index);

static int ring_buffer_write(ring_buffer *rb, const unsigned char *data, int data_length);
static int ring_buffer_read(ring_buffer *rb, unsigned char *data, int data_length);
static int ring_buffer_underflow(ring_buffer *rb, int data_length);
static int ring_buffer_overflow(ring_buffer *rb, int data_length);

static unsigned int get_current_time();

static video_hndlr vid_stream;
static sound_hndlr snd_stream;

static kthread_t* audio_thread;

static kthread_t* render_thread;

static int playing_loop;

int player_init(void) {
    snd_stream_init();
    pvr_init_defaults();

    snd_stream.shnd = SND_STREAM_INVALID;
    snd_stream.vol = 240;
    snd_stream.rate = ROQ_SAMPLE_RATE;
    snd_stream.status = PLAYER_STATUS_NULL;

    audio_thread = thd_create(0, player_snd_thread, NULL);
    if(audio_thread != NULL) {
		snd_stream.status = PLAYER_STATUS_READY;
	}
    else {
        snd_stream.status = PLAYER_STATUS_ERROR;
        return PLAYER_ERROR;
    }

    vid_stream.status = PLAYER_STATUS_NULL;

    render_thread = thd_create(0, player_rndr_thread, NULL);
    if(render_thread != NULL) {
        vid_stream.status = PLAYER_STATUS_READY;
	}
    else {
        vid_stream.status = PLAYER_STATUS_ERROR;
        return PLAYER_ERROR;
    }

    return PLAYER_SUCCESS;
}

void player_shutdown(roq_player_t* player) {
    snd_stream.status = PLAYER_STATUS_DONE;
    vid_stream.status = PLAYER_STATUS_DONE;
    playing_loop = 0;

    thd_join(audio_thread, NULL);
    if(snd_stream.shnd != SND_STREAM_INVALID) {
        snd_stream_stop(snd_stream.shnd);
        snd_stream_destroy(snd_stream.shnd);
        snd_stream.shnd = SND_STREAM_INVALID;
        snd_stream.vol = 240;
        snd_stream.status = PLAYER_STATUS_NULL;
    }

    thd_join(render_thread, NULL);
    if(vid_stream.initialized) {
        vid_stream.initialized = 0;
        vid_stream.load_frame_index = 0;
        vid_stream.render_frame_index = 0;
        vid_stream.texture_byte_length = 0;
        pvr_mem_free(vid_stream.textures[0]);
        pvr_mem_free(vid_stream.textures[1]);
        mutex_destroy(&vid_stream.decode_render_mut);
        sem_destroy(&vid_stream.frames_semaphore);
        sem_destroy(&vid_stream.load_semaphore);
        vid_stream.status = PLAYER_STATUS_NULL;
    }

    if(snd_stream.initialized) {
        snd_stream.initialized = 0;
        free(snd_stream.decode_buffer.buffer);
        mutex_destroy(&snd_stream.decode_buffer_mut); 
    }

    if(player != NULL && player->initialized_format) {
        roq_destroy(player->decoder);
        free(player);
        player = NULL;
    }
}

roq_player_t* player_create(const char* filename) {
    snd_stream_hnd_t index;
    roq_player_t* player = NULL;
    
    if(filename == NULL) {
        player_errno = PLAYER_SOURCE_ERROR;
        return NULL;
    }

    index = snd_stream_alloc(aica_callback, SND_STREAM_BUFFER_MAX/4);

    if(index == SND_STREAM_INVALID) {
        snd_stream_destroy(index);
        player_errno = PLAYER_SND_INIT_FAILURE;
        return NULL;
    }

    player = malloc(sizeof(roq_player_t));
    if(!player) {
        snd_stream_destroy(index);
        player_errno = PLAYER_OUT_OF_MEMORY;
        return NULL;
    }

    player->decoder = roq_create_with_filename(filename);
    if(!player->decoder) {
        snd_stream_destroy(index);
        player_errno = PLAYER_FORMAT_INIT_FAILURE;
        return NULL;
    }

    initialize_defaults(player, index);

    return player;
}

roq_player_t* player_create_file(FILE* file) {
    snd_stream_hnd_t index;
    roq_player_t* player = NULL;

    if(file == NULL) {
        player_errno = PLAYER_SOURCE_ERROR;
        return NULL;
    }
    
    index = snd_stream_alloc(aica_callback, SND_STREAM_BUFFER_MAX/4);

    if(index == SND_STREAM_INVALID) {
        snd_stream_destroy(index);
        player_errno = PLAYER_SND_INIT_FAILURE;
        return NULL;
    }

    player = malloc(sizeof(roq_player_t));
    if(!player) {
        snd_stream_destroy(index);
        player_errno = PLAYER_OUT_OF_MEMORY;
        return NULL;
    }

    player->decoder = roq_create_with_file(file, 1);
    if(!player->decoder) {
        snd_stream_destroy(index);
        player_errno = PLAYER_FORMAT_INIT_FAILURE;
        return NULL;
    }

    initialize_defaults(player, index);

    return player;
}

roq_player_t* player_create_memory(unsigned char* memory, const unsigned int length) {
    snd_stream_hnd_t index;
    roq_player_t* player = NULL;

    index = snd_stream_alloc(aica_callback, SND_STREAM_BUFFER_MAX/4);

    if(memory == NULL) {
        player_errno = PLAYER_SOURCE_ERROR;
        return NULL;
    }

    if(index == SND_STREAM_INVALID) {
        snd_stream_destroy(index);
        player_errno = PLAYER_SND_INIT_FAILURE;
        return NULL;
    }

    player = malloc(sizeof(roq_player_t));
    if(!player) {
        snd_stream_destroy(index);
        player_errno = PLAYER_OUT_OF_MEMORY;
        return NULL;
    }

    player->decoder = roq_create_with_memory(memory, length, 1);
    if(!player->decoder) {
        snd_stream_destroy(index);
        player_errno = PLAYER_FORMAT_INIT_FAILURE;
        return NULL;
    }

    initialize_defaults(player, index);

    return player;
}

void player_play(roq_player_t* player, frame_callback frame_cb) {
    if(snd_stream.status == PLAYER_STATUS_STREAMING ||
       vid_stream.status == PLAYER_STATUS_STREAMING)
       return;

    player->paused = 0;
    snd_stream.status = PLAYER_STATUS_RESUMING;
    vid_stream.status = PLAYER_STATUS_RESUMING;

    // Protect against recursion bc we can call player_play() in
    // frame_cb()
    if(!playing_loop)
    {
        playing_loop = 1;

        do {
            if(frame_cb)
                frame_cb();

            // We shutdown the player, exit the loop
            if(snd_stream.status == PLAYER_STATUS_NULL) {
                break;
            }

            if(!player->paused)
                roq_decode(player->decoder);
        } while (!roq_has_ended(player->decoder));
    }
}

void player_pause(roq_player_t* player) {
    player->paused = 1;
    if(snd_stream.status != PLAYER_STATUS_READY &&
       snd_stream.status != PLAYER_STATUS_PAUSING)
       snd_stream.status = PLAYER_STATUS_PAUSING;

    if(vid_stream.status != PLAYER_STATUS_READY &&
       vid_stream.status != PLAYER_STATUS_PAUSING)
       vid_stream.status = PLAYER_STATUS_PAUSING;
}

void player_stop(roq_player_t* player) {
    player->paused = 1;
    roq_rewind(player->decoder);

    if(snd_stream.status != PLAYER_STATUS_READY &&
       snd_stream.status != PLAYER_STATUS_STOPPING)
       snd_stream.status = PLAYER_STATUS_STOPPING;

    if(vid_stream.status != PLAYER_STATUS_READY &&
       vid_stream.status != PLAYER_STATUS_STOPPING)
       vid_stream.status = PLAYER_STATUS_STOPPING;
}

void player_volume(roq_player_t* player, int vol) {
    if(snd_stream.shnd == SND_STREAM_INVALID)
        return;

    if(vol > 255)
        vol = 255;

    if(vol < 0)
        vol = 0;

    snd_stream.vol = vol;
    snd_stream_volume(snd_stream.shnd, snd_stream.vol);
}

int player_isplaying(roq_player_t* player) {
    return snd_stream.status == PLAYER_STATUS_STREAMING && vid_stream.status == PLAYER_STATUS_STREAMING;
}

int player_get_loop(roq_player_t* player) {
    return roq_get_loop(player->decoder);
}

void player_set_loop(roq_player_t* player, int loop) {
    roq_set_loop(player->decoder, loop, roq_loop_cb);
}

int player_has_ended(roq_player_t* player) {
    return roq_has_ended(player->decoder);
}

static void roq_loop_cb(void* user_data) {
}

// The video decoder calls this to load the decoded video frame into video memory
static void roq_video_cb(unsigned short *texture_data, int width, int height, int stride, int texture_height, void* user_data) {

    // Wait for a slot to be available for loading (2 available)
    sem_wait(&vid_stream.load_semaphore);

    mutex_lock(&vid_stream.decode_render_mut);
    // DMA causes artifacts
    // dcache_flush_range((uint32)texture_data, vid_stream.texture_byte_length);   // dcache flush is needed when using DMA
    // pvr_txr_load_dma(texture_data, vid_stream.textures[vid_stream.load_frame_index], vid_stream.texture_byte_length, 1, NULL, 0);
    pvr_txr_load(texture_data, vid_stream.textures[vid_stream.load_frame_index], stride * texture_height * 2);

    mutex_unlock(&vid_stream.decode_render_mut);

    vid_stream.load_frame_index = !vid_stream.load_frame_index;
    
    // Let render thread know that we have some frames that need rendering
    sem_signal(&vid_stream.frames_semaphore);
}

static void roq_audio_cb(unsigned char *audio_data, int data_length, int channels, void* user_data) {
    snd_stream.channels = channels;

    if(ring_buffer_overflow(&snd_stream.decode_buffer, data_length))
        thd_pass();

    mutex_lock(&snd_stream.decode_buffer_mut);

    ring_buffer_write(&snd_stream.decode_buffer, audio_data, data_length);

    mutex_unlock(&snd_stream.decode_buffer_mut);
}

static void* aica_callback(snd_stream_hnd_t hnd, int bytes_needed, int* bytes_returning) {

    if(ring_buffer_underflow(&snd_stream.decode_buffer, bytes_needed))
        thd_pass();

    mutex_lock(&snd_stream.decode_buffer_mut);

    ring_buffer_read(&snd_stream.decode_buffer, snd_stream.pcm_buffer, bytes_needed);

    mutex_unlock(&snd_stream.decode_buffer_mut);

    *bytes_returning = bytes_needed;

    return snd_stream.pcm_buffer;
}

static void initialize_defaults(roq_player_t* player, int index) {
    roq_set_video_decode_callback(player->decoder, roq_video_cb);
    roq_set_audio_decode_callback(player->decoder, roq_audio_cb);

    player->initialized_format = 1;

    initialize_graphics(player);
    initialize_audio(index);
}

static int initialize_graphics(roq_player_t* player) {
    if(vid_stream.initialized)
        return PLAYER_SUCCESS;

    int width = roq_get_width(player->decoder);
    int height = roq_get_height(player->decoder);
    vid_stream.texture_byte_length = width * height * 2;
    vid_stream.last_frame_time = 0;
    vid_stream.framerate = roq_get_framerate(player->decoder);
    vid_stream.textures[0] = pvr_mem_malloc(vid_stream.texture_byte_length);
    vid_stream.textures[1] = pvr_mem_malloc(vid_stream.texture_byte_length);
    if (!vid_stream.textures[0] || !vid_stream.textures[1])
        return PLAYER_OUT_OF_VID_MEMORY;

    sem_init(&vid_stream.frames_semaphore, 0); // No frames available initially
    sem_init(&vid_stream.load_semaphore, 2);   // Allow up to 2 frames to be loaded
    mutex_init(&vid_stream.decode_render_mut, MUTEX_TYPE_NORMAL);

    pvr_poly_cxt_t cxt;

    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED, width, height, vid_stream.textures[0], PVR_FILTER_NONE);// PVR_FILTER_BILINEAR); //PVR_FILTER_NONE
    pvr_poly_compile(&vid_stream.hdr[0], &cxt);
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED, width, height, vid_stream.textures[1], PVR_FILTER_NONE); //PVR_FILTER_BILINEAR); //PVR_FILTER_NONE
    pvr_poly_compile(&vid_stream.hdr[1], &cxt);
    
    vid_stream.vert[0].z     = vid_stream.vert[1].z     = vid_stream.vert[2].z     = vid_stream.vert[3].z     = 1.0f; 
    vid_stream.vert[0].argb  = vid_stream.vert[1].argb  = vid_stream.vert[2].argb  = vid_stream.vert[3].argb  = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);    
    vid_stream.vert[0].oargb = vid_stream.vert[1].oargb = vid_stream.vert[2].oargb = vid_stream.vert[3].oargb = 0;  
    vid_stream.vert[0].flags = vid_stream.vert[1].flags = vid_stream.vert[2].flags = PVR_CMD_VERTEX;         
    vid_stream.vert[3].flags = PVR_CMD_VERTEX_EOL;

    float ratio;
    int ul_x, ul_y, br_x, br_y;

    ratio = 640.0 / width;
    ul_x = 0;
    br_x = (ratio * width);
    ul_y = ((480 - ratio * height) / 2);
    br_y = ul_y + ratio * height;

    vid_stream.vert[0].x = ul_x;
    vid_stream.vert[0].y = ul_y;
    vid_stream.vert[0].u = 0.0;
    vid_stream.vert[0].v = 0.0;

    vid_stream.vert[1].x = br_x;
    vid_stream.vert[1].y = ul_y;
    vid_stream.vert[1].u = 1.0;
    vid_stream.vert[1].v = 0.0;

    vid_stream.vert[2].x = ul_x;
    vid_stream.vert[2].y = br_y;
    vid_stream.vert[2].u = 0.0;
    vid_stream.vert[2].v = 1.0;

    vid_stream.vert[3].x = br_x;
    vid_stream.vert[3].y = br_y;
    vid_stream.vert[3].u = 1.0;
    vid_stream.vert[3].v = 1.0;

    vid_stream.initialized = 1;

    return PLAYER_SUCCESS;
}

static int initialize_audio(int index) {
    if(snd_stream.initialized)
        return PLAYER_SUCCESS;

    snd_stream.shnd = index;
    snd_stream.status = PLAYER_STATUS_READY;
    snd_stream.decode_buffer.head = 0;
    snd_stream.decode_buffer.tail = 0;
    snd_stream.decode_buffer.size = 0;
    snd_stream.decode_buffer.capacity = AUDIO_DECODE_BUFFER_SIZE;
    snd_stream.decode_buffer.buffer = malloc(AUDIO_DECODE_BUFFER_SIZE);
    if(snd_stream.decode_buffer.buffer == NULL)
        return PLAYER_OUT_OF_MEMORY;
    
    mutex_init(&snd_stream.decode_buffer_mut, MUTEX_TYPE_NORMAL);

    snd_stream.initialized = 1;
    
    return PLAYER_SUCCESS;
}

static void* player_snd_thread() {
    while(snd_stream.status != PLAYER_STATUS_DONE && snd_stream.status != PLAYER_STATUS_ERROR) {
        switch(snd_stream.status)
        {
            case PLAYER_STATUS_READY:
                break;
            case PLAYER_STATUS_RESUMING:
                snd_stream_start(snd_stream.shnd, snd_stream.rate, snd_stream.channels-1);
                snd_stream.status = PLAYER_STATUS_STREAMING;
                break;
            case PLAYER_STATUS_PAUSING:
                snd_stream_stop(snd_stream.shnd);
                snd_stream.status = PLAYER_STATUS_READY;
                break;
            case PLAYER_STATUS_STOPPING:
                snd_stream_stop(snd_stream.shnd);
                snd_stream.decode_buffer.head = 0;
                snd_stream.decode_buffer.tail = 0;
                snd_stream.decode_buffer.size = 0;
                snd_stream.status = PLAYER_STATUS_READY;
                break;
            case PLAYER_STATUS_STREAMING:
                snd_stream_poll(snd_stream.shnd);
                thd_sleep(20);
                break;
        }
    }

    return NULL;
}

static void* player_rndr_thread() {

    while(vid_stream.status != PLAYER_STATUS_DONE && vid_stream.status != PLAYER_STATUS_ERROR) {
        switch(vid_stream.status)
        {
            case PLAYER_STATUS_READY:
                break;
            case PLAYER_STATUS_RESUMING:
                //snd_stream_start(snd_stream.shnd, snd_stream.rate, snd_stream.channels-1);
                vid_stream.status = PLAYER_STATUS_STREAMING;
                break;
            case PLAYER_STATUS_PAUSING:
                //snd_stream_stop(snd_stream.shnd);
                snd_stream.status = PLAYER_STATUS_READY;
                break;
            case PLAYER_STATUS_STOPPING:
                // snd_stream_stop(snd_stream.shnd);
                // snd_stream.decode_buffer.head = 0;
                // snd_stream.decode_buffer.tail = 0;
                // snd_stream.decode_buffer.size = 0;
                vid_stream.status = PLAYER_STATUS_READY;
                break;
            case PLAYER_STATUS_STREAMING:
                // Wait for frames to be ready
                sem_wait(&vid_stream.frames_semaphore);
                
                unsigned int elapsed_time = get_current_time() - vid_stream.last_frame_time; // Calculate elapsed time since last frame ended
                
                if (elapsed_time < TARGET_FRAME_TIME) {
                    thd_sleep(TARGET_FRAME_TIME - elapsed_time);
                }

                mutex_lock(&vid_stream.decode_render_mut);

                pvr_wait_ready();
                pvr_scene_begin();
                pvr_list_begin(PVR_LIST_OP_POLY);

                pvr_prim(&vid_stream.hdr[vid_stream.render_frame_index], sizeof(pvr_poly_hdr_t));
                pvr_prim(&vid_stream.vert[0], sizeof(pvr_vertex_t));
                pvr_prim(&vid_stream.vert[1], sizeof(pvr_vertex_t));
                pvr_prim(&vid_stream.vert[2], sizeof(pvr_vertex_t));
                pvr_prim(&vid_stream.vert[3], sizeof(pvr_vertex_t));

                pvr_list_finish();
                pvr_scene_finish();

                mutex_unlock(&vid_stream.decode_render_mut);
                
                vid_stream.render_frame_index = !vid_stream.render_frame_index;

                vid_stream.last_frame_time = get_current_time(); // Get time the frame ended
                
                // Signal that we rendered a frame and can load another one
                sem_signal(&vid_stream.load_semaphore);
                break;
        }
    }

    return NULL;
}

static int ring_buffer_write(ring_buffer *rb, const unsigned char *data, int data_length) {
    if (data_length > rb->capacity - rb->size) {
        return 0;
    }

    for (int i = 0; i < data_length; ++i) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->capacity;
    }

    rb->size += data_length;
    return 1;
}

static int ring_buffer_read(ring_buffer *rb, unsigned char *data, int data_length) {
    if (data_length > rb->size) {
        return 0;
    }

    for (int i = 0; i < data_length; ++i) {
        data[i] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->capacity;
    }

    rb->size -= data_length;
    return 1;
}

static int ring_buffer_underflow(ring_buffer *rb, int data_length) {
    if (data_length > rb->size) {
        return 1;
    }

    return 0;
}

static int ring_buffer_overflow(ring_buffer *rb, int data_length) {
    if (data_length > rb->capacity - rb->size) {
        return 1;
    }

    return 0;
}

static unsigned int get_current_time() {
    uint32_t s, ms;
    uint64_t msec;

    timer_ms_gettime(&s, &ms);
    msec = (((uint64_t)s) * ((uint64_t)1000)) + ((uint64_t)ms);

    return (unsigned int)msec;
}
