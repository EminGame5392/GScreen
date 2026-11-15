add_executable(GScreen.Watchdog WIN32 main.cpp)
gscreen_version_resource(GScreen.Watchdog "GScreen watchdog: restores the display after a crash")
target_link_libraries(GScreen.Watchdog PRIVATE user32 shell32)
