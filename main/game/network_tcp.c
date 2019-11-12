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
//#define HOST_IP_ADDR "192.168.4.1" //"fe80::1400:4639:cc8d:3033" //"fe80::a650:46ff:feeb:efe8" 
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

static const size_t receive_buffer_size = 10000;
static char* receive_buffer = NULL;
static size_t receive_position = 0;
static bool receive_valid = false;
static bool receive_started = false;
static bool receive_completed = true;

static int64_t  timeout_timer = 0;

static const size_t send_buffer_size = 10000;
static char* send_buffer = NULL;
static size_t send_position = 0;
static bool send_completed = true;



//-----------------------------------------------------------------------------------------
static bool network_process_tcp_receive_packet( int sock )
{
    if( ! receive_completed )
    {
        while( 1 )
        {
            ESP_LOGI( TAG, "receive_packet" );
            
            uint8_t data;
            //const int length_received = lwip_recv( sock, &data, 1, MSG_DONTWAIT );
            struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            const int length_received = lwip_recvfrom( sock, &data, 1, 0, (struct sockaddr *)&source_addr, &socklen );
            if( length_received < 0 )
            {
                if( errno != EAGAIN )
                {
                    ESP_LOGE( TAG, "recv error: errno %d", errno );
                    receive_position = 0;
                    receive_started = false;
                    receive_completed = false;
                    receive_valid = false;
                    return false;
                }
                else 
                {
                    break;
                }
            }
            else if( length_received == 0 )
            {
                ESP_LOGI( TAG, "length_received == 0 " );
                break;
            }
            else if( length_received > 0 )
            {
                ESP_LOGI( TAG, "length_received > 0 " );
                
                printf("%c",data);
                
                if( data == '\x02' )
                {
                    ESP_LOGE( TAG, "receive start" );
                    
                    if( receive_started )
                    {
                        ESP_LOGE( TAG, "receive overflow" );
                    }
                    receive_position = 0;
                    receive_started = true;
                    receive_completed = false;
                    receive_valid = false;
                }
                if( receive_started )
                {
                    if( receive_position < receive_buffer_size - 1 )
                    {
                        *( receive_buffer + receive_position++ ) = data;
                    }
                    if( data == '\x03' )
                    {
                        ESP_LOGE( TAG, "receive end" );
                        
                        receive_started = false;
                        receive_completed = true;
                        receive_valid = false;

                        uint32_t crc;
                        uint16_t length;
                        
                        sscanf( receive_buffer + 1, "%08X", &crc );
                        sscanf( receive_buffer + 10, "%04hX", &length );
                        
                        const uint32_t crc_calculated = crc32_be( 0, (const uint8_t*)( receive_buffer + 15 ), length );
                        
                        //for( size_t n = 0; n < 16 + length; n++ )
                        //{
                        //    if( n % 80 == 0 )
                        //        printf("\n");
                        //    
                        //    if( receive_buffer[n] > 32 && receive_buffer[n] < 127 )
                        //        printf("%c ", receive_buffer[n] );
                        //    else
                        //        printf("<%02X> ", receive_buffer[n] );
                        //}
                        
                        if( crc == crc_calculated )
                        {
                            receive_valid = true;
                        }
                        else 
                        {
                            ESP_LOGE( TAG, "crc mismatch" );
                            receive_position = 0;
                        }
                        break;
                    }
                }
            }
        }
    }
    //if( receive_state != RECEIVE_START && receive_state != RECEIVE_COMPLETE && esp_timer_get_time() - timeout_timer > 1000 )
    //{
    //    ESP_LOGE( TAG, "timeout" );
    //    receive_position = 0;
    //    receive_state = RECEIVE_START;
    //}
    return true;
}
//-----------------------------------------------------------------------------------------
static bool network_process_tcp_send_packet( int sock )
{
    if( ! send_completed )
    {
        uint16_t length;
        sscanf( send_buffer + 10, "%04hX", &length );
        
        while( 1 )
        {
            if( send_position < 16 + length )
            {
                struct sockaddr_in6 dest_addr;
                inet6_aton( "fe80::3e71:bfff:fe58:5658", &dest_addr.sin6_addr );
                dest_addr.sin6_family = AF_INET6;
                dest_addr.sin6_port = htons(PORT);
                
                //const int length_sent = lwip_send( sock, send_buffer, 16 + length - send_position, MSG_DONTWAIT );
                const int length_sent = lwip_sendto( sock, send_buffer, 16 + length - send_position, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr) );
                if ( length_sent < 0 )
                {
                    if( errno != EAGAIN )
                    {
                        ESP_LOGE( TAG, "send error: errno %d", errno );
                        send_position = 0;
                        send_completed = true;
                        return false;
                    }
                    else 
                    {
                        break;
                    }
                }
                else if( length_sent == 0 )
                {
                    break;
                }
                else if ( length_sent > 0 )
                {
                    send_position += length_sent;
                }
            }
            if( send_position == 16 + length )
            {
                send_position = 0;
                send_completed = true;
                break;
            }
        }
    }
    return true;
}
//-----------------------------------------------------------------------------------------
static void network_tcp_prepare_send( uint16_t length )
{
    *( send_buffer + 0 ) = '\x02';
    sprintf( send_buffer + 1, "%08X", crc32_be( 0, (const uint8_t*)( send_buffer + 15 ), length ) );
    sprintf( send_buffer + 10, "%04X", length );
    *( send_buffer + 15 + length ) = '\x03';

    ESP_LOGI( TAG, "send_packet" );

    //for( size_t n = 0; n < 16 + length; n++ )
    //{
    //    if( n % 80 == 0 )
    //        printf("\n");
    //    
    //    if( send_buffer[n] > 32 && send_buffer[n] < 127 )
    //        printf("%c ", send_buffer[n] );
    //    else
    //        printf("<%02X> ", send_buffer[n] );
    //}
}
//-----------------------------------------------------------------------------------------
static void network_process_tcp_client_receive_event( game_state_t* game_state )
{
    if( xEventGroupGetBits( network_event_group ) & NETWORK_RECEIVE )
    {
        if( receive_completed )
        {
            ESP_LOGI( TAG, "process_tcp_client_receive_event" );
            if( receive_valid )
            {
                protocol_game_from_server( game_state, receive_buffer + 15, receive_position - 16 ); 
            }    
            // Checa as regras -> munições / movimento
            receive_position = 0;
            receive_started = false;
            receive_completed = false;
            receive_valid = false;
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
        if( send_completed )
        {
            ESP_LOGI( TAG, "process_tcp_client_send_event" );
        
            //network_tcp_prepare_send( protocol_game_to_server( game_state, send_buffer + 15, send_buffer_size - 16 ) );
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

    //const struct sockaddr_in dest_addr = {
    //    .sin_addr.s_addr = inet_addr(HOST_IP_ADDR),
    //    .sin_family = AF_INET,
    //    .sin_port = htons(PORT)
    //};

    //const int client_sock =  lwip_socket( AF_INET, SOCK_STREAM, IPPROTO_IP );
    const int client_sock = lwip_socket( AF_INET6, SOCK_DGRAM, IPPROTO_IPV6 );
    if (client_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: %s", lwip_strerr( errno ) );
        return;
    }
    ESP_LOGI(TAG, "socket created");

    //const int opt = 10000;
    //setsockopt( client_sock, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt) );

    while(1){
        //const int err = lwip_connect(client_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        //if (err < 0)
        //{
        //    ESP_LOGE(TAG, "Socket unable to connect: %s", lwip_strerr( errno ) );
        //    vTaskDelay( pdMS_TO_TICKS( 1000 ) );
        //}
        //else 
        //{
        //    ESP_LOGI(TAG, "Successfully connected");
        //    
            xEventGroupSetBits( network_event_group, NETWORK_CONNECTED );
            while (1) {
                ESP_LOGI( TAG, "process_client" );
                
                if( ! network_process_tcp_receive_packet( client_sock ) )
                {
                    break;
                }
                if( ! network_process_tcp_send_packet( client_sock ) )
                {
                    break;
                }
                network_process_tcp_client_receive_event( game_state );
                network_process_tcp_client_send_event( game_state );
                
                vTaskDelay( pdMS_TO_TICKS( 50 ) );
            }
            xEventGroupClearBits( network_event_group, NETWORK_CONNECTED );
        //}
        if ( client_sock != -1 ) {
            ESP_LOGE( TAG, "Shutting down socket and restarting...");
            lwip_shutdown( client_sock, 0 );
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
        if( receive_completed )
        {
            ESP_LOGI( TAG, "process_tcp_server_receive_event" );
            
            //protocol_game_from_client( game_state, receive_buffer + 15, receive_position - 16 ); 
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
        if( send_completed )
        {
            ESP_LOGI( TAG, "process_tcp_server_send_event" );
            
            //{
            //    memcpy( send_buffer + 7, "{ \"teste\": \"teste\" }", 20 );
            //    network_tcp_prepare_send( 20 );
            //}
            network_tcp_prepare_send( protocol_game_to_client( game_state, send_buffer + 15, send_buffer_size - 16 ) );
            // Checa as regras -> munições / movimento
            send_position = 0;
            send_completed = false;
        }
        xEventGroupClearBits( network_event_group, NETWORK_SEND );
        xEventGroupSetBits( network_event_group, NETWORK_SENT );
    }
}
//-----------------------------------------------------------------------------------------
static void network_process_tcp_server( void *pvParameters )
{
    game_state_t* game_state = pvParameters;
    
    struct sockaddr_in6 dest_addr;
    bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(PORT);
    
    const int server_sock = lwip_socket( AF_INET6, SOCK_DGRAM, IPPROTO_IPV6 );
    if (server_sock < 0)
    {
        ESP_LOGE( TAG, "unable to create socket: %s", lwip_strerr( errno ) );
        return;
    }
    ESP_LOGI( TAG, "socket created" );
    
    {
        const int err = lwip_bind(server_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0)
        {
            ESP_LOGE( TAG, "Socket unable to bind: %s", lwip_strerr( errno ) );
            return;
        }
        ESP_LOGI(TAG, "socket binded");
    }
    //{
    //    const int err = lwip_listen(server_sock, 1);
    //    if (err < 0)
    //    {
    //        ESP_LOGE( TAG, "Error occured during listen: %s", lwip_strerr( errno ) );
    //        return;
    //    }
    //    //ESP_LOGI( TAG, "socket listening" );
    //}
    while(1)
    {
        //struct sockaddr_in6 sourceAddr;
        //uint addrLen = sizeof(sourceAddr);
        //const int client_sock = lwip_accept( server_sock, (struct sockaddr *)&sourceAddr, &addrLen );
        //if ( client_sock < 0 )
        //{
        //    ESP_LOGE( TAG, "unable to accept connection: %s", lwip_strerr( errno ) );
        //    vTaskDelay( pdMS_TO_TICKS( 1000 ) );
        //}
        //else
        //{
        //    ESP_LOGI( TAG, "socket accepted" );
            
            xEventGroupSetBits( network_event_group, NETWORK_CONNECTED );
            while(1){
                if( ! network_process_tcp_receive_packet( server_sock ) || ! network_process_tcp_send_packet( server_sock ) )
                {
                    break;
                }
                network_process_tcp_server_receive_event( game_state );
                network_process_tcp_server_send_event( game_state );
                
                vTaskDelay( pdMS_TO_TICKS( 50 ) );
            }
            xEventGroupClearBits( network_event_group, NETWORK_CONNECTED );
        //}
        if ( server_sock != -1) 
        {
            ESP_LOGE( TAG, "Shutting down socket and restarting...");
            lwip_shutdown( server_sock, 0 );
            lwip_close( server_sock );
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
        memset( receive_buffer, 0, receive_buffer_size );
        send_buffer = pvPortMalloc( send_buffer_size );
        memset( send_buffer, 0, send_buffer_size );
        
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
        memset( receive_buffer, 0, receive_buffer_size );
        send_buffer = pvPortMalloc( send_buffer_size );
        memset( send_buffer, 0, send_buffer_size );
        
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