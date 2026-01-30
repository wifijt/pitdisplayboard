#ifndef PACMAN_ENGINE_H
#define PACMAN_ENGINE_H

#include <stdint.h>
#include "Adafruit_GFX.h"

// Updates game state (collisions, etc.) and draws the frame
void run_pacman_cycle(GFXcanvas16 *canvas, uint32_t nowMs);

// Resets game variables for a new run
void reset_pacman_game(uint32_t nowMs);

#endif // PACMAN_ENGINE_H
