#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/list.h"

#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"

#include "game.h"
#include "object.h"
#include "network.h"

#include "input.h"
#include "lcd.h"
#include "font.h"
#include "spaceship.h"
#include "asteroid.h"
#include "info.h"
#include "shield.h"
#include "projectile.h"
#include "impact.h"
#include "meteor.h"

//-----------------------------------------------------------------------------------------

#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

static const char* TAG = "game";
static TaskHandle_t game_task_handle = NULL;

static game_state_t game_state = {
    .current_player = 0,
    .play_timer = 0
};

static const uint8_t fps = 30;

void game_initialize();
void game_terminate();
static void game_process( void *pvParameters );
//-----------------------------------------------------------------------------------------
void game_initialize( game_mode_t mode ){
    ESP_LOGI(TAG,"initialize");
    if( game_task_handle == NULL ){
        if( mode == GAME_SERVER ){
            network_initialize( NETWORK_SERVER, NETWORK_TCP, &game_state );
        }
        else if( mode == GAME_CLIENT ){
            network_initialize( NETWORK_CLIENT, NETWORK_TCP, &game_state );
        }
        xTaskCreatePinnedToCore( game_process, "game_process", 4096, (void*)mode, 5, &game_task_handle, 0 );
    }
}
//-----------------------------------------------------------------------------------------
void game_terminate(){
    ESP_LOGI(TAG,"terminate");
    if( game_task_handle != NULL ){
        network_terminate();
        vTaskDelete( game_task_handle );
        game_task_handle = NULL;
    }
}
//-----------------------------------------------------------------------------------------
static void game_control_shield( bool activate ){
    spaceship_control_t* spaceship_control = &game_state.spaceships[game_state.current_player];
    
    if( activate && spaceship_control->ammunitions[AMMUNITION_SHIELD] > 0){
        spaceship_control->shield_active = true;
        spaceship_control->shield_animation_number = ( spaceship_control->shield_animation_number + 1 ) % image_shields_length;
        spaceship_control->ammunitions[AMMUNITION_SHIELD]--;
    }
    else {
        spaceship_control->shield_active = false;
    }
}
//-----------------------------------------------------------------------------------------
static void game_generate_projectile( projectile_type_t type ){
    spaceship_control_t* spaceship_control = &game_state.spaceships[game_state.current_player];
    
    if( type != PROJECTILE_MACHINEGUN ){
        if( spaceship_control->ammunitions[type] == 0 ){
            return;
        }
        spaceship_control->ammunitions[type]--;
    }

    for(size_t n = sizeof(game_state.spaceships)/sizeof(*game_state.spaceships); n < sizeof(game_state.projectiles)/sizeof(*game_state.projectiles); n++){
        projectile_control_t* projectile_control = &game_state.projectiles[n];
        
        if( projectile_control->active ){
            continue;
        }

        projectile_control->active = true;
        projectile_control->type = type;
        projectile_control->animation_number = 0;
        projectile_control->animation_countdown = 250;
        
        const image_t* spaceship_image = image_spaceships[ spaceship_control->mode ];
        if( projectile_control->type == PROJECTILE_MACHINEGUN ){
            static int8_t output = +1;
            
            const image_t* projectile_image = image_machinegun_projectile[ projectile_control->animation_number ];
            projectile_control->px = spaceship_control->px + 2 * spaceship_image->width / 3;
            projectile_control->py = spaceship_control->py + spaceship_image->height / 2 - projectile_image->height / 2 + ( spaceship_image->height / 3 ) * output;
            projectile_control->vi = +6.0;
            projectile_control->vj = 0.0;
            output *= -1;
        }
        else if( projectile_control->type == PROJECTILE_MISSILE ){
            const image_t* projectile_image = image_missile_projectile[ projectile_control->animation_number ];
            projectile_control->px = spaceship_control->px + spaceship_image->width;
            projectile_control->py = spaceship_control->py + spaceship_image->height / 2 - projectile_image->height  / 2;
            projectile_control->vi = +3.0;
            projectile_control->vj = 0.0;
        }
        else if( projectile_control->type == PROJECTILE_MINE ){
            const image_t* projectile_image = image_mine_projectile[ projectile_control->animation_number ];
            projectile_control->px = spaceship_control->px + spaceship_image->width / 2 - projectile_image->width / 2, 
            projectile_control->py =  spaceship_control->py + spaceship_image->height / 2 - projectile_image->height / 2, 
            projectile_control->vi = 0.0;
            projectile_control->vj = -0.1;
        }

        break;
    }
}
//-----------------------------------------------------------------------------------------
static void game_input_actions(){
    spaceship_control_t* spaceship_control = &game_state.spaceships[game_state.current_player];
    
    if( !spaceship_control->active ){
        return;
    }
    
    static uint8_t levels[4] = {};
    for(size_t n = 0; n < 4; n++){
        input_action(n,&levels[n]);
    }
    
    static int64_t timers[4] = {};
    const int64_t current_time = esp_timer_get_time();
    
    if(levels[ACTION_UP]){
        const uint32_t time_diff = ( current_time - timers[ACTION_UP] ) / 1000;
        if(time_diff >= 50){
            timers[ACTION_UP] = current_time;
            game_generate_projectile( PROJECTILE_MACHINEGUN );
        }
    }
    if(levels[ACTION_LEFT]){
        const uint32_t time_diff = ( current_time - timers[ACTION_LEFT] ) / 1000;
        if(time_diff >= 250){
            levels[ACTION_LEFT] = 0;
            timers[ACTION_LEFT] = current_time;
            game_generate_projectile( PROJECTILE_MISSILE );
        }
    }
    if(levels[ACTION_RIGHT]){
        const uint32_t time_diff = ( current_time - timers[ACTION_RIGHT] ) / 1000;
        if(time_diff >= 1000){
            levels[ACTION_RIGHT] = 0;
            timers[ACTION_RIGHT] = current_time;
            game_generate_projectile( PROJECTILE_MINE );
        }
    }
    
    const uint32_t time_diff = ( current_time - timers[ACTION_DOWN] ) / 1000;
    if(time_diff >= 100){
        if(levels[ACTION_DOWN]){
            timers[ACTION_DOWN] = current_time;
        }
        game_control_shield( levels[ACTION_DOWN] );
    }
}
//-----------------------------------------------------------------------------------------
static void game_input_directions(){
    spaceship_control_t* spaceship_control = &game_state.spaceships[game_state.current_player];
    
    if( !spaceship_control->active ){
        return;
    }
    
    static double x = 0.0;
    static double y = 0.0;
    input_directions(&x, &y);
    
    spaceship_control->vi = x;
    spaceship_control->vj = y;
}
//-----------------------------------------------------------------------------------------
static void game_input(){
    game_input_actions();
    game_input_directions();
}
//-----------------------------------------------------------------------------------------
static void game_move_projectiles( uint32_t milliseconds ){
    for(size_t n = 0; n < sizeof(game_state.projectiles)/sizeof(*game_state.projectiles); n++){
        projectile_control_t* projectile_control = &game_state.projectiles[n];
        
        if( !projectile_control->active ){
            continue;
        }
        
        if( projectile_control->type == PROJECTILE_MISSILE ){
            if( projectile_control->animation_countdown > milliseconds ){
                projectile_control->animation_countdown -= milliseconds;
            }
            else {
                projectile_control->animation_countdown = 250;
                projectile_control->animation_number = ( projectile_control->animation_number + 1 ) % image_missile_projectile_length;
            }
        }
        else if( projectile_control->type == PROJECTILE_MACHINEGUN ){
            // Nothing
        }
        else if( projectile_control->type == PROJECTILE_MINE ){
            if( projectile_control->animation_countdown > milliseconds ){
                projectile_control->animation_countdown -= milliseconds;
            }
            else {
                projectile_control->animation_countdown = 2000;
                if( projectile_control->animation_number == 0 ){
                    projectile_control->vj = +0.1;
                }
                else if( projectile_control->animation_number == 1 ){
                    projectile_control->vj = -0.1;
                }
                projectile_control->animation_number = ( projectile_control->animation_number + 1 ) % 2;
            }
        }
        
        projectile_control->px += projectile_control->vi;
        projectile_control->py += projectile_control->vj;
        
        const image_t* projectile_image = NULL;
        if( projectile_control->type == PROJECTILE_MACHINEGUN ){
            projectile_image = image_machinegun_projectile[ 0 ];
        }
        else if( projectile_control->type == PROJECTILE_MISSILE ){
            projectile_image = image_missile_projectile[ projectile_control->animation_number ];
        }
        else if( projectile_control->type == PROJECTILE_MINE ){
            projectile_image = image_mine_projectile[ 0 ];
        }
        if(    (int16_t)projectile_control->px + projectile_image->width < 0
            || (int16_t)projectile_control->py + projectile_image->height < 0
            || (int16_t)projectile_control->px >= lcd_width() 
            || (int16_t)projectile_control->py >= lcd_height() ){
            projectile_control->active = false;
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_move_asteroids( uint32_t milliseconds ){
    for(size_t n = 0; n < sizeof(game_state.asteroids)/sizeof(*game_state.asteroids); n++){
        asteroid_control_t* asteroid_control = &game_state.asteroids[n];
        
        if( !asteroid_control->active ){
            continue;
        }

        asteroid_control->px += asteroid_control->vi;
        asteroid_control->py += asteroid_control->vj;
        
        if( asteroid_control->damage_active ){
            if( asteroid_control->damage_countdown > milliseconds ){
                asteroid_control->damage_countdown -= milliseconds;
            }
            else {
                asteroid_control->damage_countdown = 0;
                asteroid_control->damage_active = false;
            }
        }
        else {
            if( asteroid_control->damage_countdown + milliseconds < 50 ){
                asteroid_control->damage_countdown += milliseconds;
            }
            else {
                asteroid_control->damage_countdown = 50;
            }
        }
        
        const image_t* asteroid_image = image_asteroids[ asteroid_control->number ][ asteroid_control->orientation ][ 0 ];
        if(    (int16_t)asteroid_control->px + asteroid_image->width < 0
            || (int16_t)asteroid_control->py + asteroid_image->height < 0
            || (int16_t)asteroid_control->px >= lcd_width() 
            || (int16_t)asteroid_control->py >= lcd_height() ){
            asteroid_control->active = false;
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_move_pickups( uint32_t milliseconds ){
    for(size_t n = 0; n < sizeof(game_state.pickups)/sizeof(*game_state.pickups); n++){
        pickup_control_t* pickup_control = &game_state.pickups[n];
        
        if( !pickup_control->active ){
            continue;
        }
        
        pickup_control->trajectory_progress += 0.02;
        if( pickup_control->trajectory_progress > 1.0 ){
            pickup_control->trajectory_progress = 0.0;
        }
        
        pickup_control->px += pickup_control->vi;
        pickup_control->py += pickup_control->vj * sin( 2.0 * M_PI * pickup_control->trajectory_progress );

        
        const image_t* pickup_image = image_pickups[ pickup_control->type ];
        if(    (int16_t)pickup_control->px + pickup_image->width < 0
            || (int16_t)pickup_control->py + pickup_image->height < 0
            || (int16_t)pickup_control->px >= lcd_width() 
            || (int16_t)pickup_control->py >= lcd_height() ){
            pickup_control->active = false;
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_move_spaceships( uint32_t milliseconds ){
    for(size_t n = 0; n < sizeof(game_state.spaceships)/sizeof(*game_state.spaceships); n++){
        spaceship_control_t* spaceship_control = &game_state.spaceships[n];
        
        if( !spaceship_control->active ){
            continue;
        }
        
        {
            spaceship_control->px += spaceship_control->vi * fps / 20.0 * ( spaceship_control->shield_active ? 1.5 : 1.0 );
            spaceship_control->py += spaceship_control->vj * fps / 20.0 * ( spaceship_control->shield_active ? 1.5 : 1.0 );
            
            
            const image_t* spaceship_image = image_spaceships[ spaceship_control->mode ];
            if(spaceship_control->px > ( lcd_width() - spaceship_image->width - 1 )){
                spaceship_control->px = ( lcd_width() - spaceship_image->width - 1 );
            }
            else if(spaceship_control->px <= 0.00){
                spaceship_control->px = 0.00;
            }
            
            if(spaceship_control->py > ( lcd_height() - spaceship_image->height - 1 )){
                spaceship_control->py = ( lcd_height() - spaceship_image->height - 1 );
            }
            else if(spaceship_control->py <= 0.00){
                spaceship_control->py = 0.00;
            }
        }
        
        {
            if(spaceship_control->vj > 0.30){
                spaceship_control->mode = SPACESHIP_UP;
            }
            else if(spaceship_control->vj < -0.30){
                spaceship_control->mode = SPACESHIP_DOWN;
            }
            else if(spaceship_control->vi > 0.30){
                spaceship_control->mode = SPACESHIP_RIGHT;
            }
            else if(spaceship_control->vi < -0.30){
                spaceship_control->mode = SPACESHIP_LEFT;
            }
            else {
                spaceship_control->mode = SPACESHIP_IDLE;
            }
            
            if( spaceship_control->mode != SPACESHIP_IDLE ){
                double modulus = sqrt( pow(spaceship_control->vi,2) + pow(spaceship_control->vj,2) );
                
                if(modulus > 0.90){
                    spaceship_control->mode |= SPACESHIP_STRONG;
                }
                else if(modulus > 0.30){
                    spaceship_control->mode |= SPACESHIP_MEDIUM;
                }
                else {
                    spaceship_control->mode |= SPACESHIP_WEAK;
                }
            }
        }
    
        if( spaceship_control->collision_countdown > milliseconds ){
            spaceship_control->collision_countdown -= milliseconds;
            if( spaceship_control->collision_countdown >= 2000 ){
                spaceship_control->collision_active = ( spaceship_control->collision_countdown / 250 ) % 2;
            }
            else if( spaceship_control->collision_countdown >= 500 ){
                spaceship_control->collision_active = ( spaceship_control->collision_countdown / 100 ) % 2;
            }
            else {
                spaceship_control->collision_active = ( spaceship_control->collision_countdown / 50 ) % 2;
            }
        }
        else {
            spaceship_control->collision_active = false;
            spaceship_control->collision_countdown = 0;
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_move_impacts( uint32_t milliseconds ){
    for(size_t n = 0; n < sizeof(game_state.impacts)/sizeof(*game_state.impacts); n++){
        impact_control_t* impact_control = &game_state.impacts[n];
        
        if( !impact_control->active ){
            continue;
        }

        if( impact_control->animation_countdown > milliseconds ){
            impact_control->animation_countdown -= milliseconds;
        }
        else {
            impact_control->animation_countdown = 50;
            impact_control->animation_number++;
            
            if(    ( impact_control->size == IMPACT_SMALL  && impact_control->animation_number < 1 * image_impact_shockwave_length / 3 )
                || ( impact_control->size == IMPACT_MEDIUM && impact_control->animation_number < 2 * image_impact_shockwave_length / 3 ) 
                || ( impact_control->size == IMPACT_BIG    && impact_control->animation_number < 3 * image_impact_shockwave_length / 3 ) 
            ){
                // Nothing
            }
            else {
                impact_control->active = false;
            }
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_move_meteors( uint32_t milliseconds ){
    for(size_t n = 0; n < sizeof(game_state.meteors)/sizeof(*game_state.meteors); n++){
        meteor_control_t* meteor_control = &game_state.meteors[n];
        
        if( !meteor_control->active ){
            continue;
        }

        if( meteor_control->arrow_active ){
            if( meteor_control->arrow_countdown > milliseconds ){
                meteor_control->arrow_countdown -= milliseconds;

                meteor_control->arrow_progress += 1.5;
                
                if( meteor_control->direction == DIRECTION_DOWN || meteor_control->direction == DIRECTION_UP ){
                    const image_t* arrow_image = image_arrow[ IMAGE_ROTATED_0 ];
                    
                    if( meteor_control->arrow_progress > arrow_image->width ){
                        meteor_control->arrow_progress = 0.0;
                    }
                }
                else if( meteor_control->direction == DIRECTION_LEFT || meteor_control->direction == DIRECTION_RIGHT ){
                    const image_t* arrow_image = image_arrow[ IMAGE_ROTATED_90 ];
                    
                    if( meteor_control->arrow_progress > arrow_image->height ){
                        meteor_control->arrow_progress = 0.0;
                    }
                }
            }
            else {
                meteor_control->arrow_active = false;
            }
        }
        else {
            if( meteor_control->animation_countdown > milliseconds ){
                meteor_control->animation_countdown -= milliseconds;
            }
            else {
                meteor_control->animation_countdown = 100;
                meteor_control->animation_number = ( meteor_control->animation_number + 1 ) % image_meteor_length;
            }
            
            meteor_control->px += meteor_control->vi;
            meteor_control->py += meteor_control->vj;
            
            const image_t* meteor_image = NULL;
            if( meteor_control->direction == DIRECTION_DOWN ){
                meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_0 ];
            }
            else if( meteor_control->direction == DIRECTION_LEFT ){
                meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_270 ];
            }
            else if( meteor_control->direction == DIRECTION_RIGHT ){
                meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_90 ];
            }
            else if( meteor_control->direction == DIRECTION_UP ){
                meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_180 ];
            }
            if(    (int16_t)meteor_control->px + meteor_image->width < 0
                || (int16_t)meteor_control->py + meteor_image->height < 0
                || (int16_t)meteor_control->px >= lcd_width() 
                || (int16_t)meteor_control->py >= lcd_height() ){
                meteor_control->active = false;
            }
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_move( uint32_t milliseconds ){
    //game_move_background();
    game_move_spaceships( milliseconds );
    game_move_asteroids( milliseconds );
    game_move_projectiles( milliseconds );
    game_move_impacts( milliseconds );
    game_move_pickups( milliseconds );
    game_move_meteors( milliseconds );
}
//-----------------------------------------------------------------------------------------
static bool game_objects_collide( int16_t x0, int16_t y0, const image_t* image0, int16_t x1, int16_t y1, const image_t* image1, int16_t* collision_x, int16_t* collision_y ){
    // intersection
    // https://stackoverflow.com/questions/19753134/get-the-points-of-intersection-from-2-rectangles
    const int16_t x5 = max( x0, x1 );
    const int16_t y5 = max( y0, y1 );
    const int16_t x6 = min( x0 + image0->width,  x1 + image1->width );
    const int16_t y6 = min( y0 + image0->height, y1 + image1->height );

    if (x5 < x6 && y5 < y6 )
    {
        for( int16_t y = y5; y < y6; y++ )
        {
            const uint16_t* control1_row_ptr = &image0->data[ ( y - y0 ) * image0->width + ( x5 - x0 ) ];
            const uint16_t* control2_row_ptr = &image1->data[ ( y - y1 ) * image1->width + ( x5 - x1 ) ];
            
            for( int16_t x = x5; x < x6; x++ )
            {
                // check non-transparent pixels
                if(*control1_row_ptr != image0->transparent_color && *control2_row_ptr != image1->transparent_color)
                {
                    if( collision_x != NULL ) 
                    {
                        *collision_x = x;
                    }
                    if( collision_y != NULL ) 
                    {
                        *collision_y = y;
                    }
                    return true;
                }
                control1_row_ptr++;
                control2_row_ptr++;
            }
        }
    }
    return false;
}
//-----------------------------------------------------------------------------------------
static void game_generate_impact( int16_t x, int16_t y, impact_type_t type, impact_size_t size ){
    for(size_t n = 0; n < sizeof(game_state.impacts)/sizeof(*game_state.impacts); n++){
        impact_control_t* impact_control = &game_state.impacts[n];
        
        if( impact_control->active ){
            continue;
        }

        impact_control->active = true;
        impact_control->animation_number = 0;
        impact_control->animation_countdown = 50;
        const image_t* impact_image = image_impact_shockwave[ impact_control->animation_number ];
        impact_control->px = x - impact_image->width  / 2;
        impact_control->py = y - impact_image->height / 2;
        impact_control->type = type;
        impact_control->size = size;

        break;
    }
}
//-----------------------------------------------------------------------------------------
static void game_collide_projectiles_asteroids(){
    for(size_t n = 0; n < sizeof(game_state.projectiles)/sizeof(*game_state.projectiles); n++){
        projectile_control_t* projectile_control = &game_state.projectiles[n];
        
        if( !projectile_control->active ){
            continue;
        }
        
        for(size_t n = 0; n < sizeof(game_state.asteroids)/sizeof(*game_state.asteroids); n++){
            asteroid_control_t* asteroid_control = &game_state.asteroids[n];
            
            if( ! asteroid_control->active ){
                continue;
            }
            
            const image_t* asteroid_image = image_asteroids[ asteroid_control->number ][ asteroid_control->orientation ][ asteroid_control->damage_active ];
            const image_t* projectile_image = NULL;
            if( projectile_control->type == PROJECTILE_MACHINEGUN ){
                projectile_image = image_machinegun_projectile[ 0 ];
            }
            else if( projectile_control->type == PROJECTILE_MISSILE ){
                projectile_image = image_missile_projectile[ projectile_control->animation_number ];
            }
            else if( projectile_control->type == PROJECTILE_MINE ){
                projectile_image = image_mine_projectile[ 0 ];
            }
            int16_t collision_x, collision_y;
            if( ! game_objects_collide( projectile_control->px,
                                        projectile_control->py,
                                        projectile_image, 
                                        asteroid_control->px, 
                                        asteroid_control->py, 
                                        asteroid_image, 
                                        &collision_x, 
                                        &collision_y ) 
            ){
                continue;
            }
            
            projectile_control->active = false;
            
            uint8_t damage = 0;
            if( projectile_control->type == PROJECTILE_MACHINEGUN ){
                damage = 1;
            }
            else if( projectile_control->type == PROJECTILE_MISSILE ){
                damage = 12;
                game_generate_impact( collision_x, collision_y, IMPACT_SHOCKWAVE, IMPACT_MEDIUM );
            }
            else if( projectile_control->type == PROJECTILE_MINE ){
                damage = 24;
                game_generate_impact( collision_x, collision_y, IMPACT_SHOCKWAVE, IMPACT_BIG );
            }
            
            if( asteroid_control->life > damage ){
                asteroid_control->life -= damage;
                if( !asteroid_control->damage_active && asteroid_control->damage_countdown == 50 ){
                    asteroid_control->damage_active = true;
                }
            }
            else {
                asteroid_control->active = false;  
            }
            
            break;
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_collide_spaceships_asteroids(){
    for(size_t n = 0; n < sizeof(game_state.spaceships)/sizeof(*game_state.spaceships); n++){
        spaceship_control_t* spaceship_control = &game_state.spaceships[n];
        
        if( !spaceship_control->active || spaceship_control->collision_countdown > 0 || spaceship_control->shield_active ){
            continue;
        }
        
        for(size_t n = 0; n < sizeof(game_state.asteroids)/sizeof(*game_state.asteroids); n++){
            asteroid_control_t* asteroid_control = &game_state.asteroids[n];
            
            if( !asteroid_control->active ){
                continue;
            }
            
            const image_t* spaceship_image = image_spaceships[ spaceship_control->mode ];
            const image_t* asteroid_image = image_asteroids[ asteroid_control->number ][ asteroid_control->orientation ][ asteroid_control->damage_active ];
            if( ! game_objects_collide( spaceship_control->px, 
                                        spaceship_control->py, 
                                        spaceship_image, 
                                        asteroid_control->px, 
                                        asteroid_control->py, 
                                        asteroid_image, 
                                        NULL,
                                        NULL )
            ){
                continue;
            }

            if(spaceship_control->life > 0){
                //spaceship_control->life--;
                spaceship_control->collision_countdown = 3000;
                spaceship_control->collision_active = true;
            }
            else {
                spaceship_control->active = false;
                game_generate_impact( spaceship_control->px + spaceship_image->width / 2, 
                                      spaceship_control->py + spaceship_image->height / 2,
                                      IMPACT_SHOCKWAVE, IMPACT_BIG );
            }
            
            break;
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_collide_meteors_spaceships_asteroids(){
    for(size_t n = 0; n < sizeof(game_state.meteors)/sizeof(*game_state.meteors); n++){
        meteor_control_t* meteor_control = &game_state.meteors[n];
        
        if( ! meteor_control->active || meteor_control->arrow_active){
            continue;
        }
        
        const image_t* meteor_image = NULL;
        if( meteor_control->direction == DIRECTION_DOWN ){
            meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_0 ];
        }
        else if( meteor_control->direction == DIRECTION_LEFT ){
            meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_270 ];
        }
        else if( meteor_control->direction == DIRECTION_RIGHT ){
            meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_90 ];
        }
        else if( meteor_control->direction == DIRECTION_UP ){
            meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_180 ];
        }
        
       
        for(size_t n = 0; n < sizeof(game_state.asteroids)/sizeof(*game_state.asteroids); n++){
            asteroid_control_t* asteroid_control = &game_state.asteroids[n];
            
            if( !asteroid_control->active ){
                continue;
            } 
            
            const image_t* asteroid_image = image_asteroids[ asteroid_control->number ][ asteroid_control->orientation ][ asteroid_control->damage_active ];
            
            int16_t collision_x, collision_y;
            if( ! game_objects_collide( asteroid_control->px, 
                                        asteroid_control->py, 
                                        asteroid_image, 
                                        meteor_control->px,
                                        meteor_control->py,
                                        meteor_image, 
                                        &collision_x,
                                        &collision_y )
            ){
                continue;
            }

            game_generate_impact( collision_x, collision_y, IMPACT_SHOCKWAVE, IMPACT_BIG );
            asteroid_control->active = false;  
        }

        for(size_t n = 0; n < sizeof(game_state.spaceships)/sizeof(*game_state.spaceships); n++){
            spaceship_control_t* spaceship_control = &game_state.spaceships[n];
            
            if( !spaceship_control->active || spaceship_control->collision_countdown > 0 || spaceship_control->shield_active ){
                continue;
            }    
            
            const image_t* spaceship_image = image_spaceships[ spaceship_control->mode ];
            if( ! game_objects_collide( spaceship_control->px, 
                                        spaceship_control->py, 
                                        spaceship_image, 
                                        meteor_control->px, 
                                        meteor_control->py, 
                                        meteor_image, 
                                        NULL,
                                        NULL )
            ){
                continue;
            }

            if(spaceship_control->life > 0){
                //spaceship_control->life--;
                spaceship_control->collision_countdown = 3000;
                spaceship_control->collision_active = true;
            }
            else {
                spaceship_control->active = false;
                game_generate_impact( spaceship_control->px + spaceship_image->width / 2, spaceship_control->py + spaceship_image->height / 2, IMPACT_SHOCKWAVE, IMPACT_BIG );
            }
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_collide_spaceships_pickups(){
    for(size_t n = 0; n < sizeof(game_state.spaceships)/sizeof(*game_state.spaceships); n++){
        spaceship_control_t* spaceship_control = &game_state.spaceships[n];
        
        if( !spaceship_control->active ){
            continue;
        }
        
        for(size_t n = 0; n < sizeof(game_state.pickups)/sizeof(*game_state.pickups); n++){
            pickup_control_t* pickup_control = &game_state.pickups[n];
            
            if( ! pickup_control->active ){
                continue;
            }
            
            const image_t* spaceship_image = image_spaceships[ spaceship_control->mode ];
            const image_t* pickup_image = image_pickups[ pickup_control->type ];
            if( ! game_objects_collide( spaceship_control->px, spaceship_control->py, spaceship_image, pickup_control->px, pickup_control->py, pickup_image, NULL, NULL ) ){
                continue;
            }
            
            if( pickup_control->type == PICKUP_HEART ){
                if( spaceship_control->life < 3 ){
                    spaceship_control->life++;
                    pickup_control->active = false;
                }
            }
            else if( pickup_control->type == PICKUP_MISSILE ){
                if( spaceship_control->ammunitions[AMMUNITION_MISSILE] < 20 ){
                    spaceship_control->ammunitions[AMMUNITION_MISSILE] = 20;
                    pickup_control->active = false;
                }
            }
            else if( pickup_control->type == PICKUP_MINE ){
                if( spaceship_control->ammunitions[AMMUNITION_MINE] < 10 ){
                    spaceship_control->ammunitions[AMMUNITION_MINE] = 10;
                    pickup_control->active = false;
                }
            }
            else if( pickup_control->type == PICKUP_SHIELD ){
                if( spaceship_control->ammunitions[AMMUNITION_SHIELD] < 30 ){
                    spaceship_control->ammunitions[AMMUNITION_SHIELD] = 30;
                    pickup_control->active = false;
                }
            }
            
            break;
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_collide(){
    game_collide_spaceships_asteroids();
    game_collide_spaceships_pickups();
    game_collide_projectiles_asteroids();
    game_collide_meteors_spaceships_asteroids();
}
//-----------------------------------------------------------------------------------------
static void game_draw_spaceships(){
    for(size_t n = 0; n < sizeof(game_state.spaceships)/sizeof(*game_state.spaceships); n++){
        const spaceship_control_t* spaceship_control = &game_state.spaceships[n];
        
        if( !spaceship_control->active || spaceship_control->collision_active ){
            continue;
        }

        const image_t* spaceship_image = image_spaceships[ spaceship_control->mode ];
        lcd_draw_image( spaceship_control->px, spaceship_control->py, spaceship_image );
        
        if( spaceship_control->shield_active ){
            const image_t* shield_image = image_shields[ spaceship_control->shield_animation_number ];
            lcd_draw_image( spaceship_control->px + spaceship_image->width / 2 - shield_image->width / 2, 
                            spaceship_control->py + spaceship_image->height / 2 - shield_image->height / 2, 
                            shield_image );
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_draw_background(){
    static double i = 0.0;
    lcd_draw_image( -i, 0, &image_space );
    if( i > image_space.width - lcd_width() ){
        lcd_draw_image( -i + image_space.width, 0, &image_space );
    }
    i += fps / 45.0;
    if( i >= image_space.width ){
        i = 0.0;
    }
}
//-----------------------------------------------------------------------------------------
static void game_draw_projectiles(){
    for(size_t n = 0; n < sizeof(game_state.projectiles)/sizeof(*game_state.projectiles); n++){
        const projectile_control_t* projectile_control = &game_state.projectiles[n];
        
        if( !projectile_control->active ){
            continue;
        }
        
        const image_t* projectile_image = NULL;
        if( projectile_control->type == PROJECTILE_MACHINEGUN ){
            projectile_image = image_machinegun_projectile[ 0 ];
        }
        else if( projectile_control->type == PROJECTILE_MISSILE ){
            projectile_image = image_missile_projectile[ projectile_control->animation_number ];
        }
        else if( projectile_control->type == PROJECTILE_MINE ){
            projectile_image = image_mine_projectile[ 0 ];
        }
        lcd_draw_image( projectile_control->px, projectile_control->py, projectile_image );
    }
}
//-----------------------------------------------------------------------------------------
static void game_draw_asteroids(){
    for(size_t n = 0; n < sizeof(game_state.asteroids)/sizeof(*game_state.asteroids); n++){
        const asteroid_control_t* asteroid_control = &game_state.asteroids[n];
        
        if( !asteroid_control->active ){
            continue;
        }
        
        const image_t* asteroid_image = image_asteroids[ asteroid_control->number ][ asteroid_control->orientation ][ asteroid_control->damage_active ];
        lcd_draw_image( asteroid_control->px, asteroid_control->py, asteroid_image );
    }
}
//-----------------------------------------------------------------------------------------
static void game_draw_impacts(){
    for(size_t n = 0; n < sizeof(game_state.impacts)/sizeof(*game_state.impacts); n++){
        const impact_control_t* impact_control = &game_state.impacts[n];
        
        if( !impact_control->active ){
            continue;
        }
        
        const image_t* impact_image = image_impact_shockwave[ impact_control->animation_number ];
        lcd_draw_image( impact_control->px, impact_control->py, impact_image );
    }
}
//-----------------------------------------------------------------------------------------
static void game_draw_pickups(){
    for(size_t n = 0; n < sizeof(game_state.pickups)/sizeof(*game_state.pickups); n++){
        const pickup_control_t* pickup_control = &game_state.pickups[n];
        
        if( !pickup_control->active ){
            continue;
        }
        
        const image_t* pickup_image = image_pickups[ pickup_control->type ];
        lcd_draw_image( pickup_control->px, pickup_control->py, pickup_image );
    }
}
//-----------------------------------------------------------------------------------------
static void game_draw_meteors(){
    for(size_t n = 0; n < sizeof(game_state.meteors)/sizeof(*game_state.meteors); n++){
        const meteor_control_t* meteor_control = &game_state.meteors[n];
        
        if( !meteor_control->active ){
            continue;
        }
    
        if( meteor_control->arrow_active ){
            if( meteor_control->direction == DIRECTION_DOWN ){
                const image_t* arrow_image = image_arrow[ IMAGE_ROTATED_270 ];
                
                double py = meteor_control->py - arrow_image->height + meteor_control->arrow_progress;
                while( py < lcd_height() + arrow_image->height ){
                    lcd_draw_image( meteor_control->px, py, arrow_image );
                    py += arrow_image->height;
                }
            }
            else if( meteor_control->direction == DIRECTION_LEFT ){
                const image_t* arrow_image = image_arrow[ IMAGE_ROTATED_180 ];
                
                double px = meteor_control->px + arrow_image->width - meteor_control->arrow_progress;
                while( px > 0.0 - arrow_image->width ){
                    lcd_draw_image( px, meteor_control->py, arrow_image );
                    px -= arrow_image->width;
                }
            }
            else if( meteor_control->direction == DIRECTION_RIGHT ){
                const image_t* arrow_image = image_arrow[ IMAGE_ROTATED_0 ];
                
                double px = meteor_control->px - arrow_image->width + meteor_control->arrow_progress;
                while( px < lcd_width() + arrow_image->width ){
                    lcd_draw_image( px,  meteor_control->py, arrow_image );
                    px += arrow_image->width;
                }
            }
            else if( meteor_control->direction == DIRECTION_UP ){
                const image_t* arrow_image = image_arrow[ IMAGE_ROTATED_90 ];
                
                double py = meteor_control->py + arrow_image->height - meteor_control->arrow_progress;
                while( py > 0.0 - arrow_image->height ){
                    lcd_draw_image( meteor_control->px, py, arrow_image );
                    py -= arrow_image->height;
                }
            }
        }
        else {
            const image_t* meteor_image = NULL;
            if( meteor_control->direction == DIRECTION_DOWN ){
                meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_0 ];
            }
            else if( meteor_control->direction == DIRECTION_LEFT ){
                meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_270 ];
            }
            else if( meteor_control->direction == DIRECTION_RIGHT ){
                meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_90 ];
            }
            else if( meteor_control->direction == DIRECTION_UP ){
                meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_180 ];
            }
            lcd_draw_image( meteor_control->px, meteor_control->py, meteor_image );
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_draw_infos(){
    spaceship_control_t* spaceship_control = &game_state.spaceships[game_state.current_player];
    
    lcd_draw_rectangle_filled( 0, 162, lcd_width() - 1, lcd_height() - 1, COLOR_BLACK );
 
    lcd_set_font( &font9x7 );
    for(size_t n = 0; n < image_ammunitions_length; n++){
        lcd_draw_image( 8 + 35 * n, 166, image_ammunitions[n] );
        if(n == 0){
            lcd_draw_image( 20 + 35 * n, 166, &image_infinity );
        }
        else {
            lcd_printf( 20 + 35 * n, 166, COLOR_RED, "%02u", spaceship_control->ammunitions[n] );
        }
    }
    
    for(size_t n = 0; n < 3; n++){
        lcd_draw_image( 170 + 12 * n, 166, image_hearts[ spaceship_control->life > n ] );
    }
}
//-----------------------------------------------------------------------------------------
static void game_draw(){
    game_draw_background();
    game_draw_spaceships();
    game_draw_asteroids();
    game_draw_impacts();
    game_draw_projectiles();
    game_draw_pickups();
    game_draw_meteors();
    game_draw_infos();
    lcd_refresh();
}
//-----------------------------------------------------------------------------------------
static void game_generate_meteor( uint32_t milliseconds ){
    static uint32_t generator_countdown = 1000;
    
    if( generator_countdown > milliseconds ){
        generator_countdown -= milliseconds;
    }
    else {
        generator_countdown = 1000;
        
        for(size_t n = 0; n < sizeof(game_state.meteors)/sizeof(*game_state.meteors); n++){
            meteor_control_t* meteor_control = &game_state.meteors[n];
        
            if( meteor_control->active ){
                continue;
            }
           
            meteor_control->active = true;
            meteor_control->arrow_active = true;
            meteor_control->arrow_countdown = 2000;
            meteor_control->arrow_progress = 0.0;
            meteor_control->animation_number = 0;
            meteor_control->animation_countdown = 100;

            meteor_control->direction = esp_random() % 4;
            if( meteor_control->direction == DIRECTION_DOWN ){
                const image_t* meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_0 ];
                meteor_control->vi = 0.0;
                meteor_control->vj = +6.0;
                meteor_control->px = (double)( meteor_image->width + ( esp_random() % lcd_width() - 2 * meteor_image->width ) );
                meteor_control->py = 0.0;
            }
            else if( meteor_control->direction == DIRECTION_LEFT ){
                const image_t* meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_270 ];
                meteor_control->vi = -6.0;
                meteor_control->vj = 0.0;
                meteor_control->px = (double)( lcd_width() - 1 );
                meteor_control->py = (double)( meteor_image->height + ( esp_random() % lcd_height() - 2 * meteor_image->height ) );
            }
            else if( meteor_control->direction == DIRECTION_RIGHT ){
                const image_t* meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_90 ];
                meteor_control->vi = +6.0;
                meteor_control->vj = 0.0;
                meteor_control->px = 0.0;
                meteor_control->py = (double)( meteor_image->height + ( esp_random() % lcd_height() - 2 * meteor_image->height ) );
            }
            else if( meteor_control->direction == DIRECTION_UP ){
                const image_t* meteor_image = image_meteor[ meteor_control->animation_number ][ IMAGE_ROTATED_180 ];
                meteor_control->vi = 0.0;
                meteor_control->vj = -6.0;
                meteor_control->px = (double)( meteor_image->width + ( esp_random() % lcd_width() - 2 * meteor_image->width ) );
                meteor_control->py = (double)( lcd_height() - 1 );
            }
            break;
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_generate_pickup( uint32_t milliseconds ){
    static uint32_t generator_countdown = 10000;
    
    if( generator_countdown > milliseconds ){
        generator_countdown -= milliseconds;
    }
    else {
        generator_countdown = 10000;
        
        for(size_t n = 0; n < sizeof(game_state.pickups)/sizeof(*game_state.pickups); n++){
            pickup_control_t* pickup_control = &game_state.pickups[n];
        
            if( pickup_control->active ){
                continue;
            }
            
            pickup_control->active = true;
            pickup_control->type = 1 + esp_random() % ( image_pickups_length - 1 );
            
            const image_t* pickup_image = image_pickups[ pickup_control->type ];
			pickup_control->px = lcd_width() - 1;
			pickup_control->py = ( pickup_image->height / 2 ) + esp_random() % ( lcd_height() - pickup_image->height );
            pickup_control->vi = - (double)( 2 + esp_random() % 3 );
            pickup_control->vj = 2.0 + ( esp_random() % 4 ) / 4.0;
            pickup_control->trajectory_progress = 0.0;
            break;
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_generate_asteroid( uint32_t milliseconds ){
    static uint16_t generator_countdown = 1000;

    if( generator_countdown > milliseconds ){
        generator_countdown -= milliseconds;
    }
    else {
        generator_countdown = 1000;
        
        for(size_t n = 0; n < sizeof(game_state.asteroids)/sizeof(*game_state.asteroids); n++){
            asteroid_control_t* asteroid_control = &game_state.asteroids[n];
        
            if( asteroid_control->active ){
                continue;
            }
            
            asteroid_control->active = true;
            asteroid_control->number = esp_random() % image_asteroids_length;
            asteroid_control->life = 2 + asteroid_control->number * 3;
            {
                const uint8_t rotate = esp_random() % 4;
                const uint8_t mirror = esp_random() % 4;
                
                asteroid_control->orientation = IMAGE_ROTATED_0;
                if( rotate == 1 ){
                    asteroid_control->orientation = IMAGE_ROTATED_90;
                }
                else if( rotate == 2 ){
                    asteroid_control->orientation = IMAGE_ROTATED_180;
                }
                else if( rotate == 3 ){
                    asteroid_control->orientation = IMAGE_ROTATED_270;
                }
                
                if( mirror == 1 ){
                    asteroid_control->orientation |= IMAGE_MIRRORED_HORIZONTAL;
                }
                else if( mirror == 2 ){
                    asteroid_control->orientation |= IMAGE_MIRRORED_VERTICAL;
                }
                else if( mirror == 3 ){
                    asteroid_control->orientation |= IMAGE_MIRRORED_BOTH;
                }
            }
            const image_t* asteroid_image = image_asteroids[ asteroid_control->number ][ asteroid_control->orientation ][ 0 ];
			asteroid_control->px = lcd_width() - 1;
			asteroid_control->py = esp_random() % lcd_height();
			asteroid_control->vi = - (double)( 1 + esp_random() % 3 );
            asteroid_control->vj = 0;
            break;
        }
    }
}
//-----------------------------------------------------------------------------------------
static void game_generate( uint32_t milliseconds ){
    game_generate_asteroid( milliseconds );
    game_generate_pickup( milliseconds );
    game_generate_meteor( milliseconds );
}
//-----------------------------------------------------------------------------------------
static void game_receive(){
    network_receive();
}
//-----------------------------------------------------------------------------------------
static void game_send(){
    network_send();
}
//-----------------------------------------------------------------------------------------
static void game_process( void *pvParameters ){
    game_mode_t mode = (game_mode_t)pvParameters;
    
    const spaceship_control_t spaceship_control = {
        .active = true,
        .mode = SPACESHIP_IDLE,
        .px = 50.0,
        .py = 50.0,
        .vi = 0.0,
        .vj = 0.0,
        .life = 3,
        .ammunitions = { 0, 20, 10, 30 },
    };
    if( mode != GAME_CLIENT )
    {
        game_state.current_player = 0;
    }
    else {
        game_state.current_player = 1;
    }
    game_state.spaceships[ game_state.current_player ] = spaceship_control;
        
    const TickType_t interval = pdMS_TO_TICKS( 30 );
    TickType_t current_wake_time = xTaskGetTickCount();
    TickType_t last_wake_time = current_wake_time;
    while(1){
        const uint32_t milliseconds = ( ( current_wake_time - last_wake_time ) * 1000 ) / configTICK_RATE_HZ;
        last_wake_time = current_wake_time;
        game_state.play_timer += milliseconds;
        
        if( mode != GAME_CLIENT )
        {
            game_receive();
        }
        else {
            game_send();
        }
        game_input(); // task -> wait( BIT_REFRESH )
        game_move( milliseconds ); // task -> wait( BIT_REFRESH | BIT_INPUT )
        if( mode != GAME_CLIENT )
        {
            game_generate( milliseconds ); // task -> wait( BIT_REFRESH )
        }
        game_collide(); // task -> wait( BIT_REFRESH | BIT_MOVE )
        game_draw(); // task -> wait( BIT_REFRESH | BIT_INPUT | BIT_MOVE | BIT_GENERATE )
        game_send();
        if( mode != GAME_CLIENT )
        {
            game_send();
        }
        else {
            game_receive();
        }
        vTaskDelayUntil( &current_wake_time, interval );
    }
}
//-----------------------------------------------------------------------------------------