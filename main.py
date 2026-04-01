import time
import argparse
import sys
import threading
from config import TEAM_NUMBER, EVENT_KEY, MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_CHAIN, MATRIX_PARALLEL
from tba_network import TBANetwork
from matrix_display import MatrixDisplay
from pacman_engine import PacmanEngine

def tba_update_task(tba_network, team_number, event_key, year, is_sim):
    while True:
        try:
            print("Fetching TBA Data...")
            tba_network.update_data(team_number, event_key, year)
            print(f"Fetch complete. Next event: {tba_network.next_event_name}")
        except Exception as e:
            print(f"Error fetching TBA data: {e}")

        # If in sim mode, we might just fetch once or fetch less frequently since it's historical
        time.sleep(300) # Sleep for 5 minutes

def main():
    parser = argparse.ArgumentParser(description="FRC Pit Display Board")
    parser.add_argument('--test-mode', action='store_true', help="Enable simulation mode with historical data")
    parser.add_argument('--event', type=str, default=EVENT_KEY, help="TBA Event Key (e.g., 2024mabos)")
    parser.add_argument('--team', type=str, default=TEAM_NUMBER, help="Team Number (e.g., 5459)")
    parser.add_argument('--match', type=int, default=None, help="Qualification Match Number to simulate up to")

    args = parser.parse_args()

    # Determine year from event key (first 4 characters)
    try:
        year = int(args.event[:4])
    except ValueError:
        year = time.localtime().tm_year

    print(f"Starting FRC Pit Display for Team {args.team} at Event {args.event}")
    if args.test_mode:
        print(f"SIMULATION MODE ACTIVE - Target Match: {args.match}")

    # Initialize components
    tba_network = TBANetwork(
        is_sim=args.test_mode,
        sim_event=args.event if args.test_mode else None,
        sim_team=args.team if args.test_mode else None,
        sim_match=args.match
    )

    display = MatrixDisplay(
        width=MATRIX_WIDTH,
        height=MATRIX_HEIGHT,
        chain=MATRIX_CHAIN,
        parallel=MATRIX_PARALLEL
    )

    pacman = PacmanEngine()

    # Start TBA fetch thread
    tba_thread = threading.Thread(
        target=tba_update_task,
        args=(tba_network, args.team, args.event, year, args.test_mode),
        daemon=True
    )
    tba_thread.start()

    # Main Render Loop
    print("Starting display render loop...")
    try:
        while True:
            now_sec = time.time()
            display.render(tba_network, pacman, now_sec)

            # The original code had a vTaskDelay(25) which is ~40 FPS.
            # rgbmatrix handles refresh rate internally, but we delay our logic updates to not pin CPU.
            time.sleep(0.025)

    except KeyboardInterrupt:
        print("Exiting...")
        sys.exit(0)

if __name__ == "__main__":
    main()
