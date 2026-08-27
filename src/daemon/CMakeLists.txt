# miryud - lean background watcher that auto-integrates AppImages dropped
# into the integration directory (or any configured additional directory).
add_executable(miryud main.cpp)
target_link_libraries(miryud PRIVATE miryu-core)
install(TARGETS miryud RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})
