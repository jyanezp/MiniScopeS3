idf_component_register(SRCS "miniscope_s3_main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_adc esp_timer log)
