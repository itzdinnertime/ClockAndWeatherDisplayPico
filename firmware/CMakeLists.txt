set(WIFI_SSID "$ENV{WIFI_SSID}" CACHE STRING "Wi-Fi network name")
set(WIFI_PASSWORD "$ENV{WIFI_PASSWORD}" CACHE STRING "Wi-Fi password (empty for an open network)")
if(WIFI_SSID STREQUAL "")
    message(WARNING "WIFI_SSID is empty: the display will run offline (DS3231 time only). "
                    "Pass -DWIFI_SSID=... -DWIFI_PASSWORD=... or set them as environment variables.")
endif()

# Credentials go into a generated header inside the build tree, never into git.
set(TW_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(${CMAKE_CURRENT_LIST_DIR}/secrets.h.in ${TW_GENERATED_DIR}/secrets.h @ONLY)

add_executable(time_weather_display
    main.cpp
    drivers/hub75_panel.cpp
    net/http_client.cpp
    net/ntp_client.cpp
    services/time_service.cpp
    services/weather_service.cpp
)

pico_generate_pio_header(time_weather_display ${CMAKE_CURRENT_LIST_DIR}/drivers/hub75.pio)

target_include_directories(time_weather_display PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}   # lwipopts.h, config.h
    ${TW_GENERATED_DIR}
)

target_compile_options(time_weather_display PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(time_weather_display
    tw_core
    pico_stdlib
    pico_multicore
    pico_cyw43_arch_lwip_poll
    pico_lwip_http
    hardware_pio
    hardware_i2c
    hardware_watchdog
    hardware_clocks
)

pico_set_program_name(time_weather_display "Time & Weather Display")
pico_enable_stdio_usb(time_weather_display 1)
pico_enable_stdio_uart(time_weather_display 0)
pico_add_extra_outputs(time_weather_display)
