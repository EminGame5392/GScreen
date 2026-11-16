add_executable(GScreen.Core WIN32
  src/main.cpp
  src/Log.cpp
  src/Json.cpp
  src/Config.cpp
  src/Layout.cpp
  src/DisplayTopology.cpp
  src/VddBackend.cpp
  src/Specialization.cpp
  src/Capture.cpp
  src/Compositor.cpp
  src/WindowPresenter.cpp
  src/DisplayCorePresenter.cpp
  src/PanelGuard.cpp
  src/Engine.cpp
  src/IpcServer.cpp
  src/Tray.cpp
  src/Watchdog.cpp
)

gscreen_version_resource(GScreen.Core "GScreen core: virtual monitors and compositor")

target_link_libraries(GScreen.Core PRIVATE
  d3d11 dxgi d3dcompiler user32 gdi32 shell32 advapi32 ole32 windowsapp)

# Self-test target: layout math and JSON, no GPU needed.
add_executable(GScreen.Tests src/tests.cpp src/Json.cpp src/Layout.cpp src/VddBackend.cpp src/Log.cpp)
target_link_libraries(GScreen.Tests PRIVATE shell32 ole32)
