#include "tba_network.h"
#include "config.h"
#include "globals.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_heap_caps.h" // Required for PSRAM
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

#ifndef TBA_KEY
#define TBA_KEY "YOUR_TBA_API_KEY_HERE"
#endif

// --- Buffer Strategy ---
#define PSRAM_BUF_SIZE (64 * 1024)
#define INTERNAL_BUF_SIZE (12 * 1024) // Reduced to 12KB to save heap for SSL

struct ResponseData {
    char* data;
    int len;
    int capacity;
};

// --- Custom Allocator for cJSON/Buffer ---
static void* psram_malloc(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) return ptr;
    return malloc(size);
}

static void psram_free(void* ptr) {
    free(ptr);
}

// --- HTTP Event Handler ---
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        ResponseData* buf = (ResponseData*)evt->user_data;
        if (buf && buf->data) {
            // Check for Overflow
            if (buf->len + evt->data_len + 1 > buf->capacity) {
                printf("TBA: JSON Buffer Overflow! Needs > %d bytes\n", buf->capacity);
                return ESP_FAIL;
            }

            // Append Data
            memcpy(buf->data + buf->len, evt->data, evt->data_len);
            buf->len += evt->data_len;
            buf->data[buf->len] = 0; // Null terminate
        }
    }
    return ESP_OK;
}

// --- Helper: Fetch URL ---
static int fetch_url(const char* url, ResponseData* outBuf) {
    // Diagnostic Heap Check
    printf("TBA: Pre-Fetch Heap: %d free, %d largest block\n",
           (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    esp_http_client_config_t config = {};
    config.url = url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 40000;
    config.event_handler = _http_event_handler;
    config.user_data = outBuf;
    config.keep_alive_enable = false; // Disable keep-alive to save memory?

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        printf("TBA: Failed to init HTTP client (OOM?)\n");
        return -1;
    }
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
    if (json == NULL || strlen(json) < 2) return;

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        printf("TBA: JSON Parse Failed for Matches\n");
        return;
    }

    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return;
    }

    int count = cJSON_GetArraySize(root);
    printf("TBA: Found %d matches\n", count);

    if (matchDataMutex != NULL) {
        if (xSemaphoreTake(matchDataMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
            matchCount = 0;

            for (int i = 0; i < count && i < MAX_MATCHES; i++) {
                cJSON *m = cJSON_GetArrayItem(root, i);
                MatchData* md = &allMatches[matchCount];
                memset(md, 0, sizeof(MatchData));

                cJSON *key = cJSON_GetObjectItem(m, "key");
                if (key && key->valuestring) strncpy(md->key, key->valuestring, sizeof(md->key)-1);

                cJSON *comp_level = cJSON_GetObjectItem(m, "comp_level");
                if (comp_level && comp_level->valuestring) strncpy(md->comp_level, comp_level->valuestring, sizeof(md->comp_level)-1);

                cJSON *match_number = cJSON_GetObjectItem(m, "match_number");
                md->match_number = match_number ? match_number->valueint : 0;

                cJSON *set_number = cJSON_GetObjectItem(m, "set_number");
                md->set_number = set_number ? set_number->valueint : 0;

                cJSON *actual_time = cJSON_GetObjectItem(m, "actual_time");
                cJSON *predicted_time = cJSON_GetObjectItem(m, "predicted_time");
                cJSON *time_item = cJSON_GetObjectItem(m, "time");

                if (actual_time && actual_time->valueint > 0) md->actual_time = actual_time->valueint;
                else if (predicted_time && predicted_time->valueint > 0) md->actual_time = predicted_time->valueint;
                else if (time_item && time_item->valueint > 0) md->actual_time = time_item->valueint;
                else md->actual_time = 0;

                md->our_alliance = 0;
                cJSON *alliances = cJSON_GetObjectItem(m, "alliances");
                if (alliances) {
                    cJSON *red = cJSON_GetObjectItem(alliances, "red");
                    if (red) {
                        cJSON *score = cJSON_GetObjectItem(red, "score");
                        md->red_score = (score && score->valueint >= 0) ? score->valueint : -1;
                        cJSON *teams = cJSON_GetObjectItem(red, "team_keys");
                        if (teams && cJSON_IsArray(teams)) {
                            int tCount = cJSON_GetArraySize(teams);
                            for(int t=0; t<tCount && t<3; t++) {
                                cJSON* tItem = cJSON_GetArrayItem(teams, t);
                                if (tItem && tItem->valuestring) {
                                    strncpy(md->red_teams[t], tItem->valuestring, 7);
                                    if (strcmp(tItem->valuestring, teamKey.c_str()) == 0) md->our_alliance = 1;
                                }
                            }
                        }
                    }
                    cJSON *blue = cJSON_GetObjectItem(alliances, "blue");
                    if (blue) {
                        cJSON *score = cJSON_GetObjectItem(blue, "score");
                        md->blue_score = (score && score->valueint >= 0) ? score->valueint : -1;
                        cJSON *teams = cJSON_GetObjectItem(blue, "team_keys");
                        if (teams && cJSON_IsArray(teams)) {
                            int tCount = cJSON_GetArraySize(teams);
                            for(int t=0; t<tCount && t<3; t++) {
                                cJSON* tItem = cJSON_GetArrayItem(teams, t);
                                if (tItem && tItem->valuestring) {
                                     strncpy(md->blue_teams[t], tItem->valuestring, 7);
                                     if (strcmp(tItem->valuestring, teamKey.c_str()) == 0) md->our_alliance = 2;
                                }
                            }
                        }
                    }
                }
                matchCount++;
            }

            std::sort(allMatches, allMatches + matchCount, [](const MatchData& a, const MatchData& b) {
                return a.actual_time < b.actual_time;
            });

            if (simState.active && simState.time_offset == 0 && matchCount > 0) {
                time_t firstMatch = allMatches[0].actual_time;
                time_t now;
                time(&now);
                simState.time_offset = (firstMatch - 300) - now;
                printf("SIMULATION: Auto-jump to start. Offset: %ld\n", (long)simState.time_offset);
            }

            printf("TBA: Stored %d matches.\n", matchCount);
            xSemaphoreGive(matchDataMutex);
        } else {
            printf("TBA: Could not take mutex to update matches.\n");
        }
    }

    cJSON_Delete(root);
}

// --- Parse Team Status ---
static void parse_team_status(const char* json) {
    if (json == NULL || strlen(json) < 2) return;
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) return;

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

    bool inPlayoffs = false;
    cJSON *playoff = cJSON_GetObjectItem(root, "playoff");
    if (playoff && !cJSON_IsNull(playoff)) {
        cJSON *level = cJSON_GetObjectItem(playoff, "level");
        if (level && strcmp(level->valuestring, "qm") != 0) {
             inPlayoffs = true;
        }
    }

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

    // 1. Check PSRAM
    size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    printf("TBA TASK: PSRAM Free: %d bytes\n", (int)freePsram);

    cJSON_Hooks hooks = { .malloc_fn = psram_malloc, .free_fn = psram_free };
    cJSON_InitHooks(&hooks);

    // 2. Allocate Buffer
    ResponseData buf;
    buf.data = (char*)heap_caps_malloc(PSRAM_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (buf.data) {
        buf.capacity = PSRAM_BUF_SIZE;
        printf("TBA TASK: Using PSRAM Buffer (%d bytes)\n", buf.capacity);
    } else {
        printf("TBA TASK: PSRAM Alloc Failed! Fallback to Internal RAM (%d bytes)\n", INTERNAL_BUF_SIZE);
        buf.capacity = INTERNAL_BUF_SIZE;
        buf.data = (char*)malloc(buf.capacity);
    }
    buf.len = 0;

    if (!buf.data) {
        printf("TBA TASK: CRITICAL - Failed to allocate buffer!\n");
        vTaskDelete(NULL);
    }

    while (1) {
        esp_netif_ip_info_t ip_info;
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {

            // 1. Fetch ALL Matches
            std::string matchUrl = std::string(TBA_URL_BASE) + "/event/" + eventKey + "/matches/simple";
            buf.len = 0;
            if(buf.data) buf.data[0] = 0;

            if (fetch_url(matchUrl.c_str(), &buf) == 0) {
                if (buf.data) parse_all_matches(buf.data);
            }

            vTaskDelay(pdMS_TO_TICKS(2000));

            // 2. Fetch Team Status
            std::string statusUrl = std::string(TBA_URL_BASE) + "/team/" + teamKey + "/event/" + eventKey + "/status";
            buf.len = 0;
            if(buf.data) buf.data[0] = 0;

            if (fetch_url(statusUrl.c_str(), &buf) == 0) {
                if (buf.data) parse_team_status(buf.data);
            }

            vTaskDelay(pdMS_TO_TICKS(120000));

        } else {
            printf("TBA: Waiting for WiFi...\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    if (buf.data) free(buf.data);
    vTaskDelete(NULL);
}
