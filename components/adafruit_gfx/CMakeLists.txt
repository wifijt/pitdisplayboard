set(srcs 
    "Adafruit_GFX.cpp" "Print.cpp"
    )

# "glcdfont.c" defined but not used font
idf_component_register(SRCS ${srcs}
    INCLUDE_DIRS "." "Fonts"
    REQUIRES esp_lcd)