#ifndef CONFIG_H
#define CONFIG_H

// The Blue Alliance API
#define TBA_KEY "YOUR_TBA_API_KEY_HERE"
// Note: Code was using http to avoid SSL issues
#define TBA_URL "http://www.thebluealliance.com/api/v3/team/frc5459/event/2025mabos/matches/simple"

// WiFi Credentials (Static Fallback)
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

// Provisioning
#define PROV_SERVICE_NAME "5459_TIGER_BOARD"
#define PROV_POP "5459"

#endif // CONFIG_H
