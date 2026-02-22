#ifndef CONFIG_H
#define CONFIG_H

// The Blue Alliance API
#define TBA_KEY "YOUR_TBA_API_KEY_HERE"
#define TBA_URL_BASE "https://www.thebluealliance.com/api/v3"

// Simulation Configuration
#define SIMULATION_MODE false
#define SIM_TEAM "frc88"
#define SIM_EVENT "2026week0"

// Real Configuration
#define REAL_TEAM "frc5459"
#define REAL_EVENT "2026marea" // North Shore

// WiFi Credentials (Static Fallback)
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

// Provisioning
#define PROV_SERVICE_NAME "5459_TIGER_BOARD"
#define PROV_POP "5459"

// Button GPIOs
#define BTN_UP_GPIO   GPIO_NUM_6
#define BTN_DOWN_GPIO GPIO_NUM_7

#endif // CONFIG_H
