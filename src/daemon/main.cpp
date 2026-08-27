// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Miryu App Launcher contributors
//
// miryud - a lean autointegration daemon. It watches the integration
// directory (and any configured extra directories) and registers new
// AppImages into the desktop as they appear, without a GUI.

#include "miryu-config.h"
#include "config.h"
#include "miryu.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

extern "C" {
#include <appimage/appimage.h>
}

namespace {

// Real mount points of block devices, used when MonitorMountedFilesystems is on.
QSet<QString> mountedAppLocations() {
    QSet<QString> out;
    out.insert(QStringLiteral("/Applications"));
    QFile f(QStringLiteral("/proc/mounts"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    static const QSet<QString> validFs = {
        QStringLiteral("ext2"), QStringLiteral("ext3"), QStringLiteral("ext4"),
        QStringLiteral("ntfs"), QStringLiteral("vfat"), QStringLiteral("btrfs")};
    QTextStream in(&f);
    while (!in.atEnd()) {
        const auto parts = in.readLine().split(QLatin1Char(' '));
        if (parts.size() < 3) continue;
        const auto& device = parts[0];
        const auto& mountPoint = parts[1];
        const auto& fsType = parts[2];
        if (!device.startsWith(QLatin1String("/dev/"))) continue;
        if (mountPoint == QStringLiteral("/")) continue;
        if (!validFs.contains(fsType)) continue;
        out.insert(mountPoint + QStringLiteral("/Applications"));
    }
    return out;
}

// Directories the daemon should monitor.
QStringList watchedDirectories(const miryu::Config& cfg) {
    QSet<QString> dirs;
    dirs.insert(cfg.destination());
    for (const auto& d : cfg.additionalDirectoriesToWatch())
        if (QDir(d).exists()) dirs.insert(d);
    if (cfg.monitorMountedFilesystems())
        dirs.unite(mountedAppLocations());
    return dirs.values();
}

void integrateIfAppImage(const QString& path) {
    const QFileInfo info(path);
    if (!info.isFile() || !info.fileName().contains(QLatin1Char('.')))
        return; // ignore temp files without an extension
    if (!miryu::isAppImage(path))
        return;
    if (appimage_shall_not_be_integrated(path.toUtf8().constData()) > 0)
        return;
    if (miryu::isRegistered(path))
        return;
    qInfo() << "miryud: integrating" << path;
    if (miryu::installDesktopFileAndIcons(path))
        miryu::refreshCaches();
    else
        qWarning() << "miryud: failed to integrate" << path;
}

// Scan a directory once for AppImages (used on startup and after bulk changes).
void scanDirectory(const QString& dir) {
    QDir d(dir);
    if (!d.exists()) return;
    for (const auto& entry : d.entryInfoList(QDir::Files)) {
        if (miryu::isAppImage(entry.absoluteFilePath()))
            integrateIfAppImage(entry.absoluteFilePath());
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // Translation domain must be set before the first i18n() call (the
    // KAboutData below); otherwise KI18n cannot find the .mo catalogs.
    KLocalizedString::setApplicationDomain("miryu-app-launcher");
    app.setApplicationName(QStringLiteral("miryud"));
    app.setApplicationVersion(QStringLiteral(MIRYU_VERSION));

    KAboutData about(QStringLiteral("miryud"),
                     i18n("Miryu Daemon"),
                     QStringLiteral(MIRYU_VERSION),
                     i18n("Background AppImage autointegration"),
                     KAboutLicense::GPL_V3);
    KAboutData::setApplicationData(about);

    miryu::Config cfg;
    const auto dirs = watchedDirectories(cfg);
    qInfo() << "miryud: watching" << dirs;

    QFileSystemWatcher watcher;
    for (const auto& dir : dirs) {
        QDir().mkpath(dir);
        watcher.addPath(dir);
        scanDirectory(dir);
    }

    // File system watchers can miss events on some filesystems; a periodic
    // rescan is a cheap safety net. Every 60s.
    QTimer rescanTimer;
    rescanTimer.setInterval(60 * 1000);
    rescanTimer.start();
    QObject::connect(&rescanTimer, &QTimer::timeout, [&dirs]() {
        for (const auto& dir : dirs) scanDirectory(dir);
    });

    QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged,
        [&dirs](const QString& changed) {
            qInfo() << "miryud: change in" << changed;
            scanDirectory(changed);
        });
    QObject::connect(&watcher, &QFileSystemWatcher::fileChanged,
        &integrateIfAppImage);

    return app.exec();
}
