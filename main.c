#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

//#include "profiler.h"
#include "roq-player.h"

extern uint8 romdisk[];
KOS_INIT_ROMDISK(romdisk);

static void frame_cb() {
    maple_device_t *dev;
    cont_state_t *state;

    dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);

    if(dev) {
        state = (cont_state_t *)maple_dev_status(dev);

        if(state)   {
            if(state->buttons) {
                //shutdownProfiling();
                arch_exit();
            }
                
        }
    }
}

int main()
{
    vid_set_mode(DM_640x480_NTSC_IL, PM_RGB565);

    player_init();
    format_player_t* player = player_create("/rd/roguelogo.roq");
    //player_set_loop(player, 0);
    player_play(player, frame_cb);

    player_shutdown(player);

    //shutdownProfiling();
    
    // Decode
    // do {
    //     if(controller_state(CONT_START))
    //         break;

    //     if(controller_state(CONT_A))
    //         ;
	
    //     // Decode
    //     player_decode(player);
    // } while (!player_has_ended(player));


    return 0;
}