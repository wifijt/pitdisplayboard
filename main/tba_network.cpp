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
