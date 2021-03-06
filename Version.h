#ifndef MEDIAELCH_VERSION_H
#define MEDIAELCH_VERSION_H

#include <QString>

namespace MediaElch {
namespace Constants {

constexpr char AppName[] = "MediaElch";
constexpr char AppVersionStr[] = "2.6.0";
constexpr char VersionName[] = "Ferenginar";
constexpr char OrganizationName[] = "kvibes";

#ifdef QT_NO_DEBUG
constexpr char CompilationType[] = "Release";
#else
constexpr char CompilationType[] = "Debug";
#endif

const QString CompilerString = []() -> QString {
// Taken from QtCreator (qt-creator/src/plugins/coreplugin/icore.cpp) - Modified
#if defined(Q_CC_CLANG) // must be before GNU, because clang claims to be GNU too
    QString isAppleString;
#if defined(__apple_build_version__) // Apple clang has other version numbers
    isAppleString = QLatin1String(" (Apple)");
#endif
    return QLatin1String("Clang ") + QString::number(__clang_major__) + '.' + QString::number(__clang_minor__)
           + isAppleString;

#elif defined(Q_CC_GNU)
    return QLatin1String("GCC ") + QLatin1String(__VERSION__);

#elif defined(Q_CC_MSVC)
    if (_MSC_VER > 1999) {
        return QLatin1String("MSVC <unknown>");
    }
    if (_MSC_VER >= 1910) {
        return QLatin1String("MSVC 2017");
    }
    if (_MSC_VER >= 1900) {
        return QLatin1String("MSVC 2015");
    }
#endif

    return QLatin1String("<unknown compiler>");
}();

} // namespace Constants
} // namespace MediaElch

#endif // MEDIAELCH_VERSION_H
