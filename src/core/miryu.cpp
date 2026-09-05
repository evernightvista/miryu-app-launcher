// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Miryu App Launcher contributors

#include "miryu.h"
#include "config.h"
#include "miryu-config.h"
#include "action-translations.h"

#include <KLocalizedString>

#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QList>
#include <QMessageBox>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <vector>

extern "C" {
#include <appimage/appimage.h>
#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>
}

namespace miryu {

namespace {
void gKeyFileFree(GKeyFile* p) { if (p) g_key_file_free(p); }
void gErrorFree(GError* p)    { if (p) g_error_free(p); }

// libappimage leaves the desktop file in the user applications dir; locate it so
// we can patch Miryu-specific entries into it.
QString registeredDesktopPath(const QString& pathToAppImage) {
    const auto* raw = appimage_registered_desktop_file_path(
        pathToAppImage.toUtf8().constData(), nullptr, false);
    if (!raw) return {};
    QString out = QString::fromUtf8(raw);
    // the function may (depending on version) return a path without checking it
    // actually exists, so the caller must verify.
    return out;
}

// Read the Icon= value from a desktop file.
QString readDesktopIcon(const QString& desktopPath) {
    std::shared_ptr<GKeyFile> kf(g_key_file_new(), gKeyFileFree);
    std::shared_ptr<GError*> err(nullptr, gErrorFree);
    if (!g_key_file_load_from_file(kf.get(), desktopPath.toUtf8().constData(),
                                   G_KEY_FILE_NONE, err.get()))
        return {};
    gchar* icon = g_key_file_get_string(kf.get(), G_KEY_FILE_DESKTOP_GROUP,
                                         G_KEY_FILE_DESKTOP_KEY_ICON, err.get());
    if (!icon) return {};
    QString out = QString::fromUtf8(icon);
    g_free(icon);
    return out;
}

// Read the TryExec= value from a desktop file (the path to the AppImage binary).
QString readDesktopTryExec(const QString& desktopPath) {
    std::shared_ptr<GKeyFile> kf(g_key_file_new(), gKeyFileFree);
    std::shared_ptr<GError*> err(nullptr, gErrorFree);
    if (!g_key_file_load_from_file(kf.get(), desktopPath.toUtf8().constData(),
                                   G_KEY_FILE_NONE, err.get()))
        return {};
    gchar* tryExec = g_key_file_get_string(kf.get(), G_KEY_FILE_DESKTOP_GROUP,
                                            G_KEY_FILE_DESKTOP_KEY_TRY_EXEC, err.get());
    if (!tryExec) return {};
    QString out = QString::fromUtf8(tryExec);
    g_free(tryExec);
    return out;
}

// Determine the size bucket from an icon file's parent directory name.
// e.g. "256x256" -> "256x256", "scalable" -> "scalable", "apps" -> look at grandparent.
QString iconSizeBucket(const QFileInfo& fi) {
    const QString parentDir = fi.dir().dirName();
    if (parentDir == QStringLiteral("scalable") ||
        parentDir == QStringLiteral("symbolic")) {
        return parentDir;
    } else if (parentDir.contains(QLatin1Char('x'))) {
        return parentDir; // e.g. "256x256"
    } else {
        // Fallback: try grandparent (icons/hicolor/256x256/apps/file.png)
        const QString grandParent = fi.dir().path();
        const QString gpName = QFileInfo(grandParent).dir().dirName();
        if (gpName.contains(QLatin1Char('x')) || gpName == QStringLiteral("scalable"))
            return gpName;
        return QStringLiteral("scalable"); // last resort
    }
}

// Determine icon size from image dimensions for files not in size-categorized dirs
QString iconSizeFromImage(const QString& imagePath) {
    QImageReader reader(imagePath);
    if (!reader.canRead()) {
        return QStringLiteral("scalable");
    }
    QSize size = reader.size();
    if (size.isEmpty()) {
        // Try reading the image if size metadata not available
        QImage img;
        if (img.load(imagePath)) {
            size = img.size();
        }
    }
    if (size.isEmpty()) {
        return QStringLiteral("scalable");
    }
    int maxDim = qMax(size.width(), size.height());
    // Round to nearest standard icon size
    const QList<int> stdSizes = {16, 22, 24, 32, 48, 64, 128, 256, 512};
    int best = 128;
    for (int s : stdSizes) {
        if (maxDim <= s) {
            best = s;
            break;
        }
        best = 512;
    }
    return QStringLiteral("%1x%1").arg(best);
}

// Set the Icon= key in a desktop file (GKeyFile on disk). Used when the
// registered desktop file is missing an Icon= entry that we discovered
// by extracting the AppImage payload.
bool setDesktopIcon(const QString& desktopPath, const QString& iconName) {
    std::shared_ptr<GKeyFile> kf(g_key_file_new(), gKeyFileFree);
    std::shared_ptr<GError*> err(nullptr, gErrorFree);
    const auto flags = GKeyFileFlags(G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS);
    if (!g_key_file_load_from_file(kf.get(), desktopPath.toUtf8().constData(), flags, err.get()))
        return false;
    g_key_file_set_string(kf.get(), G_KEY_FILE_DESKTOP_GROUP,
                          G_KEY_FILE_DESKTOP_KEY_ICON, iconName.toUtf8().constData());
    return g_key_file_save_to_file(kf.get(), desktopPath.toUtf8().constData(), nullptr);
}

// Check if an icon name looks like a file path rather than a theme icon name
bool isIconPath(const QString& iconName) {
    return iconName.startsWith(QLatin1Char('/')) ||
           iconName.startsWith(QLatin1Char('.')) ||
           iconName.contains(QLatin1Char('/')) ||
           iconName.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ||
           iconName.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive) ||
           iconName.endsWith(QStringLiteral(".svgz"), Qt::CaseInsensitive) ||
           iconName.endsWith(QStringLiteral(".xpm"), Qt::CaseInsensitive) ||
           iconName.endsWith(QStringLiteral(".ico"), Qt::CaseInsensitive);
}

// Extract the base icon name (without path/extension) from an icon reference
QString iconBaseName(const QString& iconRef) {
    QString name = iconRef;
    // Remove path components
    int slash = name.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) {
        name = name.mid(slash + 1);
    }
    // Remove extension
    int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        // Make sure it's an image extension before stripping
        QString ext = name.mid(dot + 1).toLower();
        if (ext == QStringLiteral("png") || ext == QStringLiteral("svg") ||
            ext == QStringLiteral("svgz") || ext == QStringLiteral("xpm") ||
            ext == QStringLiteral("ico")) {
            name = name.left(dot);
        }
    }
    return name;
}

// Find the Icon= value from the AppImage's internal .desktop file by
// extracting the payload and scanning common locations.
QString findIconNameFromPayload(const QString& squashfsRoot) {
    // .desktop files inside an AppImage are typically at the root or in
    // usr/share/applications/.
    const QStringList desktopSearchDirs = {
        squashfsRoot,
        squashfsRoot + QStringLiteral("/usr/share/applications"),
        squashfsRoot + QStringLiteral("/.local/share/applications"),
    };
    for (const auto& dir : desktopSearchDirs) {
        QDir d(dir);
        if (!d.exists()) continue;
        const auto files = d.entryInfoList(QStringList() << QStringLiteral("*.desktop"), QDir::Files);
        for (const auto& df : files) {
            const QString icon = readDesktopIcon(df.absoluteFilePath());
            if (!icon.isEmpty()) {
                // If it's a path reference, extract just the base name for theme lookup
                if (isIconPath(icon)) {
                    return iconBaseName(icon);
                }
                return icon;
            }
        }
    }
    // As a fallback, derive the icon name from the AppImage's directory name
    // pattern: if there's a .DirIcon or a matching .png at the root.
    {
        QDir root(squashfsRoot);
        const auto entries = root.entryInfoList(
            QStringList() << QStringLiteral("*.desktop"), QDir::Files);
        // If there's a desktop file but no Icon=, use its basename as icon name
        for (const auto& df : entries) {
            QString base = df.completeBaseName();
            base = base.toLower();
            // strip version suffixes like _1.0.0
            int cut = base.indexOf(QLatin1Char('_'));
            if (cut > 0) base = base.left(cut);
            if (!base.isEmpty())
                return base;
        }
    }
    return {};
}

// Extract icons from an AppImage into the user's icon theme directory.
// Uses multiple strategies to ensure icons are found and installed:
// 1. Try libappimage's own icon extraction first
// 2. Extract via --appimage-extract and search comprehensively
// 3. Fallback to .DirIcon at the root
//
// If the registered desktop file has no Icon= entry, the icon name is
// discovered from the AppImage's internal .desktop file and written back
// into the registered desktop file.
bool extractAndInstallIcons(const QString& pathToAppImage, const QString& desktopPath) {
    // First, try to read the icon name from the registered desktop file.
    QString iconName;
    if (!desktopPath.isEmpty() && QFile::exists(desktopPath))
        iconName = readDesktopIcon(desktopPath);

    // If we already have an icon name that's not a path, we still need to
    // verify the icons are installed, so we always proceed with extraction.

    const QString iconsBase = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                              + QStringLiteral("/icons/hicolor");
    bool installedAny = false;

    // Strategy: Run the AppImage with --appimage-extract to a temp dir
    // This is the most reliable method across different libappimage versions
    QTemporaryDir extractDir;
    if (extractDir.isValid()) {
        QProcess proc;
        proc.setProgram(pathToAppImage);
        proc.setArguments({QStringLiteral("--appimage-extract")});
        proc.setWorkingDirectory(extractDir.path());
        proc.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        proc.start();
        if (proc.waitForFinished(30000)) { // 30 second timeout
            if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
                const QString squashfsRoot = extractDir.path() + QStringLiteral("/squashfs-root");
                if (QDir(squashfsRoot).exists()) {
                    // If we still don't have an icon name, try to discover it from the
                    // AppImage's internal .desktop file.
                    if (iconName.isEmpty() || isIconPath(iconName)) {
                        QString discoveredName = findIconNameFromPayload(squashfsRoot);
                        if (!discoveredName.isEmpty()) {
                            iconName = discoveredName;
                            if (!desktopPath.isEmpty() && QFile::exists(desktopPath)) {
                                // Write the discovered icon name into the registered desktop file
                                // so the desktop environment can find the icon.
                                setDesktopIcon(desktopPath, iconName);
                            }
                        }
                    }

                    // Candidate icon locations inside an AppImage payload
                    const QStringList iconSearchDirs = {
                        QStringLiteral("usr/share/icons/hicolor"),
                        QStringLiteral("usr/share/icons"),
                        QStringLiteral("usr/share/pixmaps"),
                        QStringLiteral("usr/share"),
                        QStringLiteral("."),
                    };

                    // List of supported image extensions
                    const QStringList imageExtensions = {
                        QStringLiteral(".png"), QStringLiteral(".svg"),
                        QStringLiteral(".xpm"), QStringLiteral(".svgz"),
                        QStringLiteral(".ico")
                    };

                    if (!iconName.isEmpty()) {
                        // Search for exact icon name matches first
                        for (const auto& subdir : iconSearchDirs) {
                            const QString searchDir = squashfsRoot + QLatin1Char('/') + subdir;
                            if (!QDir(searchDir).exists())
                                continue;

                            // Search for files matching iconName.*
                            for (const QString& ext : imageExtensions) {
                                QDirIterator it(searchDir,
                                    QStringList() << iconName + ext,
                                    QDir::Files,
                                    QDirIterator::Subdirectories);

                                while (it.hasNext()) {
                                    const QString srcPath = it.next();
                                    const QFileInfo fi(srcPath);
                                    QString sizeBucket = iconSizeBucket(fi);

                                    // If size couldn't be determined from path, read image
                                    if (sizeBucket == QStringLiteral("scalable") &&
                                        (ext == QStringLiteral(".png") || ext == QStringLiteral(".ico"))) {
                                        sizeBucket = iconSizeFromImage(srcPath);
                                    }

                                    const QString destDir = iconsBase + QLatin1Char('/') + sizeBucket + QStringLiteral("/apps");
                                    QDir().mkpath(destDir);

                                    const QString destPath = destDir + QLatin1Char('/') + iconName + ext;
                                    if (QFile::exists(destPath))
                                        QFile::remove(destPath);
                                    if (QFile::copy(srcPath, destPath))
                                        installedAny = true;
                                }
                            }
                        }

                        // If we didn't find exact matches, search for files that contain the icon name
                        if (!installedAny) {
                            for (const auto& subdir : iconSearchDirs) {
                                const QString searchDir = squashfsRoot + QLatin1Char('/') + subdir;
                                if (!QDir(searchDir).exists())
                                    continue;

                                QDirIterator it(searchDir,
                                    QStringList() << QStringLiteral("*.png")
                                                  << QStringLiteral("*.svg")
                                                  << QStringLiteral("*.xpm")
                                                  << QStringLiteral("*.svgz"),
                                    QDir::Files,
                                    QDirIterator::Subdirectories);
                                while (it.hasNext()) {
                                    const QString srcPath = it.next();
                                    const QFileInfo fi(srcPath);
                                    const QString baseName = fi.completeBaseName().toLower();
                                    const QString targetName = iconName.toLower();

                                    // Match icon names that are similar
                                    if (baseName == targetName ||
                                        baseName.startsWith(targetName + QLatin1Char('_')) ||
                                        baseName.endsWith(QLatin1Char('_') + targetName) ||
                                        baseName.contains(targetName)) {

                                        QString sizeBucket = iconSizeBucket(fi);
                                        if (sizeBucket == QStringLiteral("scalable") &&
                                            fi.suffix().toLower() == QStringLiteral("png")) {
                                            sizeBucket = iconSizeFromImage(srcPath);
                                        }

                                        const QString destDir = iconsBase + QLatin1Char('/') + sizeBucket + QStringLiteral("/apps");
                                        QDir().mkpath(destDir);

                                        const QString destPath = destDir + QLatin1Char('/') + iconName + QStringLiteral(".") + fi.suffix().toLower();
                                        if (!QFile::exists(destPath)) {
                                            if (QFile::copy(srcPath, destPath))
                                                installedAny = true;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Strategy 2: Look for .DirIcon at the root (common AppImage pattern)
                    {
                        const QString dirIconPath = squashfsRoot + QStringLiteral("/.DirIcon");
                        if (QFile::exists(dirIconPath)) {
                            QString targetIconName = iconName;
                            if (targetIconName.isEmpty()) {
                                // Try to get name from desktop file basename
                                QDir root(squashfsRoot);
                                const auto desktops = root.entryInfoList(
                                    QStringList() << QStringLiteral("*.desktop"), QDir::Files);
                                if (!desktops.isEmpty()) {
                                    targetIconName = iconBaseName(desktops.first().completeBaseName());
                                }
                            }
                            if (targetIconName.isEmpty()) {
                                // Use a generic name based on AppImage filename
                                targetIconName = QStringLiteral("appimage-default");
                            }

                            // Determine the file type/extension
                            QFileInfo diFi(dirIconPath);
                            QString ext = QStringLiteral(".png");
                            // Try to read the image to get format
                            QImageReader reader(dirIconPath);
                            if (reader.canRead()) {
                                QByteArray fmt = reader.format();
                                if (fmt == "svg") ext = QStringLiteral(".svg");
                                else if (fmt == "svgz") ext = QStringLiteral(".svgz");
                                else if (fmt == "xpm") ext = QStringLiteral(".xpm");
                            } else if (!diFi.suffix().isEmpty()) {
                                ext = QStringLiteral(".") + diFi.suffix().toLower();
                            }

                            QString sizeBucket = iconSizeFromImage(dirIconPath);
                            if (ext == QStringLiteral(".svg") || ext == QStringLiteral(".svgz")) {
                                sizeBucket = QStringLiteral("scalable");
                            }

                            const QString destDir = iconsBase + QLatin1Char('/') + sizeBucket + QStringLiteral("/apps");
                            QDir().mkpath(destDir);

                            const QString destPath = destDir + QLatin1Char('/') + targetIconName + ext;
                            if (QFile::exists(destPath))
                                QFile::remove(destPath);
                            if (QFile::copy(dirIconPath, destPath)) {
                                installedAny = true;
                                // If we didn't have an icon name before, set it now
                                if (iconName.isEmpty() && !desktopPath.isEmpty() && QFile::exists(desktopPath)) {
                                    iconName = targetIconName;
                                    setDesktopIcon(desktopPath, iconName);
                                }
                            }
                        }
                    }

                    // Strategy 3: Look for any icon files in pixmaps or common locations
                    // even if we don't have a name - use desktop file base name
                    if (!installedAny && iconName.isEmpty()) {
                        QDir root(squashfsRoot);
                        const auto desktops = root.entryInfoList(
                            QStringList() << QStringLiteral("*.desktop"), QDir::Files);
                        if (!desktops.isEmpty()) {
                            QString targetName = iconBaseName(desktops.first().completeBaseName());

                            // Search for any image file that looks like a main icon
                            const QStringList searchDirs = {
                                QStringLiteral("usr/share/pixmaps"),
                                QStringLiteral("."),
                            };
                            for (const auto& subdir : searchDirs) {
                                const QString searchDir = squashfsRoot + QLatin1Char('/') + subdir;
                                if (!QDir(searchDir).exists()) continue;

                                QDirIterator it(searchDir,
                                    QStringList() << QStringLiteral("*.png")
                                                  << QStringLiteral("*.svg")
                                                  << QStringLiteral("*.xpm")
                                                  << QStringLiteral("*.svgz"),
                                    QDir::Files,
                                    subdir == QStringLiteral(".") ? QDirIterator::NoIteratorFlags : QDirIterator::Subdirectories);
                                while (it.hasNext()) {
                                    const QString srcPath = it.next();
                                    const QFileInfo fi(srcPath);
                                    // Skip very small icons or hicolor theme dirs
                                    if (fi.fileName().contains(QStringLiteral("hicolor"))) continue;

                                    QString sizeBucket;
                                    const QString suffix = fi.suffix().toLower();
                                    if (suffix == QStringLiteral("svg") || suffix == QStringLiteral("svgz")) {
                                        sizeBucket = QStringLiteral("scalable");
                                    } else {
                                        sizeBucket = iconSizeFromImage(srcPath);
                                    }

                                    const QString destDir = iconsBase + QLatin1Char('/') + sizeBucket + QStringLiteral("/apps");
                                    QDir().mkpath(destDir);

                                    const QString destPath = destDir + QLatin1Char('/') + targetName + QStringLiteral(".") + suffix;
                                    if (!QFile::exists(destPath)) {
                                        if (QFile::copy(srcPath, destPath)) {
                                            installedAny = true;
                                            iconName = targetName;
                                            if (!desktopPath.isEmpty() && QFile::exists(desktopPath)) {
                                                setDesktopIcon(desktopPath, iconName);
                                            }
                                            break;
                                        }
                                    }
                                }
                                if (installedAny) break;
                            }
                        }
                    }
                }
            }
        }
    }

    // If we installed icons, update the desktop database and icon cache
    if (installedAny) {
        // Ensure icon name is set in desktop file
        if (!iconName.isEmpty() && !desktopPath.isEmpty() && QFile::exists(desktopPath)) {
            setDesktopIcon(desktopPath, iconName);
        }
    }

    return installedAny || !iconName.isEmpty();
}

} // namespace

bool isAppImage(const QString& path) {
    const auto type = appimage_get_type(path.toUtf8().constData(), false);
    return type > 0 && type <= 2;
}

bool makeExecutable(const QString& path) {
    struct stat st{};
    if (stat(path.toUtf8().constData(), &st) != 0)
        return false;
    const auto uid = getuid();
    if ((st.st_uid == uid && (st.st_mode & 0100)) ||
        (st.st_gid == getgid() && (st.st_mode & 0010)) ||
        (st.st_mode & 0001)) {
        return true;
    }
    return chmod(path.toUtf8().constData(), st.st_mode | 0111) == 0;
}

QString appImageDigest(const QString& path) {
    // only type 2 carries/computes an MD5 digest
    if (appimage_get_type(path.toUtf8().constData(), false) != 2)
        return {};

    unsigned long offset = 0, length = 0;
    QByteArray buffer(16, '\0');

    if (appimage_get_elf_section_offset_and_length(
            path.toUtf8().constData(), ".digest_md5", &offset, &length) &&
        offset != 0 && length != 0) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly) && f.seek(static_cast<qint64>(offset)))
            f.read(buffer.data(), buffer.size());
    }

    // some AppImages embed an all-zero section; recompute in that case
    bool allZero = true;
    for (char c : buffer)
        if (c != '\0') { allZero = false; break; }
    if (allZero)
        if (!appimage_type2_digest_md5(path.toUtf8().constData(), buffer.data()))
            return {};

    char* hex = appimage_hexlify(buffer.constData(), static_cast<size_t>(buffer.size()));
    if (!hex) return {};
    QString out = QString::fromUtf8(hex);
    free(hex);
    return out;
}

QString buildIntegratedPath(const QString& pathToAppImage) {
    const auto digest = appImageDigest(pathToAppImage);
    const QFileInfo info(pathToAppImage);
    QString base = info.completeBaseName();
    const auto suffix = QStringLiteral("_") + digest;
    if (!digest.isEmpty() && !pathToAppImage.contains(suffix))
        base += suffix;
    QString name = base;
    if (!info.suffix().isEmpty())
        name += QStringLiteral(".") + info.suffix();
    return Config().destination() + QStringLiteral("/") + name;
}

bool isRegistered(const QString& pathToAppImage) {
    return appimage_is_registered_in_system(pathToAppImage.toUtf8().constData());
}

bool installDesktopFileAndIcons(const QString& pathToAppImage) {
    // Use legacy=true so libappimage extracts icons in addition to the
    // desktop file. The non-legacy path skips icon extraction on some
    // libappimage versions.
    if (appimage_register_in_system(pathToAppImage.toUtf8().constData(), true) != 0)
        return false;

    const auto desktopPath = registeredDesktopPath(pathToAppImage);
    if (desktopPath.isEmpty() || !QFile::exists(desktopPath))
        return false;

    // Manually extract and install icons from the AppImage payload, and
    // ensure the Icon= field is set in the registered desktop file.
    // Some libappimage builds don't extract icons reliably or don't set
    // the Icon= key at all — this is our authoritative icon pipeline.
    extractAndInstallIcons(pathToAppImage, desktopPath);

    // Reload the desktop file (extractAndInstallIcons may have patched the
    // Icon= field on disk) before applying Miryu-specific entries.
    std::shared_ptr<GKeyFile> kf(g_key_file_new(), gKeyFileFree);
    std::shared_ptr<GError*> err(nullptr, gErrorFree);

    const auto flags = GKeyFileFlags(G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS);
    if (!g_key_file_load_from_file(kf.get(), desktopPath.toUtf8().constData(), flags, err.get()))
        return false;

    // preserve existing desktop actions, then append a Miryu Remove action that
    // shells out to the CLI (miryu-app-launcher-cli unintegrate <path>)
    QStringList actions;
    if (gchar* a = g_key_file_get_string(kf.get(), G_KEY_FILE_DESKTOP_GROUP,
                                         G_KEY_FILE_DESKTOP_KEY_ACTIONS, err.get())) {
        for (const auto& s : QString::fromUtf8(a).split(QLatin1Char(';')))
            if (!s.isEmpty()) actions << s;
        g_free(a);
    }
    static const QByteArray removeKey = "Miryu-Remove-AppImage";
    actions << QString::fromUtf8(removeKey);

    // persist the (possibly extended) Actions list back into the desktop file
    QList<QByteArray> buffers;
    std::vector<const char*> ptrs;
    ptrs.reserve(static_cast<size_t>(actions.size()));
    for (const auto& s : actions) {
        buffers << s.toUtf8();
        ptrs.push_back(buffers.last().constData());
    }
    g_key_file_set_string_list(kf.get(), G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_ACTIONS,
                               ptrs.data(), ptrs.size());

    // Write the English source string as the default Name, plus Name[locale]
    // entries for every available translation. KService does NOT translate
    // [Desktop Action] Name= via X-KDE-TranslationDomain, so we must embed
    // all translations directly in the .desktop file. KConfig's readEntry("Name")
    // then picks the right one based on the current system locale.
    const QByteArray section = "Desktop Action " + removeKey;
    static const QByteArray removeActionName =
        ki18n("Remove this AppImage").untranslatedText();
    g_key_file_set_string(kf.get(), section.constData(), "Name", removeActionName.constData());
    for (int t = 0; t < actionTranslationCount; ++t) {
        const QByteArray key = QByteArray("Name[") + actionTranslations[t].locale + "]";
        g_key_file_set_string(kf.get(), section.constData(),
                              key.constData(), actionTranslations[t].text);
    }
    g_key_file_set_string(kf.get(), section.constData(), "Icon", "miryu-app-launcher");
    const QByteArray exec = ("miryu-app-launcher-cli unintegrate \"" +
                             pathToAppImage.toUtf8() + "\"");
    g_key_file_set_string(kf.get(), section.constData(), "Exec", exec.constData());

    g_key_file_set_string(kf.get(), G_KEY_FILE_DESKTOP_GROUP,
                          "X-Miryu-Version", MIRYU_VERSION);
    // Tell KDE to translate desktop file strings (Action Names, etc.) using
    // the miryu-app-launcher gettext catalog. Without this, Action Names like
    // "Remove this AppImage" are never translated.
    g_key_file_set_string(kf.get(), G_KEY_FILE_DESKTOP_GROUP,
                          "X-KDE-TranslationDomain", "miryu-app-launcher");

    if (!g_key_file_save_to_file(kf.get(), desktopPath.toUtf8().constData(), err.get()))
        return false;

    makeExecutable(desktopPath);

    // Refresh desktop and icon caches immediately after installation
    refreshCaches();

    // Also tell Plasma to refresh its icon loader
    auto msg = QDBusMessage::createSignal(
        QStringLiteral("/KIconLoader"), QStringLiteral("org.kde.KIconLoader"),
        QStringLiteral("iconChanged"));
    msg.setArguments({0});
    QDBusConnection::sessionBus().send(msg);

    // Send a more comprehensive icon change notification (try both Plasma 5 and 6 names)
    auto msg2 = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.klauncher6"),
        QStringLiteral("/KLauncher"),
        QStringLiteral("org.kde.KLauncher"),
        QStringLiteral("reparseConfiguration"));
    QDBusConnection::sessionBus().send(msg2);

    auto msg3 = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.klauncher5"),
        QStringLiteral("/KLauncher"),
        QStringLiteral("org.kde.KLauncher"),
        QStringLiteral("reparseConfiguration"));
    QDBusConnection::sessionBus().send(msg3);

    return true;
}

bool unregisterAppImage(const QString& pathToAppImage) {
    // Find the desktop file first (before unregistering, which removes it).
    const QString desktopPath = registeredDesktopPath(pathToAppImage);

    // Try to read the actual AppImage path from TryExec= before we remove
    // the desktop file. We'll use this to delete the integrated file.
    QString appImagePath;
    QString iconName;
    if (!desktopPath.isEmpty() && QFile::exists(desktopPath)) {
        appImagePath = readDesktopTryExec(desktopPath);
        iconName = readDesktopIcon(desktopPath);
    }

    // Remove desktop file and icons via libappimage (legacy mode to match
    // the registration side).
    const bool unregistered = appimage_unregister_in_system(
        pathToAppImage.toUtf8().constData(), true) == 0;

    // Also clean up our manually installed icons
    if (!iconName.isEmpty() && !isIconPath(iconName)) {
        const QString iconsBase = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                                  + QStringLiteral("/icons/hicolor");
        const QStringList sizes = {
            QStringLiteral("16x16"), QStringLiteral("22x22"), QStringLiteral("24x24"),
            QStringLiteral("32x32"), QStringLiteral("48x48"), QStringLiteral("64x64"),
            QStringLiteral("128x128"), QStringLiteral("256x256"), QStringLiteral("512x512"),
            QStringLiteral("scalable")
        };
        const QStringList exts = {
            QStringLiteral(".png"), QStringLiteral(".svg"),
            QStringLiteral(".xpm"), QStringLiteral(".svgz")
        };
        for (const auto& size : sizes) {
            for (const auto& ext : exts) {
                const QString p = iconsBase + QStringLiteral("/") + size +
                                  QStringLiteral("/apps/") + iconName + ext;
                if (QFile::exists(p)) {
                    QFile::remove(p);
                }
            }
        }
    }

    if (!unregistered)
        return false;

    // Delete the integrated AppImage file itself, but only if it lives
    // inside the integration directory (i.e. it was placed there by Miryu).
    // We never delete AppImages outside the integration dir — the user might
    // have unintegrated a file they want to keep.
    if (appImagePath.isEmpty())
        appImagePath = pathToAppImage;

    const Config cfg;
    const QString destDir = QFileInfo(cfg.destination()).absoluteFilePath();
    const QString absAppImage = QFileInfo(appImagePath).absoluteFilePath();

    if (absAppImage.startsWith(destDir + QLatin1Char('/')) ||
        absAppImage == destDir) {
        if (QFile::exists(absAppImage))
            QFile::remove(absAppImage);
    }

    // Refresh caches after removal
    refreshCaches();

    return true;
}

IntegrationResult integrateAppImage(const QString& pathToAppImage,
                                    const QString& pathToIntegratedAppImage,
                                    bool interactive) {
    QDir().mkpath(QFileInfo(pathToIntegratedAppImage).absolutePath());

    // already in place?
    if (QFileInfo(pathToAppImage).absoluteFilePath() !=
        QFileInfo(pathToIntegratedAppImage).absoluteFilePath()) {
        if (QFile::exists(pathToIntegratedAppImage)) {
            if (interactive) {
                QMessageBox mb(QMessageBox::Warning, i18nd("miryu-app-launcher", "Warning"),
                    i18nd("miryu-app-launcher", "An AppImage with the same name has already been integrated.\n\n"
                         "Do you want to overwrite it? (Choosing No will run the AppImage once.)"),
                    QMessageBox::Yes | QMessageBox::No);
                mb.setDefaultButton(QMessageBox::No);
                if (mb.exec() == QMessageBox::No)
                    return IntegrationResult::Aborted;
            }
            QFile::remove(pathToIntegratedAppImage);
        }

        if (!QFile::rename(pathToAppImage, pathToIntegratedAppImage)) {
            // cross-device or permissions: fall back to copy
            if (!QFile::copy(pathToAppImage, pathToIntegratedAppImage))
                return IntegrationResult::Failed;
        }
    }

    if (!installDesktopFileAndIcons(pathToIntegratedAppImage))
        return IntegrationResult::Failed;
    return IntegrationResult::Ok;
}

void refreshCaches() {
    const auto data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);

    // update-desktop-database is fast and needed for MIME type associations;
    // run it synchronously so it completes before we return.
    {
        QProcess p;
        p.setProgram(QStringLiteral("update-desktop-database"));
        p.setArguments({data + QStringLiteral("/applications")});
        p.setProcessChannelMode(QProcess::ForwardedChannels);
        p.start();
        p.waitForFinished(10000); // 10s timeout
    }

    // gtk-update-icon-cache and update-mime-database are not on the critical
    // path for KDE's application menu; run them detached so they don't block.
    {
        QProcess p;
        p.setProgram(QStringLiteral("gtk-update-icon-cache"));
        p.setArguments({data + QStringLiteral("/icons/hicolor"), QStringLiteral("-t"), QStringLiteral("-f")});
        p.setProcessChannelMode(QProcess::ForwardedChannels);
        p.startDetached();
    }
    {
        QProcess p;
        p.setProgram(QStringLiteral("update-mime-database"));
        p.setArguments({data + QStringLiteral("/mime")});
        p.setProcessChannelMode(QProcess::ForwardedChannels);
        p.startDetached();
    }

    // =====================================================================
    // Rebuild the KDE sycoca cache so newly integrated AppImages appear in
    // KDE Plasma's application launcher (Kickoff/KRunner) immediately.
    //
    // The CORRECT way to trigger a sycoca rebuild in KDE is to call kded's
    // D-Bus method org.kde.kbuildsycoca.recreate(). kded then calls
    // KSycoca::ensureCacheValid() in-process, which checks directory
    // timestamps, runs kbuildsycoca if needed, and properly notifies all
    // running KDE applications through KDirWatch + databaseChanged().
    //
    // Running kbuildsycoca6 directly as a standalone process does write the
    // database file, but KDirWatch in plasmashell may not reliably detect
    // the atomic rename used by QSaveFile. Using kded's D-Bus API avoids
    // this race because kded manages its own KDirWatch state and calls
    // KSycoca internals directly.
    //
    // We try kded6 first (Plasma 6 / KF6), then kded5 (Plasma 5 / KF5).
    // If neither is available (e.g. kded not running, non-KDE environment),
    // fall back to running kbuildsycoca6/kbuildsycoca5 directly.
    // =====================================================================
    bool rebuiltViaDbus = false;

    static const QStringList kdedServices = {
        QStringLiteral("org.kde.kded6"),
        QStringLiteral("org.kde.kded5"),
    };
    for (const auto& service : kdedServices) {
        QDBusInterface kded(service,
                            QStringLiteral("/kbuildsycoca"),
                            QStringLiteral("org.kde.kbuildsycoca"),
                            QDBusConnection::sessionBus());
        if (kded.isValid()) {
            // Call recreate() and wait for it to complete (it runs
            // kbuildsycoca synchronously inside kded). Use a 30s timeout
            // to match the direct-process fallback below.
            QDBusReply<void> reply = kded.callWithArgumentList(
                QDBus::BlockWithGui,
                QStringLiteral("recreate"),
                {});
            if (reply.isValid()) {
                rebuiltViaDbus = true;
                break;
            }
            // If the call failed (e.g. timeout), try the next service.
        }
    }

    // Fallback: run kbuildsycoca directly if kded D-Bus is unavailable.
    if (!rebuiltViaDbus) {
        static const QStringList kbuildsycocaNames = {
            QStringLiteral("kbuildsycoca6"),
            QStringLiteral("kbuildsycoca5"),
        };
        for (const auto& prog : kbuildsycocaNames) {
            QProcess p;
            p.setProgram(prog);
            p.setArguments({QStringLiteral("--noincremental")});
            p.setProcessChannelMode(QProcess::ForwardedChannels);
            p.start();
            if (p.waitForStarted(2000)) {
                p.waitForFinished(30000);
                break;
            }
        }
    }
}

} // namespace miryu
