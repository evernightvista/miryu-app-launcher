// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Miryu App Launcher contributors

#include "config.h"
#include "miryusettings.h"

#include <KConfigSkeleton>

#include <QDir>

namespace miryu {

// Replace a leading ~ (or ~/) with the real home directory.
static QString expandTilde(QString path) {
    if ((path.size() == 1 && path[0] == QLatin1Char('~')) ||
        (path.size() >= 2 && path.startsWith(QLatin1String("~/")))) {
        path.remove(0, 1);
        path.prepend(QDir::homePath());
    }
    return path;
}

struct Config::Private {
    MiryuSettings* settings;
    Private() : settings(MiryuSettings::self()) {}
};

Config::Config() : d(std::make_shared<Private>()) {}

// --- [Miryu] --------------------------------------------------------------
bool Config::askToMove() const {
    return d->settings->askToMove();
}

void Config::setAskToMove(bool v) {
    d->settings->setAskToMove(v);
}

QString Config::destination() const {
    return expandTilde(d->settings->destination());
}

void Config::setDestination(const QString& v) {
    d->settings->setDestination(v);
}

bool Config::enableDaemon() const {
    return d->settings->enableDaemon();
}

void Config::setEnableDaemon(bool v) {
    d->settings->setEnableDaemon(v);
}

// --- [Daemon] -------------------------------------------------------------
QStringList Config::additionalDirectoriesToWatch() const {
    return d->settings->additionalDirectories();
}

void Config::setAdditionalDirectoriesToWatch(const QStringList& v) {
    d->settings->setAdditionalDirectories(v);
}

bool Config::monitorMountedFilesystems() const {
    return d->settings->monitorMountedFilesystems();
}

void Config::setMonitorMountedFilesystems(bool v) {
    d->settings->setMonitorMountedFilesystems(v);
}

void Config::save() {
    d->settings->save();
}

void Config::defaults() {
    d->settings->setDefaults();
}

} // namespace miryu
