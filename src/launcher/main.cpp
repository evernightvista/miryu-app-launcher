// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Miryu App Launcher contributors
//
// miryu - the GUI launcher / MIME handler for AppImages.
//
// Invoked as:  miryu <path-to-AppImage> [args...]
// Behaviour mirrors AppImageLauncher's on-open flow but stays lean:
//   * no binfmt_misc, no preload bypass (so no exec recursion);
//   * integrates via the system libappimage;
//   * runs the AppImage with QProcess::startDetached (the AppImage runtime
//     self-mounts, exactly as a normal ELF would).

#include "miryu-config.h"
#include "config.h"
#include "miryu.h"

#include <KLocalizedString>
#include <KAboutData>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>

extern "C" {
#include <appimage/appimage.h>
}

namespace {

// Launch the AppImage, passing through any extra arguments. We chmod +x first
// (the file manager may not have done so). startDetached lets the AppImage
// runtime mount and run itself; because miryu is NOT a binfmt_misc handler,
// the kernel executes the AppImage ELF normally - no recursion.
bool runAppImage(const QString& path, const QStringList& args) {
    miryu::makeExecutable(path);
    QProcess p;
    p.setProgram(path);
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::ForwardedChannels);
    return p.startDetached();
}

bool isHeadless() {
    return qEnvironmentVariableIsEmpty("DISPLAY") &&
           qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
}

void warn(const QString& msg) {
    if (isHeadless())
        fprintf(stderr, "miryu: %s\n", msg.toUtf8().constData());
    else
        QMessageBox::warning(nullptr, i18n("Miryu"), msg);
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Set the gettext translation domain BEFORE any i18n() call. The KAboutData
    // block below already uses i18n(); if the domain is set afterwards, KI18n
    // looks up the catalog too late and translations are never loaded.
    KLocalizedString::setApplicationDomain("miryu-app-launcher");
    app.setApplicationName(QStringLiteral("miryu-app-launcher"));
    app.setApplicationVersion(QStringLiteral(MIRYU_VERSION));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("miryu-app-launcher")));

    KAboutData about(QStringLiteral("miryu-app-launcher"),
                     i18n("Miryu App Launcher"),
                     QStringLiteral(MIRYU_VERSION),
                     i18n("Lean AppImage desktop integration"),
                     KAboutLicense::GPL_V3); // TODO: align with project license
    about.addAuthor(i18n("Miryu contributors"));
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("Lean AppImage desktop integration for KDE Plasma 6"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
        i18n("Path to an AppImage (and optional arguments to pass to it)"),
        QStringLiteral("[file [args...]]"));
    parser.process(app);

    const auto positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        parser.showHelp(1);
    }

    const QString path = QFileInfo(positional.first()).absoluteFilePath();
    const QStringList extraArgs = positional.mid(1);

    if (!QFile::exists(path)) {
        warn(i18n("No such file: %1", path));
        return 1;
    }

    // Honor the standard "disable" switch and never intercept symlinks.
    if (qEnvironmentVariableIsSet("MIRYU_DISABLE") || QFileInfo(path).isSymLink())
        return runAppImage(path, extraArgs) ? 0 : 1;

    if (!miryu::isAppImage(path)) {
        warn(i18n("Not an AppImage: %1", path));
        return 1;
    }

    // AppImages spawned from inside another AppImage (mount point) must not be
    // re-integrated, and we honor the AppImage's own opt-out hints.
    if (path.startsWith(QStringLiteral("/tmp/.mount_")))
        return runAppImage(path, extraArgs) ? 0 : 1;
    if (appimage_shall_not_be_integrated(path.toUtf8().constData()) > 0)
        return runAppImage(path, extraArgs) ? 0 : 1;
    if (appimage_is_terminal_app(path.toUtf8().constData()) > 0)
        return runAppImage(path, extraArgs) ? 0 : 1;

    // Already integrated? Just run it.
    if (miryu::isRegistered(path)) {
        return runAppImage(path, extraArgs) ? 0 : 1;
    }

    const miryu::Config cfg;
    if (!cfg.askToMove()) {
        // User opted out of the prompt: run without integrating.
        return runAppImage(path, extraArgs) ? 0 : 1;
    }

    // No display to ask on (headless shell / scripted launch): never silently
    // integrate — fall back to a plain run.
    if (isHeadless())
        return runAppImage(path, extraArgs) ? 0 : 1;

    // The on-open integration prompt.
    QMessageBox box;
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(i18n("Miryu App Launcher"));
    box.setText(i18n("This AppImage has not been integrated yet.\n\n%1\n\n"
                     "What would you like to do?").arg(path));
    box.setInformativeText(
        i18n("<b>Integrate and Run</b> — move the AppImage to %1 and add it to the application menu.\n"
             "<b>Run Once</b> — just launch the AppImage now without moving or modifying it.")
            .arg(cfg.destination()));
    auto* integrateAndRun = box.addButton(i18n("Integrate and Run"), QMessageBox::AcceptRole);
    auto* runOnce          = box.addButton(i18n("Run Once"),          QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    // Default to the non-destructive choice: a stray Enter must never
    // silently move/integrate the AppImage.
    box.setDefaultButton(runOnce);
    box.exec();

    const auto* clicked = box.clickedButton();
    if (clicked == runOnce)
        return runAppImage(path, extraArgs) ? 0 : 1;
    if (clicked != integrateAndRun)
        return 0; // cancelled

    const QString integratedPath = miryu::buildIntegratedPath(path);
    const auto result = miryu::integrateAppImage(path, integratedPath, /*interactive=*/true);
    miryu::refreshCaches();

    if (result == miryu::IntegrationResult::Failed) {
        warn(i18n("Failed to integrate %1.", path));
        return 1;
    }
    const QString runPath = (result == miryu::IntegrationResult::Ok) ? integratedPath : path;
    return runAppImage(runPath, extraArgs) ? 0 : 1;
}
