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
#include <lwip/netdb.h>

#include "esp_err.h"
#include "esp_log.h"

#include "network.h"
#include "json.h"

//-----------------------------------------------------------------------------------------
#define EXAMPLE_WIFI_SSID "WORKGROUP2"
#define EXAMPLE_WIFI_PASS "49WNN7F3CD@22"
#define EXAMPLE_MAX_STA_CONN 2
#define HOST_IP_ADDR "192.168.1.104" //"fe80::1400:4639:cc8d:3033" //"fe80::a650:46ff:feeb:efe8" 
#define PORT 5000
//-----------------------------------------------------------------------------------------
static const char* TAG = "network";

const int IPV4_GOTIP_BIT = BIT0;
const int IPV6_GOTIP_BIT = BIT1;

static TaskHandle_t network_task_handle;
static EventGroupHandle_t wifi_event_group;
//-----------------------------------------------------------------------------------------
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        /* enable ipv6 */
        tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, IPV4_GOTIP_BIT);
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, IPV4_GOTIP_BIT);
        xEventGroupClearBits(wifi_event_group, IPV6_GOTIP_BIT);
        break;
    case SYSTEM_EVENT_AP_STA_GOT_IP6:
        xEventGroupSetBits(wifi_event_group, IPV6_GOTIP_BIT);
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP6");

        char *ip6 = ip6addr_ntoa(&event->event_info.got_ip6.ip6_info.ip);
        ESP_LOGI(TAG, "IPv6: %s", ip6);
    case SYSTEM_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "station:"MACSTR"leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
        break;
    default:
        break;
    }
    return ESP_OK;
}
//-----------------------------------------------------------------------------------------
static void network_process_tcp_client(void *pvParameters)
{
    const struct sockaddr_in destAddr = {
        .sin_addr.s_addr = inet_addr(HOST_IP_ADDR),
        .sin_family = AF_INET,
        .sin_port = htons(PORT)
    };
    
    char addr_str[128];
    inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
    
    const int client_sock =  lwip_socket( AF_INET, SOCK_STREAM, IPPROTO_IP );
    if (client_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG, "Socket created");
    
    while(1){

        int err = lwip_connect(client_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Successfully connected");

        while (1) {
            int err = lwip_send(client_sock, payload, strlen(payload), 0);
            if (err < 0) {
                ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
                break;
            }

            const int len = lwip_recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Error occured during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                ESP_LOGI(TAG, "%s", rx_buffer);
            }

            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }

        //if (client_sock != -1) {
        //    ESP_LOGE(TAG, "Shutting down socket and restarting...");
        //    lwip_shutdown(client_sock, 0);
        //    lwip_close(client_sock);
        //}
    }
    vTaskDelete(NULL);
}
//-----------------------------------------------------------------------------------------
static char buffer[15000];
static size_t position = 0;

typedef enum {
    RECEIVING_START,
    RECEIVING_LENGTH,
    RECEIVING_CRC,
    RECEIVING_DATA,
    RECEIVING_END
} state_t;

static void network_process_tcp_server(void *pvParameters)
{
    const struct sockaddr_in destAddr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(PORT)
    };
    
    const int server_sock =  lwip_socket( AF_INET, SOCK_STREAM, IPPROTO_IP );
    if (server_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG, "Socket created");
    
    int err = lwip_bind(server_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG, "Socket binded");

    err = lwip_listen(server_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG, "Socket listening");
    
    while(1){

        struct sockaddr_in6 sourceAddr;
        uint addrLen = sizeof(sourceAddr);
        const int client_sock = lwip_accept(server_sock, (struct sockaddr *)&sourceAddr, &addrLen);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket accepted");
        
        while(1){
            static state_t  state = RECEIVING_START;
            static bool     valid = false;

            static uint8_t  start;
            static uint16_t length;
            static uint32_t crc;
            static uint8_t  end;
        
            if( state == RECEIVING_START ){
                const int err = lwip_recv( client_sock, &buffer[ position ], sizeof( start ) - position, MSG_DONTWAIT );
                if( err < 0 ){
                    ESP_LOGE( TAG, "recv failed: errno %d", errno );
                    break;
                }
                position += err;
                if( position >= sizeof( start ) ){
                    start = *buffer;
                    position = 0;
                    if( start == '\x02' ){
                        valid = false;
                        state = RECEIVING_LENGTH;
                    }
                }
            }
            else if( state == RECEIVING_LENGTH ){
                const int err = lwip_recv( client_sock, &buffer[ position ], sizeof( length ) - position, MSG_DONTWAIT );
                if( err < 0 ){
                    ESP_LOGE( TAG, "recv failed: errno %d", errno );
                    break;
                }
                position += err;
                if( position >= sizeof( length ) ){
                    length = *(uint16_t*)buffer;
                    position = 0;
                    state = RECEIVING_CRC;
                }
            }
            else if( state == RECEIVING_CRC ){
                const int err = lwip_recv( client_sock, &buffer[ position ], sizeof( crc ) - position, MSG_DONTWAIT );
                if( err < 0 ){
                    ESP_LOGE( TAG, "recv failed: errno %d", errno );
                    break;
                }
                position += err;
                if( position >= sizeof( crc ) ){
                    crc = *(uint32_t*)buffer;
                    position = 0;
                    state = RECEIVING_DATA;
                }
            }
            else if( state == RECEIVING_DATA ){
                const int err = lwip_recv( client_sock, &buffer[ position ], length - position, MSG_DONTWAIT );
                if( err < 0 ){
                    ESP_LOGE( TAG, "recv failed: errno %d", errno );
                    break;
                }
                position += err;
                if( position >= length ){
                    position = 0;
                    state = RECEIVING_END;
                }
            }
            else if( state == RECEIVING_END ){
                const int err = lwip_recv( client_sock, &buffer[ position ], length + 1 - position, MSG_DONTWAIT );
                if( err < 0 ){
                    ESP_LOGE( TAG, "recv failed: errno %d", errno );
                    break;
                }
                position += err;
                if( position >= length + sizeof( end ) ){
                    end = *( buffer + length );
                    position = 0;
                    if(end == '\x03'){
                        const uint32_t crc_calculated = crc32_be( 0, (const uint8_t*)&buffer[7], length );
                        if( crc == crc_calculated ){
                            valid = true;
                        }
                        else {
                            position = 0;
                        }
                    }
                    state = RECEIVING_START;
                }
            }
            game_state_t* game_state;
            if( xTaskNotifyWait( 0, ULONG_MAX, (uint32_t*)&game_state, 0 ) )
            { 
                if( valid ){
                    json_game_from_client( game_state, buffer, position ); // Checa as regras -> munições / movimento
                    position = 0;
                    valid = false;
                }
                {
                    const size_t length = json_game_to_client( game_state, &buffer[7], sizeof( buffer ) - 8 );

                    char* ptr = buffer;
                    
                    *(uint8_t*)ptr = '\x02';
                    ptr += sizeof( uint8_t );
                    
                    *(uint16_t*)ptr = length;
                    ptr += sizeof( uint16_t );
                    
                    *(uint32_t*)ptr = crc32_be( 0, (const uint8_t*)&buffer[7], length );
                    ptr += sizeof( uint32_t );

                    ptr += length;
                    
                    *(uint8_t*)ptr = '\x03';
                    
                    int err = lwip_send( client_sock, buffer, length + 8, 0 );
                    if (err < 0) {
                        ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
                        break;
                    }
                }
            }
        }

        //char* str = NULL;
        //size_t len = 0;
        //{
        //    game_state_t* game_state;
        //    if( xTaskNotifyWait( 0, ULONG_MAX, (uint32_t*)&game_state, portMAX_DELAY ) ){
        //        
        //        for( size_t index = 0; index < game_state->spaceships_length; index++ ){
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
        //        for( size_t index = 0; index < game_state->asteroids_length; index++ ){
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
        //        for( size_t index = 0; index < game_state->impacts_length; index++ ){
        //            game_state->impacts[ index ].active                     = 12;
        //            game_state->impacts[ index ].px                         = 123.456;
        //            game_state->impacts[ index ].py                         = 123.456;
        //            game_state->impacts[ index ].type                       = 12;
        //            game_state->impacts[ index ].size                       = 12;
        //            game_state->impacts[ index ].animation_number           = 12;
        //            game_state->impacts[ index ].animation_countdown        = 12345;
        //        }
        //        for( size_t index = 0; index < game_state->projectiles_length; index++ ){
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
        //        for( size_t index = 0; index < game_state->pickups_length; index++ ){
        //            game_state->pickups[ index ].active                     = 12;
        //            game_state->pickups[ index ].px                         = 123.456;
        //            game_state->pickups[ index ].py                         = 123.456;
        //            game_state->pickups[ index ].vi                         = 12.345;
        //            game_state->pickups[ index ].vj                         = 12.345;
        //            game_state->pickups[ index ].type                       = 12;
        //            game_state->pickups[ index ].trajectory_progress        = 12.345;
        //        }
        //        for( size_t index = 0; index < game_state->meteors_length; index++ ){
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
        //        len = json_game_to_client( &str, game_state );
        //        //ESP_LOGI(TAG,"len = %zu",len);
        //        //puts(str);
        //        const int64_t end = esp_timer_get_time();
        //        ESP_LOGI(TAG,"t = %lld",end-start);
        //    }
        //}

        //
        //while (1) {
        //
        //    const int len = lwip_recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        //    // Error occured during receiving
        //    if (len < 0) {
        //        ESP_LOGE(TAG, "recv failed: errno %d", errno);
        //        break;
        //    }
        //    // Connection closed
        //    else if (len == 0) {
        //        ESP_LOGI(TAG, "Connection closed");
        //        break;
        //    }
        //    // Data received
        //    else {
        //        // Get the sender's ip address as string
        //        char addr_str[128];
        //        if (sourceAddr.sin6_family == PF_INET) {
        //            inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        //        } 
        //        else if (sourceAddr.sin6_family == PF_INET6) {
        //            inet6_ntoa_r(sourceAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
        //        }
        //    
        //        rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
        //        ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
        //        ESP_LOGI(TAG, "%s", rx_buffer);
        //    
        //        int err = lwip_send(client_sock, rx_buffer, len, 0);
        //        if (err < 0) {
        //            ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
        //            break;
        //        }
        //    }
        //}
        //
        //if (client_sock != -1) {
        //    ESP_LOGE(TAG, "Shutting down socket and restarting...");
        //    lwip_shutdown(client_sock, 0);
        //    lwip_close(client_sock);
        //}
    }
    vTaskDelete(NULL);
}
//-----------------------------------------------------------------------------------------
static void wait_for_ip()
{
    uint32_t bits = IPV4_GOTIP_BIT;// | IPV6_GOTIP_BIT ;

    ESP_LOGI(TAG, "Waiting for AP connection...");
    xEventGroupWaitBits(wifi_event_group, bits, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to AP");
}
//-----------------------------------------------------------------------------------------
static void network_initialize_common()
{
    nvs_flash_init();

    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

    ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
    tcpip_adapter_ip_info_t info;
	memset(&info, 0, sizeof(info));
	IP4_ADDR(&info.ip, 192, 168, 4, 1);
	IP4_ADDR(&info.gw, 192, 168, 4, 1);
	IP4_ADDR(&info.netmask, 255, 255, 255, 0);
	ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info));
	ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
}
//-----------------------------------------------------------------------------------------
static void network_initialize_client( network_type_t type )
{
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID for STA %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    
    wait_for_ip();
    if( type == NETWORK_TCP )
    {
        xTaskCreatePinnedToCore( network_process_tcp_client, "network_process_tcp_client", 4096, NULL, 5, &network_task_handle, 0 );
    }
    else if( type == NETWORK_UDP )
    {
        //xTaskCreatePinnedToCore( network_process_udp_client, "network_process_udp_client", 4096, NULL, 5, &network_task_handle, 0 );
    }
}
//-----------------------------------------------------------------------------------------
static void network_initialize_server( network_type_t type )
{
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_WIFI_SSID),
            .password = EXAMPLE_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    
    ESP_LOGI(TAG, "wifi_init_softap finished.SSID:%s password:%s", EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASS);
    if( type == NETWORK_TCP )
    {
        xTaskCreatePinnedToCore( network_process_tcp_server, "network_process_tcp_server", 4096, NULL, 5, &network_task_handle, 0 );
    }
    else if( type == NETWORK_UDP )
    {
        
    }
}
//-----------------------------------------------------------------------------------------
void network_initialize( network_mode_t mode, network_type_t type )
{
    network_initialize_common();
    
    if( mode == NETWORK_CLIENT ){ 
        network_initialize_client( type );
    }
    else if( mode == NETWORK_SERVER ){
        network_initialize_server( type );
    }
}
//-----------------------------------------------------------------------------------------
void network_terminate()
{
    
}
//-----------------------------------------------------------------------------------------
void network_sync( game_state_t* game_state )
{
    if( network_task_handle != NULL )
    {
        xTaskNotify( network_task_handle, (uint32_t)game_state, eSetValueWithOverwrite );
    }
}
//-----------------------------------------------------------------------------------------