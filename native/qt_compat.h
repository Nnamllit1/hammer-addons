#pragma once
#include <QtCore/qglobal.h>
// Qt 5.15.2 predates MSVC 2026, which removed the stdext array iterators.
// Use Qt's existing raw-pointer fallback without changing its ABI or SDK files.
#if defined(_MSC_VER) && _MSC_VER >= 1950
#undef QT_MAKE_CHECKED_ARRAY_ITERATOR
#undef QT_MAKE_UNCHECKED_ARRAY_ITERATOR
#define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) (x)
#define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x) (x)
#endif
