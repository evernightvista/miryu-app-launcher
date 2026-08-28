# miryu-core: shared static library used by the interpreter, daemon, cli and KCM.

# Generate MiryuSettings from miryusettings.kcfg (KConfig XT).
# KCONFIG_ADD_KCFG_FILES expects the *options* file (.kcfgc), NOT the .kcfg
# itself: it reads the "File=..." line inside the .kcfgc to locate the .kcfg,
# then runs "kconfig_compiler <kcfg> <kcfgc> -d <builddir>". Passing the .kcfg
# here makes kconfig_compiler reject it with "Codegen options file must have
# extension .kcfgc".
kconfig_add_kcfg_files(miryu-core_SRCS miryusettings.kcfgc)

# Generate a C++ header containing all available translations of the desktop
# action name ("Remove this AppImage") from the .po files. These are written as
# Name[locale]=translation entries in the integrated .desktop file so KConfig
# picks the correct language at display time (KService does NOT translate
# [Desktop Action] Name= via X-KDE-TranslationDomain).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(ACTION_TRANSLATIONS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/action-translations.h")
set(ACTION_TRANSLATIONS_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/extract-action-translations.py")
file(GLOB MIRYU_PO_FILES "${CMAKE_SOURCE_DIR}/po/*.po")
add_custom_command(
    OUTPUT "${ACTION_TRANSLATIONS_HEADER}"
    COMMAND ${Python3_EXECUTABLE} "${ACTION_TRANSLATIONS_SCRIPT}"
            "${CMAKE_SOURCE_DIR}/po"
            "${ACTION_TRANSLATIONS_HEADER}"
    DEPENDS ${MIRYU_PO_FILES} "${ACTION_TRANSLATIONS_SCRIPT}"
    COMMENT "Generating desktop action translations from .po files"
    VERBATIM
)

add_library(miryu-core STATIC
    config.h
    config.cpp
    miryu.h
    miryu.cpp
    "${ACTION_TRANSLATIONS_HEADER}"
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
