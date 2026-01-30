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
#include "esp_crt_bundle.h"

std::vector<std::string> tickerQueue;

MatrixPanel_I2S_DMA *matrix = nullptr;
GFXcanvas16 *canvas_dev = new GFXcanvas16(256, 64);

const uint8_t ghost_shape[7][7] = {
    {0,1,1,1,1,1,0},
    {1,2,1,2,1,1,1},
    {1,2,3,2,3,1,1},
    {1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1},
    {1,0,1,0,1,0,1}
};

const uint8_t pacman_open[7][7] = {
    {0,0,1,1,1,0,0},
    {0,1,1,1,1,1,0},
    {1,1,1,1,0,0,0},
    {1,1,1,0,0,0,0},
    {1,1,1,1,0,0,0},
    {0,1,1,1,1,1,0},
    {0,0,1,1,1,0,0}
};

// 0: Trans, 1: Primary, 2: Eyes(White), 3: Pupils(Blue)
const uint8_t pacman_closed[7][7] = {
    {0,0,1,1,1,0,0},
    {0,1,1,1,1,1,0},
    {1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1},
    {0,1,1,1,1,1,0},
    {0,0,1,1,1,0,0}
};

// Ghost Colors (FRC/Retro Mix)
#define GHOST_BLINKY 0xF800 // Red
#define GHOST_PINKY  0xFC18 // Pink
#define GHOST_INKY   0x07FF // Cyan
#define GHOST_CLYDE  0xFB20 // Orange

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

void getPos(float p, int &x, int &y) {
    const int perimeter = 612; 
    // Ensure p is always positive and within bounds
    float safeP = fmod(p, (float)perimeter);
    if (safeP < 0) safeP += perimeter;

    if (safeP < 249) { x = 3 + safeP; y = 3; }                // Top
    else if (safeP < 306) { x = 252; y = 3 + (safeP - 249); } // Right
    else if (safeP < 555) { x = 252 - (safeP - 306); y = 60; }// Bottom
    else { x = 3; y = 60 - (safeP - 555); }                   // Left
}

void drawPac(GFXcanvas16 *canvas, int x, int y, float p, float currentSpeed) {
    int dir; 
    bool movingForward = (currentSpeed > 0);

    // Explicitly define the rails to prevent "lap-over" errors
    if (p >= 0.0f && p < 249.0f)      dir = movingForward ? 0 : 2; // Top
    else if (p >= 249.0f && p < 306.0f) dir = movingForward ? 1 : 3; // Right
    else if (p >= 306.0f && p < 555.0f) dir = movingForward ? 2 : 0; // Bottom
    else if (p >= 555.0f && p < 612.0f) dir = movingForward ? 3 : 1; // Left
    else dir = movingForward ? 0 : 2; // Fallback to Top/Right

    // Toggle mouth
    bool open = ((int)(p / 5) % 2 == 0);
    const uint8_t (*sprite)[7] = open ? pacman_open : pacman_closed;

    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 7; j++) {
            int r_i, r_j;
            
            // MAPPING
            if (dir == 0) {      // RIGHT
                r_i = i; r_j = j; 
            } 
            else if (dir == 1) { // DOWN
                r_i = 6 - j; r_j = i; 
            } 
            else if (dir == 2) { // LEFT
                r_i = i; r_j = 6 - j; 
            } 
            else {               // UP
                r_i = j; r_j = 6 - i; 
            }

            if (sprite[r_i][r_j] == 1) {
                canvas->drawPixel(x + j, y + i, 0xFFE0); 
            }
        }
    }
}

void drawGhost(GFXcanvas16 *canvas, int x, int y, uint16_t color, bool scared, float p, float speed) {
    bool movingForward = (speed > 0);
    // Determine direction same as Pac-Man
    int dir;
    if (p >= 0.0f && p < 249.0f)      dir = movingForward ? 0 : 2; 
    else if (p >= 249.0f && p < 306.0f) dir = movingForward ? 1 : 3; 
    else if (p >= 306.0f && p < 555.0f) dir = movingForward ? 2 : 0; 
    else                               dir = movingForward ? 3 : 1;

    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 7; j++) {
            uint8_t pixel = ghost_shape[i][j];
            if (pixel == 1) canvas->drawPixel(x+j, y+i, scared ? 0x001F : color);
            else if (pixel == 2) canvas->drawPixel(x+j, y+i, 0xFFFF); // Whites
            else if (pixel == 3) {
                // Shift pupils based on direction
                int ox = 0, oy = 0;
                if (dir == 0) ox = 1;      // Look Right
                else if (dir == 1) oy = 1; // Look Down
                else if (dir == 2) ox = -1;// Look Left
                else oy = -1;              // Look Up
                canvas->drawPixel(x+j+ox, y+i+oy, scared ? 0xFFFF : 0x001F);
            }
        }
    }
}

// Update this line (Line 197)
void update_pacman_border(GFXcanvas16 *canvas, float &pPos, float gPosArr[4], bool &pMode, float speed, bool eaten[4]) {
    uint16_t ghostCols[4] = {GHOST_BLINKY, GHOST_PINKY, GHOST_INKY, GHOST_CLYDE};
    // Note: We use 616.0f directly in fmod below, so perimeter variable isn't strictly needed here
    bool movingForward = (speed > 0);

    // Update Pac-Man
    pPos = fmod(pPos + speed, 616.0f);
    if (pPos < 0) pPos += 616.0f;

    int pax, pay; getPos(pPos, pax, pay);
    drawPac(canvas, pax - 3, pay - 3, pPos, speed);

    // Update Ghosts
    for (int i = 0; i < 4; i++) {
        if (eaten[i]) continue; // Skip if Pac-Man ate them!

        float gSpeed = pMode ? 0.8f : 1.6f;
        gPosArr[i] = fmod(gPosArr[i] + (movingForward ? gSpeed : -gSpeed), 616.0f);
        if (gPosArr[i] < 0) gPosArr[i] += 616.0f;

        int gx, gy; getPos(gPosArr[i], gx, gy);
        drawGhost(canvas, gx - 3, gy - 3, ghostCols[i], pMode, gPosArr[i], speed);
    }
}


void addMatchResult(int mNum, int aF, int aC, int tF, int tC, int fouls, int total, bool fRP, bool tRP) {
    if (matchesCompleted < 12) {
        matchHistory[matchesCompleted] = {mNum, aF, aC, tF, tC, fouls, total, fRP, tRP};
        matchesCompleted++;
    }
}

#include "esp_http_client.h"
#include "cJSON.h"

#define TBA_KEY "vHWwM5zFjNEHjQo7nhdQQ93ukpqAThfVkDtKGyAwnCSupO1nELxT40fFE7YhvnN5" // Get from thebluealliance.com/account
#define TBA_URL "https://www.thebluealliance.com/api/v3/team/frc5459/event/2025mabos/matches/simple"

void parse_tba_json(const char *json_string) {
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) return;

    int match_count = cJSON_GetArraySize(root);
    matchesCompleted = 0; // Reset history to fill with fresh API data

    for (int i = 0; i < match_count && i < 12; i++) {
        cJSON *match = cJSON_GetArrayItem(root, i);
        cJSON *alliances = cJSON_GetObjectItem(match, "alliances");
        if (!alliances) continue;

        cJSON *red = cJSON_GetObjectItem(alliances, "red");
        cJSON *blue = cJSON_GetObjectItem(alliances, "blue");
        
        // We check if the score exists and is not -1 (unplayed)
        cJSON *rScoreObj = cJSON_GetObjectItem(red, "score");
        if (rScoreObj && rScoreObj->valueint >= 0) {
            matchHistory[matchesCompleted].matchNum = cJSON_GetObjectItem(match, "match_number")->valueint;
            matchHistory[matchesCompleted].totalScore = rScoreObj->valueint;
            
            // For the 2026 schema, we'll fill these with 0 for now
            matchHistory[matchesCompleted].autoFuel = 0; 
            matchHistory[matchesCompleted].teleFuel = 0;
            
            matchesCompleted++;
        }
    }
    cJSON_Delete(root);
}

void tba_api_task(void *pvParameters) {
    printf("TBA TASK: Started and running on Core %d\n", xPortGetCoreID());

    while (1) {
        // Get the WiFi interface handle
        esp_netif_ip_info_t ip_info;
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

        // Check if we actually have an IP address yet
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            
            printf("TBA TASK: IP Found (%d.%d.%d.%d). Fetching TBA...\n", 
                    IP2STR(&ip_info.ip));

            esp_http_client_config_t config = {};
            // Using HTTP for now to avoid SSL certificate issues
            config.url = "http://www.thebluealliance.com/api/v3/team/frc5459/event/2025mabos/matches/simple";
            config.crt_bundle_attach = esp_crt_bundle_attach;
            config.timeout_ms = 10000;

            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_set_header(client, "X-TBA-Auth-Key", "MY TBA KEY");

            esp_err_t err = esp_http_client_perform(client);

            if (err == ESP_OK) {
                int status = esp_http_client_get_status_code(client);
                printf("TBA TASK: HTTP Status = %d\n", status);
                
                if (status == 200) {
                    // Success!
                    int len = esp_http_client_get_content_length(client);
                    char *buffer = (char*)malloc(len + 1);
                    if (buffer) {
                        esp_http_client_read_response(client, buffer, len);
                        buffer[len] = '\0';
                        parse_tba_json(buffer);
                        free(buffer);
                    }
                    // If successful, wait 5 minutes before checking again
                    vTaskDelay(pdMS_TO_TICKS(300000)); 
                } else {
                    printf("TBA TASK: Server returned status %d\n", status);
                }
            } else {
                printf("TBA TASK: Fetch failed: %s\n", esp_err_to_name(err));
            }
            esp_http_client_cleanup(client);
        } else {
            // Not connected yet, wait 2 seconds and check again
            printf("TBA TASK: Waiting for WiFi IP...\n");
            vTaskDelay(pdMS_TO_TICKS(2000)); 
            continue; // Skip the rest and loop back to check IP again
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // Default fallback delay
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
    strcpy((char*)static_wifi_config.sta.ssid, "STATIC SSID");
    strcpy((char*)static_wifi_config.sta.password, "STATIC PWD");
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
        printf("Attempting Hardcoded WiFi: \n");
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
    static float pacPos = 0;
    static float ghostPos[4] = {-20, -40, -60, -80};
    static float worldSpeed = 1.2f;
    static bool powerMode = false;
    static uint32_t powerStartTime = 0;
    // uint16_t ghostCols[4] = {GHOST_BLINKY, GHOST_PINKY, GHOST_INKY, GHOST_CLYDE};
    // int perimeter = (256 + 64) * 2; // 640 total pixels
    static bool borderActive = false;
    static uint32_t lastBorderStartTime = 0;
    static bool ghostEaten[4] = {false, false, false, false};

    // 1. Initialize Ticker & Colors
    refreshTickerQueue(); 
    uint16_t tigerOrange = matrix->color565(255, 140, 0);
    uint16_t white = 0xFFFF;
    
    // Animation & Reset Variables
    float pulseIdx = 0;
    bool showZoom = true;
    float zoom = 0.5f;
    static uint32_t buttonHoldStart = 0;
    bool isResetting = false;

    // Robust Ticker Variables
    static int currentMsgIdx = 0;
    static int nextMsgIdx = 1;
    static float scrollX = 128.0f;
    const int GAP = 60; 

    // Rotation Control
    uint32_t lastRotationTime = 0;
    bool showUpcoming = true; // Toggle between Schedule and Stats

    while(1) {
        canvas_dev->fillScreen(0);
        uint32_t nowMs = esp_timer_get_time() / 1000;

        // --- 1. PANEL ROTATION TIMER (10 Seconds) ---
        if (nowMs - lastRotationTime > 7000) {
            showUpcoming = !showUpcoming;
            lastRotationTime = nowMs;
        }

        // --- A. PROVISIONING & RESET CHECK ---
        bool is_prov = true;
        int prov_check_counter = 0;
        if (prov_check_counter++ > 200) { // Only check every ~5 seconds
            wifi_prov_mgr_is_provisioned(&is_prov);
            prov_check_counter = 0;
        }

        if (gpio_get_level(GPIO_NUM_7) == 0) { // Button Pressed
            if (buttonHoldStart == 0) buttonHoldStart = esp_timer_get_time() / 1000;
            uint32_t holdTime = (esp_timer_get_time() / 1000) - buttonHoldStart;

            if (holdTime > 50) { 
                isResetting = true;
                canvas_dev->setFont(NULL);
                canvas_dev->setTextColor(0xF800); // Red
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

        // --- B. MAIN DRAWING LOGIC (Only if not resetting) ---
        if (!isResetting) {
            if (showZoom) {
                // Zooming Intro Animation
                int size = (int)(64 * zoom);
                int xPos = 27 - (size / 2);
                int yPos = 28 - (size / 2);
                for(int r=0; r<64; r++) {
                    for(int c=0; c<64; c++) {
                        uint16_t color = tiger_hires_map[r * 64 + c];
                        canvas_dev->drawPixel(xPos + (c*zoom), yPos + (r*zoom), color);
                    }
                }
                zoom += 0.08f;
                if (zoom >= 1.0f) { showZoom = false; }
            } else {
                // 1. Draw Tiger
                drawTiger(canvas_dev, -5, -3);
                // 2. Draw Team Number
                canvas_dev->setFont(&FreeSansBold18pt7b);
                canvas_dev->setTextColor(tigerOrange);
                canvas_dev->setCursor(52, 40);
                canvas_dev->print("5459");

                // 3. Robust Ticker Logic (Queue based)
                if (tickerQueue.empty()) refreshTickerQueue();
                
                canvas_dev->setFont(&FreeSans9pt7b);
                canvas_dev->setTextWrap(false);
                canvas_dev->setTextColor(is_prov ? white : 0xF800); 

                std::string msg1 = tickerQueue[currentMsgIdx];
                std::string msg2 = tickerQueue[nextMsgIdx];

                // Draw Message 1
                canvas_dev->setCursor((int)scrollX, 60);
                canvas_dev->print(msg1.c_str());

                // Find where Message 1 ends
                int16_t x1, y1; uint16_t w1, h1;
                canvas_dev->getTextBounds(msg1.c_str(), 0, 0, &x1, &y1, &w1, &h1);

                // Draw Message 2 following msg1
                canvas_dev->setCursor((int)scrollX + w1 + GAP, 60);
                canvas_dev->print(msg2.c_str());

                scrollX -= 2.0f; // Scrolling Speed
                canvas_dev->fillRect(129, 45, 127, 17, 0);
                // Handover: When msg1 clears the screen
                if (scrollX < -(w1 + GAP)) {
                    scrollX = 0;
                    currentMsgIdx = nextMsgIdx;
                    nextMsgIdx = (nextMsgIdx + 1) % tickerQueue.size();
                    if (currentMsgIdx == 0) refreshTickerQueue(); // Update data
                }
                // 1. Draw a vertical separator line
                canvas_dev->drawFastVLine(130, 5, 54, 0x3186); 

                canvas_dev->setFont(NULL); // Small fixed font
                if (showUpcoming) {
                    // 1. HEADER: Currently Playing
                    canvas_dev->setTextColor(0xFFE0); // Yellow
                    canvas_dev->setCursor(138, 5);
                    canvas_dev->print("NOW ON FIELD: Q");
                    canvas_dev->print(currentlyPlaying);
                    
                    canvas_dev->drawFastHLine(135, 14, 115, 0x3186); // Header separator

                    // 2. TABLE HEADERS (Labels)
                    canvas_dev->setTextColor(0x7BEF); // Gray
                    canvas_dev->setCursor(138, 18);
                    canvas_dev->print("UPCOMING");
                    

                    // 3. COLOR-CODED MATCH LIST
                    for (int i = 0; i < 3; i++) {
                        int yOff = 29 + (i * 10); // 10 pixels spacing for better legibility
                        
                        // Match Type and Number (Color-coded by Alliance)
                        canvas_dev->setTextColor(schedule[i].color);
                        canvas_dev->setCursor(138, yOff);
                        
                        // Example: "Q42" 
                        canvas_dev->print(schedule[i].type);
                        canvas_dev->print(schedule[i].number);

                        // Optional: Draw a small dot or dash in the alliance color to the left 
                        // to make it "pop" even more
                        canvas_dev->drawFastVLine(134, yOff - 1, 7, schedule[i].color);

                        // Est Time (Placeholder - assuming you add 'est_time' to your struct later)
                        canvas_dev->setTextColor(white);
                        canvas_dev->setCursor(160, yOff);
                        canvas_dev->print("-10:45A"); 
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

                        canvas_dev->setCursor(138, 27);
                        canvas_dev->setTextColor(0x07E0); canvas_dev->print("TELE:");
                        canvas_dev->setTextColor(white); canvas_dev->print(" F"); canvas_dev->print(last.teleFuel);
                        if (last.fuelRP) { canvas_dev->setTextColor(0xFBE0); canvas_dev->print("**"); }

                        canvas_dev->setCursor(138, 36);
                        canvas_dev->setTextColor(white); canvas_dev->print("CLIMB:"); 
                        canvas_dev->print(last.teleClimb/10);
                        if (last.towerRP) { canvas_dev->setTextColor(0x07FF); canvas_dev->print("*"); }

                        canvas_dev->setCursor(138, 45);
                        canvas_dev->setTextColor(0xF800); canvas_dev->print("FOUL: ");
                        canvas_dev->setTextColor(white); canvas_dev->print(last.foulPointsAwarded);
                        
                        canvas_dev->setCursor(138, 54);
                        canvas_dev->setTextColor(0xFFE0); canvas_dev->print("SCORE: ");
                        canvas_dev->print(last.totalScore);
                    }
                }
                // B. RANK INFO (Panel 4)
                canvas_dev->setFont(NULL);
                canvas_dev->setTextColor(0x7BEF);
                canvas_dev->setCursor(220, 18);
                canvas_dev->print("RANK");
                
                canvas_dev->setFont(&FreeSansBold12pt7b);
                canvas_dev->setTextColor(0xFFE0); // Yellow
                canvas_dev->setCursor(215, 45);
                canvas_dev->print("12");

                // 1. Draw Static Tiger & Pulsing Border
                
            }
        }

        // --- C. PERMANENT OVERLAYS (Clock & Status) ---
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // Clock Display
        canvas_dev->setFont(NULL);
        uint16_t clockColor = (timeinfo.tm_year > 120) ? white : 0xF800;
        canvas_dev->setTextColor(clockColor);
        int hour = timeinfo.tm_hour % 12;
        if (hour == 0) hour = 12;
        int tX = 91;
        canvas_dev->setCursor(tX, 2);
        canvas_dev->print(hour);
        
        int colonX = (hour >= 10) ? tX + 12 : tX + 6;
        if (timeinfo.tm_sec % 2 == 0) {
            canvas_dev->drawPixel(colonX + 1, 4, clockColor);
            canvas_dev->drawPixel(colonX + 1, 6, clockColor);
        }
        canvas_dev->setCursor(colonX + 4, 2);
        if (timeinfo.tm_min < 10) canvas_dev->print("0");
        canvas_dev->print(timeinfo.tm_min);
        canvas_dev->setCursor(120, 2);
        canvas_dev->print((timeinfo.tm_hour >= 12) ? "P" : "A");

        // Wi-Fi Blink Indicator
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
            if (timeinfo.tm_sec % 2 == 0) canvas_dev->fillCircle(5, 3, 1, 0xF800); 
        } 
        // --- PAC-MAN BORDER ANIMATION ---
        // --- TIMING: Turn on every 2 minutes (120,000 ms) ---
        if (!borderActive && (nowMs - lastBorderStartTime > 120000)) {
            borderActive = true;
            lastBorderStartTime = nowMs;
            pacPos = 0; // Start at Top-Left
            for(int i=0; i<4; i++) {
                ghostPos[i] = 100 + (i * 20); // Reset ghosts
                ghostEaten[i] = false;
            }
        }

        if (borderActive) {
            // 1. COLLISION DETECTION
            for (int i = 0; i < 4; i++) {
                if (ghostEaten[i]) continue;

                // Check distance between Pac-Man and Ghost
                float dist = abs(pacPos - ghostPos[i]);
                // Handle the "wrap-around" distance at the 0/616 point
                if (dist > 308) dist = 616 - dist; 

                if (dist < 8) { // Collision!
                    if (powerMode) {
                        ghostEaten[i] = true; // Pac-Man eats ghost
                    } else {
                        borderActive = false; // Ghost eats Pac-Man -> Game Over
                        lastBorderStartTime = nowMs; // RESET TIMER ON DEATH
                        // (Optional) Add a small "death" delay or effect here
                    }
                }
            }
            // 1.5 Check if all ghosts are gone
            bool allEaten = true;
            for (int i = 0; i < 4; i++) {
                if (!ghostEaten[i]) {
                    allEaten = false;
                    break;
                }
            }

            if (allEaten) {
                borderActive = false; // End game and return to pulsing border
                // Optional: Reset powerMode so it doesn't carry over to the next game
                powerMode = false; 
                lastBorderStartTime = nowMs; // RESET TIMER ON DEATH
            }
            // 2. POWER PELLET CORNERS (Same as before)
            if (!powerMode) {
                if (abs((int)pacPos - 0) < 5 || abs((int)pacPos - 250) < 5 || 
                    abs((int)pacPos - 308) < 5 || abs((int)pacPos - 558) < 5) {
                    powerMode = true;
                    powerStartTime = nowMs;
                    worldSpeed = 1.5f; 
                }
            } else if (nowMs - powerStartTime > 9000) {
                powerMode = false;
                worldSpeed = 1.2f;
            }

            // 3. DRAWING
            update_pacman_border(canvas_dev, pacPos, ghostPos, powerMode, worldSpeed, ghostEaten);
        } else {
            //  Draw Pulsing Border
                
                pulseIdx += 0.1;
                uint8_t p = 150 + (int)(100 * sin(pulseIdx));
                uint16_t pColor = matrix->color565(p, (p * 140 / 255), 0);
                canvas_dev->drawRect(0, 0, 256, 64, pColor);
                canvas_dev->drawRect(1, 1, 254, 62, pColor);
        }

        // 2. Run the Border Animation (This function now handles ALL movement and drawing)
        // --- D. FINAL RENDER ---
        uint16_t *buf = canvas_dev->getBuffer();
        for(int y = 0; y < 64; y++) {
        // We point to the start of the current row in the canvas buffer
        uint16_t* row_ptr = &buf[y * 256];
        
        // Most HUB75 libraries have a drawLine or drawIcon function, 
        // but the fastest way without custom hacking is this:
        for(int x = 0; x < 256; x++) {
            // Only draw if the pixel isn't black to save internal logic time
            if (row_ptr[x] != 0) {
                matrix->drawPixel(x, y, row_ptr[x]);
            } else {
                // If the pixel is black, we still need to clear it on the matrix
                matrix->drawPixel(x, y, 0);
                }
            }
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
        xTaskCreatePinnedToCore(tba_api_task, "tba_api_task", 10240, NULL, 5, NULL, 1);
    }
}