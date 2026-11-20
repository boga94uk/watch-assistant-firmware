idf_component_register(
    SRCS "app.c" "app_fsm.c"
    INCLUDE_DIRS "include"
    REQUIRES ui display settings audio net queue board
)
