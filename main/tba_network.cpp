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
            config.timeout_ms = 10000;

            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_set_header(client, "X-TBA-Auth-Key", TBA_KEY);

            // 1. Fetch Match Data
            esp_err_t err = esp_http_client_perform(client);
            if (err == ESP_OK && esp_http_client_get_status_code(client) == 200) {
                int len = esp_http_client_get_content_length(client);
                char *buffer = (char*)malloc(len + 1);
                if (buffer) {
                    esp_http_client_read_response(client, buffer, len);
                    buffer[len] = '\0';
                    parse_tba_json(buffer);
                    free(buffer);
                }
            } else {
                printf("TBA TASK: Match Fetch failed\n");
            }
            // Cleanup handle to re-use or reset (esp_http_client_cleanup is full destroy)
            // Ideally we re-init or set URL. Re-init is safer given the simple config struct.
            esp_http_client_cleanup(client);

            // 2. Fetch Event Data
            config.url = "https://www.thebluealliance.com/api/v3/team/frc5459/events/2026/simple";
            client = esp_http_client_init(&config);
            esp_http_client_set_header(client, "X-TBA-Auth-Key", TBA_KEY);

            err = esp_http_client_perform(client);
            int status = esp_http_client_get_status_code(client);
            printf("TBA TASK: Event Fetch Status = %d\n", status);

            if (err == ESP_OK && status == 200) {
                int len = esp_http_client_get_content_length(client);
                printf("TBA TASK: Content Length = %d\n", len);

                if (len > 0) {
                    char *buffer = (char*)malloc(len + 1);
                    if (buffer) {
                        int read_len = esp_http_client_read_response(client, buffer, len);
                        if (read_len > 0) {
                            buffer[read_len] = '\0';
                            parse_tba_events(buffer);
                        } else {
                            printf("TBA TASK: Read response failed or empty\n");
                        }
                        free(buffer);
                    } else {
                        printf("TBA TASK: Malloc failed\n");
                    }
                } else {
                     // Chunked encoding or no content
                     printf("TBA TASK: Zero/Unknown Content Length (Chunked?)\n");
                     // Fallback: Read in chunks (simplified for now to fixed buffer)
                     char *buffer = (char*)malloc(4096);
                     if (buffer) {
                         int read_len = esp_http_client_read_response(client, buffer, 4095);
                         if (read_len > 0) {
                             buffer[read_len] = '\0';
                             printf("TBA TASK: Read %d bytes via fallback\n", read_len);
                             parse_tba_events(buffer);
                         }
                         free(buffer);
                     }
                }
            } else {
                printf("TBA TASK: Event Fetch failed or Status %d\n", status);
            }
            esp_http_client_cleanup(client);

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
