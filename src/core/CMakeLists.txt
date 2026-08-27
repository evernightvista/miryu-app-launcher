# miryu-core: shared static library used by the interpreter, daemon, cli and KCM.

# Generate MiryuSettings from miryusettings.kcfg (KConfig XT).
# KCONFIG_ADD_KCFG_FILES expects the *options* file (.kcfgc), NOT the .kcfg
# itself: it reads the "File=..." line inside the .kcfgc to locate the .kcfg,
# then runs "kconfig_compiler <kcfg> <kcfgc> -d <builddir>". Passing the .kcfg
# here makes kconfig_compiler reject it with "Codegen options file must have
# extension .kcfgc".
kconfig_add_kcfg_files(miryu-core_SRCS miryusettings.kcfgc)

add_library(miryu-core STATIC
    config.h
    config.cpp
    miryu.h
    miryu.cpp
    ${miryu-core_SRCS}
)
# PIC is required because miryu-core is linked into the kcm_miryu.so shared
# module (KDE System Settings plugin). Without -fPIC the linker rejects the
# static archive with "relocation R_X86_64_32S ... can not be used when making
# a shared object".
set_property(TARGET miryu-core PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(miryu-core PUBLIC
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_BINARY_DIR}/config"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(miryu-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::I18n
    Qt6::DBus
    PkgConfig::APPIMAGE
    # Low-level helpers (appimage_type2_digest_md5, appimage_hexlify,
    # appimage_get_elf_section_offset_and_length) live in this separate static
    # archive and are NOT pulled in by libappimage.pc.
    ${APPIMAGE_SHARED_LIBRARY}
    PkgConfig::GLIB
)
