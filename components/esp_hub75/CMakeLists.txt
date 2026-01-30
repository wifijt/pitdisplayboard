# Simplified CMakeLists for Ipswich Tigers 5459 (ESP-IDF Native)
cmake_minimum_required(VERSION 3.5)

idf_build_get_property(target IDF_TARGET)

# Define the source files needed for S3 DMA
set(SRCS 
    "src/ESP32-HUB75-MatrixPanel-I2S-DMA.cpp" 
    "src/ESP32-HUB75-MatrixPanel-leddrivers.cpp"
    "src/platforms/esp32s3/gdma_lcd_parallel16.cpp"
)

# Register the component
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "./src"
                       REQUIRES esp_lcd driver)

# Force the NO_GFX flag because we aren't using Arduino's Adafruit_GFX
target_compile_options(${COMPONENT_LIB} PUBLIC -DNO_GFX -DHUB75_FOR_IDF)