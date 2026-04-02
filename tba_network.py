import requests
import json
import datetime
from urllib.parse import urljoin

try:
    from config import TBA_KEY
except ImportError:
    # Use empty/dummy key if config is not present, will fail API calls
    TBA_KEY = ""

TBA_BASE_URL = "https://www.thebluealliance.com/api/v3/"

class TBANetwork:
    def __init__(self, is_sim=False, sim_event=None, sim_team=None, sim_match=None):
        self.headers = {"X-TBA-Auth-Key": TBA_KEY}
        self.is_sim = is_sim
        self.sim_event = sim_event
        self.sim_team = sim_team
        self.sim_match = sim_match

        self.matches_completed = 0
        self.match_history = []
        self.next_event_name = ""
        self.next_event_date = 0

    def get_events_simple(self, team_key, year):
        url = urljoin(TBA_BASE_URL, f"team/{team_key}/events/{year}/simple")
        try:
            response = requests.get(url, headers=self.headers, timeout=10)
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching events: {e}")
            return None

    def get_matches_simple(self, event_key):
        url = urljoin(TBA_BASE_URL, f"event/{event_key}/matches/simple")
        try:
            response = requests.get(url, headers=self.headers, timeout=10)
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching matches: {e}")
            return None

    def parse_date(self, date_str):
        try:
            dt = datetime.datetime.strptime(date_str, "%Y-%m-%d")
            return dt.timestamp()
        except ValueError:
            return 0

    def parse_tba_events(self, team_key, year):
        events = self.get_events_simple(team_key, year)
        if not events:
            return

        now = datetime.datetime.now().timestamp()
        min_diff = -1

        for evt in events:
            start_date = evt.get("start_date")
            city = evt.get("city")

            if start_date and city:
                evt_time = self.parse_date(start_date)
                if evt_time > now:
                    diff = evt_time - now
                    if min_diff == -1 or diff < min_diff:
                        min_diff = diff
                        self.next_event_date = evt_time
                        self.next_event_name = city

    def parse_tba_json(self, event_key, filter_team=None, sim_target_match=None):
        matches = self.get_matches_simple(event_key)
        if not matches:
            return

        self.match_history = []

        # Sort matches by match_number if possible, or actual_time
        # Simple sort just by match number for Quals
        quals = [m for m in matches if m.get("comp_level") == "qm"]
        quals.sort(key=lambda x: x.get("match_number", 0))

        # If we are in simulation mode, and a specific match is targeted:
        # We only consider matches up to the simulation target match as completed.
        for match in quals:
            match_num = match.get("match_number")

            # If we're filtering by team, make sure team is in alliances
            if filter_team:
                team_key = f"frc{filter_team}"
                red_teams = match.get("alliances", {}).get("red", {}).get("team_keys", [])
                blue_teams = match.get("alliances", {}).get("blue", {}).get("team_keys", [])
                if team_key not in red_teams and team_key not in blue_teams:
                    continue # Skip this match, our team isn't in it

            if self.is_sim and sim_target_match is not None:
                if match_num > sim_target_match:
                    continue # Pretend matches after the sim target haven't happened yet

            alliances = match.get("alliances")
            if not alliances:
                continue

            red = alliances.get("red")
            # For simplicity, we get the score from the red alliance (following original code logic).
            # In a real scenario, you'd get the score of the alliance the team is on.
            if red and "score" in red and red["score"] >= 0:
                match_data = {
                    "matchNum": match_num,
                    "totalScore": red["score"],
                    "autoFuel": 0, # Legacy placeholder
                    "teleFuel": 0, # Legacy placeholder
                    "autoClimb": 0,
                    "teleClimb": 0,
                    "fuelRP": False,
                    "towerRP": False,
                    "foulPointsAwarded": 0
                }

                # If we're in simulation mode and this is the target match,
                # we don't treat it as completed if we're simulating BEFORE the match.
                # However, the prompt says "grab historical data (allow me to give the event, team, and qual match)"
                # which implies we show that match's data. Let's include it.
                self.match_history.append(match_data)

        # Keep only the last 12 matches
        self.match_history = self.match_history[-12:]
        self.matches_completed = len(self.match_history)

    def update_data(self, team_number, event_key, year):
        team_key = f"frc{team_number}"
        if self.is_sim:
            # Simulate using historical data based on the provided event/team
            self.parse_tba_events(team_key, year)
            self.parse_tba_json(self.sim_event or event_key, self.sim_team or team_number, self.sim_match)
        else:
            self.parse_tba_events(team_key, year)
            self.parse_tba_json(event_key, team_number)
