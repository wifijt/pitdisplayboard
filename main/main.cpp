#include <vector>
#include <string>
#include <cstring>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "Adafruit_GFX.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "lwip/apps/sntp.h"
#include "esp_timer.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

#include "config.h"
#include "globals.h"
#include "messages.h"
#include "pacman_engine.h"
#include "tba_network.h"
#include "matrix_display.h"

// --- Fallback Configuration ---
#ifndef REAL_TEAM
#define REAL_TEAM "frc5459"
#endif
#ifndef REAL_EVENT
#define REAL_EVENT "2026marea"
#endif

// --- Global Variable Definitions ---
std::vector<std::string> tickerQueue;
std::vector<MatchData> allMatches;
SemaphoreHandle_t matchDataMutex = NULL;
MatchPhase currentPhase = PHASE_QUALS;

// Simulation State
SimState simState = {false, 0, 0};
std::string teamKey = REAL_TEAM;
std::string eventKey = REAL_EVENT;

// Rank and Alliance
int currentRank = 0;
std::string allianceName = "";

MatrixPanel_I2S_DMA *matrix = nullptr;
GFXcanvas16 *canvas_dev = new GFXcanvas16(256, 64);

// Pac-Man Game State
float pacPos = 0;
float ghostPos[4] = {-20, -40, -60, -80};
float worldSpeed = 1.2f;
bool powerMode = false;
uint32_t powerStartTime = 0;
bool borderActive = false;
uint32_t lastBorderStartTime = 0;
GhostState ghostState[4] = {GHOST_ALIVE, GHOST_ALIVE, GHOST_ALIVE, GHOST_ALIVE};
uint32_t winStartTime = 0;

// Event Schedule
std::string nextEventName = "";
time_t nextEventDate = 0;

// Button Handling
#define DEBOUNCE_TIME 200 // ms
uint32_t lastBtnPressTime = 0;

time_t get_current_time() {
    time_t now;
    time(&now);
    if (simState.active) {
        return now + simState.time_offset;
    }
    return now;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Endless retry loop
        esp_wifi_connect();
        printf("WiFi Disconnected. Reconnecting...\n");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        // Fix for undefined IP2CO2IP macro, using correct IP2STR
        printf("Got IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
    }
}

void setup_networking() {
    // 1. Initialize NVS (Required for WiFi storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Buttons (GPIO 6 & 7)
    // GPIO 7 is Down/Reset, GPIO 6 is Up
    gpio_reset_pin(GPIO_NUM_7);
    gpio_set_direction(GPIO_NUM_7, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_7, GPIO_PULLUP_ONLY);
    
    gpio_reset_pin(GPIO_NUM_6);
    gpio_set_direction(GPIO_NUM_6, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_6, GPIO_PULLUP_ONLY);

    // 3. Init Network Stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register Event Handler for Auto-Reconnect
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));


    // 4. Define Hardcoded Credentials
    wifi_config_t static_wifi_config = {};
    strcpy((char*)static_wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)static_wifi_config.sta.password, WIFI_PASS);

    const char *service_name = PROV_SERVICE_NAME;
    const char *pop = PROV_POP; // Proof of Possession

    // 5. Setup Provisioning Manager
    wifi_prov_mgr_config_t config = {};
    config.scheme = wifi_prov_scheme_ble;
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned) {
        // STEP A: Try the Hardcoded WiFi first
        printf("Attempting Hardcoded WiFi: \n");
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &static_wifi_config);
        esp_wifi_start();
        // connect is handled by event handler now

        // Wait 10 seconds to see if hardcoded works
        int retry = 0;
        bool connected = false;
        while (retry < 10) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                connected = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            retry++;
        }

        // STEP B: If hardcoded failed, launch Provisioning AP
        if (!connected) {
            printf("Starting BLE Provisioning... Look for %s\n", service_name);
            ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, service_name, NULL));
        }
    } else {
        // STEP C: Already provisioned via App, just connect
        wifi_prov_mgr_stop_provisioning();
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        // connect is handled by event handler
    }

    // 6. Time Sync
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
}

extern "C" void app_main(void) {
    matchDataMutex = xSemaphoreCreateMutex();

    // Check Config
    #ifdef SIMULATION_MODE
        if (SIMULATION_MODE) {
            simState.active = true;
            // Fallback for SIM constants if not defined
            #ifndef SIM_TEAM
            #define SIM_TEAM "frc88"
            #endif
            #ifndef SIM_EVENT
            #define SIM_EVENT "2026week0"
            #endif

            teamKey = SIM_TEAM;
            eventKey = SIM_EVENT;
            printf("Running in SIMULATION MODE: %s @ %s\n", teamKey.c_str(), eventKey.c_str());
        }
    #endif

    HUB75_I2S_CFG mxconfig(64, 64, 4);
    mxconfig.gpio.r1 = 42; mxconfig.gpio.g1 = 41; mxconfig.gpio.b1 = 40;
    mxconfig.gpio.r2 = 38; mxconfig.gpio.g2 = 39; mxconfig.gpio.b2 = 37;
    mxconfig.gpio.a = 45;  mxconfig.gpio.b = 36;  mxconfig.gpio.c = 48;
    mxconfig.gpio.d = 35;  mxconfig.gpio.e = 21;
    mxconfig.gpio.lat = 47; mxconfig.gpio.oe = 14; mxconfig.gpio.clk = 2;
    mxconfig.clkphase = false;
    mxconfig.latch_blanking = 4;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_8M;

    matrix = new MatrixPanel_I2S_DMA(mxconfig);
    if (matrix->begin()) {
        matrix->setBrightness8(60);
        setup_networking();
        xTaskCreatePinnedToCore(matrix_task, "matrix_task", 8192, NULL, 10, NULL, 1);
        xTaskCreatePinnedToCore(tba_api_task, "tba_api_task", 10240, NULL, 5, NULL, 1);
    }
}
