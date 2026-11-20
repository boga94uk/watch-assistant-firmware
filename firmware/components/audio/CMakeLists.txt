idf_component_register(
    SRCS "audio.c" "es8311.c" "wav_util.c"
    INCLUDE_DIRS "include" "."
    REQUIRES board settings app ui driver esp_timer esp_mm
)
