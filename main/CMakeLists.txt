idf_component_register(
    SRCS
        "main.c"
        "epd_1in54.c"
        "epd_paint.c"
        "font8x8.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)
