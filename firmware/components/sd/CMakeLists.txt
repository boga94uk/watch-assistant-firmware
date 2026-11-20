idf_component_register(
    SRCS "sd.c"
    INCLUDE_DIRS "include"
    REQUIRES board fatfs sdmmc driver
)
