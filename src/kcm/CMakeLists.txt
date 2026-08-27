# kcm_miryu - the Miryu settings module for KDE System Settings (Plasma 6 / KF6).
# Shows up under "Miryu App Launcher" in systemsettings / kcmshell6.
# It reads/writes the same miryurc the core uses, via miryu::Config, so there is
# a single source of truth (no separate KConfigSkeleton). The form is built in
# C++ (no .ui file needed).

set(kcm_SRC kcm_miryu.cpp)

kcoreaddons_add_plugin(kcm_miryu
    SOURCES ${kcm_SRC}
    INSTALL_NAMESPACE "plasma/kcms/systemsettings_qwidgets"
)

target_link_libraries(kcm_miryu
    miryu-core
    KF6::KCMUtils
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::I18n
    KF6::CoreAddons
    KF6::WidgetsAddons
    Qt6::Widgets
)

# Auto-generate the .desktop file from the JSON metadata
kcmutils_generate_desktop_file(kcm_miryu)
