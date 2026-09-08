# Third-party components

The UI uses Qt Core, Gui and Widgets 5.15.2, copyright The Qt Company Ltd. and
other contributors, through dynamic linking. Qt is available under LGPL v3,
GPL or commercial terms; this project uses the open-source distribution.
Qt is not covered by this project's own license.

The build downloads the unmodified development archive from download.qt.io.
No Qt runtime DLLs are distributed or installed by Hammer Addons. At runtime
the UI uses the Qt DLLs already provided with CS2 Workshop Tools.

- [Qt 5.15.2 source](https://code.qt.io/cgit/qt/qtbase.git/tree/?h=v5.15.2)
- [Qt LGPL v3 license](https://code.qt.io/cgit/qt/qtbase.git/tree/LICENSE.LGPL3?h=v5.15.2)
- [Qt GPL v3 license](https://code.qt.io/cgit/qt/qtbase.git/tree/LICENSE.GPL3?h=v5.15.2)

Our source, CMake configuration and the pinned archive URL/checksum in
scripts/fetch-qt.py are available to rebuild the UI DLL. The SDK files in the
cache are unmodified. native/qt_compat.h selects Qt's raw-pointer iterator
fallback for the MSVC 2026 compiler, which removed the older stdext adapters.
