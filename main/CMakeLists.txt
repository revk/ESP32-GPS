set (COMPONENT_SRCS "GPS.c" "email.c" "../settings.c")
set (COMPONENT_REQUIRES "ESP32-RevK" "fatfs" "sdmmc" "driver" "esp_driver_sdmmc")
register_component ()
