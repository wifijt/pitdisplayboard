#include "matrix_display.h"
#include "globals.h"
#include "messages.h"
#include "pacman_engine.h"
#include "tiger_hires.h"
#include "Adafruit_GFX.h"
#include "FreeSansBold18pt7b.h"
#include "FreeSansBold12pt7b.h"
#include "FreeSans9pt7b.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "esp_system.h"
#include <time.h>
#include <math.h>
#include "sponsors.h"

enum SponsorState {
    SPONSOR_IDLE,
    SPONSOR_INTRO,
    SPONSOR_SHOW_LIST,
    SPONSOR_OUTRO
};

bool isSafeForSponsors() {
    time_t now;
    time(&now);

    // Check against schedule
    for (int i = 0; i < 3; i++) {
        if (schedule[i].estTime != 0) {
            double diff = difftime(schedule[i].estTime, now);
            // "within 30 min of a match" -> -30min to +30min (approx)
            // If match is 10 mins ago, diff is -600.
            // If match is 10 mins in future, diff is 600.
            if (fabs(diff) < 30 * 60) return false;
        }
    }
    return true;
}

void drawTiger(GFXcanvas16 *canvas, int x, int y) {
    for (int row = 0; row < 64; row++) {
        for (int col = 0; col < 64; col++) {
            uint16_t color = tiger_hires_map[row * 64 + col];
            canvas->drawPixel(x + col, y + row, color);
        }
    }
}

void refreshTickerQueue() {
    tickerQueue.clear();

    // Message 1: Team Identity
    tickerQueue.push_back(MSG_TEAM_NAME);

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
            tickerQueue.push_back(std::string(MSG_WEEK0_PREFIX) + std::to_string(days) + MSG_WEEK0_SUFFIX);
        } else if (days == 0) {
            tickerQueue.push_back(MSG_WEEK0_NOW);
        } else {
            tickerQueue.push_back(MSG_SEASON_START);
        }
    } else {
        tickerQueue.push_back(MSG_WAIT_SYNC);
    }

    // Message 3: Fun/Static Info
    tickerQueue.push_back(MSG_SUBTITLE);
}

void addMatchResult(int mNum, int aF, int aC, int tF, int tC, int fouls, int total, bool fRP, bool tRP) {
    if (matchesCompleted < 12) {
        matchHistory[matchesCompleted] = {mNum, aF, aC, tF, tC, fouls, total, fRP, tRP};
        matchesCompleted++;
    }
}

void matrix_task(void *pvParameters) {
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

    // Sponsor Logic Variables
    SponsorState sponsorState = SPONSOR_IDLE;
    time_t lastSponsorRunTime = 0;
    float sponsorScrollX = 256;
    unsigned int sponsorListIdx = 0;
    uint32_t sponsorWaitStart = 0; // ms
    float sponsorListY = 64;
    uint32_t outroStartTime = 0;

    // Initialize last run time to (now - 14.5 mins) so it runs 30 seconds after boot for testing
    time(&lastSponsorRunTime);
    lastSponsorRunTime -= (15 * 60) - 30;

    while(1) {
        canvas_dev->fillScreen(0);
        uint32_t nowMs = esp_timer_get_time() / 1000;

        // --- SPONSOR CHECK ---
        if (sponsorState == SPONSOR_IDLE) {
            time_t nowSec;
            time(&nowSec);
            if (difftime(nowSec, lastSponsorRunTime) >= 15 * 60) {
                if (isSafeForSponsors()) {
                    sponsorState = SPONSOR_INTRO;
                    sponsorScrollX = 256;
                    lastSponsorRunTime = nowSec;
                }
            }
        }

        if (sponsorState != SPONSOR_IDLE) {
            // --- SPONSOR DISPLAY LOGIC ---
            // Draw Green Pulsing Border
            pulseIdx += 0.3; // Faster pulse
            uint8_t p = 150 + (int)(100 * sin(pulseIdx));
            uint16_t pColor = matrix->color565(0, p, 0); // Green
            canvas_dev->drawRect(0, 0, 256, 64, pColor);
            canvas_dev->drawRect(1, 1, 254, 62, pColor);

            if (sponsorState == SPONSOR_INTRO) {
                // Static Header
                canvas_dev->setFont(NULL);
                canvas_dev->setTextColor(0xFFFF);

                const char* header = SPONSOR_HEADER_TEXT;
                int16_t x1, y1; uint16_t w, h;
                // getTextBounds with NULL font works differently or not at all in some GFX versions,
                // but default font is 6x8 pixels.
                // Centering manually assuming 6px width per char
                int len = strlen(header);
                int w_guess = len * 6;
                int startX = (256 - w_guess) / 2;
                if (startX < 0) startX = 0;

                canvas_dev->setCursor(startX, 5);
                canvas_dev->print(header);

                // Wait briefly then move to list
                if (sponsorWaitStart == 0) sponsorWaitStart = nowMs;
                if (nowMs - sponsorWaitStart > 2000) {
                     sponsorState = SPONSOR_SHOW_LIST;
                     sponsorListIdx = 0;
                     sponsorListY = 64;
                     sponsorWaitStart = 0;
                }
            }
            else if (sponsorState == SPONSOR_SHOW_LIST) {
                if (sponsorListIdx < SPONSOR_LIST.size()) {
                    std::string name = SPONSOR_LIST[sponsorListIdx];
                    canvas_dev->setFont(&FreeSansBold12pt7b);
                    canvas_dev->setTextColor(0xFC00); // Orange-ish

                    // Basic Word Wrap Logic
                    // 1. Check width
                    int16_t x1, y1; uint16_t w, h;
                    canvas_dev->getTextBounds(name.c_str(), 0, 0, &x1, &y1, &w, &h);

                    std::vector<std::string> lines;
                    if (w > 250) {
                        // Needs wrap. Simple split by finding middle space?
                        // For simplicity, just split if too long.
                        // Real wrapping requires parsing spaces.
                        size_t splitPos = name.length() / 2;
                        size_t spacePos = name.find(' ', splitPos);
                         if (spacePos == std::string::npos) spacePos = name.find_last_of(' ', splitPos);

                         if (spacePos != std::string::npos) {
                             lines.push_back(name.substr(0, spacePos));
                             lines.push_back(name.substr(spacePos + 1));
                         } else {
                             lines.push_back(name); // Can't split
                         }
                    } else {
                        lines.push_back(name);
                    }

                    int totalHeight = lines.size() * 25; // approx height with spacing
                    float targetY = 35 + (totalHeight / 2); // Center in remaining space

                    if (sponsorWaitStart == 0) {
                        // Scrolling up
                        if (sponsorListY > targetY) {
                            sponsorListY -= 2.0;
                        } else {
                            // Arrived
                            sponsorListY = targetY;
                            sponsorWaitStart = nowMs;
                        }
                    } else {
                        // Waiting
                        if (nowMs - sponsorWaitStart > 1000) {
                             // Done waiting, move next (up)
                             sponsorListY -= 2.0;
                             // Just scroll off top
                             if (sponsorListY < -50) {
                                 sponsorListIdx++;
                                 sponsorListY = 64;
                                 sponsorWaitStart = 0;
                             }
                        }
                    }

                    // Draw Lines
                    int currentY = (int)sponsorListY;
                    // Move up slightly if multi-line to center block
                    if (lines.size() > 1) currentY -= (10 * (lines.size()-1));

                    for (const auto& line : lines) {
                        canvas_dev->getTextBounds(line.c_str(), 0, 0, &x1, &y1, &w, &h);
                        int drawX = (256 - w) / 2;
                        canvas_dev->setCursor(drawX, currentY);
                        canvas_dev->print(line.c_str());
                        currentY += 25; // Line height
                    }

                    // Draw Header ON TOP of scrolling text with black background
                    canvas_dev->fillRect(0, 0, 256, 15, 0x0000); // Black box
                    canvas_dev->setFont(NULL);
                    canvas_dev->setTextColor(0xFFFF);
                    const char* header = SPONSOR_HEADER_TEXT;
                    int len = strlen(header);
                    int w_guess = len * 6;
                    int startX = (256 - w_guess) / 2;
                    if (startX < 0) startX = 0;
                    canvas_dev->setCursor(startX, 5);
                    canvas_dev->print(header);

                } else {
                    sponsorState = SPONSOR_OUTRO;
                    outroStartTime = nowMs;
                }
            }
            else if (sponsorState == SPONSOR_OUTRO) {
                float progress = (nowMs - outroStartTime) / 1000.0f; // seconds
                if (progress > 4.0f) {
                    sponsorState = SPONSOR_IDLE;
                } else {
                    // Static Thanks
                    canvas_dev->setFont(&FreeSansBold18pt7b);
                    uint16_t color = (fmod(progress, 0.5f) < 0.25f) ? 0xFFFF : 0x07E0; // White/Green flash
                    canvas_dev->setTextColor(color);

                    std::string thanks = "THANK YOU!!";
                    int16_t x1, y1; uint16_t w, h;
                    canvas_dev->getTextBounds(thanks.c_str(), 0, 0, &x1, &y1, &w, &h);

                    canvas_dev->setCursor((256 - w)/2, 45);
                    canvas_dev->print(thanks.c_str());
                }
            }
        }

        // Only run normal logic if IDLE
        if (sponsorState == SPONSOR_IDLE) {

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
        if (!borderActive && (nowMs - lastBorderStartTime > 120000)) {
            reset_pacman_game(nowMs);
        }

        if (borderActive) {
            run_pacman_cycle(canvas_dev, nowMs);
        } else {
            //  Draw Pulsing Border

            pulseIdx += 0.1;
            uint8_t p = 150 + (int)(100 * sin(pulseIdx));
            uint16_t pColor = matrix->color565(p, (p * 140 / 255), 0);
            canvas_dev->drawRect(0, 0, 256, 64, pColor);
            canvas_dev->drawRect(1, 1, 254, 62, pColor);
        }
        } // End of sponsorState == SPONSOR_IDLE check

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
