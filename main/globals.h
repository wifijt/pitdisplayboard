#ifndef GLOBALS_H
#define GLOBALS_H

#include <vector>
#include <string>
#include <stdint.h>
#include <time.h>
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "Adafruit_GFX.h"

// --- Constants & Colors ---
#define GHOST_BLINKY 0xF800 // Red
#define GHOST_PINKY  0xFC18 // Pink
#define GHOST_INKY   0x07FF // Cyan
#define GHOST_CLYDE  0xFB20 // Orange

// --- Data Structures ---
struct MatchEntry {
    char type;    // 'Q' or 'P'
    int number;
    uint16_t color; // 0xF800 (Red) or 0x001F (Blue)
    time_t estTime; // Estimated match time (Unix timestamp)
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

struct LastMatchData {
    int matchNum;
    int redScore;
    int blueScore;
    bool weWereRed;
    int rpEarned;
};

// --- Shared Variables ---

// Display Objects
extern MatrixPanel_I2S_DMA *matrix;
extern GFXcanvas16 *canvas_dev;

// Data Queues/History
extern std::vector<std::string> tickerQueue;
extern GameScore matchHistory[12];
extern int matchesCompleted;
extern MatchEntry schedule[3];
extern int currentlyPlaying;
extern LastMatchData lastMatch;

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

#endif // GLOBALS_H
