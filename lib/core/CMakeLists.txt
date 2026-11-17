# Hardware-independent core: compiled into the firmware AND the host test binary.
add_library(tw_core STATIC
    src/captive_net.cpp
    src/ds3231.cpp
    src/graphics.cpp
    src/http_util.cpp
    src/hub75_pack.cpp
    src/json_lite.cpp
    src/ntp.cpp
    src/provisioning.cpp
    src/recovery.cpp
    src/time_utils.cpp
    src/weather.cpp
    src/web.cpp
)
target_include_directories(tw_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_features(tw_core PUBLIC cxx_std_20)
