import math

# Ghost States
GHOST_DEAD = 0
GHOST_ALIVE = 1
GHOST_EYES = 2

# Colors
GHOST_BLINKY = (255, 0, 0)
GHOST_PINKY = (255, 184, 255)
GHOST_INKY = (0, 255, 255)
GHOST_CLYDE = (255, 184, 82)
SCARED_COLOR = (0, 0, 255)
WHITE = (255, 255, 255)
YELLOW = (255, 255, 0)
BLACK = (0, 0, 0)

ghost_shape = [
    [0,1,1,1,1,1,0],
    [1,2,1,2,1,1,1],
    [1,2,3,2,3,1,1],
    [1,1,1,1,1,1,1],
    [1,1,1,1,1,1,1],
    [1,1,1,1,1,1,1],
    [1,0,1,0,1,0,1]
]

pacman_open = [
    [0,0,1,1,1,0,0],
    [0,1,1,1,1,1,0],
    [1,1,1,1,0,0,0],
    [1,1,1,0,0,0,0],
    [1,1,1,1,0,0,0],
    [0,1,1,1,1,1,0],
    [0,0,1,1,1,0,0]
]

pacman_closed = [
    [0,0,1,1,1,0,0],
    [0,1,1,1,1,1,0],
    [1,1,1,1,1,1,1],
    [1,1,1,1,1,1,1],
    [1,1,1,1,1,1,1],
    [0,1,1,1,1,1,0],
    [0,0,1,1,1,0,0]
]

ghost_eyes = [
    [0,0,0,0,0,0,0],
    [0,2,2,0,2,2,0],
    [0,2,3,0,2,3,0],
    [0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0]
]

class PacmanEngine:
    def __init__(self):
        self.pac_pos = 0.0
        self.ghost_pos = [-20.0, -40.0, -60.0, -80.0]
        self.world_speed = 1.2
        self.power_mode = False
        self.power_start_time = 0
        self.border_active = False
        self.last_border_start_time = 0
        self.ghost_state = [GHOST_ALIVE, GHOST_ALIVE, GHOST_ALIVE, GHOST_ALIVE]
        self.win_start_time = 0
        self.ghost_colors = [GHOST_BLINKY, GHOST_PINKY, GHOST_INKY, GHOST_CLYDE]

    def get_pos(self, p):
        perimeter = 612
        safe_p = p % perimeter
        if safe_p < 0:
            safe_p += perimeter

        if safe_p < 249:
            x, y = 3 + safe_p, 3
        elif safe_p < 306:
            x, y = 252, 3 + (safe_p - 249)
        elif safe_p < 555:
            x, y = 252 - (safe_p - 306), 60
        else:
            x, y = 3, 60 - (safe_p - 555)

        return int(x), int(y)

    def draw_pac(self, draw, x, y, p, current_speed):
        moving_forward = current_speed > 0

        if 0.0 <= p < 249.0:
            direction = 0 if moving_forward else 2
        elif 249.0 <= p < 306.0:
            direction = 1 if moving_forward else 3
        elif 306.0 <= p < 555.0:
            direction = 2 if moving_forward else 0
        else:
            direction = 3 if moving_forward else 1

        is_open = int(p / 5) % 2 == 0
        sprite = pacman_open if is_open else pacman_closed

        for i in range(7):
            for j in range(7):
                if direction == 0:   # RIGHT
                    r_i, r_j = i, j
                elif direction == 1: # DOWN
                    r_i, r_j = 6 - j, i
                elif direction == 2: # LEFT
                    r_i, r_j = i, 6 - j
                else:                # UP
                    r_i, r_j = j, 6 - i

                if sprite[r_i][r_j] == 1:
                    draw.point((x + j, y + i), fill=YELLOW)

    def draw_ghost(self, draw, x, y, color, scared, p, speed, state):
        if state == GHOST_DEAD:
            return

        if state == GHOST_EYES:
            for i in range(7):
                for j in range(7):
                    pixel = ghost_eyes[i][j]
                    if pixel == 2:
                        draw.point((x + j, y + i), fill=WHITE)
                    elif pixel == 3:
                        draw.point((x + j, y + i), fill=SCARED_COLOR)
            return

        moving_forward = speed > 0
        if 0.0 <= p < 249.0:
            direction = 0 if moving_forward else 2
        elif 249.0 <= p < 306.0:
            direction = 1 if moving_forward else 3
        elif 306.0 <= p < 555.0:
            direction = 2 if moving_forward else 0
        else:
            direction = 3 if moving_forward else 1

        for i in range(7):
            for j in range(7):
                pixel = ghost_shape[i][j]
                if pixel == 1:
                    draw.point((x + j, y + i), fill=SCARED_COLOR if scared else color)
                elif pixel == 2:
                    draw.point((x + j, y + i), fill=WHITE)
                elif pixel == 3:
                    ox, oy = 0, 0
                    if direction == 0: ox = 1
                    elif direction == 1: oy = 1
                    elif direction == 2: ox = -1
                    else: oy = -1
                    draw.point((x + j + ox, y + i + oy), fill=WHITE if scared else SCARED_COLOR)

    def update_pacman_border(self, draw):
        moving_forward = self.world_speed > 0

        if self.win_start_time == 0:
            self.pac_pos = (self.pac_pos + self.world_speed) % 616.0

        pax, pay = self.get_pos(self.pac_pos)
        self.draw_pac(draw, pax - 3, pay - 3, self.pac_pos, self.world_speed)

        for i in range(4):
            if self.ghost_state[i] == GHOST_DEAD:
                continue

            if self.ghost_state[i] == GHOST_ALIVE:
                g_speed = 0.8 if self.power_mode else 1.6
                delta = g_speed if moving_forward else -g_speed
                self.ghost_pos[i] = (self.ghost_pos[i] + delta) % 616.0
            elif self.ghost_state[i] == GHOST_EYES:
                self.ghost_pos[i] -= 6.0
                if self.ghost_pos[i] <= 0:
                    self.ghost_pos[i] = 0
                    self.ghost_state[i] = GHOST_DEAD
                    continue

            gx, gy = self.get_pos(self.ghost_pos[i])
            self.draw_ghost(draw, gx - 3, gy - 3, self.ghost_colors[i], self.power_mode, self.ghost_pos[i], self.world_speed, self.ghost_state[i])

    def reset_pacman_game(self, now_sec):
        self.border_active = True
        self.last_border_start_time = now_sec
        self.win_start_time = 0
        self.pac_pos = 0.0
        for i in range(4):
            self.ghost_pos[i] = 100.0 + (i * 20.0)
            self.ghost_state[i] = GHOST_ALIVE

    def run_pacman_cycle(self, draw, now_sec, sponsors_active):
        if sponsors_active:
            return

        if not self.border_active and (now_sec - self.last_border_start_time > 120):
            self.reset_pacman_game(now_sec)

        if not self.border_active:
            return

        if self.win_start_time != 0:
            if now_sec - self.win_start_time > 4.0:
                self.border_active = False
                self.power_mode = False
                self.last_border_start_time = now_sec
                self.win_start_time = 0
            else:
                p = (int(now_sec * 1000) % 500) / 500.0
                w_color = WHITE if p < 0.5 else BLACK
                draw.rectangle([0, 0, 255, 63], outline=w_color)
                draw.rectangle([1, 1, 254, 62], outline=w_color)
                self.update_pacman_border(draw)
            return

        # Collision detection
        for i in range(4):
            if self.ghost_state[i] != GHOST_ALIVE:
                continue

            dist = abs(self.pac_pos - self.ghost_pos[i])
            if dist > 308:
                dist = 616 - dist

            if dist < 8:
                if self.power_mode:
                    self.ghost_state[i] = GHOST_EYES
                else:
                    self.border_active = False
                    self.last_border_start_time = now_sec

        all_dead = all(state == GHOST_DEAD for state in self.ghost_state)
        if all_dead and self.win_start_time == 0:
            self.win_start_time = now_sec

        # Power pellets corners
        if not self.power_mode:
            pac_int = int(self.pac_pos)
            if abs(pac_int - 0) < 5 or abs(pac_int - 250) < 5 or \
               abs(pac_int - 308) < 5 or abs(pac_int - 558) < 5:
                self.power_mode = True
                self.power_start_time = now_sec
                self.world_speed = 1.5
        elif now_sec - self.power_start_time > 9.0:
            self.power_mode = False
            self.world_speed = 1.2

        self.update_pacman_border(draw)
