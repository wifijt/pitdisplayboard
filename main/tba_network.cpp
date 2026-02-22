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

struct ResponseData {
    char* data;
    int len;
    int capacity;
};

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        ResponseData* buf = (ResponseData*)evt->user_data;
        if (buf) {
            if (buf->len + evt->data_len + 1 > buf->capacity) {
                int new_cap = buf->capacity + evt->data_len + 1024;
                char* new_data = (char*)realloc(buf->data, new_cap);
                if (new_data) {
                    buf->data = new_data;
                    buf->capacity = new_cap;
                } else {
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

static time_t parse_date(const char* date_str) {
    struct tm tm = {0};
    if (sscanf(date_str, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
        tm.tm_isdst = -1;
        return mktime(&tm);
    }
    return 0;
}

static void parse_tba_events(const char *json_string) {
    printf("TBA DEBUG: Parsing Events JSON...\n");
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        printf("TBA DEBUG: JSON Parse Failed\n");
        return;
    }

    int event_count = cJSON_GetArraySize(root);
    printf("TBA DEBUG: Found %d events\n", event_count);

    time_t now;
    time(&now);
    time_t min_diff = -1;

    for (int i = 0; i < event_count; i++) {
        cJSON *evt = cJSON_GetArrayItem(root, i);
        cJSON *date_item = cJSON_GetObjectItem(evt, "start_date");
        cJSON *city_item = cJSON_GetObjectItem(evt, "city");

        if (date_item && city_item) {
            time_t evt_time = parse_date(date_item->valuestring);
            printf("TBA DEBUG: Event %d Date: %s Parsed: %ld Now: %ld\n", i, date_item->valuestring, (long)evt_time, (long)now);

            if (evt_time > now) {
                double diff = difftime(evt_time, now);
                if (min_diff == -1 || diff < min_diff) {
                    min_diff = (time_t)diff;
                    nextEventDate = evt_time;
                    nextEventName = std::string(city_item->valuestring); // Use City name
                    printf("TBA DEBUG: Selected Next Event: %s\n", nextEventName.c_str());
                }
            }
        }
    }
    cJSON_Delete(root);
}

static void parse_tba_json(const char *json_string) {
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) return;

    int match_count = cJSON_GetArraySize(root);
    matchesCompleted = 0; // Reset history to fill with fresh API data

    for (int i = 0; i < match_count && i < 12; i++) {
        cJSON *match = cJSON_GetArrayItem(root, i);
        cJSON *alliances = cJSON_GetObjectItem(match, "alliances");
        if (!alliances) continue;

        cJSON *red = cJSON_GetObjectItem(alliances, "red");
        // cJSON *blue = cJSON_GetObjectItem(alliances, "blue"); // Unused for now

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
            config.url = TBA_URL;
            config.crt_bundle_attach = esp_crt_bundle_attach;
            config.timeout_ms = 40000;

            // 1. Fetch Match Data
            ResponseData matchBuf = { (char*)malloc(1024), 0, 1024 };
            if (matchBuf.data) matchBuf.data[0] = 0;

            config.event_handler = _http_event_handler;
            config.user_data = &matchBuf;

            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_set_header(client, "X-TBA-Auth-Key", TBA_KEY);

            esp_err_t err = esp_http_client_perform(client);
            if (err == ESP_OK && esp_http_client_get_status_code(client) == 200) {
                parse_tba_json(matchBuf.data);
            } else {
                printf("TBA TASK: Match Fetch failed (Status %d)\n", esp_http_client_get_status_code(client));
            }
            esp_http_client_cleanup(client);
            if (matchBuf.data) free(matchBuf.data);

            // Brief pause
            vTaskDelay(pdMS_TO_TICKS(2000));

            // 2. Fetch Event Data
            ResponseData evtBuf = { (char*)malloc(4096), 0, 4096 };
            if (evtBuf.data) evtBuf.data[0] = 0;

            config.url = "https://www.thebluealliance.com/api/v3/team/frc5459/events/2026/simple";
            config.event_handler = _http_event_handler;
            config.user_data = &evtBuf;

            client = esp_http_client_init(&config);
            esp_http_client_set_header(client, "X-TBA-Auth-Key", TBA_KEY);

            err = esp_http_client_perform(client);
            int status = esp_http_client_get_status_code(client);
            printf("TBA TASK: Event Fetch Status = %d\n", status);

            if (err == ESP_OK && status == 200) {
                printf("TBA TASK: Received %d bytes\n", evtBuf.len);
                parse_tba_events(evtBuf.data);
            } else {
                printf("TBA TASK: Event Fetch failed\n");
            }
            esp_http_client_cleanup(client);
            if (evtBuf.data) free(evtBuf.data);

            // Wait 5 minutes
            vTaskDelay(pdMS_TO_TICKS(300000));
        } else {
            // Not connected yet, wait 2 seconds and check again
            printf("TBA TASK: Waiting for WiFi IP...\n");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue; // Skip the rest and loop back to check IP again
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // Default fallback delay
    }
}
