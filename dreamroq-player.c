/*
 * Dreamroq by Mike Melanson
 *
 * This is the sample Dreamcast player app, designed to be run under
 * the KallistiOS operating system.
 */

#include "kos.h"
#include "dreamroqlib.h"
#include "profiler.h"

#define DC_CACHE_SIZE 16*1024 // DC cache size 16 KB

extern uint8 romdisk[];
KOS_INIT_ROMDISK(romdisk);

static pvr_ptr_t textures[2];
static pvr_poly_hdr_t hdr[2];
static pvr_vertex_t vert[4];
static int current_frame = 0;

static kthread_t * render_thread;        /* Video thread */
static volatile int render_thd=0;        /* Video thread status */ 

float min(float a, float b) {
    return (a < b) ? a : b;
}

void initialize_graphics(int width, int height) 
{
    pvr_poly_cxt_t cxt;
    
    float ratio;
    /* screen coordinates of upper left and bottom right corners */
    int ul_x, ul_y, br_x, br_y;

    textures[0] = pvr_mem_malloc(width * height * 2);
    textures[1] = pvr_mem_malloc(width * height * 2);
    if (!textures[0] || !textures[1]) {
        return;
    }

    /* Precompile the poly headers */
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED, width, height, textures[0], PVR_FILTER_NONE);
    pvr_poly_compile(&hdr[0], &cxt);
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED, width, height, textures[1], PVR_FILTER_NONE);
    pvr_poly_compile(&hdr[1], &cxt);

    /* this only works if width ratio <= height ratio */
    ratio = 640.0 / width;
    ul_x = 0;
    br_x = (ratio * width);
    ul_y = ((480 - ratio * height) / 2);
    br_y = ul_y + ratio * height;

    /* Things common to vertices */
    vert[0].z     = vert[1].z     = vert[2].z     = vert[3].z     = 1.0f; 
    vert[0].argb  = vert[1].argb  = vert[2].argb  = vert[3].argb  = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);    
    vert[0].oargb = vert[1].oargb = vert[2].oargb = vert[3].oargb = 0;  
    vert[0].flags = vert[1].flags = vert[2].flags = PVR_CMD_VERTEX;         
    vert[3].flags = PVR_CMD_VERTEX_EOL; 

    vert[0].x = ul_x;
    vert[0].y = ul_y;
    vert[0].u = 0.0;
    vert[0].v = 0.0;

    vert[1].x = br_x;
    vert[1].y = ul_y;
    vert[1].u = 1.0;
    vert[1].v = 0.0;

    vert[2].x = ul_x;
    vert[2].y = br_y;
    vert[2].u = 0.0;
    vert[2].v = 1.0;

    vert[3].x = br_x;
    vert[3].y = br_y;
    vert[3].u = 1.0;
    vert[3].v = 1.0;
}

static void* video_thd()
{ 
    render_thd=1;  /* Signal Thread is active */

    /* Match the Audio and Video Time Stamps */
    // VTS = ++frame / VIDEO_RATE;
    // while( ATS < VTS ) thd_pass();

    /* Draw the frame using the PVR */
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);

    //pvr_submit_poly(pvr_frame); 
    pvr_prim(&hdr[0], sizeof(pvr_poly_hdr_t));
    pvr_prim(&vert[0], sizeof(pvr_vertex_t));
    pvr_prim(&vert[1], sizeof(pvr_vertex_t));
    pvr_prim(&vert[2], sizeof(pvr_vertex_t));
    pvr_prim(&vert[3], sizeof(pvr_vertex_t));

    pvr_list_finish();       
    pvr_scene_finish(); 

    render_thd=0;   /* Signal Thread is finished */

    return NULL;
}

void video_callback(unsigned short* frame_data, int width, int height, int stride, int texture_height)
{
    unsigned short *buf = frame_data;

    /* send the video frame as a texture over to video RAM */
    pvr_txr_load(buf, textures[current_frame], stride * texture_height * 2);

    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);

    pvr_prim(&hdr[current_frame], sizeof(pvr_poly_hdr_t));
    pvr_prim(&vert[0], sizeof(pvr_vertex_t));
    pvr_prim(&vert[1], sizeof(pvr_vertex_t));
    pvr_prim(&vert[2], sizeof(pvr_vertex_t));
    pvr_prim(&vert[3], sizeof(pvr_vertex_t));

    pvr_list_finish();
    pvr_scene_finish();

    current_frame = !current_frame;

    /* Wait for last frame to finish render */
    // while(render_thd)
    //    thd_pass();

    // /* Current decoded frame */
    // //dcache_flush_range((unsigned int)frame_data, DC_CACHE_SIZE); /* Flush the SH4 Cache */
    // pvr_wait_ready();                  /* Wait for PVR to be in a ready state */
    // while (!pvr_dma_ready());          /* Wait for PVR DMA to finish transfer */
    
    // // pvr_dma_transfer( src,(unsigned int)pvr->vram_tex, 
    // //                   PVR_DMA_VRAM64, 1, NULL, 0 );
    // pvr_txr_load_dma(frame_data, textures[0], stride * texture_height * 2, 1,
    //                  NULL, 0);
       
    // /* Create a thread to render the current frame */   
    // render_thread = thd_create(0, video_thd, NULL);
}

void audio_callback(unsigned char *audio_frame_data, int size, int channels)
{

}

int controller_state(int button)
{
    maple_device_t *cont;
    cont_state_t *state;

    /* check controller state */
    cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if(!cont)
    {
        /* controller read error */
        return 1;
    }
    state = (cont_state_t *)maple_dev_status(cont);
    return state->buttons & button;
}

int main()
{
    vid_set_mode(DM_640x480_NTSC_IL, PM_RGB565);
    pvr_init_defaults();

    // Load a .mpg (MPEG Program Stream) file
    roq_t *roq = roq_create_with_filename("/rd/roguelogo.roq");

    // Install the video & audio decode callbacks
    roq_set_video_decode_callback(roq, video_callback);
    roq_set_audio_decode_callback(roq, audio_callback);
    roq_set_loop(roq, 1);

    initialize_graphics(roq_get_width(roq), roq_get_height(roq));

    printf("RoQ file plays at %d frames/sec\n", roq_get_framerate(roq));

    // Decode
    do {
        if(controller_state(CONT_START))
            break;

        if(controller_state(CONT_A))
            roq_set_loop(roq, 1);
	
        // Decode
        roq_decode(roq);
    } while (!roq_has_ended(roq));

    // if(audio_init)
    // {
    //   free( pcm_buf );
    //   pcm_buf = NULL;
    //   pcm_size = 0;
    //   samples_done = 0; 
    //   //mutex_destroy(&pcm_mut);                  /* Destroy the PCM mutex */
    //   snddrv_exit();                           /* Exit the AICA Driver */ 
    // }

    // if(graphics_initialized)
    // {               
    //     pvr_free( &roq_vram_ptr );             /* Free the PVR memory */
    // }

    // All done
    roq_destroy(roq);

    shutdownProfiling();

    pvr_shutdown(); 
    
    return 0;
}