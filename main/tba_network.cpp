#include "tba_network.h"
#include "config.h"
#include "globals.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <string>

// --- Fallback Configuration Check ---
#ifndef TBA_URL_BASE
#define TBA_URL_BASE "https://www.thebluealliance.com/api/v3"
#endif

// --- Fallback for old TBA_KEY ---
#ifndef TBA_KEY
#define TBA_KEY "YOUR_TBA_API_KEY_HERE"
#endif

struct ResponseData {
    char* data;
    int len;
    int capacity;
};

// --- HTTP Event Handler ---
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        ResponseData* buf = (ResponseData*)evt->user_data;
        if (buf) {
            if (buf->data == NULL) return ESP_FAIL;

            if (buf->len + evt->data_len + 1 > buf->capacity) {
                int new_cap = buf->capacity + evt->data_len + 1024;
                char* new_data = (char*)realloc(buf->data, new_cap);
                if (new_data) {
                    buf->data = new_data;
                    buf->capacity = new_cap;
                } else {
                    printf("TBA: OOM Realloc Failed! (Cap: %d)\n", new_cap);
                    return ESP_FAIL; // OOM
                }
            }
            memcpy(buf->data + buf->len, evt->data, evt->data_len);
            buf->len += evt->data_len;
            buf->data[buf->len] = 0; // Null terminate
        }
    }
    return ESP_OK;
}

// --- Helper: Fetch URL ---
static int fetch_url(const char* url, ResponseData* outBuf) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 40000;
    config.event_handler = _http_event_handler;
    config.user_data = outBuf;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "X-TBA-Auth-Key", TBA_KEY);

    printf("TBA: Fetching %s\n", url);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200) {
        return 0;
    } else {
        printf("TBA: Error fetching %s (Status: %d, Err: %d)\n", url, status, err);
        return -1;
    }
}

// --- Parse All Matches ---
static void parse_all_matches(const char* json) {
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        printf("TBA: JSON Parse Failed for Matches\n");
        return;
    }

    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return;
    }

    std::vector<MatchData> newMatches;
    int count = cJSON_GetArraySize(root);
    printf("TBA: Found %d matches\n", count);

    // Reserve to prevent reallocations
    newMatches.reserve(count);

    for (int i = 0; i < count; i++) {
        cJSON *m = cJSON_GetArrayItem(root, i);
        MatchData md;
        memset(&md, 0, sizeof(MatchData)); // Clear struct

        cJSON *key = cJSON_GetObjectItem(m, "key");
        if (key && key->valuestring) strncpy(md.key, key->valuestring, sizeof(md.key)-1);

        cJSON *comp_level = cJSON_GetObjectItem(m, "comp_level");
        if (comp_level && comp_level->valuestring) strncpy(md.comp_level, comp_level->valuestring, sizeof(md.comp_level)-1);

        cJSON *match_number = cJSON_GetObjectItem(m, "match_number");
        md.match_number = match_number ? match_number->valueint : 0;

        cJSON *set_number = cJSON_GetObjectItem(m, "set_number");
        md.set_number = set_number ? set_number->valueint : 0;

        // Time logic: actual_time > predicted_time > time
        cJSON *actual_time = cJSON_GetObjectItem(m, "actual_time");
        cJSON *predicted_time = cJSON_GetObjectItem(m, "predicted_time");
        cJSON *time_item = cJSON_GetObjectItem(m, "time");

        if (actual_time && actual_time->valueint > 0) md.actual_time = actual_time->valueint;
        else if (predicted_time && predicted_time->valueint > 0) md.actual_time = predicted_time->valueint;
        else if (time_item && time_item->valueint > 0) md.actual_time = time_item->valueint;
        else md.actual_time = 0;

        // Alliances
        md.our_alliance = 0;
        cJSON *alliances = cJSON_GetObjectItem(m, "alliances");
        if (alliances) {
            // RED
            cJSON *red = cJSON_GetObjectItem(alliances, "red");
            if (red) {
                cJSON *score = cJSON_GetObjectItem(red, "score");
                md.red_score = (score && score->valueint >= 0) ? score->valueint : -1;

                cJSON *teams = cJSON_GetObjectItem(red, "team_keys");
                if (teams && cJSON_IsArray(teams)) {
                    int tCount = cJSON_GetArraySize(teams);
                    for(int t=0; t<tCount && t<3; t++) {
                        cJSON* tItem = cJSON_GetArrayItem(teams, t);
                        if (tItem && tItem->valuestring) {
                            strncpy(md.red_teams[t], tItem->valuestring, 7); // Safe limit
                            if (strcmp(tItem->valuestring, teamKey.c_str()) == 0) md.our_alliance = 1; // Red
                        }
                    }
                }
            }

            // BLUE
            cJSON *blue = cJSON_GetObjectItem(alliances, "blue");
            if (blue) {
                cJSON *score = cJSON_GetObjectItem(blue, "score");
                md.blue_score = (score && score->valueint >= 0) ? score->valueint : -1;

                cJSON *teams = cJSON_GetObjectItem(blue, "team_keys");
                if (teams && cJSON_IsArray(teams)) {
                    int tCount = cJSON_GetArraySize(teams);
                    for(int t=0; t<tCount && t<3; t++) {
                        cJSON* tItem = cJSON_GetArrayItem(teams, t);
                        if (tItem && tItem->valuestring) {
                             strncpy(md.blue_teams[t], tItem->valuestring, 7); // Safe limit
                             if (strcmp(tItem->valuestring, teamKey.c_str()) == 0) md.our_alliance = 2; // Blue
                        }
                    }
                }
            }
        }

        newMatches.push_back(md);
    }

    cJSON_Delete(root);

    // Sort by Time
    std::sort(newMatches.begin(), newMatches.end(), [](const MatchData& a, const MatchData& b) {
        return a.actual_time < b.actual_time;
    });

    // Update Global with Mutex
    if (matchDataMutex != NULL) {
        if (xSemaphoreTake(matchDataMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            allMatches = newMatches;
            xSemaphoreGive(matchDataMutex);
            printf("TBA: Updated allMatches with %d entries\n", allMatches.size());
        }
    }
}

// --- Parse Team Status ---
static void parse_team_status(const char* json) {
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) return;

    // Rank
    cJSON *qual = cJSON_GetObjectItem(root, "qual");
    if (qual) {
        cJSON *ranking = cJSON_GetObjectItem(qual, "ranking");
        if (ranking) {
            cJSON *rank = cJSON_GetObjectItem(ranking, "rank");
            if (rank) {
                currentRank = rank->valueint;
            }
        }
    }

    // Playoff Status / Alliance
    bool inPlayoffs = false;
    cJSON *playoff = cJSON_GetObjectItem(root, "playoff");
    if (playoff && !cJSON_IsNull(playoff)) {
        // Check if we are eliminated or playing?
        cJSON *level = cJSON_GetObjectItem(playoff, "level");
        if (level && strcmp(level->valuestring, "qm") != 0) {
             inPlayoffs = true;
        }
    }

    // Alliance Name
    cJSON *alliance = cJSON_GetObjectItem(root, "alliance");
    if (alliance && !cJSON_IsNull(alliance)) {
        cJSON *name = cJSON_GetObjectItem(alliance, "name");
        if (name) {
            allianceName = std::string(name->valuestring);
            inPlayoffs = true;
        }
    } else {
        allianceName = "";
    }

    // Update Phase
    if (matchDataMutex != NULL) {
        if (xSemaphoreTake(matchDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            currentPhase = inPlayoffs ? PHASE_PLAYOFFS : PHASE_QUALS;
            xSemaphoreGive(matchDataMutex);
        }
    }

    cJSON_Delete(root);
}

// --- Main Task ---
void tba_api_task(void *pvParameters) {
    printf("TBA TASK: Started on Core %d\n", xPortGetCoreID());

    // START SMALL: 4KB instead of 64KB to avoid immediate OOM
    const int INIT_BUF_SIZE = 4096;
    ResponseData buf = { (char*)malloc(INIT_BUF_SIZE), 0, INIT_BUF_SIZE };

    if (!buf.data) {
        printf("TBA TASK: CRITICAL - Failed to allocate initial buffer!\n");
        // We can't do much without memory. Delay and retry loop?
        // Or just let it crash safely later.
        while(1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }

    while (1) {
        // Check WiFi
        esp_netif_ip_info_t ip_info;
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {

            // 1. Fetch ALL Matches
            std::string matchUrl = std::string(TBA_URL_BASE) + "/event/" + eventKey + "/matches/simple";
            buf.len = 0; // Reset length, reuse buffer
            // Ensure first byte is null terminated just in case
            if(buf.data) buf.data[0] = 0;

            if (fetch_url(matchUrl.c_str(), &buf) == 0) {
                if (buf.data) parse_all_matches(buf.data);
            }

            vTaskDelay(pdMS_TO_TICKS(2000));

            // 2. Fetch Team Status (Rank & Alliance)
            std::string statusUrl = std::string(TBA_URL_BASE) + "/team/" + teamKey + "/event/" + eventKey + "/status";
            buf.len = 0;
            if(buf.data) buf.data[0] = 0;

            if (fetch_url(statusUrl.c_str(), &buf) == 0) {
                if (buf.data) parse_team_status(buf.data);
            }

            // Sleep for 2 minutes (API rules say be nice)
            vTaskDelay(pdMS_TO_TICKS(120000));

        } else {
            printf("TBA: Waiting for WiFi...\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    if (buf.data) free(buf.data);
    vTaskDelete(NULL);
}
