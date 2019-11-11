//-----------------------------------------------------------------------------------------
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "rom/crc.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/api.h"
#include <lwip/netdb.h>

#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"

#include "object.h"
#include "network_tcp.h"
#include "protocol.h"

//-----------------------------------------------------------------------------------------
#define HOST_IP_ADDR "192.168.4.1" //"fe80::1400:4639:cc8d:3033" //"fe80::a650:46ff:feeb:efe8" 
#define PORT 5000
//-----------------------------------------------------------------------------------------
static const char* TAG = "network_tcp";

typedef enum {
    NETWORK_CONNECTED = BIT(0),
    NETWORK_RECEIVE   = BIT(1),
    NETWORK_RECEIVED  = BIT(2),
    NETWORK_SEND      = BIT(3),
    NETWORK_SENT      = BIT(4)
} network_event_t;

static TaskHandle_t network_task_handle = NULL;
static EventGroupHandle_t network_event_group = NULL;

typedef enum {
    RECEIVE_START,
    RECEIVE_LENGTH,
    RECEIVE_CRC,
    RECEIVE_DATA,
    RECEIVE_END,
    RECEIVE_COMPLETE
} receive_state_t;

static const size_t receive_buffer_size = 10000;
static char* receive_buffer = NULL;
static size_t receive_position = 0;
static receive_state_t receive_state = RECEIVE_START;

static int64_t  timeout_timer = 0;

typedef enum {
    SEND_START,
    SEND_LENGTH,
    SEND_CRC,
    SEND_DATA,
    SEND_END,
    SEND_COMPLETE
} send_state_t;

static const size_t send_buffer_size = 10000;
static char* send_buffer = NULL;
static size_t send_position = 0;
static send_state_t send_state = SEND_COMPLETE;

//-----------------------------------------------------------------------------------------
static bool network_process_tcp_receive_packet( int sock )
{
    const receive_state_t prev_state = receive_state;

    if( receive_state == RECEIVE_START )
    {
        const int length_received = lwip_recv( sock, receive_buffer + receive_position, 1 - receive_position, MSG_DONTWAIT );
        if( length_received < 0 && errno != EAGAIN )
        {
            ESP_LOGE( TAG, "recv error: errno %d", errno );
            receive_position = 0;
            receive_state = RECEIVE_START;
            return false;
        } 
        else if( length_received > 0 )
        {
            if( receive_buffer[0] > 32 && receive_buffer[0] < 127 )
                printf("%c ", send_buffer[0] );
            else
                printf("%02X ", receive_buffer[0] );
            
            receive_position += length_received;
            if( receive_position == 1 )
            {
                const uint8_t start = *(uint8_t*)( receive_buffer + 0 );
                if( start == '\x02' )
                {
                    receive_state = RECEIVE_LENGTH;
                    timeout_timer = esp_timer_get_time();
                }
                else {
                    receive_position = 0;
                }
            }
        }
    }
    if( receive_state == RECEIVE_LENGTH )
    {
        const int length_received = lwip_recv( sock, receive_buffer + receive_position, 3 - receive_position, MSG_DONTWAIT );
        if( length_received < 0 && errno != EAGAIN )
        {
            ESP_LOGE( TAG, "recv error: errno %d", errno );
            receive_position = 0;
            receive_state = RECEIVE_START;
            return false;
        }
        else
        {
            receive_position += length_received;
            if( receive_position == 3 )
            {
                receive_state = RECEIVE_CRC;
            }
            timeout_timer = esp_timer_get_time();
        }
    }
    if( receive_state == RECEIVE_CRC )
    {
        const int length_received = lwip_recv( sock, receive_buffer + receive_position, 7 - receive_position, MSG_DONTWAIT );
        if( length_received < 0 && errno != EAGAIN )
        {
            ESP_LOGE( TAG, "recv error: errno %d", errno );
            receive_position = 0;
            receive_state = RECEIVE_START;
            return false;
        }
        else
        {
            receive_position += length_received;
            if( receive_position == 7 )
            {
                receive_state = RECEIVE_DATA;
            }
            timeout_timer = esp_timer_get_time();
        }
    }
    if( receive_state == RECEIVE_DATA )
    {
        const uint16_t length = *(uint16_t*)( receive_buffer + 1 );
        
        const int length_received = lwip_recv( sock, receive_buffer + receive_position, 7 + length - receive_position, MSG_DONTWAIT );
        if( length_received < 0 && errno != EAGAIN )
        {
            ESP_LOGE( TAG, "recv error: errno %d", errno );
            receive_position = 0;
            receive_state = RECEIVE_START;
            return false;
        }
        else
        {
            receive_position += length_received;
            if( receive_position == 7 + length )
            {
                receive_state = RECEIVE_END;
            }
            timeout_timer = esp_timer_get_time();
        }
    }
    if( receive_state == RECEIVE_END )
    {
        const uint16_t length = *(uint16_t*)( receive_buffer + 1 );
        
        const int length_received = lwip_recv( sock, receive_buffer + receive_position, 8 + length - receive_position, MSG_DONTWAIT );
        if( length_received < 0 && errno != EAGAIN )
        {
            ESP_LOGE( TAG, "recv error: errno %d", errno );
            receive_position = 0;
            receive_state = RECEIVE_START;
            return false;
        }
        else
        {
            receive_position += length_received;
            if( receive_position == 8 + length )
            {
                const uint32_t crc = *(uint32_t*)( receive_buffer + 3 );
                const uint8_t end = *(uint8_t*)( receive_buffer + 7 + length );
                const uint32_t crc_calculated = crc32_be( 0, (const uint8_t*)( receive_buffer + 7 ), length );
                
                for( size_t n = 0; n < 8 + length; n++ )
                {
                    if( n % 40 == 0 )
                        printf("\n");
                    
                    if( receive_buffer[n] > 32 && receive_buffer[n] < 127 )
                        printf("%c ", receive_buffer[n] );
                    else
                        printf("%02X ", receive_buffer[n] );
                }
                
                if( end == '\x03' && crc == crc_calculated )
                {
                    receive_state = RECEIVE_COMPLETE;
                }
                else 
                {
                    ESP_LOGE( TAG, "length = 0x%04X", length );
                    if( end != '\x03' )
                    {
                        ESP_LOGE( TAG, "end = 0x%02X", end );
                    }
                    if( crc != crc_calculated )
                    {
                        ESP_LOGE( TAG, "crc = 0x%08X / calculted = 0x%08X", crc, crc_calculated );
                    }
                    receive_position = 0;
                    receive_state = RECEIVE_START;
                }
            }
        }
    }
    if( receive_state != RECEIVE_START && receive_state != RECEIVE_COMPLETE && esp_timer_get_time() - timeout_timer > 1000 )
    {
        ESP_LOGE( TAG, "timeout" );
        receive_position = 0;
        receive_state = RECEIVE_START;
    }
    if( prev_state != receive_state )
    {
        ESP_LOGI( TAG, "receive_state = %d", receive_state );
    }
    return true;
}
//-----------------------------------------------------------------------------------------
static bool network_process_tcp_send_packet( int sock )
{
    const send_state_t prev_state = send_state;
    
    if( send_state != SEND_COMPLETE )
    {
        const uint16_t length = *(uint16_t*)( send_buffer + 1 );

        if( send_position < 8 + length )
        {
            const int length_sent = lwip_send( sock, send_buffer, 8 + length - send_position, MSG_DONTWAIT | MSG_MORE );
            if ( length_sent < 0 && errno != EAGAIN )
            {
                ESP_LOGE( TAG, "send error: errno %d", errno );
                send_state = SEND_COMPLETE;
                return false;
            }
            send_position += length_sent;
        }
        if( send_position < 1 )
        {
            send_state = SEND_START;
        }
        else if( send_position < 3 )
        {
            send_state = SEND_LENGTH;
        }
        else if( send_position < 7 )
        {
            send_state = SEND_CRC;
        }
        else if( send_position < 7 + length )
        {
            send_state = SEND_DATA;
        }
        else if( send_position < 8 + length )
        {
            send_state = SEND_END;
        }
        else 
        {
            send_state = SEND_COMPLETE;
        }
    }
    if( prev_state != send_state )
    {
        ESP_LOGI( TAG, "send_state = %d", send_state );
    }
    return true;
}
//-----------------------------------------------------------------------------------------
static void network_tcp_prepare_send( uint16_t length )
{
    *(uint8_t*)( send_buffer + 0 ) = '\x02';
    *(uint16_t*)( send_buffer + 1 ) = length;
    *(uint32_t*)( send_buffer + 3 ) = crc32_be( 0, (const uint8_t*)( send_buffer + 7 ), length );
    *(uint8_t*)( send_buffer + 7 + length ) = '\x03';
    
    for( size_t n = 0; n < 8 + length; n++ )
    {
        if( n % 40 == 0 )
            printf("\n");
        
        if( send_buffer[n] > 32 && send_buffer[n] < 127 )
            printf("%c ", send_buffer[n] );
        else
            printf("%02X ", send_buffer[n] );
    }
}
//-----------------------------------------------------------------------------------------
static void network_process_tcp_client_receive_event( game_state_t* game_state )
{
    if( xEventGroupGetBits( network_event_group ) & NETWORK_RECEIVE )
    {
        if( receive_state == RECEIVE_COMPLETE )
        {
            ESP_LOGI( TAG, "process_tcp_client_receive_event" );
            
            protocol_game_from_server( game_state, receive_buffer + 7, receive_position - 8 ); 
            // Checa as regras -> munições / movimento
            receive_position = 0;
            receive_state = RECEIVE_START;
        }
        xEventGroupClearBits( network_event_group, NETWORK_RECEIVE );
        xEventGroupSetBits( network_event_group, NETWORK_RECEIVED );
    }
}
//-----------------------------------------------------------------------------------------
static void network_process_tcp_client_send_event( game_state_t* game_state )
{
    if( xEventGroupGetBits( network_event_group ) & NETWORK_SEND )
    {
        if( send_state == SEND_COMPLETE )
        {
            ESP_LOGI( TAG, "process_tcp_client_send_event" );
        
            //network_tcp_prepare_send( protocol_game_to_server( game_state, send_buffer + 7, send_buffer_size - 8 ) );
            // Checa as regras -> munições / movimento
            //send_position = 0;
            //send_state = SEND_START;
        }
        xEventGroupClearBits( network_event_group, NETWORK_SEND );
        xEventGroupSetBits( network_event_group, NETWORK_SENT );
    }
}
//-----------------------------------------------------------------------------------------
static void network_process_tcp_client( void *pvParameters )
{
    game_state_t* game_state = pvParameters;
    
    const struct sockaddr_in destAddr = {
        .sin_addr.s_addr = inet_addr(HOST_IP_ADDR),
        .sin_family = AF_INET,
        .sin_port = htons(PORT)
    };
    
    const int client_sock =  lwip_socket( AF_INET, SOCK_STREAM, IPPROTO_IP );
    if (client_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: %s", lwip_strerr( errno ) );
        return;
    }
    ESP_LOGI(TAG, "Socket created");

    while(1){
        const int err = lwip_connect(client_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (err < 0)
        {
            ESP_LOGE(TAG, "Socket unable to connect: %s", lwip_strerr( errno ) );
            vTaskDelay( pdMS_TO_TICKS( 1000 ) );
        }
        else 
        {
            ESP_LOGI(TAG, "Successfully connected");
            
            xEventGroupSetBits( network_event_group, NETWORK_CONNECTED );
            while (1) {
                if( ! network_process_tcp_receive_packet( client_sock ) || ! network_process_tcp_send_packet( client_sock ) )
                {
                    break;
                }
                network_process_tcp_client_receive_event( game_state );
                network_process_tcp_client_send_event( game_state );
                
                vTaskDelay( pdMS_TO_TICKS( 500 ) );
            }
            xEventGroupClearBits( network_event_group, NETWORK_CONNECTED );
        }
        if (client_sock != -1) {
            lwip_shutdown(client_sock, 0);
            lwip_close( client_sock );
        }
    }
    vTaskDelete( NULL );
}
//-----------------------------------------------------------------------------------------
static void network_process_tcp_server_receive_event( game_state_t* game_state )
{
    if( xEventGroupGetBits( network_event_group ) & NETWORK_RECEIVE )
    {
        if( receive_state == RECEIVE_COMPLETE )
        {
            ESP_LOGI( TAG, "process_tcp_server_receive_event" );
            
            //protocol_game_from_client( game_state, receive_buffer + 7, receive_position - 8 ); 
            //// Checa as regras -> munições / movimento
            //receive_position = 0;
            //receive_state = RECEIVE_START;
        }
        xEventGroupClearBits( network_event_group, NETWORK_RECEIVE );
        xEventGroupSetBits( network_event_group, NETWORK_RECEIVED );
    }
}
//-----------------------------------------------------------------------------------------
static void network_process_tcp_server_send_event( game_state_t* game_state )
{
    if( xEventGroupGetBits( network_event_group ) & NETWORK_SEND )
    {
        if( send_state == SEND_COMPLETE )
        {
            ESP_LOGI( TAG, "process_tcp_server_send_event" );
            
            //{
            //    memcpy( send_buffer + 7, "{ \"teste\": \"teste\" }", 20 );
            //    network_tcp_prepare_send( 20 );
            //}
            network_tcp_prepare_send( protocol_game_to_client( game_state, send_buffer + 7, send_buffer_size - 8 ) );
            // Checa as regras -> munições / movimento
            send_position = 0;
            send_state = SEND_START;
        }
        xEventGroupClearBits( network_event_group, NETWORK_SEND );
        xEventGroupSetBits( network_event_group, NETWORK_SENT );
    }
}
//-----------------------------------------------------------------------------------------
static void network_process_tcp_server( void *pvParameters )
{
    game_state_t* game_state = pvParameters;
    
    const struct sockaddr_in destAddr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(PORT)
    };
    
    const int server_sock = lwip_socket( AF_INET, SOCK_STREAM, IPPROTO_IP );
    if (server_sock < 0)
    {
        ESP_LOGE( TAG, "unable to create socket: %s", lwip_strerr( errno ) );
        return;
    }
    ESP_LOGI( TAG, "socket created" );
    
    {
        const int err = lwip_bind(server_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (err < 0)
        {
            ESP_LOGE( TAG, "Socket unable to bind: %s", lwip_strerr( errno ) );
            return;
        }
        ESP_LOGI(TAG, "socket binded");
    }
    {
        const int err = lwip_listen(server_sock, 1);
        if (err < 0)
        {
            ESP_LOGE( TAG, "Error occured during listen: %s", lwip_strerr( errno ) );
            return;
        }
        //ESP_LOGI( TAG, "socket listening" );
    }
    while(1)
    {
        struct sockaddr_in6 sourceAddr;
        uint addrLen = sizeof(sourceAddr);
        const int client_sock = lwip_accept( server_sock, (struct sockaddr *)&sourceAddr, &addrLen );
        if ( client_sock < 0 )
        {
            ESP_LOGE( TAG, "unable to accept connection: %s", lwip_strerr( errno ) );
            vTaskDelay( pdMS_TO_TICKS( 1000 ) );
        }
        else
        {
            ESP_LOGI( TAG, "socket accepted" );
            
            xEventGroupSetBits( network_event_group, NETWORK_CONNECTED );
            while(1){
                if( ! network_process_tcp_receive_packet( client_sock ) || ! network_process_tcp_send_packet( client_sock ) )
                {
                    break;
                }
                network_process_tcp_server_receive_event( game_state );
                network_process_tcp_server_send_event( game_state );
                
                vTaskDelay( pdMS_TO_TICKS( 500 ) );
            }
            xEventGroupClearBits( network_event_group, NETWORK_CONNECTED );
        }
        if (client_sock != -1) 
        {
            ESP_LOGE( TAG, "Shutting down socket and restarting...");
            lwip_shutdown( client_sock, 0 );
            lwip_close( client_sock );
        }

        //char* str = NULL;
        //size_t length_received = 0;
        //{
        //    game_state_t* game_state;
        //    if( xTaskNotifyWait( 0, ULONG_MAX, (uint32_t*)&game_state, portMAX_DELAY ) ){
        //        
        //        for( size_t index = 0; index < sizeof(game_state->spaceships)/sizeof(*game_state->spaceships); index++ ){
        //            game_state->spaceships[ index ].active                  = 12;
        //            game_state->spaceships[ index ].px                      = 123.456;
        //            game_state->spaceships[ index ].py                      = 123.456;
        //            game_state->spaceships[ index ].vi                      = 12.345;
        //            game_state->spaceships[ index ].vj                      = 12.345;
        //            game_state->spaceships[ index ].mode                    = 12;
        //            game_state->spaceships[ index ].life                    = 12;
        //            game_state->spaceships[ index ].collision_countdown     = 12345;
        //            game_state->spaceships[ index ].collision_active        = 12;
        //            game_state->spaceships[ index ].shield_active           = 12;
        //            game_state->spaceships[ index ].shield_animation_number = 12;
        //            game_state->spaceships[ index ].alive_timer             = 123456;
        //            game_state->spaceships[ index ].asteroids_counter       = 12345;
        //        }
        //        for( size_t index = 0; index < sizeof(game_state->asteroids)/sizeof(*game_state->asteroids); index++ ){
        //            game_state->asteroids[ index ].active                   = 12;
        //            game_state->asteroids[ index ].px                       = 123.456;
        //            game_state->asteroids[ index ].py                       = 123.456;
        //            game_state->asteroids[ index ].vi                       = 12.345;
        //            game_state->asteroids[ index ].vj                       = 12.345;
        //            game_state->asteroids[ index ].number                   = 12;
        //            game_state->asteroids[ index ].orientation              = 12;
        //            game_state->asteroids[ index ].life                     = 12;
        //            game_state->asteroids[ index ].damage_countdown         = 12345;
        //            game_state->asteroids[ index ].damage_active            = 12;
        //        }
        //        for( size_t index = 0; index < sizeof(game_state->impacts)/sizeof(*game_state->impacts); index++ ){
        //            game_state->impacts[ index ].active                     = 12;
        //            game_state->impacts[ index ].px                         = 123.456;
        //            game_state->impacts[ index ].py                         = 123.456;
        //            game_state->impacts[ index ].type                       = 12;
        //            game_state->impacts[ index ].size                       = 12;
        //            game_state->impacts[ index ].animation_number           = 12;
        //            game_state->impacts[ index ].animation_countdown        = 12345;
        //        }
        //        for( size_t index = 0; index < sizeof(game_state->projectiles)/sizeof(*game_state->projectiles); index++ ){
        //            game_state->projectiles[ index ].active                 = 12;
        //            game_state->projectiles[ index ].px                     = 123.456;
        //            game_state->projectiles[ index ].py                     = 123.456;
        //            game_state->projectiles[ index ].vi                     = 12.345;
        //            game_state->projectiles[ index ].vj                     = 12.345;
        //            game_state->projectiles[ index ].owner                  = 12;
        //            game_state->projectiles[ index ].type                   = 12;
        //            game_state->projectiles[ index ].animation_number       = 12;
        //            game_state->projectiles[ index ].animation_countdown    = 12345;
        //        }
        //        for( size_t index = 0; index < sizeof(game_state->pickups)/sizeof(*game_state->pickups); index++ ){
        //            game_state->pickups[ index ].active                     = 12;
        //            game_state->pickups[ index ].px                         = 123.456;
        //            game_state->pickups[ index ].py                         = 123.456;
        //            game_state->pickups[ index ].vi                         = 12.345;
        //            game_state->pickups[ index ].vj                         = 12.345;
        //            game_state->pickups[ index ].type                       = 12;
        //            game_state->pickups[ index ].trajectory_progress        = 12.345;
        //        }
        //        for( size_t index = 0; index < sizeof(game_state->meteors)/sizeof(*game_state->meteors); index++ ){
        //            game_state->meteors[ index ].active                     = 12;
        //            game_state->meteors[ index ].px                         = 123.456;
        //            game_state->meteors[ index ].py                         = 123.456;
        //            game_state->meteors[ index ].vi                         = 12.345;
        //            game_state->meteors[ index ].vj                         = 12.345;
        //            game_state->meteors[ index ].direction                  = 12;
        //            game_state->meteors[ index ].animation_number           = 12;
        //            game_state->meteors[ index ].animation_countdown        = 12345;
        //            game_state->meteors[ index ].arrow_active               = 12;
        //            game_state->meteors[ index ].arrow_countdown            = 12345;
        //            game_state->meteors[ index ].arrow_progress             = 12.345;
        //        }
        //        
        //        const int64_t start = esp_timer_get_time();
        //        length_received = protocol_game_to_client( &str, game_state );
        //        ////ESP_LOGI(TAG,"length_received = %zu",length_received);
        //        //puts(str);
        //        const int64_t end = esp_timer_get_time();
        //        //ESP_LOGI(TAG,"t = %lld",end-start);
        //    }
        //}
    }
    vTaskDelete(NULL);
}
//-----------------------------------------------------------------------------------------
void network_initialize_tcp_client( game_state_t* game_state )
{
    if( network_task_handle == NULL )
    {
        ESP_LOGI( TAG, "initialize_tcp_client" );
        
        receive_buffer = pvPortMalloc( receive_buffer_size );
        send_buffer = pvPortMalloc( send_buffer_size );
        network_event_group = xEventGroupCreate();
        xTaskCreatePinnedToCore( network_process_tcp_client, "network_process_tcp_client", 8192, game_state, 5, &network_task_handle, 1 );
    }
}
//-----------------------------------------------------------------------------------------
void network_initialize_tcp_server( game_state_t* game_state )
{
    if( network_task_handle == NULL )
    {
        ESP_LOGI( TAG, "initialize_tcp_server" );
        
        receive_buffer = pvPortMalloc( receive_buffer_size );
        send_buffer = pvPortMalloc( send_buffer_size );
        network_event_group = xEventGroupCreate();
        xTaskCreatePinnedToCore( network_process_tcp_server, "network_process_tcp_server", 8192, game_state, 5, &network_task_handle, 1 );
    }
}
//-----------------------------------------------------------------------------------------
void network_tcp_send( )
{
    if( network_task_handle != NULL )
    {
        if( xEventGroupGetBits( network_event_group ) & NETWORK_CONNECTED ){
            xEventGroupSetBits( network_event_group, NETWORK_SEND );
            xEventGroupWaitBits( network_event_group, NETWORK_SENT, pdTRUE, pdTRUE, portMAX_DELAY );
        }
    }
}
//-----------------------------------------------------------------------------------------
void network_tcp_receive( )
{
    if( network_task_handle != NULL )
    {   
        if( xEventGroupGetBits( network_event_group ) & NETWORK_CONNECTED ){
            xEventGroupSetBits( network_event_group, NETWORK_RECEIVE );
            xEventGroupWaitBits( network_event_group, NETWORK_RECEIVED, pdTRUE, pdTRUE, portMAX_DELAY );
        }
    }
}
//-----------------------------------------------------------------------------------------