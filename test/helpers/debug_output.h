#ifndef TEST_DEBUG_OUTPUT
#define TEST_DEBUG_OUTPUT

#include <QString>
#include <QUrl>
#include <ostream>

inline std::ostream &operator<<(std::ostream &os, const QByteArray &value)
{
    return os << '"' << (value.isEmpty() ? "" : value.constData()) << '"';
}

inline std::ostream &operator<<(std::ostream &os, const QLatin1String &value)
{
    return os << '"' << value.latin1() << '"';
}

inline std::ostream &operator<<(std::ostream &os, const QString &value)
{
    return os << value.toLocal8Bit();
}

inline std::ostream &operator<<(std::ostream &os, const QUrl &value)
{
    return os << value.toDisplayString();
}

#endif // TEST_DEBUG_OUTPUT
