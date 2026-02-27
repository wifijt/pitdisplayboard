#ifndef GLOBALS_H
#define GLOBALS_H

#include <vector>
#include <string>
#include <stdint.h>
#include <time.h>
#include "freertos/FreeRTOS.h" // Must be before semphr.h
#include "freertos/semphr.h"
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "Adafruit_GFX.h"

// --- Constants & Colors ---
#define GHOST_BLINKY 0xF800 // Red
#define GHOST_PINKY  0xFC18 // Pink
#define GHOST_INKY   0x07FF // Cyan
#define GHOST_CLYDE  0xFB20 // Orange

// --- Data Structures ---
enum MatchPhase {
    PHASE_QUALS,
    PHASE_PLAYOFFS
};

struct MatchData {
    char key[16];
    char comp_level[4];
    int match_number;
    int set_number;
    time_t actual_time;
    char red_teams[3][8];
    char blue_teams[3][8];
    int red_score;
    int blue_score;
    int our_alliance;
};

struct SimState {
    bool active;
    time_t time_offset;
    int match_index_override;
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

// --- Shared Variables ---

// Display Objects
extern MatrixPanel_I2S_DMA *matrix;
extern GFXcanvas16 *canvas_dev;

// Data Queues/History
extern std::vector<std::string> tickerQueue;

// Static Array for Matches (No Heap OOM)
#define MAX_MATCHES 200
extern MatchData allMatches[MAX_MATCHES];
extern int matchCount;

extern SemaphoreHandle_t matchDataMutex;
extern MatchPhase currentPhase;
extern SimState simState;

// Pac-Man Game State
enum GhostState {
    GHOST_ALIVE,
    GHOST_EYES,
    GHOST_DEAD
};

extern bool borderActive;
extern uint32_t lastBorderStartTime;
extern float pacPos;
extern float ghostPos[4];
extern float worldSpeed;
extern bool powerMode;
extern uint32_t powerStartTime;
extern GhostState ghostState[4];
extern uint32_t winStartTime;

// Event Schedule
extern std::string nextEventName;
extern time_t nextEventDate;
extern std::string teamKey;
extern std::string eventKey;
extern int currentRank;
extern std::string allianceName;

// Helper Function for Time
time_t get_current_time();

#endif // GLOBALS_H
