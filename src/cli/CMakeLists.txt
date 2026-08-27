# miryu-app-launcher-cli - command line helper for (un)integration, listing and cleanup.
add_executable(miryu-app-launcher-cli main.cpp)
target_link_libraries(miryu-app-launcher-cli PRIVATE miryu-core)
install(TARGETS miryu-app-launcher-cli RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})
