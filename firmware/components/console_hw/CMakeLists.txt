idf_component_register(
    SRCS "console_hw.c"
    INCLUDE_DIRS "include"
    REQUIRES board app settings ui console net queue audio sd
)
