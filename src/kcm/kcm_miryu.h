// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Miryu App Launcher contributors
//
// kcm_miryu - the KDE System Settings module for Miryu. All configuration is
// fully accessible here; there is no separate settings dialog.

#pragma once

#include <KCModule>
#include <KPluginMetaData>

class QCheckBox;
class QLineEdit;
class QListWidget;

class KcmMiryu : public KCModule {
    Q_OBJECT
public:
    explicit KcmMiryu(QObject* parent, const KPluginMetaData& data);
    ~KcmMiryu() override;

public Q_SLOTS:
    void save() override;
    void load() override;
    void defaults() override;

private Q_SLOTS:
    void markChanged();
    void addWatchDir();
    void removeWatchDir();

private:
    QCheckBox*  m_askToMove = nullptr;
    QLineEdit*  m_destination = nullptr;
    QCheckBox*  m_enableDaemon = nullptr;
    QListWidget* m_watchDirs = nullptr;
    QCheckBox*  m_monitorMounts = nullptr;
};
