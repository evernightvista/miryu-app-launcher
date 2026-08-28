// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Miryu App Launcher contributors
//
// miryu-app-launcher-cli - command line tool.
//   miryu-app-launcher-cli integrate    <path...>   move into the integration dir + register
//   miryu-app-launcher-cli unintegrate  <path...>   unregister (and leave the file in place)
//   miryu-app-launcher-cli list                     list integrated AppImages
//   miryu-app-launcher-cli cleanup                   drop desktop files whose AppImage is gone

#include "miryu-config.h"
#include "config.h"
#include "miryu.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTextStream>

extern "C" {
#include <glib.h>
}

namespace {
QTextStream out(stdout);
QTextStream err(stderr);

int doIntegrate(const QStringList& args) {
    if (args.isEmpty()) {
        err << i18n("Usage: miryu-app-launcher-cli integrate <path...>") << Qt::endl;
        return 1;
    }
    int failures = 0;
    miryu::Config cfg;
    QDir().mkpath(cfg.destination());
    for (QString path : args) {
        path = QFileInfo(path).absoluteFilePath();
        if (!QFileInfo(path).isFile()) {
            err << i18n("Not a file, skipping: %1", path) << Qt::endl;
            ++failures; continue;
        }
        if (!miryu::isAppImage(path)) {
            err << i18n("Not an AppImage, skipping: %1", path) << Qt::endl;
            ++failures; continue;
        }
        const QString target = miryu::buildIntegratedPath(path);
        if (QFileInfo(path).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
            if (QFile::exists(target)) QFile::remove(target);
            if (!QFile::rename(path, target) && !QFile::copy(path, target)) {
                err << i18n("Could not move %1 into the integration directory", path) << Qt::endl;
                ++failures; continue;
            }
        }
        if (!miryu::installDesktopFileAndIcons(target)) {
            err << i18n("Failed to register %1", target) << Qt::endl;
            ++failures; continue;
        }
        // installDesktopFileAndIcons() already calls refreshCaches() synchronously.
        out << i18n("Integrated: %1", target) << Qt::endl;
    }
    return failures ? 2 : 0;
}

int doUnintegrate(const QStringList& args) {
    if (args.isEmpty()) {
        err << i18n("Usage: miryu-app-launcher-cli unintegrate <path...>") << Qt::endl;
        return 1;
    }
    int failures = 0;
    for (const auto& path : args) {
        const QString abs = QFileInfo(path).absoluteFilePath();
        // unregisterAppImage() already calls refreshCaches() synchronously.
        if (miryu::unregisterAppImage(abs)) {
            out << i18n("Unintegrated: %1", abs) << Qt::endl;
        } else {
            err << i18n("Failed to unintegrate %1", abs) << Qt::endl;
            ++failures;
        }
    }
    return failures ? 2 : 0;
}

// List registered AppImages by scanning the user applications dir for the
// libappimage-generated desktop files (appimagekit_*.desktop).
int doList() {
    const auto appsDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                         + QStringLiteral("/applications");
    QDirIterator it(appsDir, QStringList() << QStringLiteral("appimagekit_*.desktop"));
    QJsonArray arr;
    while (it.hasNext()) {
        const auto path = it.next();
        GKeyFile* kf = g_key_file_new();
        GError* e = nullptr;
        if (!g_key_file_load_from_file(kf, path.toUtf8().constData(), G_KEY_FILE_NONE, &e)) {
            g_clear_error(&e); g_key_file_free(kf); continue;
        }
        auto* exec = g_key_file_get_string(kf, G_KEY_FILE_DESKTOP_GROUP,
                                           G_KEY_FILE_DESKTOP_KEY_EXEC, nullptr);
        auto* name = g_key_file_get_string(kf, G_KEY_FILE_DESKTOP_GROUP,
                                           G_KEY_FILE_DESKTOP_KEY_NAME, nullptr);
        QJsonObject o;
        o[QStringLiteral("desktop")] = path;
        o[QStringLiteral("name")] = QString::fromUtf8(name ? name : "");
        o[QStringLiteral("exec")]  = QString::fromUtf8(exec ? exec : "");
        arr.append(o);
        g_free(exec); g_free(name); g_key_file_free(kf);
    }
    out << QJsonDocument(arr).toJson(QJsonDocument::Indented);
    return 0;
}

// Remove desktop files whose backing AppImage no longer exists.
int doCleanup() {
    const auto appsDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                         + QStringLiteral("/applications");
    QDirIterator it(appsDir, QStringList() << QStringLiteral("appimagekit_*.desktop"));
    int removed = 0;
    while (it.hasNext()) {
        const auto path = it.next();
        GKeyFile* kf = g_key_file_new();
        if (!g_key_file_load_from_file(kf, path.toUtf8().constData(), G_KEY_FILE_NONE, nullptr)) {
            g_key_file_free(kf); continue;
        }
        auto* tryExec = g_key_file_get_string(kf, G_KEY_FILE_DESKTOP_GROUP,
                                               G_KEY_FILE_DESKTOP_KEY_TRY_EXEC, nullptr);
        const QString appPath = QString::fromUtf8(tryExec ? tryExec : "");
        g_free(tryExec); g_key_file_free(kf);
        if (!appPath.isEmpty() && !QFile::exists(appPath)) {
            if (QFile::remove(path)) {
                out << i18n("Removed stale entry: %1", path) << Qt::endl;
                ++removed;
            }
        }
    }
    miryu::refreshCaches();
    out << i18np("Removed %1 stale entry.", "Removed %1 stale entries.", removed) << Qt::endl;
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // Translation domain must be set before the first i18n() call (the
    // KAboutData below); otherwise KI18n cannot find the .mo catalogs.
    KLocalizedString::setApplicationDomain("miryu-app-launcher");
    app.setApplicationName(QStringLiteral("miryu-app-launcher-cli"));
    app.setApplicationVersion(QStringLiteral(MIRYU_VERSION));

    KAboutData about(QStringLiteral("miryu-app-launcher-cli"),
                     i18n("Miryu CLI"),
                     QStringLiteral(MIRYU_VERSION),
                     i18n("Command line AppImage integration helper"),
                     KAboutLicense::GPL_V3);
    KAboutData::setApplicationData(about);

    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty() || args.first() == QStringLiteral("--help") || args.first() == QStringLiteral("-h")) {
        out << i18n("Usage: miryu-app-launcher-cli <command> [args...]\n\nCommands:\n"
                    "  integrate   <path...>   Integrate AppImages into the desktop\n"
                    "  unintegrate <path...>   Remove AppImage desktop integration\n"
                    "  list                    List integrated AppImages (JSON)\n"
                    "  cleanup                 Remove stale desktop entries") << Qt::endl;
        return args.isEmpty() ? 1 : 0;
    }
    const QString cmd = args.first();
    const QStringList rest = args.mid(1);

    if (cmd == QStringLiteral("integrate"))    return doIntegrate(rest);
    if (cmd == QStringLiteral("unintegrate")) return doUnintegrate(rest);
    if (cmd == QStringLiteral("list"))        return doList();
    if (cmd == QStringLiteral("cleanup"))     return doCleanup();

    err << i18n("Unknown command: %1", cmd) << Qt::endl;
    return 1;
}
