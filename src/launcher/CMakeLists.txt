# The miryu-app-launcher GUI launcher: default MIME handler for AppImage files.
# Invoked by the file manager (Dolphin, etc.) when opening an AppImage.
# Binary name is "miryu-app-launcher" (not "miryu") to avoid file conflicts
# with other miryu-* packages that may ship a /usr/bin/miryu binary.
add_executable(miryu-app-launcher main.cpp)
target_link_libraries(miryu-app-launcher PRIVATE miryu-core)
install(TARGETS miryu-app-launcher RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})
