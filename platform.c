/*
Future notes on things to do:
- Limit air dashes before touching the ground
- Add an option for wall jump that only allows alternating walls.
- Currently, dashing through walls checks the potential end point, and if it isn't clear then it continues with the normal dash routine. 
    The result is that there could be a valid landing point across a wall, but the player is just a little too close for it to register. 
    I could create a 'look-back' loop that runs through the intervening tiles until it finds an empty landing spot.
- The bounce event is a funny one, because it can have the player going up without being in the jump state. I should perhaps add some error catching stuff for such situations
- Can I have a wall_jump init ahead of the normal jump init? If it's just checking a few more boxes....
- Improve ladder situation: jump from ladder option, bug with hitting the bottom of ladders, other stuff?
- Add check for camera bounds on Dash Init
- Solid actors have a fault: they only check collisions once on enter. This is especially problematic because of how GBStudio does invincibility frames,
  because it means the player can attach to a platform without causing the hit trigger to run. 
     - 2 potential solutions: 
     - give the player a minimum velocity every frame, forcing a re-collision. This might break the moving platform code though
     - manually re-trigger the collision call if the actor is attached.
TARGETS for Optimization
- State script assignment could be 100% be re-written to avoid all those assignments and directly use the pointers. I am not canny enough to do that.
- I should be able to combine solid actors and platform actors into a single check...
- It's inelegant that the dash check requires me to check again later if it succeeded or not. Can I reorganize this somehow?
- I think I can probably combine actor_attached and last_actor
- I need to refactor the downwards collision for Y, it's a bit of a mess at this point. I just can't wrap my head around it atm
- Wall Slide could be optimized to skip the acceleration bit, as the only thing that matters is tapping away
THINGS TO WATCH
- Does every state (that needs to) end up resetting DeltaX?
NOTES on GBStudio Quirks
- 256 velocities per position, 16 'positions' per pixel, 8 pixels per tile
- Player bounds: for an ordinary 16x16 sprite, bounds.left starts at 0 and bounds.right starts at 16. If it's smaller, bounds.left is POSITIVE
    For bounds.top, however, Y starts counting from the middle of a sprite. bounds.top is negative and bounds.bottom is positive
- CameraX is in the middle of the screen, not left corner
GENERAL STRUCTURE OF THIS FILE
The old format was well structured as a state-machine, isolating all the components into states. Unfortunately, it seems like the overhead of calling 
collision functions on the GameBoy makes this model unperformant. However, I'm also limited to the total amount of code that can be placed in a single bank. 
I cannot get rid of the functions and move the code into the file itself. New structure is a compromise that uses goto commands to skip some sections that are
shared by most of the states. 
INIT()
    Tweak a few fields so they don't overflow variables
    Normalize some fields so that they are applied over multiple frames
    Initialize variables
UPDATE()
    A. Input Checks (Double-tap Dash, Drop-Through)    I'm considering moving the drop-through check into a state
    B. STATE MACHINE 1 SWITCH: Falling, Ground, Jumping, Dashing, Climbing a Ladder, Wall Sliding
        State Initialization
        Calculate Change in Vertical Movement
        Some calculate horizontal movement
        Some calculate collisions
    C. Shared Collision
        Acceleration Code   
        Basic X Collision           gotoXCol
        Basic Y Collision           gotoYCol
        Actor Collision Check       gotoActorCol
    D. STATE MACHINE 2 SWITCH:      gotoSwitch2
        Animation
        State Change Logic
        Some Counters
    E. Trigger Check                gotoTriggerCol
    G. Tic Counters                 gotoCounters
BUGS:
 - When the player is on a moving platform and is hit by another one, they get caught mid-way on the next one.
*/
#pragma bank 3
#include <string.h>
#include "data/states_defines.h"
#include "states/platform.h"
#include "actor.h"
#include "camera.h"
#include "collision.h"
#include "data_manager.h"
#include "game_time.h"
#include "input.h"
#include "math.h"
#include "scroll.h"
#include "trigger.h"
#include "vm.h"
#include "bankdata.h"
#include "music_manager.h"

#ifndef INPUT_PLATFORM_JUMP
#define INPUT_PLATFORM_JUMP        INPUT_A
#endif
#ifndef INPUT_PLATFORM_RUN
#define INPUT_PLATFORM_RUN         INPUT_B
#endif
#ifndef INPUT_PLATFORM_INTERACT
#define INPUT_PLATFORM_INTERACT    INPUT_A
#endif
#ifndef PLATFORM_CAMERA_DEADZONE_Y
#define PLATFORM_CAMERA_DEADZONE_Y 16
#endif

// Constantes para el sistema de salud y daño
#define SONIDO_MUERTE 0
#define ANIM_HURT 1
#define ANIM_DEATH 2

// Índices de variables (ajusta según tu proyecto)
#define VAR_HP 0
#define VAR_ZONA_DAÑO 12

// Grupos de colisión para enemigos (ajustados para GB Studio)
#define GRUPO_ENEMIGO_MAPACHE 2   // Enemigos tipo mapache
#define GRUPO_ENEMIGO_TRIGGER 3   // Enemigos tipo trigger  
#define GRUPO_ENEMIGO_JEFE 4      // Jefes con más daño

//TEST
script_state_t state_events[21];

//DEFAULT ENGINE VARIABLES
WORD plat_min_vel = 0;
WORD plat_walk_vel = 2000;
WORD plat_run_vel = 4000;
WORD plat_climb_vel = 2000;
WORD plat_walk_acc = 200;
WORD plat_run_acc = 400;
WORD plat_dec = 300;
WORD plat_jump_vel = 6000;
WORD plat_grav = 1500;
WORD plat_hold_grav = 1000;
WORD plat_max_fall_vel = 12000;

//PLATFORMER PLUS ENGINE VARIABLES
BYTE plat_camera_deadzone_x = 16;
UBYTE plat_camera_block = 0;
UBYTE plat_drop_through = 1;
UBYTE plat_mp_group = 1;
UBYTE plat_solid_group = 2;
WORD plat_jump_min = 2000;
UBYTE plat_hold_jump_max = 15;
UBYTE plat_extra_jumps = 1;
WORD plat_jump_reduction = 1000;
UBYTE plat_coyote_max = 5;
UBYTE plat_buffer_max = 5;
UBYTE plat_wall_jump_max = 1;
UBYTE plat_wall_slide = 1;
WORD plat_wall_grav = 2000;
WORD plat_wall_kick = 3000;
UBYTE plat_float_input = 1;
WORD plat_float_grav = 2000;
UBYTE plat_air_control = 1;
UBYTE plat_turn_control = 1;
WORD plat_air_dec = 200;
UBYTE plat_run_type = 2;
WORD plat_turn_acc = 300;
UBYTE plat_run_boost = 0;
UBYTE plat_dash = 0;
UBYTE plat_dash_style = 0;
UBYTE plat_dash_momentum = 0;
UBYTE plat_dash_through = 0;
WORD plat_dash_dist = 4000;
UBYTE plat_dash_frames = 10;
UBYTE plat_dash_ready_max = 30;
UBYTE plat_dash_deadzone = 32;

// NUEVO SISTEMA DE SALUD Y DAÑO
// Variables globales para el sistema de salud
UINT8 jugador_salud = 4;
UINT8 jugador_salud_max = 4;
UINT8 jugador_inmune = 0;
UINT8 jugador_dolor = 0;
UINT8 jugador_muerto = 0;
UINT8 jugador_parpadeo = 0;
actor_t* hud_corazon = NULL;

// Constantes para el sistema de daño
#define INMUNIDAD_MAPACHE 60    // 1 segundo
#define DOLOR_MAPACHE 20        // 0.33 segundos
#define INMUNIDAD_TRIGGER 60    // 1 segundo
#define DOLOR_TRIGGER 15        // 0.25 segundos
#define TEMBLOR_CAMARA_MAPACHE 20
#define TEMBLOR_CAMARA_TRIGGER 15
#define PARPADEO_FRAMES 4       // Parpadear cada 4 frames

// Variables temporales para el sistema de salud
UINT8 game_over_timer = 0;
UINT8 camera_shake_timer_local = 0;
UINT8 camera_shake_intensity = 0;

// Función simple de random para evitar dependencias
UINT8 simple_rand_seed = 123;
UINT8 simple_rand(void) {
    simple_rand_seed = (simple_rand_seed * 73 + 17) % 251;
    return simple_rand_seed;
}

enum pStates {
    FALL_INIT = 0,
    FALL_STATE,
    FALL_END,
    GROUND_INIT,
    GROUND_STATE,
    GROUND_END,
    JUMP_INIT,
    JUMP_STATE,
    JUMP_END,
    DASH_INIT,
    DASH_STATE,
    DASH_END,
    LADDER_INIT,
    LADDER_STATE,
    LADDER_END,
    WALL_INIT,
    WALL_STATE,
    WALL_END,
    KNOCKBACK_INIT,
    KNOCKBACK_STATE,
    BLANK_INIT,
    BLANK_STATE
};

enum pStates plat_state;
enum pStates que_state;

UBYTE nocontrol_h;
UBYTE nocollide;
UBYTE flutter_frame;
WORD deltaX;
WORD deltaY;

//COUNTER variables
UBYTE ct_val;
UBYTE jb_val;
UBYTE wc_val;
UBYTE hold_jump_val;
UBYTE dj_val;
UBYTE wj_val;

//WALL variables
BYTE last_wall;
BYTE col;

//DASH VARIABLES
UBYTE dash_ready_val;
WORD dash_dist;
UBYTE dash_currentframe;
BYTE tap_val;
UBYTE dash_end_clear;

//COLLISION VARS
actor_t *last_actor;
UBYTE actor_attached;
WORD mp_last_x;
WORD mp_last_y;

//JUMPING VARIABLES
WORD jump_reduction_val;
WORD jump_per_frame;
WORD jump_reduction;
WORD boost_val;

//WALKING AND RUNNING VARIABLES
WORD pl_vel_x;
WORD pl_vel_y;

//VARIABLES FOR CAMERAS
WORD *edge_left;
WORD *edge_right;
WORD mod_image_right;
WORD mod_image_left;

//VARIABLES FOR EVENT PLUGINS
BYTE run_stage;
UBYTE jump_type;

// Funciones del nuevo sistema de salud
void inicializar_sistema_salud(void) BANKED;
void actualizar_sistema_daño(void) BANKED;
void dano_mapache(void) BANKED;
void dano_trigger(void) BANKED;
void jugador_muere(void) BANKED;
void ir_a_game_over(void) BANKED;
void actualizar_hud_salud(void) BANKED;
void aplicar_retroceso_mapache(void) BANKED;
void aplicar_retroceso_trigger(void) BANKED;
void activar_temblor_camara(UINT8 intensidad) BANKED;
void establecer_animacion_jugador(UINT8 anim_id) BANKED;
void reproducir_sonido(UINT8 sonido) BANKED;
void aplicar_dano_jugador(UINT8 cantidad, UINT8 tipo) BANKED;
void actualizar_parpadeo_invulnerabilidad(void) BANKED;
void procesar_salto_sobre_enemigo(actor_t* enemigo) BANKED;

// Declaración de variables globales
extern UBYTE game_time;

void platform_init(void) BANKED {
    //Initialize Camera
    camera_offset_x = 0;
    camera_offset_y = 0;
    camera_deadzone_x = plat_camera_deadzone_x;
    camera_deadzone_y = PLATFORM_CAMERA_DEADZONE_Y;
    
    if ((camera_settings & CAMERA_LOCK_X_FLAG)){
        camera_x = (PLAYER.pos.x >> 4) + 8;
    } else{
        camera_x = 0;
    }
    if ((camera_settings & CAMERA_LOCK_Y_FLAG)){
        camera_y = (PLAYER.pos.y >> 4) + 8;
    } else{
        camera_y = 0;
    }
    
    //Initialize Camera Bounds
    mod_image_right = image_width - SCREEN_WIDTH;
    mod_image_left = 0;
    if (plat_camera_block & 1){
        edge_left = &scroll_x;
    }
    else{
        edge_left = &mod_image_left;
    }
    if (plat_camera_block & 2){
        edge_right = &scroll_x;
    }
    else{
        edge_right = &image_width;
    }
    
    //Make sure jumping doesn't overflow variables
    while (32000 - (plat_jump_vel/MIN(15,plat_hold_jump_max)) - plat_jump_min < 0){
        plat_hold_jump_max += 1;
    }
    if (plat_run_boost != 0){
        while((32000/plat_run_boost) < ((plat_run_vel>>8)/plat_hold_jump_max)){
            plat_run_boost--;
        }
    }
    
    //Normalize variables by number of frames
    jump_per_frame = plat_jump_vel / MIN(15, plat_hold_jump_max);
    jump_reduction = plat_jump_reduction / plat_hold_jump_max;
    dash_dist = plat_dash_dist / plat_dash_frames;
    boost_val = plat_run_boost / plat_hold_jump_max;
    
    //Initialize State
    plat_state = GROUND_STATE;
    que_state = GROUND_STATE;
    actor_attached = FALSE;
    run_stage = 0;
    nocontrol_h = 0;
    nocollide = 0;
    flutter_frame = 0;
    
    if (PLAYER.dir == DIR_UP || PLAYER.dir == DIR_DOWN || PLAYER.dir == DIR_NONE) {
        PLAYER.dir = DIR_RIGHT;
    }
    
    //Initialize other vars
    game_time = 0;
    pl_vel_x = 0;
    pl_vel_y = 4000;
    last_wall = 0;
    hold_jump_val = plat_hold_jump_max;
    dj_val = 0;
    wj_val = plat_wall_jump_max;
    dash_end_clear = FALSE;
    jump_type = 0;
    deltaX = 0;
    deltaY = 0;
    
    // Inicializar sistema de salud
    inicializar_sistema_salud();
}

void platform_update(void) BANKED {
    // NUEVA VERIFICACIÓN PARA CAMBIO DE ESCENA Y ESTADO DEL SUELO
    UBYTE tile_start = (((PLAYER.pos.x >> 4) + PLAYER.bounds.left)  >> 3);
    UBYTE tile_end   = (((PLAYER.pos.x >> 4) + PLAYER.bounds.right) >> 3) + 1;
    UBYTE tile_y = (((PLAYER.pos.y >> 4) + PLAYER.bounds.bottom) >> 3);
    UBYTE on_ground = FALSE;
    
    while (tile_start != tile_end) {
        if (tile_at(tile_start, tile_y) & COLLISION_TOP) {
            on_ground = TRUE;
            break;
        }
        tile_start++;
    }
    
    if (on_ground && plat_state == FALL_STATE) {
        plat_state = GROUND_STATE;
        que_state = GROUND_STATE;
        pl_vel_y = 256;
        flutter_frame = 0;
    }
    
    // Actualizar sistema de daño
    actualizar_sistema_daño();
    
    // Actualizar parpadeo de invulnerabilidad
    actualizar_parpadeo_invulnerabilidad();
    
    // Si el jugador está muerto, no procesar física
    if (jugador_muerto) {
        // Procesar el timer de game over
        if (game_over_timer > 0) {
            game_over_timer--;
            if (game_over_timer == 0) {
                ir_a_game_over();
            }
        }
        return;
    }
    
    // Actualizar temblor de cámara local
    if (camera_shake_timer_local > 0) {
        camera_shake_timer_local--;
        // Aplicar temblor a la cámara con intensidad variable
        UINT8 shake_x = (simple_rand() % (camera_shake_intensity * 2 + 1)) - camera_shake_intensity;
        UINT8 shake_y = (simple_rand() % (camera_shake_intensity * 2 + 1)) - camera_shake_intensity;
        camera_offset_x = shake_x;
        camera_offset_y = shake_y;
    } else {
        // Restaurar offset de cámara
        camera_offset_x = 0;
        camera_offset_y = 0;
        camera_shake_intensity = 0;
    }
    
    //INITIALIZE VARS
    WORD temp_y = 0;
    col = 0;
    
    // A. INPUT CHECK
    UBYTE dash_press = FALSE;
    switch(plat_dash){
        case 1:
            if (INPUT_PRESSED(INPUT_PLATFORM_INTERACT)){
                dash_press = TRUE;
            }
        break;
        case 2:
            if (INPUT_PRESSED(INPUT_LEFT)){
                if(tap_val < 0){
                    dash_press = TRUE;
                } else{
                    tap_val = -15;
                }
            } else if (INPUT_PRESSED(INPUT_RIGHT)){
                if(tap_val > 0){
                    dash_press = TRUE;
                } else{
                    tap_val = 15;
                }
            }
        break;
        case 3:
            if ((INPUT_PRESSED(INPUT_DOWN) && INPUT_PLATFORM_JUMP) || (INPUT_DOWN && INPUT_PRESSED(INPUT_PLATFORM_JUMP))){
                dash_press = TRUE;
            }
        break;
    }
    
    // B. STATE MACHINE
    plat_state = que_state;
    switch(plat_state){
        case FALL_INIT:
            que_state = FALL_STATE;
        case FALL_STATE: {
            jump_type = 0;
            //Vertical Movement
            UBYTE float_condition = FALSE;
            if (plat_float_input == 1 && INPUT_PLATFORM_JUMP) float_condition = TRUE;
            if (plat_float_input == 2 && INPUT_UP) float_condition = TRUE;
            
            if (float_condition && pl_vel_y >= 0) {
                jump_type = 4;
                pl_vel_y = plat_float_grav;
            } else if (nocollide != 0){
                pl_vel_y = 7000;
            } else if (INPUT_PLATFORM_JUMP && pl_vel_y < 0) {
                pl_vel_y += plat_hold_grav;
                pl_vel_y = MIN(pl_vel_y, plat_max_fall_vel);
            } else {
                if (INPUT_PRESSED(INPUT_PLATFORM_JUMP) && flutter_frame == 0) {
                    flutter_frame = 8;
                    pl_vel_y = -750;
                } else if (INPUT_PRESSED(INPUT_PLATFORM_JUMP) && flutter_frame > 0) {
                    flutter_frame = MIN(flutter_frame + 8, 30);
                    pl_vel_y = -750;
                }
                if (flutter_frame > 0) {
                    pl_vel_y += 150;
                    flutter_frame -= 1;
                } else {
                    pl_vel_y += plat_grav;
                }
                pl_vel_y = MIN(pl_vel_y, plat_max_fall_vel);
            }
            //Collision
            deltaY += pl_vel_y >> 8;
            temp_y = PLAYER.pos.y;
            //Horizontal Movement
            if (nocontrol_h != 0 || plat_air_control == 0){
                deltaX += pl_vel_x >> 8;
                goto gotoXCol;
            }
        }
        break;
        case GROUND_INIT:
            que_state = GROUND_STATE;
            pl_vel_y = 256;
            jump_type = 0;
            wc_val = 0;
            ct_val = plat_coyote_max;
            dj_val = plat_extra_jumps;
            wj_val = plat_wall_jump_max;
            jump_reduction_val = 0;
        case GROUND_STATE:{
            if (actor_attached){
                if(last_actor->disabled == TRUE){
                    que_state = FALL_INIT;
                    actor_attached = FALSE;
                } else if (PLAYER.pos.x + (PLAYER.bounds.left << 4) > last_actor->pos.x + 16 + (last_actor->bounds.right<< 4)) {
                    que_state = FALL_INIT;
                    actor_attached = FALSE;
                } else if (PLAYER.pos.x + 16 + (PLAYER.bounds.right << 4) < last_actor->pos.x + (last_actor->bounds.left << 4)){
                    que_state = FALL_INIT;
                    actor_attached = FALSE;
                } else{
                    deltaX += (last_actor->pos.x - mp_last_x);
                    mp_last_x = last_actor->pos.x;
                }
                pl_vel_y = 0;
                deltaY += last_actor->pos.y - mp_last_y;
                mp_last_y = last_actor->pos.y;
                temp_y = last_actor->pos.y;
            } else if (nocollide != 0){
                pl_vel_y = 7000;
                temp_y = PLAYER.pos.y;
            } else {
                pl_vel_y += plat_grav;
                temp_y = PLAYER.pos.y;
                que_state = FALL_INIT;
            }
            deltaY += pl_vel_y >> 8;
        }
        break;
        case JUMP_INIT:
            hold_jump_val = plat_hold_jump_max;
            actor_attached = FALSE;
            pl_vel_y = -plat_jump_min;
            jb_val = 0;
            ct_val = 0;
            wc_val = 0;
            plat_walk_vel = 4000;
            que_state = JUMP_STATE;
        case JUMP_STATE: {
            if (hold_jump_val !=0 && INPUT_PLATFORM_JUMP){
                pl_vel_y -= jump_per_frame;
                if (plat_jump_vel >= jump_reduction_val){
                    pl_vel_y += jump_reduction_val;
                } else {
                    pl_vel_y = 0;
                }
                WORD tempBoost = (pl_vel_x >> 8) * boost_val;
                tempBoost = MAX(tempBoost, -tempBoost);
                if (tempBoost > 32767 + pl_vel_y){
                    pl_vel_y = -32767;
                }
                else{
                    pl_vel_y += -tempBoost;
                }
                hold_jump_val -=1;
            } else if (INPUT_PLATFORM_JUMP && pl_vel_y < 0){
                pl_vel_y += plat_hold_grav;
            } else if (pl_vel_y >= 0){
                que_state = FALL_INIT;
                pl_vel_y += plat_grav;
            } else {
                pl_vel_y += plat_grav;
            }
            temp_y = PLAYER.pos.y;
            deltaY += pl_vel_y >> 8;
            if (nocontrol_h != 0 || plat_air_control == 0){
                deltaX += pl_vel_x >> 8;
                goto gotoXCol;
            }
        }
        break;
        case DASH_INIT:{
            dash_init_switch();
        }
        goto gotoCounters;
        case DASH_STATE: {
            UBYTE tile_current;
            UBYTE tile_start = (((PLAYER.pos.y >> 4) + PLAYER.bounds.top)    >> 3);
            UBYTE tile_end   = (((PLAYER.pos.y >> 4) + PLAYER.bounds.bottom) >> 3) + 1;
            col = 0;
            if (PLAYER.dir == DIR_RIGHT){
                tile_current = ((PLAYER.pos.x >> 4) + PLAYER.bounds.right) >> 3;
                UWORD new_x = PLAYER.pos.x + (dash_dist);
                UBYTE tile_x = (((new_x >> 4) + PLAYER.bounds.right) >> 3) + 1;
                while (tile_current != tile_x){
                    if ((plat_camera_block & 2) && tile_current > (camera_x + SCREEN_WIDTH_HALF - 16) >> 3){
                        new_x = ((((tile_current) << 3) - PLAYER.bounds.right) << 4) -1;
                        dash_currentframe = 0;
                        goto endRcol;
                    }
                    while (tile_start != tile_end) {
                        if(plat_dash_through != 3 || dash_end_clear == FALSE){
                            if (tile_at(tile_current, tile_start) & COLLISION_LEFT) {
                                new_x = ((((tile_current) << 3) - PLAYER.bounds.right) << 4) -1;
                                col = 1;
                                last_wall = 1;
                                wc_val = plat_coyote_max;
                                dash_currentframe = 0;
                                goto endRcol;
                            }
                        }
                        tile_start++;
                    }
                    tile_start = (((PLAYER.pos.y >> 4) + PLAYER.bounds.top) >> 3);
                    tile_current += 1;
                }
                endRcol:
                if(plat_dash_momentum == 1 || plat_dash_momentum == 3){
                    pl_vel_x = plat_run_vel;
                } else{
                    pl_vel_x = 0;
                }
                PLAYER.pos.x = MIN((image_width - 16) << 4, new_x);
            }
            else if (PLAYER.dir == DIR_LEFT){
                tile_current = ((PLAYER.pos.x >> 4) + PLAYER.bounds.left) >> 3;
                WORD new_x = PLAYER.pos.x - (dash_dist);
                UBYTE tile_x = (((new_x >> 4) + PLAYER.bounds.left) >> 3)-1;
                while (tile_current != tile_x){
                    if ((plat_camera_block & 1) && tile_current < (camera_x - SCREEN_WIDTH_HALF) >> 3){
                        new_x = ((((tile_current + 1) << 3) - PLAYER.bounds.left) << 4)+1;
                        dash_currentframe = 0;
                        goto endLcol;
                    }
                    while (tile_start != tile_end) {
                        if(plat_dash_through != 3 || dash_end_clear == FALSE){
                            if (tile_at(tile_current, tile_start) & COLLISION_RIGHT) {
                                new_x = ((((tile_current + 1) << 3) - PLAYER.bounds.left) << 4)+1;
                                col = -1;
                                last_wall = -1;
                                dash_currentframe = 0;
                                wc_val = plat_coyote_max;
                                goto endLcol;
                            }
                        }
                        tile_start++;
                    }
                    tile_start = (((PLAYER.pos.y >> 4) + PLAYER.bounds.top) >> 3);
                    tile_current -= 1;
                }
                endLcol:
                if(plat_dash_momentum == 1 || plat_dash_momentum == 3){
                    pl_vel_x = -plat_run_vel;
                } else{
                    pl_vel_x = 0;
                }
                PLAYER.pos.x = MAX(0, new_x);
            }
            if(plat_dash_momentum >= 2){
                pl_vel_y += plat_hold_grav;
                if (INPUT_PRESSED(INPUT_PLATFORM_JUMP)){
                    if (ct_val != 0){
                        actor_attached = FALSE;
                        pl_vel_y = -(plat_jump_min + (plat_jump_vel/2));
                        jb_val = 0;
                        ct_val = 0;
                        jump_type = 1;
                    } else if (dj_val != 0){
                        dj_val -= 1;
                        jump_reduction_val += jump_reduction;
                        actor_attached = FALSE;
                        pl_vel_y = -(plat_jump_min + (plat_jump_vel/2));
                        jb_val = 0;
                        ct_val = 0;
                        jump_type = 2;
                    }
                }
                temp_y = PLAYER.pos.y;
                deltaY += pl_vel_y >> 8;
                deltaY = CLAMP(deltaY, -127, 127);
                UBYTE tile_start = (((PLAYER.pos.x >> 4) + PLAYER.bounds.left)  >> 3);
                UBYTE tile_end   = (((PLAYER.pos.x >> 4) + PLAYER.bounds.right) >> 3) + 1;
                if (deltaY > 0) {
                    WORD new_y = PLAYER.pos.y + deltaY;
                    UBYTE tile_y = ((new_y >> 4) + PLAYER.bounds.bottom) >> 3;
                    while (tile_start != tile_end) {
                        if (tile_at(tile_start, tile_y) & COLLISION_TOP) {
                            new_y = ((((tile_y) << 3) - PLAYER.bounds.bottom) << 4) - 1;
                            actor_attached = FALSE;
                            pl_vel_y = 256;
                            break;
                        }
                        tile_start++;
                    }
                    PLAYER.pos.y = new_y;
                } else if (deltaY < 0) {
                    WORD new_y = PLAYER.pos.y + deltaY;
                    UBYTE tile_y = (((new_y >> 4) + PLAYER.bounds.top) >> 3);
                    while (tile_start != tile_end) {
                        if (tile_at(tile_start, tile_y) & COLLISION_BOTTOM) {
                            new_y = ((((UBYTE)(tile_y + 1) << 3) - PLAYER.bounds.top) << 4) + 1;
                            pl_vel_y = 0;
                            break;
                        }
                        tile_start++;
                    }
                    PLAYER.pos.y = new_y;
                }
                pl_vel_y = CLAMP(pl_vel_y,-plat_max_fall_vel, plat_max_fall_vel);
            } else{
                temp_y = PLAYER.pos.y;
            }
        }
        if (plat_dash_through >= 1){
            goto gotoSwitch2;
        }
        goto gotoActorCol;
        case LADDER_INIT:
            que_state = LADDER_STATE;
            jump_type = 0;
        case LADDER_STATE:{
            ladder_switch();
        }
        goto gotoActorCol;
        case WALL_INIT:
            que_state = WALL_STATE;
            jump_type = 0;
            run_stage = 0;
        case WALL_STATE:{
            if (nocollide != 0){
                pl_vel_y += 7000;
            } else if (pl_vel_y < 0){
                pl_vel_y += plat_grav;
            } else if (plat_wall_slide) {
                pl_vel_y = plat_wall_grav;
            } else{
                pl_vel_y += plat_grav;
            }
            deltaY += pl_vel_y >> 8;
            temp_y = PLAYER.pos.y;
        }
        break;
        case KNOCKBACK_INIT:
            run_stage = 0;
            jump_type = 0;
            que_state = KNOCKBACK_STATE;
            // Configurar velocidades de knockback más suaves
            if (PLAYER.dir == DIR_RIGHT) {
                pl_vel_x = -2000; // Reducido para menos paralización
            } else {
                pl_vel_x = 2000;
            }
            pl_vel_y = -3000; // Reducido para menos paralización
            nocontrol_h = 10; // Reducido de 20 a 10 frames
        case KNOCKBACK_STATE: {
            // Aplicar fricción horizontal más rápida
            if (pl_vel_x < 0) {
                pl_vel_x += plat_air_dec * 2; // Fricción doble
                pl_vel_x = MIN(pl_vel_x, 0);
            } else if (pl_vel_x > 0) {
                pl_vel_x -= plat_air_dec * 2; // Fricción doble
                pl_vel_x = MAX(pl_vel_x, 0);
            }
            deltaX += pl_vel_x >> 8;
            
            // Aplicar gravedad
            pl_vel_y += plat_grav;
            pl_vel_y = MIN(pl_vel_y, plat_max_fall_vel);
            deltaY += pl_vel_y >> 8;
            temp_y = PLAYER.pos.y;
            
            // Salir del estado de knockback más rápido
            if (pl_vel_y >= 0) {
                que_state = FALL_INIT; // Cambiar a FALL_INIT en lugar de GROUND_INIT
                nocontrol_h = 0; // Restaurar control inmediatamente
            }
            
            nocollide = 0;
        }
        goto gotoXCol;
        case BLANK_INIT:
            que_state = BLANK_STATE;
            pl_vel_x = 0;
            pl_vel_y = 0;
            run_stage = 0;
            jump_type = 0;
        case BLANK_STATE:
            goto gotoActorCol;
    }
    
    //FUNCTION ACCELERATION
    if (INPUT_LEFT || INPUT_RIGHT){
        BYTE dir = 1;
        if (INPUT_LEFT){
            dir = -1;
            pl_vel_x = -pl_vel_x;
        }
        if (pl_vel_x < 0 && plat_turn_acc != 0){
            pl_vel_x += plat_turn_acc;
            run_stage = -1;
        } else {
            run_stage = 0;
            pl_vel_x = CLAMP(pl_vel_x + plat_walk_acc, plat_min_vel, plat_walk_vel);
        }
        pl_vel_x *= dir;
        deltaX += pl_vel_x >> 8;
    } else{
        if (pl_vel_x < 0) {
            if (plat_state == GROUND_STATE){
                pl_vel_x += plat_dec;
            } else {
                pl_vel_x += plat_air_dec;
            }
            if (pl_vel_x > 0) {
                pl_vel_x = 0;
            }
        } else if (pl_vel_x > 0) {
            if (plat_state == GROUND_STATE){
                pl_vel_x -= plat_dec;
            }
            else {
                pl_vel_x -= plat_air_dec;
            }
            if (pl_vel_x < 0) {
                pl_vel_x = 0;
            }
        }
        run_stage = 0;
        deltaX += pl_vel_x >> 8;
    }
    
    //FUNCTION X COLLISION
    gotoXCol:
    {
        deltaX = CLAMP(deltaX, -127, 127);
        UBYTE tile_start = (((PLAYER.pos.y >> 4) + PLAYER.bounds.top)    >> 3);
        UBYTE tile_end   = (((PLAYER.pos.y >> 4) + PLAYER.bounds.bottom) >> 3) + 1;
        UWORD new_x = PLAYER.pos.x + deltaX;
        if (new_x > (*edge_right + SCREEN_WIDTH - 16) <<4){
            if (new_x > PLAYER.pos.x){
                new_x = PLAYER.pos.x;
                pl_vel_x = 0;
            } else {
                new_x = PLAYER.pos.x - MIN(PLAYER.pos.x - ((*edge_right + SCREEN_WIDTH - 16)<<4), 16);
            }
        } else if (new_x < *edge_left << 4){
            if (deltaX < 0){
                new_x = PLAYER.pos.x;
                pl_vel_x = 0;
            } else {
                new_x = PLAYER.pos.x + MIN(((*edge_left+8)<<4)-PLAYER.pos.x, 16);
            }
        }
        if (new_x > PLAYER.pos.x) {
            UBYTE tile_x = ((new_x >> 4) + PLAYER.bounds.right) >> 3;
            while (tile_start != tile_end) {
                if (tile_at(tile_x, tile_start) & COLLISION_LEFT) {
                    new_x = (((tile_x << 3) - PLAYER.bounds.right) << 4) - 1;
                    pl_vel_x = 0;
                    col = 1;
                    last_wall = 1;
                    wc_val = plat_coyote_max + 1;
                    break;
                }
                tile_start++;
            }
        } else if (new_x < PLAYER.pos.x) {
            UBYTE tile_x = ((new_x >> 4) + PLAYER.bounds.left) >> 3;
            while (tile_start != tile_end) {
                if (tile_at(tile_x, tile_start) & COLLISION_RIGHT) {
                    new_x = ((((tile_x + 1) << 3) - PLAYER.bounds.left) << 4) + 1;
                    pl_vel_x = 0;
                    col = -1;
                    last_wall = -1;
                    wc_val = plat_coyote_max + 1;
                    break;
                }
                tile_start++;
            }
        }
        PLAYER.pos.x = new_x;
    }
    
    gotoYCol:
    {
        deltaY = CLAMP(deltaY, -127, 127);
        UBYTE tile_start = (((PLAYER.pos.x >> 4) + PLAYER.bounds.left)  >> 3);
        UBYTE tile_end   = (((PLAYER.pos.x >> 4) + PLAYER.bounds.right) >> 3) + 1;
        if (deltaY > 0) {
            WORD new_y = PLAYER.pos.y + deltaY;
            UBYTE tile_y = ((new_y >> 4) + PLAYER.bounds.bottom) >> 3;
            if (nocollide == 0){
                while (tile_start != tile_end) {
                    if (tile_at(tile_start, tile_y) & COLLISION_TOP) {
                        if (drop_press()){
                            while (tile_start != tile_end) {
                                if (tile_at(tile_start, tile_y) & COLLISION_BOTTOM){
                                    goto land;
                                }
                            tile_start++;
                            }
                            nocollide = 5;
                            pl_vel_y += plat_grav;
                            break;
                        }
                        land:
                        new_y = ((((tile_y) << 3) - PLAYER.bounds.bottom) << 4) - 1;
                        actor_attached = FALSE;
                        if(plat_state == GROUND_STATE){
                            que_state = GROUND_STATE;
                            pl_vel_y = 256;
                        } else if(plat_state == GROUND_INIT){
                            que_state = GROUND_STATE;
                        } else {que_state = GROUND_INIT;}
                        break;
                    }
                    tile_start++;
                }
            }
            PLAYER.pos.y = new_y;
        } else if (deltaY < 0) {
            WORD new_y = PLAYER.pos.y + deltaY;
            UBYTE tile_y = (((new_y >> 4) + PLAYER.bounds.top) >> 3);
            while (tile_start != tile_end) {
                if (tile_at(tile_start, tile_y) & COLLISION_BOTTOM) {
                    new_y = ((((UBYTE)(tile_y + 1) << 3) - PLAYER.bounds.top) << 4) + 1;
                    pl_vel_y = 0;
                    if(actor_attached){
                        temp_y = last_actor->pos.y;
                        if (last_actor->bounds.top > 0){
                            temp_y += last_actor->bounds.top + last_actor->bounds.bottom << 5;
                        }
                        new_y = temp_y;
                    }
                    ct_val = 0;
                    que_state = FALL_INIT;
                    break;
                }
                tile_start++;
            }
            PLAYER.pos.y = new_y;
        }
    }
    
    gotoActorCol:
    {
        deltaX = 0;
        deltaY = 0;
        actor_t *hit_actor;
        hit_actor = actor_overlapping_player(FALSE);
        if (hit_actor != NULL && hit_actor->collision_group) {
            if (hit_actor->collision_group == plat_solid_group){
                if(!actor_attached || hit_actor != last_actor){
                    if (temp_y < (hit_actor->pos.y + (hit_actor->bounds.top << 4)) && pl_vel_y >= 0){
                        last_actor = hit_actor;
                        mp_last_x = hit_actor->pos.x;
                        mp_last_y = hit_actor->pos.y;
                        PLAYER.pos.y = hit_actor->pos.y + (hit_actor->bounds.top << 4) - (PLAYER.bounds.bottom << 4) - 4;
                        pl_vel_y = 0;
                        actor_attached = TRUE;
                        que_state = GROUND_INIT;
                    } else if (temp_y + (PLAYER.bounds.top << 4) > hit_actor->pos.y + (hit_actor->bounds.bottom<<4)){
                        deltaY += (hit_actor->pos.y - PLAYER.pos.y) + ((-PLAYER.bounds.top + hit_actor->bounds.bottom)<<4) + 32;
                        pl_vel_y = plat_grav;
                        if(que_state == JUMP_STATE || actor_attached){
                            que_state = FALL_INIT;
                        }
                    } else if (PLAYER.pos.x < hit_actor->pos.x){
                        deltaX = (hit_actor->pos.x - PLAYER.pos.x) - ((PLAYER.bounds.right + -hit_actor->bounds.left)<<4);
                        col = 1;
                        last_wall = 1;
                        wc_val = plat_coyote_max + 1;
                        if(!INPUT_RIGHT){
                            pl_vel_x = 0;
                        }
                        if(que_state == DASH_STATE){
                            que_state = FALL_INIT;
                        }
                    } else if (PLAYER.pos.x > hit_actor->pos.x){
                        deltaX = (hit_actor->pos.x - PLAYER.pos.x) + ((-PLAYER.bounds.left + hit_actor->bounds.right)<<4)+16;
                        col = -1;
                        last_wall = -1;
                        wc_val = plat_coyote_max  + 1;
                        if (!INPUT_LEFT){
                            pl_vel_x = 0;
                        }
                        if(que_state == DASH_STATE){
                            que_state = FALL_INIT;
                        }
                    }
                }
            } else if (hit_actor->collision_group == plat_mp_group){
                if(!actor_attached || hit_actor != last_actor){
                    if (temp_y < hit_actor->pos.y + (hit_actor->bounds.top << 4) && pl_vel_y >= 0){
                        last_actor = hit_actor;
                        mp_last_x = hit_actor->pos.x;
                        mp_last_y = hit_actor->pos.y;
                        PLAYER.pos.y = hit_actor->pos.y + (hit_actor->bounds.top << 4) - (PLAYER.bounds.bottom << 4) - 4;
                        pl_vel_y = 0;
                        actor_attached = TRUE;
                        que_state = GROUND_INIT;
                    }
                }
            } else if (hit_actor->collision_group == GRUPO_ENEMIGO_MAPACHE) {
                // Solo procesar si el enemigo no está desactivado
                if (!hit_actor->disabled) {
                    // Verificar si el jugador está saltando sobre el enemigo (detección más precisa)
                    if (pl_vel_y > 0 && 
                        PLAYER.pos.y + (PLAYER.bounds.bottom << 4) <= hit_actor->pos.y + (hit_actor->bounds.top << 4) + 32 &&
                        PLAYER.pos.x + (PLAYER.bounds.left << 4) < hit_actor->pos.x + (hit_actor->bounds.right << 4) &&
                        PLAYER.pos.x + (PLAYER.bounds.right << 4) > hit_actor->pos.x + (hit_actor->bounds.left << 4)) {
                        // Salto sobre enemigo - dar rebote
                        procesar_salto_sobre_enemigo(hit_actor);
                    } else if (jugador_inmune == 0 && jugador_muerto == 0) {
                        // Colisión lateral - recibir daño
                        aplicar_dano_jugador(1, 1);
                    }
                }
            } else if (hit_actor->collision_group == GRUPO_ENEMIGO_TRIGGER) {
                // Solo procesar si el enemigo no está desactivado
                if (!hit_actor->disabled) {
                    // Verificar si el jugador está saltando sobre el enemigo (detección más precisa)
                    if (pl_vel_y > 0 && 
                        PLAYER.pos.y + (PLAYER.bounds.bottom << 4) <= hit_actor->pos.y + (hit_actor->bounds.top << 4) + 32 &&
                        PLAYER.pos.x + (PLAYER.bounds.left << 4) < hit_actor->pos.x + (hit_actor->bounds.right << 4) &&
                        PLAYER.pos.x + (PLAYER.bounds.right << 4) > hit_actor->pos.x + (hit_actor->bounds.left << 4)) {
                        // Salto sobre enemigo - dar rebote
                        procesar_salto_sobre_enemigo(hit_actor);
                    } else if (jugador_inmune == 0 && jugador_muerto == 0) {
                        // Colisión lateral - recibir daño
                        aplicar_dano_jugador(1, 2);
                    }
                }
            } else if (hit_actor->collision_group == GRUPO_ENEMIGO_JEFE) {
                // Solo procesar si el enemigo no está desactivado
                if (!hit_actor->disabled) {
                    // Verificar si el jugador está saltando sobre el jefe (detección más precisa)
                    if (pl_vel_y > 0 && 
                        PLAYER.pos.y + (PLAYER.bounds.bottom << 4) <= hit_actor->pos.y + (hit_actor->bounds.top << 4) + 32 &&
                        PLAYER.pos.x + (PLAYER.bounds.left << 4) < hit_actor->pos.x + (hit_actor->bounds.right << 4) &&
                        PLAYER.pos.x + (PLAYER.bounds.right << 4) > hit_actor->pos.x + (hit_actor->bounds.left << 4)) {
                        // Salto sobre jefe - dar rebote
                        procesar_salto_sobre_enemigo(hit_actor);
                    } else if (jugador_inmune == 0 && jugador_muerto == 0) {
                        // Colisión lateral - recibir más daño
                        aplicar_dano_jugador(2, 1);
                    }
                }
            }
            player_register_collision_with(hit_actor);
        } else if (INPUT_PRESSED(INPUT_PLATFORM_INTERACT)) {
            if (!hit_actor) {
                hit_actor = actor_in_front_of_player(8, TRUE);
            }
            if (hit_actor && !hit_actor->collision_group && hit_actor->script.bank) {
                script_execute(hit_actor->script.bank, hit_actor->script.ptr, 0, 1, 0);
            }
        }
    }
    
    gotoSwitch2:
    switch(plat_state){
        case FALL_INIT:
            actor_attached = FALSE;
        case FALL_STATE: {
            //ANIMATION
            basic_anim();
            //STATE CHANGE
            wall_check();
            if(dash_press && dash_ready_val == 0){
                if (plat_dash_style != 0){
                    if (col == 0 || (col == 1 && !INPUT_RIGHT) || (col == -1 && !INPUT_LEFT)){
                    que_state = DASH_INIT;
                    plat_state = FALL_END;
                    break;
                    }
                }
                else if (que_state == GROUND_INIT && plat_dash_style != 1){
                    que_state = DASH_INIT;
                    plat_state = FALL_END;
                    break;
                }
            }
            if (INPUT_PRESSED(INPUT_PLATFORM_JUMP)){
                if(wc_val != 0 && wj_val != 0){
                    jump_type = 3;
                    wj_val -= 1;
                    nocontrol_h = 5;
                    pl_vel_x += (plat_wall_kick + plat_walk_vel)*-last_wall;
                    que_state = JUMP_INIT;
                    plat_state = FALL_END;
                    break;
                } else if (ct_val != 0){
                    jump_type = 1;
                    que_state = JUMP_INIT;
                    plat_state = FALL_END;
                    break;
                } else if (dj_val != 0){
                    jump_type = 2;
                    if (dj_val != 255){
                        dj_val -= 1;
                    }
                    jump_reduction_val += jump_reduction;
                    que_state = JUMP_INIT;
                    plat_state = FALL_END;
                    break;
                } else {
                    jb_val = plat_buffer_max;
                }
            }
            ladder_check();
            if (que_state != FALL_STATE){
                plat_state = FALL_END;
            }
            if (jb_val != 0){
                jb_val -= 1;
            }
            if (nocontrol_h != 0){
                nocontrol_h -= 1;
            }
            if (ct_val != 0 && que_state != GROUND_STATE){
                ct_val -= 1;
            }
            if (wc_val !=0 && col == 0){
                wc_val -= 1;
            }
            if (nocollide != 0){
                nocollide -= 1;
            }
        }
        break;
        case GROUND_INIT:
        case GROUND_STATE:{
            //ANIMATION
            if (jugador_dolor == 0 && !jugador_muerto) {
                if (INPUT_LEFT){
                    actor_set_dir(&PLAYER, DIR_LEFT, TRUE);
                } else if (INPUT_RIGHT){
                    actor_set_dir(&PLAYER, DIR_RIGHT, TRUE);
                } else if (pl_vel_x < 0) {
                    actor_set_dir(&PLAYER, DIR_LEFT, TRUE);
                } else if (pl_vel_x > 0) {
                    actor_set_dir(&PLAYER, DIR_RIGHT, TRUE);
                } else {
                    actor_set_anim_idle(&PLAYER);
                }
            }
            //STATE CHANGE
            if (dash_press && plat_dash_style != 1 && dash_ready_val == 0) {
                que_state = DASH_INIT;
                plat_state = GROUND_END;
                break;
            }
            if (INPUT_PRESSED(INPUT_PLATFORM_JUMP) || jb_val != 0){
                if (nocollide == 0){
                    jump_type = 1;
                    que_state = JUMP_INIT;
                    plat_state = GROUND_END;
                    break;
                }
            }
            jb_val = 0;
            ladder_check();
            if (que_state != GROUND_STATE){
                plat_state = GROUND_END;
            }
            if (nocollide != 0){
                nocollide -= 1;
            }
        }
        break;
        case JUMP_INIT:
        case JUMP_STATE: {
            basic_anim();
            wall_check();
            if(dash_press && dash_ready_val == 0){
                if(plat_dash_style != 0 || ct_val != 0){
                    que_state = DASH_INIT;
                    plat_state = JUMP_END;
                    break;
                }
            }
            if (INPUT_PRESSED(INPUT_PLATFORM_JUMP)){
                if(wc_val != 0 && wj_val != 0){
                    jump_type = 3;
                    wj_val -= 1;
                    nocontrol_h = 5;
                    pl_vel_x = (plat_wall_kick + plat_walk_vel)*-last_wall;
                    que_state = JUMP_INIT;
                    plat_state = JUMP_END;
                } else if (dj_val != 0){
                    jump_type = 2;
                    if (dj_val != 255){
                        dj_val -= 1;
                    }
                    jump_reduction_val += jump_reduction;
                    que_state = JUMP_INIT;
                    plat_state = JUMP_END;
                }
                break;
            }
            ladder_check();
            if (que_state != JUMP_STATE){
                plat_state = JUMP_END;
            }
            if (nocontrol_h != 0){
                nocontrol_h -= 1;
            }
        }
        break;
        case DASH_STATE: {
            basic_anim();
            if (dash_currentframe == 0){
                que_state = FALL_INIT;
            } else{
                dash_currentframe -= 1;
            }
            if (que_state != DASH_STATE){
                plat_state = DASH_END;
            }
            if(plat_dash_through >= 2){
                goto gotoCounters;
            }
        }
        break;
        case WALL_INIT:
        case WALL_STATE:{
            if (jugador_dolor == 0 && !jugador_muerto) {
                if (col == 1){
                    actor_set_dir(&PLAYER, DIR_LEFT, TRUE);
                } else if (col == -1){
                    actor_set_dir(&PLAYER, DIR_RIGHT, TRUE);
                }
            }
            wall_check();
            if(dash_press && plat_dash_style != 0 && dash_ready_val == 0){
                if ((col == 1 && !INPUT_RIGHT) || (col == -1 && !INPUT_LEFT)){
                    que_state = DASH_INIT;
                    plat_state = WALL_END;
                    break;
                }
            }
            if ((INPUT_PRESSED(INPUT_PLATFORM_JUMP) || jb_val != 0) && wj_val != 0){
                wj_val -= 1;
                nocontrol_h = 5;
                pl_vel_x = (plat_wall_kick + plat_walk_vel)*-last_wall;
                jump_type = 3;
                que_state = JUMP_INIT;
                plat_state = WALL_END;
                break;
            }
            ladder_check();
            if (que_state != WALL_STATE){
                plat_state = WALL_END;
            }
            if (nocollide != 0){
                nocollide -= 1;
            }
        }
        break;
        case KNOCKBACK_INIT:
        case KNOCKBACK_STATE:
            // Mantener animación de dolor durante el knockback
            if (jugador_dolor > 0) {
                establecer_animacion_jugador(ANIM_HURT);
            } else {
                // Si no hay dolor, usar animación normal de caída
                basic_anim();
            }
            
            // El knockback se maneja en el switch principal, no aquí
            break;
    }
    
    gotoTriggerCol:
    trigger_activate_at_intersection(&PLAYER.bounds, &PLAYER.pos, INPUT_UP_PRESSED);
    
    gotoCounters:
    if (dash_ready_val != 0){
        dash_ready_val -=1;
    }
    if (tap_val > 0){
        tap_val -= 1;
    } else if (tap_val < 0){
        tap_val += 1;
    }
    if (flutter_frame != 0) {
        flutter_frame -= 1;
    }
    if (camera_deadzone_x > plat_camera_deadzone_x){
        camera_deadzone_x -= 1;
    }
    if(state_events[plat_state].script_addr != 0){
        script_execute(state_events[plat_state].script_bank, state_events[plat_state].script_addr, 0, 0);
    }
}

// NUEVO SISTEMA DE SALUD Y DAÑO - IMPLEMENTACIÓN
void inicializar_sistema_salud(void) BANKED {
    // Inicializar valores - usar siempre valores por defecto para simplicidad
    jugador_salud = 4;
    jugador_salud_max = 4;
    
    jugador_inmune = 0;
    jugador_dolor = 0;
    jugador_muerto = 0;
    jugador_parpadeo = 0;
    game_over_timer = 0;
    
    hud_corazon = NULL;
    
    actualizar_hud_salud();
}

void actualizar_sistema_daño(void) BANKED {
    // El sistema de daño funcionará directamente por colisiones
    // sin necesidad de variables externas por ahora
    
    // Actualizar temporizadores
    if (jugador_inmune > 0) jugador_inmune--;
    if (jugador_dolor > 0) jugador_dolor--;
    
    // Actualizar animación si está en estado de dolor
    if (jugador_dolor > 0 && !jugador_muerto) {
        establecer_animacion_jugador(ANIM_HURT);
    }
}

void dano_mapache(void) BANKED {
    if (jugador_inmune > 0 || jugador_muerto) return;
    
    if (jugador_salud > 0) {
        jugador_salud -= 1;
    }
    
    jugador_inmune = INMUNIDAD_MAPACHE;
    jugador_dolor = DOLOR_MAPACHE;
    activar_temblor_camara(TEMBLOR_CAMARA_MAPACHE);
    aplicar_retroceso_mapache();
    
    actualizar_hud_salud();
    
    if (jugador_salud == 0) {
        jugador_muere();
    }
}

void dano_trigger(void) BANKED {
    if (jugador_inmune > 0 || jugador_muerto) return;
    
    if (jugador_salud > 0) {
        jugador_salud -= 1;
    }
    
    jugador_inmune = INMUNIDAD_TRIGGER;
    jugador_dolor = DOLOR_TRIGGER;
    activar_temblor_camara(TEMBLOR_CAMARA_TRIGGER);
    aplicar_retroceso_trigger();
    
    actualizar_hud_salud();
    
    if (jugador_salud == 0) {
        jugador_muere();
    }
}

void jugador_muere(void) BANKED {
    jugador_muerto = 1;
    establecer_animacion_jugador(ANIM_DEATH);
    reproducir_sonido(SONIDO_MUERTE);
    
    pl_vel_x = 0;
    pl_vel_y = 0;
    
    game_over_timer = 120;
}

void ir_a_game_over(void) BANKED {
    // Reiniciar valores del jugador
    jugador_salud = jugador_salud_max;
    jugador_muerto = 0;
    jugador_inmune = 0;
    jugador_dolor = 0;
    jugador_parpadeo = 0;
    game_over_timer = 0;
    
    plat_state = GROUND_STATE;
    que_state = GROUND_STATE;
    pl_vel_x = 0;
    pl_vel_y = 0;
    
    actualizar_hud_salud();
}

void actualizar_hud_salud(void) BANKED {
    // Buscar el actor del HUD si no lo hemos encontrado aún
    if (!hud_corazon) {
        // Buscar un actor en la esquina superior izquierda (posición típica del HUD)
        // Este es un enfoque simple, podrías mejorarlo según tu configuración específica
        actor_t* actor_ptr = actors;
        UBYTE i;
        for (i = 0; i < MAX_ACTORS && actor_ptr; i++, actor_ptr++) {
            if (actor_ptr->pos.x < 320 && actor_ptr->pos.y < 320 && !actor_ptr->disabled) {
                hud_corazon = actor_ptr;
                break;
            }
        }
    }
    
    // Actualizar HUD de salud si está disponible
    if (hud_corazon && !hud_corazon->disabled) {
        UBYTE frame_corazon = jugador_salud_max - jugador_salud;
        if (frame_corazon > jugador_salud_max) {
            frame_corazon = jugador_salud_max;
        }
        // Actualizar frame del HUD usando método seguro
        hud_corazon->frame = frame_corazon;
    }
}

// Funciones de retroceso ahora integradas en aplicar_dano_jugador

void activar_temblor_camara(UINT8 intensidad) BANKED {
    camera_shake_timer_local = intensidad;
    camera_shake_intensity = intensidad / 2;
    if (camera_shake_intensity < 3) {
        camera_shake_intensity = 3;
    }
}

void establecer_animacion_jugador(UINT8 anim_id) BANKED {
    if (anim_id == ANIM_HURT) {
        actor_set_anim(&PLAYER, ANIM_HURT);
    } else if (anim_id == ANIM_DEATH) {
        actor_set_anim(&PLAYER, ANIM_DEATH);
    }
}

void reproducir_sonido(UINT8 sonido) BANKED {
    if (sonido == SONIDO_MUERTE) {
        // sound_play_sfx(sonido, SFX_PRIORITY_HIGH, 0x00);
    }
}

void aplicar_dano_jugador(UINT8 cantidad, UINT8 tipo) BANKED {
    if (jugador_inmune > 0 || jugador_muerto) return;
    
    // Asegurar que se reste la salud correctamente
    if (jugador_salud > cantidad) {
        jugador_salud -= cantidad;
    } else {
        jugador_salud = 0;
    }
    
    // Aplicar efectos según el tipo de daño
    if (tipo == 1) { // Mapache
        jugador_inmune = INMUNIDAD_MAPACHE;
        jugador_dolor = DOLOR_MAPACHE;
        activar_temblor_camara(TEMBLOR_CAMARA_MAPACHE);
        que_state = KNOCKBACK_INIT; // Activar knockback directamente
    } else if (tipo == 2) { // Trigger
        jugador_inmune = INMUNIDAD_TRIGGER;
        jugador_dolor = DOLOR_TRIGGER;
        activar_temblor_camara(TEMBLOR_CAMARA_TRIGGER);
        que_state = KNOCKBACK_INIT; // Activar knockback directamente
    }
    
    actualizar_hud_salud();
    
    if (jugador_salud == 0) {
        jugador_muere();
    }
}

void actualizar_parpadeo_invulnerabilidad(void) BANKED {
    if (jugador_inmune > 0 && !jugador_muerto) {
        jugador_parpadeo++;
        if (jugador_parpadeo >= PARPADEO_FRAMES) {
            jugador_parpadeo = 0;
            // Alternar visibilidad del jugador usando campo hidden
            if (PLAYER.hidden) {
                PLAYER.hidden = FALSE;
            } else {
                PLAYER.hidden = TRUE;
            }
        }
    } else {
        // Asegurar que el jugador sea visible cuando no está inmune
        if (PLAYER.hidden) {
            PLAYER.hidden = FALSE;
        }
        jugador_parpadeo = 0;
    }
}

void procesar_salto_sobre_enemigo(actor_t* enemigo) BANKED {
    // Dar rebote inmediato al jugador
    pl_vel_y = -4000;
    que_state = FALL_INIT; // Usar FALL_INIT para mejor control
    
    if (enemigo && !enemigo->disabled) {
        // Iniciar animación de muerte del enemigo
        actor_set_anim(enemigo, ANIM_DEATH);
        // El enemigo se desactivará después de mostrar la animación
        enemigo->disabled = TRUE;
        enemigo->hidden = TRUE;
    }
}

void basic_anim(void) BANKED {
    if (jugador_dolor > 0 || jugador_muerto) {
        return;
    }
    
    if(plat_turn_control){
        if (INPUT_LEFT){
            PLAYER.dir = DIR_LEFT;
        } else if (INPUT_RIGHT){
            PLAYER.dir = DIR_RIGHT;
        } else if (pl_vel_x < 0) {
            PLAYER.dir = DIR_LEFT;
        } else if (pl_vel_x > 0) {
            PLAYER.dir = DIR_RIGHT;
        } else if (plat_state == FALL_STATE) {
            if (PLAYER.dir != DIR_LEFT && PLAYER.dir != DIR_RIGHT) {
                PLAYER.dir = DIR_RIGHT;
            }
        }
    }
    if (PLAYER.dir == DIR_LEFT){
        actor_set_anim(&PLAYER, ANIM_JUMP_LEFT);
    } else {
        actor_set_anim(&PLAYER, ANIM_JUMP_RIGHT);
    }
}

void wall_check(void) BANKED {
    if(col != 0 && pl_vel_y >= 0 && plat_wall_slide){
        if (que_state != WALL_STATE ){
            que_state = WALL_INIT;
        }
    } else if (que_state == WALL_STATE){
        que_state = FALL_INIT;
    }
}

void ladder_check(void) BANKED {
    UBYTE p_half_width = (PLAYER.bounds.right - PLAYER.bounds.left) >> 1;
    if (INPUT_UP || INPUT_DOWN) {
        UBYTE tile_x_mid = ((PLAYER.pos.x >> 4) + PLAYER.bounds.left + p_half_width) >> 3;
        UBYTE tile_y   = ((PLAYER.pos.y >> 4) >> 3);
        if (tile_at(tile_x_mid, tile_y) & TILE_PROP_LADDER) {
            PLAYER.pos.x = (((tile_x_mid << 3) + 4 - (PLAYER.bounds.left + p_half_width) << 4));
            que_state = LADDER_INIT;
            pl_vel_x = 0;
        }
    }
}

void ladder_switch(void) BANKED {
    UBYTE p_half_width = (PLAYER.bounds.right - PLAYER.bounds.left) >> 1;
    UBYTE tile_x_mid = ((PLAYER.pos.x >> 4) + PLAYER.bounds.left + p_half_width) >> 3;
    pl_vel_y = 0;
    
    if (INPUT_UP) {
        UBYTE tile_y = ((PLAYER.pos.y >> 4) + PLAYER.bounds.top + 1) >> 3;
        if (tile_at(tile_x_mid, tile_y) & TILE_PROP_LADDER) {
            pl_vel_y = -plat_climb_vel;
        }
    } else if (INPUT_DOWN) {
        UBYTE tile_y = ((PLAYER.pos.y >> 4) + PLAYER.bounds.bottom + 1) >> 3;
        if (tile_at(tile_x_mid, tile_y) & TILE_PROP_LADDER) {
            pl_vel_y = plat_climb_vel;
        }
    } else if (INPUT_LEFT) {
        que_state = FALL_INIT;
        UBYTE tile_start = (((PLAYER.pos.y >> 4) + PLAYER.bounds.top)    >> 3);
        UBYTE tile_end   = (((PLAYER.pos.y >> 4) + PLAYER.bounds.bottom) >> 3) + 1;
        while (tile_start != tile_end) {
            if (tile_at(tile_x_mid - 1, tile_start) & COLLISION_RIGHT) {
                que_state = LADDER_STATE;
                break;
            }
            tile_start++;
        }
    } else if (INPUT_RIGHT) {
        que_state = FALL_INIT;
        UBYTE tile_start = (((PLAYER.pos.y >> 4) + PLAYER.bounds.top)    >> 3);
        UBYTE tile_end   = (((PLAYER.pos.y >> 4) + PLAYER.bounds.bottom) >> 3) + 1;
        while (tile_start != tile_end) {
            if (tile_at(tile_x_mid + 1, tile_start) & COLLISION_LEFT) {
                que_state = LADDER_STATE;
                break;
            }
            tile_start++;
        }
    }
    
    PLAYER.pos.y += (pl_vel_y >> 8);
    actor_set_anim(&PLAYER, ANIM_CLIMB);
    if (pl_vel_y == 0) {
        actor_stop_anim(&PLAYER);
    }
    
    if (INPUT_PRESSED(INPUT_PLATFORM_JUMP)){
        que_state = FALL_INIT;
    }
    if (que_state != LADDER_STATE){
        plat_state = LADDER_END;
    }
}

void dash_init_switch(void) BANKED {
    WORD new_x;
    if (INPUT_RIGHT){
        PLAYER.dir = DIR_RIGHT;
    }
    else if(INPUT_LEFT){
        PLAYER.dir = DIR_LEFT;
    }
    if (PLAYER.dir == DIR_RIGHT){
        new_x = PLAYER.pos.x + (dash_dist*plat_dash_frames);
    }
    else{
        new_x = PLAYER.pos.x + (-dash_dist*plat_dash_frames);
    }
    if(plat_dash_through == 3 && plat_dash_momentum < 2){
        dash_end_clear = true;
        UBYTE tile_start = (((PLAYER.pos.y >> 4) + PLAYER.bounds.top)    >> 3);
        UBYTE tile_end   = (((PLAYER.pos.y >> 4) + PLAYER.bounds.bottom) >> 3) + 1;
        if (PLAYER.dir == DIR_RIGHT){
            if (PLAYER.pos.x + (PLAYER.bounds.right <<4) + (dash_dist*(plat_dash_frames)) > (image_width -16) << 4){
                dash_end_clear = false;
            } else {
                UBYTE tile_xr = (((new_x >> 4) + PLAYER.bounds.right) >> 3) +1;
                UBYTE tile_xl = ((new_x >> 4) + PLAYER.bounds.left) >> 3;
                while (tile_xl != tile_xr){
                    while (tile_start != tile_end) {
                        if (tile_at(tile_xl, tile_start) & COLLISION_ALL) {
                            dash_end_clear = false;
                            goto initDash;
                        }
                        tile_start++;
                    }
                    tile_start = (((PLAYER.pos.y >> 4) + PLAYER.bounds.top)    >> 3);
                    tile_xl++;
                }
            }
        } else if(PLAYER.dir == DIR_LEFT) {
            if (PLAYER.pos.x <= ((dash_dist*plat_dash_frames)+(PLAYER.bounds.left << 4))+(8<<4)){
                dash_end_clear = false;
            } else {
                UBYTE tile_xl = ((new_x >> 4) + PLAYER.bounds.left) >> 3;
                UBYTE tile_xr = (((new_x >> 4) + PLAYER.bounds.right) >> 3) +1;
                while (tile_xl != tile_xr){
                    while (tile_start != tile_end) {
                        if (tile_at(tile_xl, tile_start) & COLLISION_ALL) {
                            dash_end_clear = false;
                            goto initDash;
                        }
                        tile_start++;
                    }
                    tile_start = (((PLAYER.pos.y >> 4) + PLAYER.bounds.top)    >> 3);
                    tile_xl++;
                }
            }
        }
    }
    initDash:
    actor_attached = FALSE;
    camera_deadzone_x = plat_dash_deadzone;
    dash_ready_val = plat_dash_ready_max + plat_dash_frames;
    if(plat_dash_momentum < 2){
        pl_vel_y = 0;
    }
    dash_currentframe = plat_dash_frames;
    tap_val = 0;
    jump_type = 0;
    run_stage = 0;
    que_state = DASH_STATE;
}

UBYTE drop_press(void) BANKED {
    switch(plat_drop_through){
        case 1:
        if(INPUT_DOWN){
            return 1;
        }
        return 0;
        case 2:
        if (INPUT_PRESSED(INPUT_DOWN)){
            return 1;
        }
        return 0;
        case 3:
        if (INPUT_DOWN && INPUT_PLATFORM_JUMP){
            return 1;
        }
        return 0;
        case 4:
        if ((INPUT_PRESSED(INPUT_DOWN) && INPUT_PLATFORM_JUMP) || (INPUT_DOWN && INPUT_PRESSED(INPUT_PLATFORM_JUMP))){
            return 1;
        }
        return 0;
    }
    return 0;
}

void assign_state_script(SCRIPT_CTX * THIS) OLDCALL BANKED {
    UWORD *slot = VM_REF_TO_PTR(FN_ARG2);
    UBYTE *bank = VM_REF_TO_PTR(FN_ARG1);
    UBYTE **ptr = VM_REF_TO_PTR(FN_ARG0);
    state_events[*slot].script_bank = *bank;
    state_events[*slot].script_addr = *ptr;
}

void clear_state_script(SCRIPT_CTX * THIS) OLDCALL BANKED {
    UWORD *slot = VM_REF_TO_PTR(FN_ARG0);
    state_events[*slot].script_bank = NULL;
    state_events[*slot].script_addr = NULL;
}