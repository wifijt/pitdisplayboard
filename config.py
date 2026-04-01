# config.example.py
# Rename this file to config.py and fill in your details

# The Blue Alliance API
TBA_KEY = "YOUR_TBA_API_KEY_HERE"

# Default configuration
TEAM_NUMBER = "5459"
EVENT_KEY = "2025mabos"

# Hardware Configuration
# 64 x 256 matrix (daisy chained, 4 panels of 64x64)
MATRIX_WIDTH = 256
MATRIX_HEIGHT = 64

# Matrix chain/parallel config. For 1st port daisy chained: chain=4, parallel=1.
# You mentioned later you will have 3 directly connected and 1 daisy chained through the 3rd port.
MATRIX_CHAIN = 4
MATRIX_PARALLEL = 1
