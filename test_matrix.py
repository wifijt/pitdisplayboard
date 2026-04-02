from adafruit_blinka_raspberry_pi5_piomatter import Pinout

for name, member in Pinout.__members__.items():
    print(name, member.value)
