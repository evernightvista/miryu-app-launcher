// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Miryu App Launcher contributors
//
// Lean AppImage desktop-integration core. Unlike AppImageLauncher, Miryu does
// NOT bundle libappimage/AppImageUpdate/squashfuse: it links the system
// libappimage C API directly. It also drops the 32-bit preload binfmt-bypass,
// the Qt update UI and the Lite edition - the binfmt interpreter just execs the
// AppImage after (optionally) integrating it.

#pragma once

#include <QDir>
#include <QString>

namespace miryu {

enum class IntegrationResult {
    Failed = 0,
    Ok,
    Aborted,
};

// True if the file is an AppImage (type 1 or 2).
bool isAppImage(const QString& path);

// Make a file executable (chmod +x for the owner). No-op if already executable.
bool makeExecutable(const QString& path);

// Read the embedded (or computed) MD5 digest used to build content-aware names.
QString appImageDigest(const QString& path);

// Build the canonical path in the integration directory for the given AppImage,
// appending a "_<digest>" suffix when available.
QString buildIntegratedPath(const QString& pathToAppImage);

// True if the AppImage already has a desktop file registered in the system.
bool isRegistered(const QString& pathToAppImage);

// Register the AppImage's desktop entry and icons (via libappimage) and patch
// the desktop file with a Miryu "Remove" action pointing at the CLI. Returns
// false on failure.
bool installDesktopFileAndIcons(const QString& pathToAppImage);

// Undo the desktop registration, remove related icons, and delete the
// integrated AppImage file itself. Returns false on failure.
bool unregisterAppImage(const QString& pathToAppImage);

// Move an AppImage into the integration directory, then install its desktop
// file/icons. On name collision a graphical confirmation is shown (unless the
// caller is the daemon/CLI, which overwrite).
IntegrationResult integrateAppImage(const QString& pathToAppImage,
                                    const QString& pathToIntegratedAppImage,
                                    bool interactive);

// Refresh desktop/icon/mime caches (best-effort, used by the daemon).
void refreshCaches();

} // namespace miryu
