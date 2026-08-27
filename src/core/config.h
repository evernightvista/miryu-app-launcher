// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Miryu App Launcher contributors
//
// Shared configuration, backed by KConfig XT (miryu.kcfg → MiryuSettings).
// The same settings are read/written by the binfmt interpreter, the daemon,
// the CLI and the KDE System Settings KCM, so there is a single source of truth.
//
// Defaults are defined in src/core/miryu.kcfg (KConfig XT format).
// The MiryuSettings singleton is generated from miryu.kcfg + miryu.kcfgc.

#pragma once

#include <memory>
#include <QString>
#include <QStringList>

namespace miryu {

// Default location integrated AppImages are moved into.
// Kept for backward compatibility with code that needs a compile-time default.
// The authoritative default lives in miryu.kcfg.
inline QString defaultDestination() {
    return QString::fromUtf8(qgetenv("HOME")) + QStringLiteral("/.miryu-app");
}

// Thin wrapper around the generated MiryuSettings (KConfigSkeleton).
// Adds tilde-expansion for the destination path and keeps call sites stable.
class Config {
public:
    Config();

    // --- [Miryu] group ----------------------------------------------------
    // If false, AppImages are run directly without ever asking to integrate.
    bool askToMove() const;
    void setAskToMove(bool v);

    // Directory integrated AppImages are stored in (~/.miryu-app by default).
    QString destination() const;
    void setDestination(const QString& v);

    // Whether the miryud autointegration daemon should be (auto)started.
    bool enableDaemon() const;
    void setEnableDaemon(bool v);

    // --- [Daemon] group ---------------------------------------------------
    // Extra directories to watch for AppImages (in addition to the destination).
    QStringList additionalDirectoriesToWatch() const;
    void setAdditionalDirectoriesToWatch(const QStringList& v);

    // Scan mounted real filesystems for an <mount>/Applications directory.
    bool monitorMountedFilesystems() const;
    void setMonitorMountedFilesystems(bool v);

    // Persist changes to disk.
    void save();

    // Reset all settings to their kcfg-defined defaults.
    void defaults();

private:
    struct Private;
    // shared_ptr so the KConfigSkeleton-backed members live as long as needed
    // and the object is cheaply copyable (used from the KCM).
    std::shared_ptr<Private> d;
};

} // namespace miryu
