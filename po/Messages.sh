#!/bin/sh
# KDE i18n extraction for the "miryu-app-launcher" gettext domain.
# Run by the KDE release scripts (or manually) to (re)generate po/miryu-app-launcher.pot.
# Usage from repo root:  bash po/Messages.sh
set -e

# Directory containing this script (i.e. po/).
podir="$(cd "$(dirname "$0")" && pwd)"
# Repository root (parent of po/).
srcdir="$(cd "$podir/.." && pwd)"

# xgettext recognises KDE's i18n*() functions via --keyword entries.
#   i18n        -> 1 msgid
#   i18nc       -> 2 (context, msgid) -> msgctxt "ctx" \n msgid "..."
#   i18np       -> 2 (singular, plural)
#   i18ncp      -> 3 (context, singular, plural)
#   ki18n       -> 1 (same as i18n)
KEYWORDS="
-k_i18n
--keyword=i18n:1
--keyword=i18nc:1c,2
--keyword=i18np:1,2
--keyword=i18ncp:1c,2,3
--keyword=ki18n:1
"

# Collect the translatable C++ sources.
sources="$(find "$srcdir/src" -type f \( -name '*.cpp' -o -name '*.h' \) | sort)"

# Extract from C++ sources first.
xgettext --from-code=UTF-8 \
    --package-name=miryu-app-launcher \
    --package-version=1.0.0 \
    --msgid-bugs-address=https://github.com/evernightvista/miryu-app-launcher/issues \
    --language=cpp \
    --add-comments=i18n \
    $KEYWORDS \
    $sources \
    -o "$podir/miryu-app-launcher.pot"

# Extract from JSON metadata files (KPlugin Name, Description, etc.).
# KDE ECM uses kcoreaddons to translate JSON metadata at runtime via the
# I18n.DefaultDomain field. We must ensure these strings are in the .pot file.
json_files="$(find "$srcdir/src" -name '*.json' -type f | sort)"
if [ -n "$json_files" ]; then
    for json in $json_files; do
        # Use Python to extract Name and Description from KPlugin section.
        python3 -c "
import json, sys
with open('$json') as f:
    data = json.load(f)
kp = data.get('KPlugin', {})
for key in ('Name', 'Description', 'Comment'):
    val = kp.get(key)
    if val and isinstance(val, str):
        print(f'#: {sys.argv[1]}')
        print(f'msgid \"{val}\"')
        print('msgstr \"\"')
        print()
" "$json" >> "$podir/miryu-app-launcher.pot.tmp" 2>/dev/null || true
    done
    # Merge the JSON entries into the .pot if we got any
    if [ -f "$podir/miryu-app-launcher.pot.tmp" ] && [ -s "$podir/miryu-app-launcher.pot.tmp" ]; then
        # Create a temporary pot from the JSON entries
        echo '# SOME DESCRIPTIVE TITLE.' > "$podir/json.pot"
        echo '# Copyright (C) YEAR THE PACKAGE'"'"'S COPYRIGHT HOLDER' >> "$podir/json.pot"
        echo 'msgid ""' >> "$podir/json.pot"
        echo 'msgstr ""' >> "$podir/json.pot"
        echo '"Project-Id-Version: miryu-app-launcher 1.0.0\n"' >> "$podir/json.pot"
        echo '"MIME-Version: 1.0\n"' >> "$podir/json.pot"
        echo '"Content-Type: text/plain; charset=UTF-8\n"' >> "$podir/json.pot"
        echo '"Content-Transfer-Encoding: 8bit\n"' >> "$podir/json.pot"
        cat "$podir/miryu-app-launcher.pot.tmp" >> "$podir/json.pot"
        msgcat --use-first "$podir/miryu-app-launcher.pot" "$podir/json.pot" -o "$podir/miryu-app-launcher.pot"
        rm -f "$podir/miryu-app-launcher.pot.tmp" "$podir/json.pot"
    fi
fi
rm -f "$podir/miryu-app-launcher.pot.tmp" 2>/dev/null || true

# Merge the freshly extracted strings back into existing catalogs so the .po
# files can be updated without losing existing translations.
if command -v msgmerge >/dev/null 2>&1; then
    for po in "$podir"/*.po; do
        [ -f "$po" ] || continue
        msgmerge --update --no-fuzzy-matching --backup=none "$po" "$podir/miryu-app-launcher.pot"
    done
fi
