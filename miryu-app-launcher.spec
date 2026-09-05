Name:           miryu-app-launcher
Version:        1.0.0
Release:        4%{?dist}
Summary:        Lean AppImage desktop integration for KDE Plasma 6

License:        MIT
URL:            https://github.com/evernightvista/miryu-app-launcher
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  gcc-c++
BuildRequires:  extra-cmake-modules >= 6.0
BuildRequires:  qt6-qtbase-devel >= 6.6
BuildRequires:  kf6-kcoreaddons-devel >= 6.0
BuildRequires:  kf6-ki18n-devel >= 6.0
BuildRequires:  kf6-kconfig-devel >= 6.0
BuildRequires:  kf6-kcmutils-devel >= 6.0
BuildRequires:  kf6-kwidgetsaddons-devel >= 6.0
BuildRequires:  libappimage-devel >= 1.0
BuildRequires:  glib2-devel >= 2.40
BuildRequires:  gettext
BuildRequires:  python3
BuildRequires:  systemd-rpm-macros
BuildRequires:  desktop-file-utils


Requires:       %{_bindir}/update-desktop-database
Requires:       kf6-kcoreaddons >= 6.0
Requires:       kf6-ki18n >= 6.0
Requires:       kf6-kconfig >= 6.0
Requires:       libappimage >= 1.0
Requires:       glib2 >= 2.40

%description
Miryu is a lean AppImage desktop integration helper for KDE Plasma 6.
It registers as the MIME handler for AppImage files and provides:
- Automatic desktop integration (desktop file + icons)
- Configurable integration directory
- Optional background directory watcher daemon
- KDE System Settings module (KCM) for configuration

Unlike AppImageLauncher, Miryu does not use binfmt_misc interception,
avoiding the complexity of a preload library and recursive execution.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install
%find_lang %{name}

%post
/usr/bin/update-desktop-database -q %{_datadir}/applications &>/dev/null || :

%postun
/usr/bin/update-desktop-database -q %{_datadir}/applications &>/dev/null || :


%files
%{_bindir}/miryu-app-launcher
%{_bindir}/miryud
%{_bindir}/miryu-app-launcher-cli
%{_qt6_plugindir}/plasma/kcms/systemsettings_qwidgets/kcm_miryu.so
%{_datadir}/applications/miryu-app-launcher.desktop
%{_datadir}/applications/miryu-app-launcher-cli.desktop
%{_datadir}/applications/kcm_miryu.desktop
%{_datadir}/mime/packages/appimage.xml
%{_userunitdir}/miryud.service
%{_datadir}/locale/*/LC_MESSAGES/miryu-app-launcher.mo

%changelog
* Sat Sep 05 2026 KairikiFedora <13278297951@sina.cn> - 1.0.0-4
- Fix KDE application launcher not immediately showing newly integrated
  AppImages: run kbuildsycoca6 synchronously (was startDetached) so the
  KSycoca database is rebuilt before the integration call returns.
  plasmashell's KDirWatch then detects the new database and Kickoff
  reloads its application list without a logout.
- Add kf6-kservice runtime dependency (provides kbuildsycoca6).
- Run update-desktop-database synchronously; keep gtk-update-icon-cache
  and update-mime-database detached (not on the KDE menu critical path).
- Fall back to kbuildsycoca5 for Plasma 5 / KF5 systems.
- Remove redundant refreshCaches() calls from daemon, CLI and launcher
  (installDesktopFileAndIcons / unregisterAppImage already refresh).

* Fri Aug 28 2026 KairikiFedora <13278297951@sina.cn> - 1.0.0-3
- Fix Remove AppImage action not following system language:
  * KF6 KService does NOT translate [Desktop Action] Name= via
    X-KDE-TranslationDomain; it only reads the raw string
  * Embed all available translations as Name[locale]= entries in the
    desktop file at integration time
  * Build-time Python script extracts translations from .po files into
    a generated C++ header
  * KConfig readEntry("Name") auto-selects the matching locale entry
- Fix Remove AppImage action not following system language changes:
  * Write untranslated source string to desktop file instead of i18nc() result
  * KDE now translates it at display time via X-KDE-TranslationDomain
  * Remove msgctxt from .po files so dgettext lookup succeeds
  * Add kli18n keyword to Messages.sh for xgettext extraction
- Fix fastfetch not counting integrated AppImages:
  * Change default integration directory from ~/.miryu-app to ~/Applications
    (fastfetch scans ~/AppImages and ~/Applications for *.appimage files)
  * Use config destination() in launcher prompt instead of hardcoded path

* Thu Aug 27 2026 KairikiFedora <13278297951@sina.cn> - 1.0.0-1
- Initial package with fixes:
  * Fix KCM not showing in KDE System Settings
  * Fix settings page layout integration
  * Fix AppImage icon extraction after integration
  * Use correct QWidget KCM install path (systemsettings_qwidgets)
  * Improve icon extraction with multiple fallback strategies
  * Add proper cache refresh after integration
