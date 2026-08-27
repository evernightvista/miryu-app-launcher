# Miryu App Launcher - a lean AppImage desktop integration helper
# Built with Qt6 + KDE Frameworks 6. Settings live in a KDE System Settings KCM.
# Translations use gettext/PO via KI18n.
#
# DESIGN NOTE: AppImageLauncher intercepts AppImage launches via binfmt_misc,
# which forces it to ship a "binfmt-bypass" preload library to avoid the exec
# recursion (kernel -> handler -> exec app -> kernel -> handler -> ...). That
# machinery (plus bundled libappimage/AppImageUpdate/squashfuse) is the bulk of
# its complexity. Miryu is deliberately lean: it registers as the *MIME handler*
# for AppImage files instead, so launching never recurses - the AppImage runtime
# self-mounts when miryu startDetaches it. The trade-off is documented in README.

cmake_minimum_required(VERSION 3.21)

project(miryu
    VERSION 1.0.0
    DESCRIPTION "Lean AppImage desktop integration for KDE Plasma 6"
    LANGUAGES C CXX
)

set(PROJECT_HOMEPAGE_URL "https://github.com/evernightvista/miryu-app-launcher")

# ---- KDE/ECM boilerplate (provides KDEInstallDirs6, ki18n_install, etc.) ----
find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH})
include(KDEInstallDirs6)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDECMakeSettings)
include(FeatureSummary)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- find dependencies ----------------------------------------------------
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(KF6 6.0 REQUIRED COMPONENTS CoreAddons I18n Config KCMUtils WidgetsAddons)

# ---- libappimage + glib-2.0 -------------------------------------------------
# Miryu links the *system* libappimage (no FetchContent bundling).
#
# Discovery uses a three-level fallback chain so the build keeps working when
# pkg-config is missing or the distro does not ship a .pc file:
#   1. pkg-config via PkgConfig (canonical path)
#   2. find_path / find_library (native CMake)
#   3. FATAL_ERROR with a one-line install hint
# The result is always exposed as the same imported targets (PkgConfig::APPIMAGE
# and PkgConfig::GLIB) so src/core/CMakeLists.txt never has to care which path
# actually found them.

find_package(PkgConfig QUIET)

# --- libappimage ---
if(PKG_CONFIG_FOUND)
    pkg_check_modules(APPIMAGE QUIET IMPORTED_TARGET appimage>=1.0)
endif()
if(NOT APPIMAGE_FOUND)
    find_path(APPIMAGE_INCLUDE_DIR
        NAMES appimage/appimage.h
        DOC "Path to libappimage headers (appimage/appimage.h)"
    )
    find_library(APPIMAGE_LIBRARY
        NAMES appimage libappimage
        DOC "Path to libappimage shared/static library"
    )
    if(APPIMAGE_INCLUDE_DIR AND APPIMAGE_LIBRARY)
        set(APPIMAGE_FOUND TRUE)
        add_library(PkgConfig::APPIMAGE UNKNOWN IMPORTED)
        set_target_properties(PkgConfig::APPIMAGE PROPERTIES
            IMPORTED_LOCATION "${APPIMAGE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${APPIMAGE_INCLUDE_DIR}"
        )
    else()
        set(APPIMAGE_FOUND FALSE)
    endif()
endif()

# --- libappimage_shared (low-level helpers) ---
# Functions such as appimage_type2_digest_md5, appimage_hexlify and
# appimage_get_elf_section_offset_and_length live in a separate static archive
# (libappimage_shared.a) that is NOT referenced by libappimage.pc. Without it
# every consumer of miryu-core fails at link time with undefined references.
find_library(APPIMAGE_SHARED_LIBRARY
    NAMES appimage_shared libappimage_shared
    DOC "Path to libappimage_shared static library (ELF/MD5/hexlify helpers)"
)

# --- glib-2.0 ---
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GLIB QUIET IMPORTED_TARGET glib-2.0>=2.40)
endif()
if(NOT GLIB_FOUND)
    find_path(GLIB_INCLUDE_DIR
        NAMES glib.h
        PATH_SUFFIXES glib-2.0
        DOC "Path to glib-2.0 headers (glib.h)"
    )
    # glib installs glibconfig.h under the architecture-specific directory
    find_path(GLIBCONFIG_INCLUDE_DIR
        NAMES glibconfig.h
        PATH_SUFFIXES glib-2.0/include lib/glib-2.0/include lib64/glib-2.0/include
        DOC "Path to glibconfig.h"
    )
    find_library(GLIB_LIBRARY
        NAMES glib-2.0 libglib-2.0
        DOC "Path to libglib-2.0 shared/static library"
    )
    if(GLIB_INCLUDE_DIR AND GLIBCONFIG_INCLUDE_DIR AND GLIB_LIBRARY)
        set(GLIB_FOUND TRUE)
        add_library(PkgConfig::GLIB UNKNOWN IMPORTED)
        set_target_properties(PkgConfig::GLIB PROPERTIES
            IMPORTED_LOCATION "${GLIB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GLIB_INCLUDE_DIR};${GLIBCONFIG_INCLUDE_DIR}"
        )
    else()
        set(GLIB_FOUND FALSE)
    endif()
endif()

if(NOT APPIMAGE_FOUND)
    message(FATAL_ERROR
        "libappimage (>= 1.0) was not found.\n"
        "Install the development package, e.g.:\n"
        "  Fedora/RHEL:  sudo dnf install libappimage-devel\n"
        "  Debian/Ubuntu: sudo apt install libappimage-dev\n"
        "Or pass -DAPPIMAGE_INCLUDE_DIR=... -DAPPIMAGE_LIBRARY=... to cmake.\n"
        "miryu-core cannot be built without libappimage."
    )
endif()
if(NOT APPIMAGE_SHARED_LIBRARY)
    message(FATAL_ERROR
        "libappimage_shared.a was not found.\n"
        "It ships in the same development package as libappimage.\n"
        "  Fedora/RHEL:  sudo dnf install libappimage-devel\n"
        "  Debian/Ubuntu: sudo apt install libappimage-dev\n"
        "Or pass -DAPPIMAGE_SHARED_LIBRARY=/path/to/libappimage_shared.a to cmake.\n"
        "miryu-core needs it for appimage_type2_digest_md5 / hexlify / ELF helpers."
    )
endif()
if(NOT GLIB_FOUND)
    message(FATAL_ERROR
        "glib-2.0 (>= 2.40) was not found.\n"
        "Install the development package, e.g.:\n"
        "  Fedora/RHEL:  sudo dnf install glib2-devel\n"
        "  Debian/Ubuntu: sudo apt install libglib2.0-dev\n"
        "Or pass -DGLIB_INCLUDE_DIR=... -DGLIBCONFIG_INCLUDE_DIR=... "
        "-DGLIB_LIBRARY=... to cmake."
    )
endif()

option(MIRYU_BUILD_KCM    "Build the KDE System Settings module" ON)
option(MIRYU_BUILD_DAEMON "Build the miryud directory watcher"  ON)
option(MIRYU_BUILD_CLI    "Build the miryu-app-launcher-cli command line tool" ON)

# expose build-time paths to the C++ code
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/miryu-config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config/miryu-config.h"
    @ONLY
)

add_subdirectory(src/core)
add_subdirectory(src/launcher)

if(MIRYU_BUILD_CLI)
    add_subdirectory(src/cli)
endif()
if(MIRYU_BUILD_DAEMON)
    add_subdirectory(src/daemon)
endif()
if(MIRYU_BUILD_KCM)
    add_subdirectory(src/kcm)
endif()

# ---- data resources -------------------------------------------------------
# The launcher desktop entry: registers miryu as a handler for AppImage MIME
# types. NoDisplay because it is not an app you launch from the menu directly.
# The Name is "Miryu App Launcher" so that KDE Plasma 6's file association
# dialog shows the full product name rather than just "Miryu".
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/applications/miryu-app-launcher.desktop"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/applications/miryu-app-launcher-cli.desktop"
    DESTINATION ${KDE_INSTALL_APPDIR})

# AppImage MIME type definition (in case the distro doesn't ship one).
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/mime/packages/appimage.xml"
    DESTINATION ${KDE_INSTALL_MIMEDIR})

# No standalone icon files are packaged. The icon name "miryu-app-launcher"
# is used in all desktop files and KCM metadata; the actual icon for
# integrated AppImages is extracted from each AppImage's payload at
# integration time (see extractAndInstallIcons in miryu.cpp).

# user systemd unit for the optional daemon
if(MIRYU_BUILD_DAEMON)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/data/systemd/miryud.service.in"
        "${CMAKE_CURRENT_BINARY_DIR}/data/systemd/miryud.service"
        @ONLY
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/data/systemd/miryud.service"
        DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()

# ---- translations (gettext/PO via msgfmt) ----------------------------------
# Manually compile each .po file to .mo and install it. This is more reliable
# than ki18n_install(), which can silently produce .mo files with the wrong
# domain name when the source tree stores flat po/<lang>.po files.
find_program(MSGFMT_EXECUTABLE msgfmt)
if(MSGFMT_EXECUTABLE)
    file(GLOB MIRYU_PO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/po/*.po")
    set(MIRYU_MO_FILES)
    foreach(po_file ${MIRYU_PO_FILES})
        get_filename_component(lang "${po_file}" NAME_WE)
        set(mo_file "${CMAKE_CURRENT_BINARY_DIR}/locale/${lang}/LC_MESSAGES/miryu-app-launcher.mo")
        add_custom_command(
            OUTPUT "${mo_file}"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${CMAKE_CURRENT_BINARY_DIR}/locale/${lang}/LC_MESSAGES"
            COMMAND ${MSGFMT_EXECUTABLE} "${po_file}" -o "${mo_file}"
            DEPENDS "${po_file}"
            COMMENT "Compiling ${lang} translation for miryu-app-launcher"
        )
        list(APPEND MIRYU_MO_FILES "${mo_file}")
        install(FILES "${mo_file}"
            DESTINATION "${KDE_INSTALL_LOCALEDIR}/${lang}/LC_MESSAGES")
    endforeach()
    add_custom_target(miryu-translations ALL DEPENDS ${MIRYU_MO_FILES})
else()
    message(WARNING "msgfmt not found — translations will not be installed. "
                    "Install gettext to enable translation support.")
endif()

# NOTE: feature_summary's WHAT arg does not accept ALL together with
# FATAL_ON_MISSING_REQUIRED_PACKAGE (CMake parses the latter as another WHAT
# value). Required-package failures are already FATAL_ERROR above via
# find_package(REQUIRED); keep this informational only.
feature_summary(WHAT ALL)
