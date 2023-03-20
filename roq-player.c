
#include <kos/thread.h>
#include <dc/sound/stream.h>
#include <dc/pvr.h>
#include <arch/timer.h>

#include <stdlib.h>
#include <string.h>

#include "dreamroqlib.h"
#include "roq-player.h"

#define SND_STREAM_STATUS_NULL         0x00
#define SND_STREAM_STATUS_READY        0x01
#define SND_STREAM_STATUS_STREAMING    0x02
#define SND_STREAM_STATUS_PAUSING      0x03
#define SND_STREAM_STATUS_STOPPING     0x04
#define SND_STREAM_STATUS_RESUMING     0x05
#define SND_STREAM_STATUS_DONE         0x06
#define SND_STREAM_STATUS_ERROR        0x07

int player_errno = 0;

struct format_player_t {
    roq_t* format;
    int vol;
    int initialized_format;
};

#define STREAM_BUFFER_SIZE 1024*1024
typedef int snd_stream_hnd_t;

typedef struct {
    snd_stream_hnd_t shnd; 
    volatile int status;
    mutex_t decode_buffer_mut;
    int pcm_size;
    int initialized;
    unsigned int vol;
    unsigned int rate;
    unsigned int channels;
    unsigned char* decode_buffer;
    unsigned char pcm_buffer[65536+16384];
} sound_hndlr;

typedef struct {
    pvr_ptr_t textures[2];
    pvr_poly_hdr_t hdr[2];
    pvr_vertex_t vert[4];
    int current_frame;
    int initialized;
} video_hndlr;

static video_hndlr vid_stream;
static sound_hndlr snd_stream;

static void* player_snd_thread();
static void* aica_callback(snd_stream_hnd_t hnd, int req, int* done);

static int initialize_graphics(int width, int height);
static int initialize_audio();

// The blueprint of these callbacks can be different depending on the format
static void roq_video_cb(unsigned short *buf, int width, int height, int stride, int texture_height);
static void roq_audio_cb(unsigned char *buf, int size, int channels);

static float ms_per_frame = 33.3f;
static uint64_t last_time;
static uint64_t dc_get_time();
static void frame_delay();

int player_init() {
    snd_stream_init();
    pvr_init_defaults();

    // if(snd_stream_init() < 0)
	// 	return ERROR;

    // if(pvr_init_defaults() < 0)
    //      return ERROR;

    snd_stream.shnd = SND_STREAM_INVALID;
    snd_stream.vol = 240;
    snd_stream.rate = 22050;
    snd_stream.status = SND_STREAM_STATUS_NULL;

    if(thd_create(0, player_snd_thread, NULL) != NULL) {
		snd_stream.status = SND_STREAM_STATUS_READY;
        return SUCCESS;
	}
    else {
        snd_stream.status = SND_STREAM_STATUS_ERROR;
        return ERROR;
    }
}

void player_shutdown(format_player_t* format_player) {
    snd_stream.status = SND_STREAM_STATUS_DONE;

    if(snd_stream.shnd != SND_STREAM_INVALID) {
        snd_stream_stop(snd_stream.shnd);
        snd_stream_destroy(snd_stream.shnd);
        snd_stream.shnd = SND_STREAM_INVALID;
        snd_stream.vol = 240;
        snd_stream.status = SND_STREAM_STATUS_NULL;
    }

    if(vid_stream.initialized) {
        pvr_mem_free(vid_stream.textures[0]);
        pvr_mem_free(vid_stream.textures[1]);
    }

    if(snd_stream.initialized) {
        free(snd_stream.decode_buffer);
        mutex_destroy(&snd_stream.decode_buffer_mut); 
    }

    if(format_player != NULL && format_player->initialized_format)
        roq_destroy(format_player->format);
}

format_player_t* player_create(const char* filename, int loop) {
    snd_stream_hnd_t index;
    format_player_t* player = NULL;
    
    if(filename == NULL) {
        player_errno = SOURCE_ERROR;
        return NULL;
    }

    index = snd_stream_alloc(aica_callback, SND_STREAM_BUFFER_MAX/4);

    if(index == SND_STREAM_INVALID) {
        snd_stream_destroy(index);
        player_errno = SND_INIT_FAILURE;
        return NULL;
    }

    player = malloc(sizeof(format_player_t));
    if(!player) {
        snd_stream_destroy(index);
        player_errno = OUT_OF_MEMORY;
        return NULL;
    }

    player->format = roq_create_with_filename(filename);
    if(!player->format) {
        snd_stream_destroy(index);
        player_errno = FORMAT_INIT_FAILURE;
        return NULL;
    }

    roq_set_video_decode_callback(player->format, roq_video_cb);
    roq_set_audio_decode_callback(player->format, roq_audio_cb);
    roq_set_loop(player->format, loop);

    snd_stream.shnd = index;
    snd_stream.status = SND_STREAM_STATUS_READY;

    player->initialized_format = 1;

    initialize_graphics(roq_get_width(player->format), roq_get_height(player->format));
    initialize_audio();

    return player;
}

format_player_t* player_create_fd(FILE* file, int loop) {
    snd_stream_hnd_t index;
    format_player_t* player = NULL;

    if(file == NULL) {
        player_errno = SOURCE_ERROR;
        return NULL;
    }
    
    index = snd_stream_alloc(aica_callback, SND_STREAM_BUFFER_MAX/4);

    if(index == SND_STREAM_INVALID) {
        snd_stream_destroy(index);
        player_errno = SND_INIT_FAILURE;
        return NULL;
    }

    player = malloc(sizeof(format_player_t));
    if(!player) {
        snd_stream_destroy(index);
        player_errno = OUT_OF_MEMORY;
        return NULL;
    }

    player->format = roq_create_with_file(file, 1);
    if(!player->format) {
        snd_stream_destroy(index);
        player_errno = FORMAT_INIT_FAILURE;
        return NULL;
    }

    roq_set_video_decode_callback(player->format, roq_video_cb);
    roq_set_audio_decode_callback(player->format, roq_audio_cb);
    roq_set_loop(player->format, loop);

    snd_stream.shnd = index;
    snd_stream.status = SND_STREAM_STATUS_READY;

    player->initialized_format = 1;

    initialize_graphics(roq_get_width(player->format), roq_get_height(player->format));
    initialize_audio();

    return player;
}

format_player_t* player_create_buf(const unsigned char* buf, const unsigned int length, int loop) {
    snd_stream_hnd_t index;
    format_player_t* player = NULL;

    index = snd_stream_alloc(aica_callback, SND_STREAM_BUFFER_MAX/4);

    if(buf == NULL) {
        player_errno = SOURCE_ERROR;
        return NULL;
    }

    if(index == SND_STREAM_INVALID) {
        snd_stream_destroy(index);
        player_errno = SND_INIT_FAILURE;
        return NULL;
    }

    player = malloc(sizeof(format_player_t));
    if(!player) {
        snd_stream_destroy(index);
        player_errno = OUT_OF_MEMORY;
        return NULL;
    }

    player->format = roq_create_with_memory(buf, length, 1);
    if(!player->format) {
        snd_stream_destroy(index);
        player_errno = FORMAT_INIT_FAILURE;
        return NULL;
    }

    roq_set_video_decode_callback(player->format, roq_video_cb);
    roq_set_audio_decode_callback(player->format, roq_audio_cb);
    roq_set_loop(player->format, loop);

    snd_stream.shnd = index;
    snd_stream.status = SND_STREAM_STATUS_READY;

    player->initialized_format = 1;

    initialize_graphics(roq_get_width(player->format), roq_get_height(player->format));
    initialize_audio();

    return player;
}

// void player_decode(format_player_t* format_player) {
//    format_decode(format_player->format);
// }

void player_play(format_player_t* format_player, frame_callback frame_cb) {
    if(snd_stream.status == SND_STREAM_STATUS_STREAMING)
       return;

    snd_stream.status = SND_STREAM_STATUS_RESUMING;

    do {
        if(frame_cb)
            frame_cb();

        roq_decode(format_player->format);
    } while (!roq_has_ended(format_player->format) && 
            (snd_stream.status == SND_STREAM_STATUS_STREAMING ||
             snd_stream.status == SND_STREAM_STATUS_RESUMING));

    // if(format_has_ended(format_player->format))
    //     player_shutdown(format_player->format);
}

void player_pause(format_player_t* format_player) {
    if(snd_stream.status == SND_STREAM_STATUS_READY ||
       snd_stream.status == SND_STREAM_STATUS_PAUSING)
       return;
       
    snd_stream.status = SND_STREAM_STATUS_PAUSING;
}

void player_stop(format_player_t* format_player) {
    if(snd_stream.status == SND_STREAM_STATUS_READY ||
       snd_stream.status == SND_STREAM_STATUS_STOPPING)
       return;
       
    snd_stream.status = SND_STREAM_STATUS_STOPPING;
}

void player_volume(format_player_t* format_player, int vol) {
    if(snd_stream.shnd == SND_STREAM_INVALID)
        return;

    if(vol > 255)
        vol = 255;

    if(vol < 0)
        vol = 0;

    snd_stream.vol = format_player->vol = vol;
    snd_stream_volume(snd_stream.shnd, snd_stream.vol);
}

int player_isplaying(format_player_t* format_player) {
    return snd_stream.status == SND_STREAM_STATUS_STREAMING;
}

int player_get_loop(format_player_t* format_player) {
    return roq_get_loop(format_player->format);
}

void player_set_loop(format_player_t* format_player, int loop) {
    roq_set_loop(format_player->format, loop);
}

int player_has_ended(format_player_t* format_player) {
    return roq_has_ended(format_player->format);
}

static void roq_video_cb(unsigned short *texture_data, int width, int height, int stride, int texture_height) {
    unsigned short *buf = texture_data;

    /* send the video frame as a texture over to video RAM */
    pvr_txr_load(buf, vid_stream.textures[vid_stream.current_frame], stride * texture_height * 2);

    frame_delay();

    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);

    pvr_prim(&vid_stream.hdr[vid_stream.current_frame], sizeof(pvr_poly_hdr_t));
    pvr_prim(&vid_stream.vert[0], sizeof(pvr_vertex_t));
    pvr_prim(&vid_stream.vert[1], sizeof(pvr_vertex_t));
    pvr_prim(&vid_stream.vert[2], sizeof(pvr_vertex_t));
    pvr_prim(&vid_stream.vert[3], sizeof(pvr_vertex_t));

    pvr_list_finish();
    pvr_scene_finish();

    vid_stream.current_frame = !vid_stream.current_frame;
    last_time = dc_get_time();
}

static void roq_audio_cb(unsigned char *audio_data, int data_length, int channels) {

    snd_stream.channels = channels;

    /* Copy the decoded PCM samples to our local PCM buffer */
    mutex_lock(&snd_stream.decode_buffer_mut);         

    memcpy(snd_stream.decode_buffer + snd_stream.pcm_size, audio_data, data_length);
    snd_stream.pcm_size += data_length;

    mutex_unlock(&snd_stream.decode_buffer_mut);
}

// When we call snd_stream_poll(), it calls this callback
static void* aica_callback(snd_stream_hnd_t hnd, int bytes_needed, int* bytes_returning) {

    /* Wait for Format Decoder to produce enough samples */
    while(snd_stream.pcm_size < bytes_needed)
        thd_pass();

    /* Copy the Requested PCM Samples to the AICA Driver */         
    mutex_lock(&snd_stream.decode_buffer_mut);

    memcpy(snd_stream.pcm_buffer, snd_stream.decode_buffer, bytes_needed);
    snd_stream.pcm_size -= bytes_needed;
    memmove(snd_stream.decode_buffer, snd_stream.decode_buffer+bytes_needed, snd_stream.pcm_size);

    mutex_unlock(&snd_stream.decode_buffer_mut);

    *bytes_returning = bytes_needed;

    return snd_stream.pcm_buffer; /* Return the requested samples to the AICA driver */
}

static int initialize_graphics(int width, int height) 
{
    if(vid_stream.initialized)
        return SUCCESS;

    vid_stream.textures[0] = pvr_mem_malloc(width * height * 2);
    vid_stream.textures[1] = pvr_mem_malloc(width * height * 2);
    if (!vid_stream.textures[0] || !vid_stream.textures[1])
        return OUT_OF_VID_MEMORY;

    pvr_poly_cxt_t cxt;

    /* Precompile the poly headers */
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED, width, height, vid_stream.textures[0], PVR_FILTER_NONE);
    pvr_poly_compile(&vid_stream.hdr[0], &cxt);
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED, width, height, vid_stream.textures[1], PVR_FILTER_NONE);
    pvr_poly_compile(&vid_stream.hdr[1], &cxt);

    float ratio;
    /* screen coordinates of upper left and bottom right corners */
    int ul_x, ul_y, br_x, br_y;

    /* this only works if width ratio <= height ratio */
    ratio = 640.0 / width;
    ul_x = 0;
    br_x = (ratio * width);
    ul_y = ((480 - ratio * height) / 2);
    br_y = ul_y + ratio * height;

    /* Things common to vertices */
    vid_stream.vert[0].z     = vid_stream.vert[1].z     = vid_stream.vert[2].z     = vid_stream.vert[3].z     = 1.0f; 
    vid_stream.vert[0].argb  = vid_stream.vert[1].argb  = vid_stream.vert[2].argb  = vid_stream.vert[3].argb  = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);    
    vid_stream.vert[0].oargb = vid_stream.vert[1].oargb = vid_stream.vert[2].oargb = vid_stream.vert[3].oargb = 0;  
    vid_stream.vert[0].flags = vid_stream.vert[1].flags = vid_stream.vert[2].flags = PVR_CMD_VERTEX;         
    vid_stream.vert[3].flags = PVR_CMD_VERTEX_EOL; 

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

    last_time = dc_get_time();

    return SUCCESS;
}

static int initialize_audio() {
    if(snd_stream.initialized)
        return SUCCESS;

    /* allocate PCM buffer */
    snd_stream.decode_buffer = malloc(STREAM_BUFFER_SIZE);
    if(snd_stream.decode_buffer == NULL)
        return OUT_OF_MEMORY;
    
    /* Create a mutex to handle the double-threaded buffer */
    mutex_init(&snd_stream.decode_buffer_mut, MUTEX_TYPE_NORMAL);

    snd_stream.initialized = 1;
    
    return SUCCESS;
}

static void* player_snd_thread() {
    while(snd_stream.status != SND_STREAM_STATUS_DONE && snd_stream.status != SND_STREAM_STATUS_ERROR) {
        switch(snd_stream.status)
        {
            case SND_STREAM_STATUS_READY:
                //
                break;
            case SND_STREAM_STATUS_RESUMING:
                snd_stream_start(snd_stream.shnd, snd_stream.rate, snd_stream.channels-1);
                snd_stream.status = SND_STREAM_STATUS_STREAMING;
                break;
            case SND_STREAM_STATUS_PAUSING:
                snd_stream_stop(snd_stream.shnd);
                snd_stream.status = SND_STREAM_STATUS_READY;
                break;
            case SND_STREAM_STATUS_STOPPING:
                snd_stream_stop(snd_stream.shnd);
                snd_stream.status = SND_STREAM_STATUS_READY;
                break;
            case SND_STREAM_STATUS_STREAMING:
                snd_stream_poll(snd_stream.shnd);
                thd_sleep(20);
                break;
        }
    }

    return NULL;
}

uint64_t dc_get_time() {
    uint32_t s, ms;
	uint64_t msec;

	timer_ms_gettime(&s, &ms);
	msec = (((uint64)s) * ((uint64)1000)) + ((uint64)ms);

	return msec;
}

static void frame_delay() {
    uint64_t CPU_real_time = dc_get_time() - last_time;         
    while(CPU_real_time < ms_per_frame) {
        CPU_real_time = dc_get_time() - last_time;
        thd_pass();
    }
}
