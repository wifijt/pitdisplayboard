#include <vector>   // For std::vector (the message queue)
#include <string>   // For std::string (the message text)
#include <cstring>  // For C-style functions like memset and strcpy
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "Adafruit_GFX.h"
#include "FreeSansBold18pt7b.h" 
#include "FreeSansBold12pt7b.h" 
#include "FreeSans9pt7b.h"
#include "tiger_hires.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "lwip/apps/sntp.h"
#include "esp_timer.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

std::vector<std::string> tickerQueue;

MatrixPanel_I2S_DMA *matrix = nullptr;
GFXcanvas16 *canvas_dev = new GFXcanvas16(256, 64);

struct MatchEntry {
    char type;    // 'Q' or 'P'
    int number;
    uint16_t color; // 0xF800 (Red) or 0x001F (Blue)
};

struct GameScore {
    int matchNum;
    int autoFuel;
    int autoClimb;
    int teleFuel;
    int teleClimb;
    int foulPointsAwarded;
    int totalScore;
    bool fuelRP;
    bool towerRP;
};

// Array to store up to 12 matches (standard district qualifying count)
GameScore matchHistory[12];

int matchesCompleted = 0; // Tracks how many games we've played

// Mock data for the table
MatchEntry schedule[3] = {
    {'Q', 42, 0xF800}, // Next
    {'Q', 51, 0x001F}, // Following
    {'Q', 68, 0xF800}  // Final scheduled
};

int currentlyPlaying = 39;

struct LastMatchData {
    int matchNum = 38;
    int redScore = 124;
    int blueScore = 110;
    bool weWereRed = true;
    int rpEarned = 3;
};
LastMatchData lastMatch;

void drawTiger(GFXcanvas16 *canvas, int x, int y) {
    for (int row = 0; row < 64; row++) {
        for (int col = 0; col < 64; col++) {
            uint16_t color = tiger_hires_map[row * 64 + col];
            canvas->drawPixel(x + col, y + row, color);
        }
    }
}

void addMatchResult(int mNum, int aF, int aC, int tF, int tC, int fouls, int total, bool fRP, bool tRP) {
    if (matchesCompleted < 12) {
        matchHistory[matchesCompleted] = {mNum, aF, aC, tF, tC, fouls, total, fRP, tRP};
        matchesCompleted++;
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

    // 2. THE AP RESET (GPIO 7 - Down Button)
    // We do this BEFORE starting WiFi so we can wipe the slate clean
    gpio_reset_pin(GPIO_NUM_7);
    gpio_set_direction(GPIO_NUM_7, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_7, GPIO_PULLUP_ONLY);
    
    // 3. Init Network Stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 4. Define Hardcoded Credentials
    wifi_config_t static_wifi_config = {};
    strcpy((char*)static_wifi_config.sta.ssid, "Olympus Ranger");
    strcpy((char*)static_wifi_config.sta.password, "prowler12345");
    const char *service_name = "5459_TIGER_BOARD";
    const char *pop = "5459"; // Proof of Possession

    // 5. Setup Provisioning Manager
    wifi_prov_mgr_config_t config = {};
    config.scheme = wifi_prov_scheme_ble;
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned) {
        // STEP A: Try the Hardcoded WiFi first
        printf("Attempting Hardcoded WiFi: linksis54\n");
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &static_wifi_config);
        esp_wifi_start();
        esp_wifi_connect();

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
            printf("Starting BLE Provisioning... Look for 5459_TIGER_BOARD\n");
            ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, service_name, NULL));
        }
    } else {
        // STEP C: Already provisioned via App, just connect
        wifi_prov_mgr_stop_provisioning();
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        esp_wifi_connect();
    }

    // 6. Time Sync
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
}

void refreshTickerQueue() {
    tickerQueue.clear();
    
    // Message 1: Team Identity
    tickerQueue.push_back("IPSWICH TIGERS 5459");

    // Message 2: Dynamic Countdown
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // If year > 2020, we know NTP has synced
    if (timeinfo.tm_year > 120) {
        struct tm target;
        memset(&target, 0, sizeof(struct tm));
        target.tm_year = 2026 - 1900;
        target.tm_mon = 1;  // February (0-indexed is Jan, so 1 is Feb)
        target.tm_mday = 21; // Week 0 Date
        
        double diff = difftime(mktime(&target), now);
        int days = (int)(diff / 86400);

        if (days > 0) {
            tickerQueue.push_back("T-" + std::to_string(days) + " DAYS UNTIL WEEK 0");
        } else if (days == 0) {
            tickerQueue.push_back("IT IS WEEK 0! GO TIGERS!");
        } else {
            tickerQueue.push_back("COMPETITION SEASON IS HERE!");
        }
    } else {
        tickerQueue.push_back("WAITING FOR TIME SYNC...");
    }

    // Message 3: Fun/Static Info
    tickerQueue.push_back("ROBOTICS TEAM 5459");
}

void matrix_task(void *pvParameters) {
    refreshTickerQueue(); 
    uint16_t tigerOrange = matrix->color565(255, 140, 0);
    uint16_t white = 0xFFFF;
    
    // Animation & Control Variables
    float pulseIdx = 0;
    bool showZoom = true;
    float zoom = 0.5f;
    static uint32_t buttonHoldStart = 0;
    bool isResetting = false;

    // Ticker Control
    static int currentMsgIdx = 0;
    static float scrollX = 128.0f; // Start at the divider
    const int GAP = 60; 

    // Rotation Control
    uint32_t lastRotationTime = 0;
    bool showUpcoming = true; // Toggle between Schedule and Stats

    while(1) {
        canvas_dev->fillScreen(0);
        uint32_t nowMs = esp_timer_get_time() / 1000;

        // --- 1. PANEL ROTATION TIMER (10 Seconds) ---
        if (nowMs - lastRotationTime > 10000) {
            showUpcoming = !showUpcoming;
            lastRotationTime = nowMs;
        }

        // --- 2. RESET BUTTON CHECK ---
        if (gpio_get_level(GPIO_NUM_7) == 0) { 
            if (buttonHoldStart == 0) buttonHoldStart = nowMs;
            uint32_t holdTime = nowMs - buttonHoldStart;

            if (holdTime > 50) { 
                isResetting = true;
                canvas_dev->setFont(NULL);
                canvas_dev->setTextColor(0xF800); 
                canvas_dev->setCursor(80, 25);
                canvas_dev->print("HOLD TO RESET:");
                canvas_dev->setCursor(110, 35);
                canvas_dev->print(3 - (holdTime / 1000));
            }
            if (holdTime > 3000) {
                nvs_flash_erase();
                esp_restart();
            }
        } else {
            buttonHoldStart = 0;
            isResetting = false;
        }

        // --- 3. MAIN DRAWING LOGIC ---
        if (!isResetting) {
            if (showZoom) {
                // Intro Zoom
                int size = (int)(64 * zoom);
                int xPos = 27 - (size / 2); int yPos = 28 - (size / 2);
                for(int r=0; r<64; r++) {
                    for(int c=0; c<64; c++) {
                        uint16_t color = tiger_hires_map[r * 64 + c];
                        canvas_dev->drawPixel(xPos + (c*zoom), yPos + (r*zoom), color);
                    }
                }
                zoom += 0.08f;
                if (zoom >= 1.0f) showZoom = false;
            } else {
                // A. STATIC BRANDING (LEFT HALF)
                drawTiger(canvas_dev, -5, -5);
                canvas_dev->setFont(&FreeSansBold18pt7b);
                canvas_dev->setTextColor(tigerOrange);
                canvas_dev->setCursor(45, 38); 
                canvas_dev->print("5459");

                // B. CENTER DIVIDER
                canvas_dev->drawFastVLine(130, 2, 60, 0x3186);

                // C. THE TICKER (Scrolling under 5459 only)
                if (tickerQueue.empty()) refreshTickerQueue();
                canvas_dev->setFont(&FreeSans9pt7b);
                canvas_dev->setTextWrap(false);
                canvas_dev->setTextColor(white);
                
                int16_t tx1, ty1; uint16_t tw1, th1;
                canvas_dev->getTextBounds(tickerQueue[currentMsgIdx].c_str(), 0, 0, &tx1, &ty1, &tw1, &th1);
                
                canvas_dev->setCursor((int)scrollX, 63); // Bottom edge
                canvas_dev->print(tickerQueue[currentMsgIdx].c_str());
                scrollX -= 2.5f;

                if (scrollX < -tw1) scrollX = 128.0f; // Reset to center line

                // D. TICKER CLIPPER (Erase anything that goes past X=130)
                canvas_dev->fillRect(131, 48, 125, 16, 0); 

                // E. DATA PANELS (RIGHT HALF)
                canvas_dev->setFont(NULL);
                if (showUpcoming) {
                    // --- UPCOMING SCHEDULE VIEW ---
                    canvas_dev->setTextColor(0xFFE0); // Yellow
                    canvas_dev->setCursor(138, 5);
                    canvas_dev->print("NOW ON FIELD: Q"); canvas_dev->print(currentlyPlaying);
                    canvas_dev->drawFastHLine(135, 14, 115, 0x3186);

                    for (int i = 0; i < 3; i++) {
                        int yOff = 26 + (i * 9);
                        canvas_dev->setTextColor(schedule[i].color);
                        canvas_dev->setCursor(138, yOff);
                        canvas_dev->print(schedule[i].type); canvas_dev->print(schedule[i].number);
                        canvas_dev->setTextColor(white);
                        canvas_dev->print(" - 10:45A");
                    }
                } else {
                    // --- LAST MATCH STATS VIEW ---
                    if (matchesCompleted == 0) {
                        canvas_dev->setCursor(145, 30);
                        canvas_dev->print("AWAITING DATA...");
                    } else {
                        GameScore last = matchHistory[matchesCompleted - 1];
                        canvas_dev->setTextColor(0x07FF); // Cyan
                        canvas_dev->setCursor(138, 5);
                        canvas_dev->print("LAST MATCH: Q"); canvas_dev->print(last.matchNum);
                        canvas_dev->drawFastHLine(135, 14, 115, 0x3186);

                        canvas_dev->setCursor(138, 18);
                        canvas_dev->setTextColor(0xF81F); canvas_dev->print("AUTO:");
                        canvas_dev->setTextColor(white); canvas_dev->print(" F"); canvas_dev->print(last.autoFuel);
                        canvas_dev->print(" C"); canvas_dev->print(last.autoClimb);

                        canvas_dev->setCursor(138, 28);
                        canvas_dev->setTextColor(0x07E0); canvas_dev->print("TELE:");
                        canvas_dev->setTextColor(white); canvas_dev->print(" F"); canvas_dev->print(last.teleFuel);
                        if (last.fuelRP) { canvas_dev->setTextColor(0xFBE0); canvas_dev->print("**"); }

                        canvas_dev->setCursor(138, 38);
                        canvas_dev->setTextColor(white); canvas_dev->print("CLIMB L"); 
                        canvas_dev->print(last.teleClimb/10);
                        if (last.towerRP) { canvas_dev->setTextColor(0x07FF); canvas_dev->print("*"); }

                        canvas_dev->setCursor(138, 48);
                        canvas_dev->setTextColor(0xF800); canvas_dev->print("FOULS: ");
                        canvas_dev->setTextColor(white); canvas_dev->print(last.foulPointsAwarded);
                        
                        canvas_dev->setCursor(138, 56);
                        canvas_dev->setTextColor(0xFFE0); canvas_dev->print("SCORE: ");
                        canvas_dev->print(last.totalScore);
                    }
                }

                // F. GLOBAL RANK (Stays in corner)
                canvas_dev->setFont(NULL);
                canvas_dev->setTextColor(0x7BEF);
                canvas_dev->setCursor(220, 18); canvas_dev->print("RANK");
                canvas_dev->setFont(&FreeSansBold12pt7b);
                canvas_dev->setTextColor(0xFFE0);
                canvas_dev->setCursor(215, 45); canvas_dev->print("12");

                // G. PULSING BORDER
                pulseIdx += 0.1;
                uint8_t p = 150 + (int)(100 * sin(pulseIdx));
                uint16_t pColor = matrix->color565(p, (p * 140 / 255), 0);
                canvas_dev->drawRect(0, 0, 256, 64, pColor);
            }
        }

        // --- 4. OVERLAYS (Clock & WiFi) ---
        time_t now; struct tm ti; time(&now); localtime_r(&now, &ti);
        canvas_dev->setFont(NULL);
        uint16_t clkC = (ti.tm_year > 120) ? white : 0xF800;
        canvas_dev->setTextColor(clkC);
        int hr = ti.tm_hour % 12; if (hr == 0) hr = 12;
        canvas_dev->setCursor(95, 2); // Shifted for 5459 clearance
        canvas_dev->print(hr);
        if (ti.tm_sec % 2 == 0) canvas_dev->drawPixel((hr >= 10 ? 108 : 102), 4, clkC);
        canvas_dev->setCursor((hr >= 10 ? 112 : 106), 2);
        if (ti.tm_min < 10) canvas_dev->print("0");
        canvas_dev->print(ti.tm_min);

        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK && ti.tm_sec % 2 == 0) 
            canvas_dev->fillCircle(5, 3, 1, 0xF800); 

        // --- 5. RENDER ---
        uint16_t *buf = canvas_dev->getBuffer();
        for(int y=0; y<64; y++) {
            uint16_t* row = &buf[y * 256];
            for(int x=0; x<256; x++) matrix->drawPixel(x, y, row[x]);
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

extern "C" void app_main(void) {
    HUB75_I2S_CFG mxconfig(64, 64, 4);
    mxconfig.gpio.r1 = 42; mxconfig.gpio.g1 = 41; mxconfig.gpio.b1 = 40;
    mxconfig.gpio.r2 = 38; mxconfig.gpio.g2 = 39; mxconfig.gpio.b2 = 37;
    mxconfig.gpio.a = 45;  mxconfig.gpio.b = 36;  mxconfig.gpio.c = 48;
    mxconfig.gpio.d = 35;  mxconfig.gpio.e = 21;
    mxconfig.gpio.lat = 47; mxconfig.gpio.oe = 14; mxconfig.gpio.clk = 2;
    mxconfig.clkphase = false;
    mxconfig.latch_blanking = 4;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_8M;
    addMatchResult(12, 15, 15, 45, 30, 5, 156, true, true);  // Great game
    addMatchResult(24, 8, 0, 32, 20, 15, 98, false, false); // Rough game
    matrix = new MatrixPanel_I2S_DMA(mxconfig);
    if (matrix->begin()) {
        matrix->setBrightness8(60);
        setup_networking();
        xTaskCreatePinnedToCore(matrix_task, "matrix_task", 8192, NULL, 10, NULL, 1);
    }
}