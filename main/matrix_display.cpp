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
#include "config.h"

// Define Buttons if not in config (safety)
#ifndef BTN_UP_GPIO
#define BTN_UP_GPIO GPIO_NUM_6
#endif
#ifndef BTN_DOWN_GPIO
#define BTN_DOWN_GPIO GPIO_NUM_7
#endif

enum SponsorState {
    SPONSOR_IDLE,
    SPONSOR_INTRO,
    SPONSOR_SHOW_LIST,
    SPONSOR_OUTRO
};

// Simulation Helper
void step_simulation(bool forward) {
    if (allMatches.empty()) return;

    time_t vNow = get_current_time();
    time_t targetTime = 0;
    bool found = false;

    if (matchDataMutex != NULL && xSemaphoreTake(matchDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (forward) {
            // Find first match > vNow
            for (const auto& m : allMatches) {
                if (m.actual_time > vNow + 60) { // Buffer 1 min
                    targetTime = m.actual_time;
                    found = true;
                    break;
                }
            }
        } else {
            // Find match just before vNow
            // Iterate backwards
            for (int i = allMatches.size() - 1; i >= 0; i--) {
                if (allMatches[i].actual_time < vNow - 60) {
                    targetTime = allMatches[i].actual_time;
                    found = true;
                    break;
                }
            }
        }
        xSemaphoreGive(matchDataMutex);
    }

    if (found) {
        time_t realNow;
        time(&realNow);
        simState.time_offset = targetTime - realNow;
        simState.time_offset -= 120; // 2 mins before
    }
}


bool isSafeForSponsors() {
    time_t now = get_current_time();

    // Check against schedule
    if (matchDataMutex != NULL && xSemaphoreTake(matchDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (const auto& m : allMatches) {
             if (m.our_alliance != 0) { // It's our match
                 double diff = difftime(m.actual_time, now);
                 if (fabs(diff) < 30 * 60) {
                     xSemaphoreGive(matchDataMutex);
                     return false;
                 }
             }
        }
        xSemaphoreGive(matchDataMutex);
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
    time_t now = get_current_time();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year > 120) {
        if (nextEventDate > 0) {
            double diff = difftime(nextEventDate, now);
            int days = (int)(diff / 86400);

            if (days > 0) {
                tickerQueue.push_back("T-" + std::to_string(days) + " DAYS UNTIL " + nextEventName + " COMP");
            } else if (days == 0) {
                tickerQueue.push_back("IT IS TIME FOR " + nextEventName + " COMP!");
            } else {
                tickerQueue.push_back(MSG_SEASON_START);
            }
        } else {
            tickerQueue.push_back("CHECKING SCHEDULE...");
        }
    } else {
        tickerQueue.push_back(MSG_WAIT_SYNC);
    }
    tickerQueue.push_back(MSG_SUBTITLE);
}

// --- VIEW DRAWING HELPERS ---

void draw_quals_view(time_t now) {
    canvas_dev->setFont(NULL);
    canvas_dev->setTextColor(0x7BEF); // Gray
    canvas_dev->setCursor(138, 5);
    canvas_dev->print("QUALIFICATIONS");
    canvas_dev->drawFastHLine(135, 14, 115, 0x3186);

    // List next 3 matches
    int count = 0;
    if (matchDataMutex != NULL && xSemaphoreTake(matchDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (const auto& m : allMatches) {
            if (m.our_alliance != 0 && m.actual_time > now) {
                int yOff = 25 + (count * 12);

                // Color based on alliance
                uint16_t color = (m.our_alliance == 1) ? 0xF800 : 0x001F; // Red : Blue
                canvas_dev->setTextColor(color);
                canvas_dev->setCursor(138, yOff);

                // "Q42"
                canvas_dev->print("Q"); canvas_dev->print(m.match_number);

                // Time Delta
                double diff = difftime(m.actual_time, now);
                int mins = (int)(diff / 60);

                canvas_dev->setTextColor(0xFFFF);
                canvas_dev->setCursor(170, yOff);
                if (mins > 60) {
                    canvas_dev->print(" >1h");
                } else {
                    canvas_dev->print(" "); canvas_dev->print(mins); canvas_dev->print("m");
                }

                count++;
                if (count >= 3) break;
            }
        }
        xSemaphoreGive(matchDataMutex);
    }

    if (count == 0) {
        canvas_dev->setTextColor(0xFFFF);
        canvas_dev->setCursor(140, 30);
        canvas_dev->print("NO MORE MATCHES");
    }

    // Rank
    canvas_dev->setTextColor(0x7BEF);
    canvas_dev->setCursor(220, 18);
    canvas_dev->print("RANK");
    canvas_dev->setFont(&FreeSansBold12pt7b);
    canvas_dev->setTextColor(0xFFE0);
    canvas_dev->setCursor(215, 45);
    canvas_dev->print(currentRank);
}

void draw_playoffs_view(time_t now) {
    // Header
    canvas_dev->setFont(NULL);
    canvas_dev->setTextColor(0x07E0); // Green
    canvas_dev->setCursor(138, 5);
    canvas_dev->print("PLAYOFFS");

    // Alliance Name
    canvas_dev->setTextColor(0xFFFF);
    canvas_dev->setCursor(200, 5);
    if (!allianceName.empty()) {
        // "Alliance 1" -> "A1"
        if (allianceName.find("Alliance ") == 0) {
            canvas_dev->print("A"); canvas_dev->print(allianceName.substr(9).c_str());
        } else {
            canvas_dev->print(allianceName.c_str());
        }
    }
    canvas_dev->drawFastHLine(135, 14, 115, 0x3186);

    // Data Search
    MatchData lastMatch;
    MatchData nextMatch;
    bool hasLast = false;
    bool hasNext = false;
    int matchesUntil = 0;
    int opponentLastScore = -1;

    if (matchDataMutex != NULL && xSemaphoreTake(matchDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // Find Last Played
        for (int i = allMatches.size() - 1; i >= 0; i--) {
            if (allMatches[i].our_alliance != 0 && allMatches[i].actual_time < now) {
                lastMatch = allMatches[i];
                hasLast = true;
                break;
            }
        }

        // Find Next Scheduled
        for (const auto& m : allMatches) {
            if (m.actual_time > now) {
                if (m.our_alliance != 0) {
                    nextMatch = m;
                    hasNext = true;
                    break;
                }
                matchesUntil++;
            }
        }

        // Logic for Opponent Score if Next Match exists
        if (hasNext) {
            // Determine Opponent Teams
            char (*oppTeams)[8] = (nextMatch.our_alliance == 1) ? nextMatch.blue_teams : nextMatch.red_teams;
            // Find most recent match for these teams
            for (int i = allMatches.size() - 1; i >= 0; i--) {
                if (allMatches[i].match_number == nextMatch.match_number && strcmp(allMatches[i].comp_level, nextMatch.comp_level) == 0) continue; // Skip same match

                bool oppPlayed = false;
                // Check if any opp team was in this match
                for (int t=0; t<3; t++) {
                    const char* ot = oppTeams[t];
                    if (ot[0] == 0) continue;
                    for (int rt=0; rt<3; rt++) if (strcmp(ot, allMatches[i].red_teams[rt]) == 0) oppPlayed = true;
                    for (int bt=0; bt<3; bt++) if (strcmp(ot, allMatches[i].blue_teams[bt]) == 0) oppPlayed = true;
                }

                if (oppPlayed && allMatches[i].red_score >= 0) {
                    // Get their score
                    // We need to know which side they were on in THAT match
                    bool wasRed = false;
                    for (int t=0; t<3; t++) {
                        const char* ot = oppTeams[t];
                        if (ot[0] == 0) continue;
                        for (int rt=0; rt<3; rt++) if (strcmp(ot, allMatches[i].red_teams[rt]) == 0) wasRed = true;
                    }
                    opponentLastScore = wasRed ? allMatches[i].red_score : allMatches[i].blue_score;
                    break;
                }
            }
        }
        xSemaphoreGive(matchDataMutex);
    }

    // Display Last Result
    int y = 20;
    canvas_dev->setFont(NULL);
    if (hasLast) {
        bool won = false;
        if (lastMatch.our_alliance == 1) won = (lastMatch.red_score > lastMatch.blue_score);
        else won = (lastMatch.blue_score > lastMatch.red_score);

        canvas_dev->setTextColor(won ? 0x07E0 : 0xF800); // Green/Red
        canvas_dev->setCursor(138, y);
        canvas_dev->print(won ? "WIN " : "LOSS ");
        canvas_dev->setTextColor(0xFFFF);
        if (lastMatch.our_alliance == 1) {
            canvas_dev->print(lastMatch.red_score); canvas_dev->print("-"); canvas_dev->print(lastMatch.blue_score);
        } else {
            canvas_dev->print(lastMatch.blue_score); canvas_dev->print("-"); canvas_dev->print(lastMatch.red_score);
        }
    } else {
         canvas_dev->setTextColor(0xFFFF);
         canvas_dev->setCursor(138, y);
         canvas_dev->print("NO PREV DATA");
    }

    // Display Next Match
    y += 12;
    if (hasNext) {
        canvas_dev->setTextColor(0xFFE0); // Yellow
        canvas_dev->setCursor(138, y);
        // "SF1-1"
        if (strcmp(nextMatch.comp_level, "sf") == 0) canvas_dev->print("SF");
        else if (strcmp(nextMatch.comp_level, "f") == 0) canvas_dev->print("F");
        else canvas_dev->print(nextMatch.comp_level);

        canvas_dev->print(nextMatch.match_number);
        canvas_dev->print("-"); canvas_dev->print(nextMatch.set_number);

        // Opponent Score
        y += 10;
        canvas_dev->setCursor(138, y);
        canvas_dev->setTextColor(0x7BEF); canvas_dev->print("THEIR LAST: ");
        canvas_dev->setTextColor(0xFFFF);
        if (opponentLastScore >= 0) canvas_dev->print(opponentLastScore);
        else canvas_dev->print("N/A");

        // Countdown
        y += 10;
        canvas_dev->setCursor(138, y);
        canvas_dev->setTextColor(0xF800); // Red alert
        canvas_dev->print("IN "); canvas_dev->print(matchesUntil); canvas_dev->print(" MATCHES");

    } else {
        y += 12;
        canvas_dev->setTextColor(0xFFFF);
        canvas_dev->setCursor(138, y);
        canvas_dev->print("SEASON COMPLETE");
    }
}


void matrix_task(void *pvParameters) {
    refreshTickerQueue();
    uint16_t tigerOrange = matrix->color565(255, 140, 0);
    uint16_t white = 0xFFFF;

    float pulseIdx = 0;
    bool showZoom = true;
    float zoom = 0.5f;
    static uint32_t buttonHoldStart = 0;
    static uint32_t lastUpBtnTime = 0;
    bool isResetting = false;

    static int currentMsgIdx = 0;
    static int nextMsgIdx = 1;
    static float scrollX = 128.0f;
    const int GAP = 60;

    SponsorState sponsorState = SPONSOR_IDLE;
    time_t lastSponsorRunTime = 0;
    float sponsorScrollX = 256;
    unsigned int sponsorListIdx = 0;
    uint32_t sponsorWaitStart = 0;
    float sponsorListY = 64;
    uint32_t outroStartTime = 0;

    time(&lastSponsorRunTime);
    lastSponsorRunTime -= (15 * 60) - 30;

    while(1) {
        canvas_dev->fillScreen(0);
        uint32_t nowMs = esp_timer_get_time() / 1000;
        time_t virtualNow = get_current_time();

        // --- SIMULATION BUTTONS ---
        if (simState.active) {
            if (gpio_get_level(BTN_UP_GPIO) == 0) {
                if (nowMs - lastUpBtnTime > 500) {
                    step_simulation(true);
                    lastUpBtnTime = nowMs;
                }
            }
        }

        // --- SPONSOR CHECK ---
        if (sponsorState == SPONSOR_IDLE && !simState.active) {
            time_t nowSec = virtualNow;
            if (difftime(nowSec, lastSponsorRunTime) >= 15 * 60) {
                if (isSafeForSponsors()) {
                    sponsorState = SPONSOR_INTRO;
                    sponsorScrollX = 256;
                    lastSponsorRunTime = nowSec;
                }
            }
        }

        if (sponsorState != SPONSOR_IDLE) {
            // (Keeping Sponsor Logic same as before)
            if (sponsorState == SPONSOR_INTRO) {
                canvas_dev->setFont(NULL);
                canvas_dev->setTextColor(0xFFFF);
                const char* header = SPONSOR_HEADER_TEXT;
                int len = strlen(header);
                int startX = (256 - (len * 6)) / 2;
                if (startX < 0) startX = 0;
                canvas_dev->setCursor(startX, 5);
                canvas_dev->print(header);

                if (sponsorWaitStart == 0) sponsorWaitStart = nowMs;
                if (nowMs - sponsorWaitStart > 2000) {
                     sponsorState = SPONSOR_SHOW_LIST;
                     sponsorListIdx = 0;
                     sponsorListY = 90;
                     sponsorWaitStart = 0;
                }
            }
            else if (sponsorState == SPONSOR_SHOW_LIST) {
                if (sponsorListIdx < SPONSOR_LIST.size()) {
                    std::string name = SPONSOR_LIST[sponsorListIdx];
                    canvas_dev->setFont(&FreeSansBold12pt7b);
                    canvas_dev->setTextColor(0xFC00);

                    int16_t x1, y1; uint16_t w, h;
                    canvas_dev->getTextBounds(name.c_str(), 0, 0, &x1, &y1, &w, &h);
                    std::vector<std::string> lines;
                    if (w > 250) {
                        size_t splitPos = name.length() / 2;
                        size_t spacePos = name.find(' ', splitPos);
                         if (spacePos == std::string::npos) spacePos = name.find_last_of(' ', splitPos);
                         if (spacePos != std::string::npos) {
                             lines.push_back(name.substr(0, spacePos));
                             lines.push_back(name.substr(spacePos + 1));
                         } else {
                             lines.push_back(name);
                         }
                    } else {
                        lines.push_back(name);
                    }

                    float targetY = 40;

                    if (sponsorWaitStart == 0) {
                        if (sponsorListY > targetY) sponsorListY -= 2.0;
                        else { sponsorListY = targetY; sponsorWaitStart = nowMs; }
                    } else {
                        if (nowMs - sponsorWaitStart > 1000) {
                             sponsorListY -= 2.0;
                             if (sponsorListY < -50) {
                                 sponsorListIdx++;
                                 sponsorListY = 90;
                                 sponsorWaitStart = 0;
                             }
                        }
                    }

                    int currentY = (int)sponsorListY;
                    if (lines.size() > 1) currentY -= (10 * (lines.size()-1));

                    for (const auto& line : lines) {
                        canvas_dev->getTextBounds(line.c_str(), 0, 0, &x1, &y1, &w, &h);
                        int drawX = (256 - w) / 2;
                        canvas_dev->setCursor(drawX, currentY);
                        canvas_dev->print(line.c_str());
                        currentY += 25;
                    }

                    canvas_dev->fillRect(0, 0, 256, 15, 0x0000);
                    canvas_dev->setFont(NULL);
                    canvas_dev->setTextColor(0xFFFF);
                    canvas_dev->setCursor((256 - (strlen(SPONSOR_HEADER_TEXT) * 6)) / 2, 5);
                    canvas_dev->print(SPONSOR_HEADER_TEXT);

                } else {
                    sponsorState = SPONSOR_OUTRO;
                    outroStartTime = nowMs;
                }
            }
            else if (sponsorState == SPONSOR_OUTRO) {
                float progress = (nowMs - outroStartTime) / 1000.0f;
                if (progress > 4.0f) {
                    sponsorState = SPONSOR_IDLE;
                } else {
                    canvas_dev->setFont(&FreeSansBold18pt7b);
                    uint16_t color = (fmod(progress, 0.5f) < 0.25f) ? 0xFFFF : 0x07E0;
                    canvas_dev->setTextColor(color);
                    std::string thanks = "THANK YOU!!";
                    int16_t x1, y1; uint16_t w, h;
                    canvas_dev->getTextBounds(thanks.c_str(), 0, 0, &x1, &y1, &w, &h);
                    canvas_dev->setCursor((256 - w)/2, 45);
                    canvas_dev->print(thanks.c_str());
                }
            }

            pulseIdx += 0.3;
            uint8_t p = 150 + (int)(100 * sin(pulseIdx));
            uint16_t pColor = matrix->color565(0, p, 0);
            canvas_dev->drawRect(0, 0, 256, 64, pColor);
            canvas_dev->drawRect(1, 1, 254, 62, pColor);
        }

        if (sponsorState == SPONSOR_IDLE) {

        bool is_prov = true;
        int prov_check_counter = 0;
        if (prov_check_counter++ > 200) {
            wifi_prov_mgr_is_provisioned(&is_prov);
            prov_check_counter = 0;
        }

        if (gpio_get_level(BTN_DOWN_GPIO) == 0) {
            if (buttonHoldStart == 0) buttonHoldStart = esp_timer_get_time() / 1000;
            uint32_t holdTime = (esp_timer_get_time() / 1000) - buttonHoldStart;

            if (holdTime > 2000) {
                isResetting = true;
                canvas_dev->setFont(NULL);
                canvas_dev->setTextColor(0xF800);
                canvas_dev->setCursor(80, 25);
                canvas_dev->print("HOLD TO RESET:");
                canvas_dev->setCursor(110, 35);
                canvas_dev->print(3 - (holdTime / 1000));
            }
            if (holdTime > 5000) {
                nvs_flash_erase();
                esp_restart();
            }
        } else {
            if (buttonHoldStart != 0) {
                 uint32_t holdTime = (esp_timer_get_time() / 1000) - buttonHoldStart;
                 if (holdTime < 2000 && simState.active) {
                     step_simulation(false);
                 }
            }
            buttonHoldStart = 0;
            isResetting = false;
        }

        // --- B. MAIN DRAWING LOGIC ---
        if (!isResetting) {
            if (showZoom) {
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
                drawTiger(canvas_dev, -5, -3);
                canvas_dev->setFont(&FreeSansBold18pt7b);
                canvas_dev->setTextColor(tigerOrange);
                canvas_dev->setCursor(52, 40);
                canvas_dev->print("5459");

                if (tickerQueue.empty()) refreshTickerQueue();

                canvas_dev->setFont(&FreeSans9pt7b);
                canvas_dev->setTextWrap(false);
                canvas_dev->setTextColor(is_prov ? white : 0xF800);

                std::string msg1 = tickerQueue[currentMsgIdx];
                std::string msg2 = tickerQueue[nextMsgIdx];

                canvas_dev->setCursor((int)scrollX, 60);
                canvas_dev->print(msg1.c_str());
                int16_t x1, y1; uint16_t w1, h1;
                canvas_dev->getTextBounds(msg1.c_str(), 0, 0, &x1, &y1, &w1, &h1);

                canvas_dev->setCursor((int)scrollX + w1 + GAP, 60);
                canvas_dev->print(msg2.c_str());

                scrollX -= 2.0f;
                canvas_dev->fillRect(129, 45, 127, 17, 0);
                if (scrollX < -(w1 + GAP)) {
                    scrollX = 0;
                    currentMsgIdx = nextMsgIdx;
                    nextMsgIdx = (nextMsgIdx + 1) % tickerQueue.size();
                    if (currentMsgIdx == 0) refreshTickerQueue();
                }
                canvas_dev->drawFastVLine(130, 5, 54, 0x3186);

                // --- UI PHASES LOGIC ---
                // Sim Indicator
                if (simState.active) {
                    canvas_dev->setFont(NULL);
                    canvas_dev->setTextColor(0xF800);
                    canvas_dev->setCursor(240, 2);
                    canvas_dev->print("SIM");

                    // Override Phase for Simulation based on Virtual Time
                    bool playoffStarted = false;
                    if (matchDataMutex != NULL && xSemaphoreTake(matchDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        for (const auto& m : allMatches) {
                    if (strcmp(m.comp_level, "qm") != 0 && m.actual_time <= virtualNow) {
                                playoffStarted = true;
                                break;
                            }
                        }
                        xSemaphoreGive(matchDataMutex);
                    }
                    currentPhase = playoffStarted ? PHASE_PLAYOFFS : PHASE_QUALS;
                }

                // Decide View Based on Phase
                if (currentPhase == PHASE_PLAYOFFS) {
                    draw_playoffs_view(virtualNow);
                } else {
                    draw_quals_view(virtualNow);
                }

            }
        }

        // --- C. PERMANENT OVERLAYS ---
        struct tm timeinfo;
        time_t dispTime = virtualNow;
        localtime_r(&dispTime, &timeinfo);

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

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
            if (timeinfo.tm_sec % 2 == 0) canvas_dev->fillCircle(5, 3, 1, 0xF800);
        }

        if (!borderActive && (nowMs - lastBorderStartTime > 120000)) {
            reset_pacman_game(nowMs);
        }

        if (borderActive) {
            run_pacman_cycle(canvas_dev, nowMs);
        } else {
            pulseIdx += 0.1;
            uint8_t p = 150 + (int)(100 * sin(pulseIdx));
            uint16_t pColor = matrix->color565(p, (p * 140 / 255), 0);
            canvas_dev->drawRect(0, 0, 256, 64, pColor);
            canvas_dev->drawRect(1, 1, 254, 62, pColor);
        }
        }

        // --- D. FINAL RENDER ---
        uint16_t *buf = canvas_dev->getBuffer();
        for(int y = 0; y < 64; y++) {
            uint16_t* row_ptr = &buf[y * 256];
            for(int x = 0; x < 256; x++) {
                if (row_ptr[x] != 0) matrix->drawPixel(x, y, row_ptr[x]);
                else matrix->drawPixel(x, y, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
