# The environment sensors behind sensor_get. The chip drivers and the bus are
# private: only the sampler talks to them.
idf_component_register(SRCS "sensors.c" "sht4x.c" "scd4x.c" "sensor_bus.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES plugin_sdk
                       PRIV_REQUIRES esp_driver_i2c esp_timer)
