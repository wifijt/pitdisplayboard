import math
import time
try:
    from rgbmatrix import RGBMatrix, RGBMatrixOptions, graphics
except ImportError:
    pass
from PIL import Image, ImageDraw, ImageFont

# --- Messages and Constants ---
MSG_TEAM_NAME       = "IPSWICH TIGERS 5459"
MSG_SEASON_START    = "COMPETITION SEASON IS HERE!"
MSG_WAIT_SYNC       = "WAITING FOR TIME SYNC..."
MSG_SUBTITLE        = "ROBOTICS TEAM 5459"
SPONSOR_HEADER_TEXT = "Thank you to our sponsors"

SPONSOR_LIST = [
    "EBSCO Information Services",
    "New England Biolabs",
    "Analog Devices",
    "Ipswich Public Schools",
    "Institution for Savings",
    "Rotary Club of Ipswich",
    "Corning Foundation",
    "Applied Materials"
]

class MatrixDisplay:
    def __init__(self, width=256, height=64, chain=4, parallel=1):
        try:
            from adafruit_blinka_raspberry_pi5_piomatter import Adafruit_RGBMatrix
            # Setup Adafruit Triple LED Matrix Bonnet
            # The exact pins will depend on the bonnet and library, assuming default bonnet pins
            import board
            self.matrix = Adafruit_RGBMatrix(
                width=width, height=height, bit_depth=4,
                rgb_pins=[board.D5, board.D13, board.D6, board.D21, board.D20, board.D19],
                addr_pins=[board.D22, board.D23, board.D24, board.D25, board.D12],
                clock_pin=board.D17, latch_pin=board.D26, output_enable_pin=board.D4)
        except ImportError:
            try:
                from rgbmatrix import RGBMatrix, RGBMatrixOptions
                options = RGBMatrixOptions()
                options.rows = height
                options.cols = width // chain
                options.chain_length = chain
                options.parallel = parallel
                options.hardware_mapping = 'regular'
                options.drop_privileges = False
                self.matrix = RGBMatrix(options=options)
            except ImportError:
                print("Warning: No matrix library found. Running in headless mode.")
                self.matrix = None

        # We will render onto a PIL Image and then draw it to the matrix
        # This makes it easier to do complex text and shapes
        self.canvas = Image.new('RGB', (width, height), (0, 0, 0))
        self.draw = ImageDraw.Draw(self.canvas)

        self.width = width
        self.height = height

        # Ticker state
        self.ticker_queue = []
        self.current_msg_idx = 0
        self.next_msg_idx = 1
        self.scroll_x = 128.0
        self.gap = 60

        # Panel Rotation
        self.show_upcoming = True
        self.last_rotation_time = time.time()

        # Sponsor state
        self.SPONSOR_IDLE = 0
        self.SPONSOR_INTRO = 1
        self.SPONSOR_SHOW_LIST = 2
        self.SPONSOR_OUTRO = 3

        self.sponsor_state = self.SPONSOR_IDLE
        # Trigger sponsor 30 seconds after boot
        self.last_sponsor_run_time = time.time() - (15 * 60) + 30
        self.sponsor_list_idx = 0
        self.sponsor_wait_start = 0
        self.sponsor_list_y = 64
        self.outro_start_time = 0
        self.pulse_idx = 0.0

        # Colors
        self.tiger_orange = (255, 140, 0)
        self.white = (255, 255, 255)
        self.red = (255, 0, 0)
        self.yellow = (255, 255, 0)
        self.cyan = (0, 255, 255)
        self.gray = (128, 128, 128)
        self.green = (0, 255, 0)

        # Fonts
        # Since standard fonts might not be available, we load default PIL font or simple ones
        # For a real implementation, you'd load actual .ttf files
        try:
            self.font_large = ImageFont.truetype("DejaVuSans-Bold.ttf", 18)
            self.font_medium = ImageFont.truetype("DejaVuSans-Bold.ttf", 12)
            self.font_small = ImageFont.truetype("DejaVuSans.ttf", 9)
            self.font_tiny = ImageFont.load_default()
        except:
            self.font_large = ImageFont.load_default()
            self.font_medium = ImageFont.load_default()
            self.font_small = ImageFont.load_default()
            self.font_tiny = ImageFont.load_default()

        self.zoom = 0.5
        self.show_zoom = True

        # Dummy schedule data for upcoming (will be updated via TBA)
        # Type, Number, Color, EstTime
        self.schedule = [
            {'type': 'Q', 'number': 42, 'color': self.red, 'estTime': 0},
            {'type': 'Q', 'number': 51, 'color': self.cyan, 'estTime': 0},
            {'type': 'Q', 'number': 68, 'color': self.red, 'estTime': 0}
        ]
        self.currently_playing = 39

    def load_tiger_logo(self):
        # We'll skip the actual 64x64 tiger mapping here for brevity and draw a placeholder
        # In the full port, we'd convert tiger_hires_map to a python list
        pass

    def draw_tiger(self, x, y):
        # Placeholder for the tiger logo
        self.draw.rectangle([x+10, y+10, x+50, y+50], fill=self.tiger_orange, outline=self.white)

    def is_safe_for_sponsors(self):
        now = time.time()
        for match in self.schedule:
            if match['estTime'] != 0:
                diff = match['estTime'] - now
                if abs(diff) < 30 * 60:
                    return False
        return True

    def refresh_ticker_queue(self, next_event_date, next_event_name):
        self.ticker_queue = [MSG_TEAM_NAME]

        now = time.time()
        if next_event_date > 0:
            diff = next_event_date - now
            days = int(diff / 86400)
            if days > 0:
                self.ticker_queue.append(f"T-{days} DAYS UNTIL {next_event_name} COMP")
            elif days == 0:
                self.ticker_queue.append(f"IT IS TIME FOR {next_event_name} COMP!")
            else:
                self.ticker_queue.append(MSG_SEASON_START)
        else:
            self.ticker_queue.append("CHECKING SCHEDULE...")

        self.ticker_queue.append(MSG_SUBTITLE)

    def draw_text(self, text, x, y, font, fill):
        self.draw.text((x, y), text, font=font, fill=fill)

    def get_text_width(self, text, font):
        return self.draw.textlength(text, font=font)

    def update_sponsors(self, now_sec):
        if self.sponsor_state == self.SPONSOR_IDLE:
            if (now_sec - self.last_sponsor_run_time) >= 15 * 60:
                if self.is_safe_for_sponsors():
                    self.sponsor_state = self.SPONSOR_INTRO
                    self.last_sponsor_run_time = now_sec

        if self.sponsor_state != self.SPONSOR_IDLE:
            if self.sponsor_state == self.SPONSOR_INTRO:
                header = SPONSOR_HEADER_TEXT
                w = self.get_text_width(header, self.font_tiny)
                start_x = max(0, (256 - w) / 2)
                self.draw_text(header, start_x, 5, self.font_tiny, self.white)

                if self.sponsor_wait_start == 0:
                    self.sponsor_wait_start = now_sec
                if now_sec - self.sponsor_wait_start > 2.0:
                    self.sponsor_state = self.SPONSOR_SHOW_LIST
                    self.sponsor_list_idx = 0
                    self.sponsor_list_y = 90
                    self.sponsor_wait_start = 0

            elif self.sponsor_state == self.SPONSOR_SHOW_LIST:
                if self.sponsor_list_idx < len(SPONSOR_LIST):
                    name = SPONSOR_LIST[self.sponsor_list_idx]

                    w = self.get_text_width(name, self.font_medium)
                    lines = []
                    if w > 250:
                        split_pos = len(name) // 2
                        space_pos = name.find(' ', split_pos)
                        if space_pos == -1:
                            space_pos = name.rfind(' ', 0, split_pos)

                        if space_pos != -1:
                            lines.append(name[:space_pos])
                            lines.append(name[space_pos+1:])
                        else:
                            lines.append(name)
                    else:
                        lines.append(name)

                    target_y = 40

                    if self.sponsor_wait_start == 0:
                        if self.sponsor_list_y > target_y:
                            self.sponsor_list_y -= 2.0 * 0.5 # delta time approx
                        else:
                            self.sponsor_list_y = target_y
                            self.sponsor_wait_start = now_sec
                    else:
                        if now_sec - self.sponsor_wait_start > 1.0:
                            self.sponsor_list_y -= 2.0 * 0.5
                            if self.sponsor_list_y < -50:
                                self.sponsor_list_idx += 1
                                self.sponsor_list_y = 90
                                self.sponsor_wait_start = 0

                    current_y = int(self.sponsor_list_y)
                    if len(lines) > 1:
                        current_y -= 10 * (len(lines) - 1)

                    for line in lines:
                        w = self.get_text_width(line, self.font_medium)
                        draw_x = (256 - w) / 2
                        self.draw_text(line, draw_x, current_y, self.font_medium, self.tiger_orange)
                        current_y += 25

                    # Header on top with black background
                    self.draw.rectangle([0, 0, 256, 15], fill=(0,0,0))
                    header = SPONSOR_HEADER_TEXT
                    w = self.get_text_width(header, self.font_tiny)
                    start_x = max(0, (256 - w) / 2)
                    self.draw_text(header, start_x, 5, self.font_tiny, self.white)

                else:
                    self.sponsor_state = self.SPONSOR_OUTRO
                    self.outro_start_time = now_sec

            elif self.sponsor_state == self.SPONSOR_OUTRO:
                progress = now_sec - self.outro_start_time
                if progress > 4.0:
                    self.sponsor_state = self.SPONSOR_IDLE
                else:
                    color = self.white if (progress % 0.5) < 0.25 else self.green
                    thanks = "THANK YOU!!"
                    w = self.get_text_width(thanks, self.font_large)
                    self.draw_text(thanks, (256-w)/2, 45, self.font_large, color)

            # Draw green pulsing border
            self.pulse_idx += 0.3
            p = 150 + int(100 * math.sin(self.pulse_idx))
            p_color = (0, p, 0)
            self.draw.rectangle([0, 0, 255, 63], outline=p_color)
            self.draw.rectangle([1, 1, 254, 62], outline=p_color)

            return True # Sponsors are active
        return False

    def render(self, tba_network, pacman_engine, now_sec):
        # Clear canvas
        self.draw.rectangle([0, 0, self.width, self.height], fill=(0,0,0))

        # --- SPONSOR CHECK ---
        sponsors_active = self.update_sponsors(now_sec)

        if not sponsors_active:
            # --- PANEL ROTATION ---
            if now_sec - self.last_rotation_time > 7.0:
                self.show_upcoming = not self.show_upcoming
                self.last_rotation_time = now_sec

            # Intro Zoom Animation
            if self.show_zoom:
                self.draw_tiger(int(27 - (64*self.zoom)/2), int(28 - (64*self.zoom)/2))
                self.zoom += 0.08
                if self.zoom >= 1.0:
                    self.show_zoom = False
            else:
                self.draw_tiger(-5, -3)

                self.draw_text("5459", 52, 40, self.font_large, self.tiger_orange)

                # Ticker
                if not self.ticker_queue:
                    self.refresh_ticker_queue(tba_network.next_event_date, tba_network.next_event_name)

                msg1 = self.ticker_queue[self.current_msg_idx]
                msg2 = self.ticker_queue[self.next_msg_idx]

                w1 = self.get_text_width(msg1, self.font_small)
                self.draw_text(msg1, int(self.scroll_x), 60, self.font_small, self.white)
                self.draw_text(msg2, int(self.scroll_x + w1 + self.gap), 60, self.font_small, self.white)

                self.scroll_x -= 2.0
                self.draw.rectangle([129, 45, 255, 62], fill=(0,0,0)) # Clear area for ticker handover logic?

                if self.scroll_x < -(w1 + self.gap):
                    self.scroll_x = 0
                    self.current_msg_idx = self.next_msg_idx
                    self.next_msg_idx = (self.next_msg_idx + 1) % len(self.ticker_queue)
                    if self.current_msg_idx == 0:
                        self.refresh_ticker_queue(tba_network.next_event_date, tba_network.next_event_name)

                self.draw.line((130, 5, 130, 59), fill=self.gray)

                if self.show_upcoming:
                    self.draw_text(f"NOW ON FIELD: Q{self.currently_playing}", 138, 5, self.font_tiny, self.yellow)
                    self.draw.line((135, 14, 250, 14), fill=self.gray)
                    self.draw_text("UPCOMING", 138, 18, self.font_tiny, self.gray)

                    for i in range(min(3, len(self.schedule))):
                        y_off = 29 + (i * 10)
                        match = self.schedule[i]
                        self.draw_text(f"{match['type']}{match['number']}", 138, y_off, self.font_tiny, match['color'])
                        self.draw.line((134, y_off-1, 134, y_off+6), fill=match['color'])
                        self.draw_text("-10:45A", 160, y_off, self.font_tiny, self.white)
                else:
                    if tba_network.matches_completed == 0:
                        self.draw_text("AWAITING DATA...", 145, 30, self.font_tiny, self.white)
                    else:
                        last = tba_network.match_history[-1]
                        self.draw_text(f"LAST MATCH: Q{last['matchNum']}", 138, 5, self.font_tiny, self.cyan)
                        self.draw.line((135, 14, 250, 14), fill=self.gray)

                        self.draw_text("AUTO:", 138, 18, self.font_tiny, self.red)
                        self.draw_text(f" F{last['autoFuel']} C{last['autoClimb']}", 170, 18, self.font_tiny, self.white)

                        self.draw_text("TELE:", 138, 27, self.font_tiny, self.green)
                        self.draw_text(f" F{last['teleFuel']}", 170, 27, self.font_tiny, self.white)

                        self.draw_text(f"CLIMB: {int(last['teleClimb']/10)}", 138, 36, self.font_tiny, self.white)
                        self.draw_text(f"FOUL: {last['foulPointsAwarded']}", 138, 45, self.font_tiny, self.red)
                        self.draw_text(f"SCORE: {last['totalScore']}", 138, 54, self.font_tiny, self.yellow)

                # Rank info
                self.draw_text("RANK", 220, 18, self.font_tiny, self.gray)
                self.draw_text("12", 215, 45, self.font_medium, self.yellow)

        # --- CLOCK ---
        t = time.localtime(now_sec)
        hour = t.tm_hour % 12
        if hour == 0: hour = 12

        self.draw_text(str(hour), 91, 2, self.font_tiny, self.white)
        colon_x = 91 + 12 if hour >= 10 else 91 + 6
        if t.tm_sec % 2 == 0:
            self.draw.point((colon_x + 1, 4), fill=self.white)
            self.draw.point((colon_x + 1, 6), fill=self.white)

        self.draw_text(f"{t.tm_min:02d}", colon_x + 4, 2, self.font_tiny, self.white)
        self.draw_text("P" if t.tm_hour >= 12 else "A", 120, 2, self.font_tiny, self.white)

        # --- PACMAN ---
        pacman_engine.run_pacman_cycle(self.draw, now_sec, sponsors_active)

        if not pacman_engine.border_active and not sponsors_active and pacman_engine.win_start_time == 0:
            self.pulse_idx += 0.1
            p = 150 + int(100 * math.sin(self.pulse_idx))
            p_color = (p, int(p * 140 / 255), 0)
            self.draw.rectangle([0, 0, 255, 63], outline=p_color)
            self.draw.rectangle([1, 1, 254, 62], outline=p_color)

        # Draw the canvas to the matrix
        if self.matrix:
            if hasattr(self.matrix, 'SetImage'):
                self.matrix.SetImage(self.canvas)
            elif hasattr(self.matrix, 'display'):
                # For adafruit_blinka_raspberry_pi5_piomatter we might need to convert Image to RGB
                self.matrix.display(self.canvas)
