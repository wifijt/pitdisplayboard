#include "pacman_engine.h"
#include "globals.h"
#include "pacman_sprites.h"
#include <math.h>
#include <cmath>
#include <cstdlib> // for abs

// Helper functions (formerly in main.cpp)

static void getPos(float p, int &x, int &y) {
    const int perimeter = 612;
    // Ensure p is always positive and within bounds
    float safeP = fmod(p, (float)perimeter);
    if (safeP < 0) safeP += perimeter;

    if (safeP < 249) { x = 3 + safeP; y = 3; }                // Top
    else if (safeP < 306) { x = 252; y = 3 + (safeP - 249); } // Right
    else if (safeP < 555) { x = 252 - (safeP - 306); y = 60; }// Bottom
    else { x = 3; y = 60 - (safeP - 555); }                   // Left
}

static void drawPac(GFXcanvas16 *canvas, int x, int y, float p, float currentSpeed) {
    int dir;
    bool movingForward = (currentSpeed > 0);

    // Explicitly define the rails to prevent "lap-over" errors
    if (p >= 0.0f && p < 249.0f)      dir = movingForward ? 0 : 2; // Top
    else if (p >= 249.0f && p < 306.0f) dir = movingForward ? 1 : 3; // Right
    else if (p >= 306.0f && p < 555.0f) dir = movingForward ? 2 : 0; // Bottom
    else if (p >= 555.0f && p < 612.0f) dir = movingForward ? 3 : 1; // Left
    else dir = movingForward ? 0 : 2; // Fallback to Top/Right

    // Toggle mouth
    bool open = ((int)(p / 5) % 2 == 0);
    const uint8_t (*sprite)[7] = open ? pacman_open : pacman_closed;

    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 7; j++) {
            int r_i, r_j;

            // MAPPING
            if (dir == 0) {      // RIGHT
                r_i = i; r_j = j;
            }
            else if (dir == 1) { // DOWN
                r_i = 6 - j; r_j = i;
            }
            else if (dir == 2) { // LEFT
                r_i = i; r_j = 6 - j;
            }
            else {               // UP
                r_i = j; r_j = 6 - i;
            }

            if (sprite[r_i][r_j] == 1) {
                canvas->drawPixel(x + j, y + i, 0xFFE0);
            }
        }
    }
}

static void drawGhost(GFXcanvas16 *canvas, int x, int y, uint16_t color, bool scared, float p, float speed) {
    bool movingForward = (speed > 0);
    // Determine direction same as Pac-Man
    int dir;
    if (p >= 0.0f && p < 249.0f)      dir = movingForward ? 0 : 2;
    else if (p >= 249.0f && p < 306.0f) dir = movingForward ? 1 : 3;
    else if (p >= 306.0f && p < 555.0f) dir = movingForward ? 2 : 0;
    else                               dir = movingForward ? 3 : 1;

    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 7; j++) {
            uint8_t pixel = ghost_shape[i][j];
            if (pixel == 1) canvas->drawPixel(x+j, y+i, scared ? 0x001F : color);
            else if (pixel == 2) canvas->drawPixel(x+j, y+i, 0xFFFF); // Whites
            else if (pixel == 3) {
                // Shift pupils based on direction
                int ox = 0, oy = 0;
                if (dir == 0) ox = 1;      // Look Right
                else if (dir == 1) oy = 1; // Look Down
                else if (dir == 2) ox = -1;// Look Left
                else oy = -1;              // Look Up
                canvas->drawPixel(x+j+ox, y+i+oy, scared ? 0xFFFF : 0x001F);
            }
        }
    }
}

static void update_pacman_border(GFXcanvas16 *canvas, float &pPos, float gPosArr[4], bool &pMode, float speed, bool eaten[4]) {
    uint16_t ghostCols[4] = {GHOST_BLINKY, GHOST_PINKY, GHOST_INKY, GHOST_CLYDE};
    // Note: We use 616.0f directly in fmod below, so perimeter variable isn't strictly needed here
    bool movingForward = (speed > 0);

    // Update Pac-Man
    pPos = fmod(pPos + speed, 616.0f);
    if (pPos < 0) pPos += 616.0f;

    int pax, pay; getPos(pPos, pax, pay);
    drawPac(canvas, pax - 3, pay - 3, pPos, speed);

    // Update Ghosts
    for (int i = 0; i < 4; i++) {
        if (eaten[i]) continue; // Skip if Pac-Man ate them!

        float gSpeed = pMode ? 0.8f : 1.6f;
        gPosArr[i] = fmod(gPosArr[i] + (movingForward ? gSpeed : -gSpeed), 616.0f);
        if (gPosArr[i] < 0) gPosArr[i] += 616.0f;

        int gx, gy; getPos(gPosArr[i], gx, gy);
        drawGhost(canvas, gx - 3, gy - 3, ghostCols[i], pMode, gPosArr[i], speed);
    }
}

void reset_pacman_game(uint32_t nowMs) {
    borderActive = true;
    lastBorderStartTime = nowMs;
    pacPos = 0; // Start at Top-Left
    for(int i=0; i<4; i++) {
        ghostPos[i] = 100 + (i * 20); // Reset ghosts
        ghostEaten[i] = false;
    }
}

void run_pacman_cycle(GFXcanvas16 *canvas, uint32_t nowMs) {
    // 1. COLLISION DETECTION
    for (int i = 0; i < 4; i++) {
        if (ghostEaten[i]) continue;

        // Check distance between Pac-Man and Ghost
        float dist = std::abs(pacPos - ghostPos[i]);
        // Handle the "wrap-around" distance at the 0/616 point
        if (dist > 308) dist = 616 - dist;

        if (dist < 8) { // Collision!
            if (powerMode) {
                ghostEaten[i] = true; // Pac-Man eats ghost
            } else {
                borderActive = false; // Ghost eats Pac-Man -> Game Over
                lastBorderStartTime = nowMs; // RESET TIMER ON DEATH
                // (Optional) Add a small "death" delay or effect here
            }
        }
    }
    // 1.5 Check if all ghosts are gone
    bool allEaten = true;
    for (int i = 0; i < 4; i++) {
        if (!ghostEaten[i]) {
            allEaten = false;
            break;
        }
    }

    if (allEaten) {
        borderActive = false; // End game and return to pulsing border
        // Optional: Reset powerMode so it doesn't carry over to the next game
        powerMode = false;
        lastBorderStartTime = nowMs; // RESET TIMER ON DEATH
    }
    // 2. POWER PELLET CORNERS (Same as before)
    if (!powerMode) {
        if (std::abs((int)pacPos - 0) < 5 || std::abs((int)pacPos - 250) < 5 ||
            std::abs((int)pacPos - 308) < 5 || std::abs((int)pacPos - 558) < 5) {
            powerMode = true;
            powerStartTime = nowMs;
            worldSpeed = 1.5f;
        }
    } else if (nowMs - powerStartTime > 9000) {
        powerMode = false;
        worldSpeed = 1.2f;
    }

    // 3. DRAWING
    update_pacman_border(canvas, pacPos, ghostPos, powerMode, worldSpeed, ghostEaten);
}
