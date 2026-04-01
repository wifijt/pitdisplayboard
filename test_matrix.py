import numpy as np
from adafruit_blinka_raspberry_pi5_piomatter import PioMatter, Colorspace, Pinout, Geometry

geometry = Geometry(256, 64, 5) # 256x64, 5 address lines (since it's 1/32 scan usually)
# RGB888 expects 4 bytes per pixel (XRGB or RGBX) based on the error
framebuffer = np.zeros((64, 256, 4), dtype=np.uint8)

matrix = PioMatter(Colorspace.RGB888, Pinout.AdafruitMatrixBonnet, framebuffer, geometry)

print("Matrix initialized!")
