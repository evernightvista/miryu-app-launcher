// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Miryu App Launcher contributors

#include "kcm_miryu.h"

#include "config.h"
#include "miryu-config.h"

#include <KAboutData>
#include <KLocalizedString>
#include <KPluginFactory>

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

K_PLUGIN_CLASS_WITH_JSON(KcmMiryu, "kcm_miryu.json")

KcmMiryu::KcmMiryu(QObject* parent, const KPluginMetaData& data)
    : KCModule(parent, data)
{
    // NOTE: We intentionally do NOT call setApplicationDomain() here.
    // KCM plugins run inside the systemsettings process; changing the global
    // translation domain would break translation of systemsettings' own UI
    // (e.g. the unsaved-changes warning dialog). Instead, we use i18ndc() /
    // i18nd() with an explicit domain so our strings still translate correctly
    // without affecting the host process.

    auto* mainLayout = new QVBoxLayout(widget());
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* tabWidget = new QTabWidget(widget());

    // --- Settings Tab ---
    auto* settingsPage = new QWidget(tabWidget);
    auto* outer = new QVBoxLayout(settingsPage);
    outer->setContentsMargins(0, 0, 0, 0);

    // Integration group
    auto* integ = new QGroupBox(i18ndc("miryu-app-launcher", "@title:group", "Integration"), settingsPage);
    auto* f1 = new QFormLayout(integ);
    m_askToMove = new QCheckBox(i18ndc("miryu-app-launcher", "@option:check",
        "Ask whether to integrate an AppImage when it is opened"), integ);
    auto* destRow = new QHBoxLayout;
    m_destination = new QLineEdit(integ);
    auto* browse = new QPushButton(i18ndc("miryu-app-launcher", "@button", "Browse..."), integ);
    browse->setObjectName(QStringLiteral("browse"));
    destRow->addWidget(m_destination);
    destRow->addWidget(browse);
    f1->addRow(m_askToMove);
    f1->addRow(i18ndc("miryu-app-launcher", "@label:textbox", "Integration directory:"), destRow);
    outer->addWidget(integ);

    // Daemon group
    auto* dae = new QGroupBox(i18ndc("miryu-app-launcher", "@title:group", "Background Daemon"), settingsPage);
    auto* f2 = new QFormLayout(dae);
    m_enableDaemon = new QCheckBox(i18ndc("miryu-app-launcher", "@option:check",
        "Enable the miryud autointegration daemon"), dae);
    auto* dirsRow = new QHBoxLayout;
    m_watchDirs = new QListWidget(dae);
    m_watchDirs->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto* btnCol = new QVBoxLayout;
    auto* addDir = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                             i18ndc("miryu-app-launcher", "@button", "Add..."), dae);
    auto* removeDir = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                i18ndc("miryu-app-launcher", "@button", "Remove"), dae);
    btnCol->addWidget(addDir);
    btnCol->addWidget(removeDir);
    btnCol->addStretch();
    dirsRow->addWidget(m_watchDirs);
    dirsRow->addLayout(btnCol);
    m_monitorMounts = new QCheckBox(i18ndc("miryu-app-launcher", "@option:check",
        "Also watch <mount>/Applications on mounted filesystems"), dae);
    f2->addRow(m_enableDaemon);
    f2->addRow(i18ndc("miryu-app-launcher", "@label", "Additional directories to watch:"), dirsRow);
    f2->addRow(m_monitorMounts);
    outer->addWidget(dae);

    outer->addStretch();
    tabWidget->addTab(settingsPage, i18ndc("miryu-app-launcher", "@title:tab", "Settings"));

    // --- About Tab ---
    auto* aboutPage = new QWidget(tabWidget);
    auto* aboutLayout = new QVBoxLayout(aboutPage);

    // Header: Icon on left, Title + Copyright on right
    auto* headerRow = new QHBoxLayout();
    auto* iconLabel = new QLabel(aboutPage);
    iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("miryu-app-launcher")).pixmap(64, 64));
    headerRow->addWidget(iconLabel);

    auto* titleCol = new QVBoxLayout();
    auto* titleLabel = new QLabel(i18ndc("miryu-app-launcher", "@title", "Miryu App Launcher"), aboutPage);
    auto titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() * 1.5);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleCol->addWidget(titleLabel);

    auto* versionLabel = new QLabel(i18ndc("miryu-app-launcher", "@label", "Version %1", QStringLiteral(MIRYU_VERSION)), aboutPage);
    titleCol->addWidget(versionLabel);

    auto* copyrightLabel = new QLabel(i18ndc("miryu-app-launcher", "@info", "© 2027 KairikiFedora, © 2027 MiryuGaming"), aboutPage);
    titleCol->addWidget(copyrightLabel);

    headerRow->addLayout(titleCol);
    headerRow->addStretch();
    aboutLayout->addLayout(headerRow);

    aboutLayout->addSpacing(10);

    // License
    auto* licenseLabel = new QLabel(i18ndc("miryu-app-launcher", "@info", "Licensed under the MIT License."), aboutPage);
    aboutLayout->addWidget(licenseLabel);

    aboutLayout->addSpacing(20);

    // Contributors (on new line)
    auto* contributorsLabel = new QLabel(i18ndc("miryu-app-launcher", "@title:group", "Thanks to the following contributors:"), aboutPage);
    auto contributorsFont = contributorsLabel->font();
    contributorsFont.setBold(true);
    contributorsLabel->setFont(contributorsFont);
    aboutLayout->addWidget(contributorsLabel);

    aboutLayout->addSpacing(5);

    // Contributors arranged horizontally
    auto* contributorsRow = new QHBoxLayout();
    contributorsRow->setSpacing(20);

    // Helper to create clickable link labels
    auto createLinkLabel = [aboutPage](const QString& name, const QString& url) -> QLabel* {
        auto* label = new QLabel(aboutPage);
        label->setText(QStringLiteral("<a href=\"%1\" style=\"text-decoration: none;\">%2</a>").arg(url, name));
        label->setTextFormat(Qt::RichText);
        label->setTextInteractionFlags(Qt::TextBrowserInteraction);
        label->setOpenExternalLinks(false);
        QObject::connect(label, &QLabel::linkActivated, label, [url](const QString&) {
            QDesktopServices::openUrl(QUrl(url));
        });
        return label;
    };

    contributorsRow->addWidget(createLinkLabel(
        i18ndc("miryu-app-launcher", "@info:contributor", "MiryuGaming"),
        QStringLiteral("https://github.com/miryugaming")));
    contributorsRow->addWidget(createLinkLabel(
        i18ndc("miryu-app-launcher", "@info:contributor", "jtgg114514"),
        QStringLiteral("https://github.com/jtgg114514")));
    contributorsRow->addWidget(createLinkLabel(
        i18ndc("miryu-app-launcher", "@info:contributor", "FarnaHerry"),
        QStringLiteral("https://github.com/FarnaHerry")));
    contributorsRow->addStretch();
    aboutLayout->addLayout(contributorsRow);

    aboutLayout->addStretch();
    tabWidget->addTab(aboutPage, i18ndc("miryu-app-launcher", "@title:tab", "About"));

    mainLayout->addWidget(tabWidget);

    // connect changes so System Settings shows the "Apply" button
    auto mark = [this]() { markChanged(); };
    for (auto* cb : {m_askToMove, m_enableDaemon, m_monitorMounts}) {
        connect(cb, &QCheckBox::toggled, this, mark);
    }
    connect(m_destination, &QLineEdit::textChanged, this, mark);
    connect(m_watchDirs, &QListWidget::itemChanged, this, mark);
    connect(m_watchDirs, &QListWidget::itemSelectionChanged, this, mark);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const auto dir = QFileDialog::getExistingDirectory(
            widget(), i18ndc("miryu-app-launcher", "@title:window", "Select Integration Directory"),
            m_destination->text());
        if (!dir.isEmpty()) m_destination->setText(dir);
    });
    connect(addDir, &QPushButton::clicked, this, &KcmMiryu::addWatchDir);
    connect(removeDir, &QPushButton::clicked, this, &KcmMiryu::removeWatchDir);
}

KcmMiryu::~KcmMiryu() = default;

void KcmMiryu::load() {
    miryu::Config cfg;
    m_askToMove->setChecked(cfg.askToMove());
    m_destination->setText(cfg.destination());
    m_enableDaemon->setChecked(cfg.enableDaemon());
    m_monitorMounts->setChecked(cfg.monitorMountedFilesystems());
    m_watchDirs->clear();
    for (const auto& d : cfg.additionalDirectoriesToWatch())
        m_watchDirs->addItem(d);
    setNeedsSave(false);
}

void KcmMiryu::save() {
    miryu::Config cfg;
    cfg.setAskToMove(m_askToMove->isChecked());
    cfg.setDestination(m_destination->text());
    cfg.setEnableDaemon(m_enableDaemon->isChecked());
    cfg.setMonitorMountedFilesystems(m_monitorMounts->isChecked());
    QStringList dirs;
    for (int i = 0; i < m_watchDirs->count(); ++i)
        dirs << m_watchDirs->item(i)->text();
    cfg.setAdditionalDirectoriesToWatch(dirs);
    cfg.save();
    setNeedsSave(false);
}

void KcmMiryu::defaults() {
    miryu::Config cfg;
    cfg.defaults();
    cfg.save();
    load();
    markChanged();
}

void KcmMiryu::markChanged() { setNeedsSave(true); }

void KcmMiryu::addWatchDir() {
    const auto dir = QFileDialog::getExistingDirectory(
        widget(), i18ndc("miryu-app-launcher", "@title:window", "Select Directory to Watch"));
    if (!dir.isEmpty()) {
        m_watchDirs->addItem(dir);
        markChanged();
    }
}

void KcmMiryu::removeWatchDir() {
    for (auto* item : m_watchDirs->selectedItems())
        delete item;
    markChanged();
}

#include "kcm_miryu.moc"
