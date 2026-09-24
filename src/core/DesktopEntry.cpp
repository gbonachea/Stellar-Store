#include "core/DesktopEntry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
const QStringList kDesktopDirs = {
    QStringLiteral("/usr/share/applications"),
    QStringLiteral("/usr/local/share/applications"),
    QDir::homePath() + QStringLiteral("/.local/share/applications"),
};

QString stripDesktopSuffix(const QString &id)
{
    return id.endsWith(QLatin1String(".desktop"))
        ? id.left(id.size() - int(QStringLiteral(".desktop").size()))
        : id;
}
} // namespace

QString findDesktopFile(const QString &appId)
{
    for (const QString &dir : kDesktopDirs) {
        const QString path =
            QDir(dir).filePath(appId + QLatin1String(".desktop"));
        if (QFileInfo::exists(path))
            return path;
        const QString stripped = stripDesktopSuffix(appId);
        const QString alt = QDir(dir).filePath(stripped + QLatin1String(".desktop"));
        if (stripped != appId && QFileInfo::exists(alt))
            return alt;
    }
    return QString();
}

QString desktopIcon(const QString &desktopFile)
{
    QFile file(desktopFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QString value;
    while (!file.atEnd()) {
        const QString line =
            QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1String("Icon="))) {
            value = line.mid(5).trimmed();
            break;
        }
    }
    file.close();

    if (value.isEmpty() || value.startsWith(QLatin1Char('/')))
        return value;

    // Bare theme name: resolve the plain file when the distro ships it under
    // /usr/share/pixmaps, so cards show artwork even if the current icon
    // theme does not know the name.
    if (!value.contains(QLatin1Char('/'))) {
        for (const QString &ext :
             {QStringLiteral(""), QStringLiteral(".png"), QStringLiteral(".svg"),
              QStringLiteral(".xpm")}) {
            const QString candidate =
                QStringLiteral("/usr/share/pixmaps/") + value + ext;
            if (QFileInfo::exists(candidate))
                return candidate;
        }
    }
    return value;
}

bool parseDesktopEntry(const QString &path, DesktopEntry &entry)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString name, summary, icon, exec;
    QStringList categories;
    while (!file.atEnd()) {
        const QString raw = QString::fromUtf8(file.readLine());
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char('[')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("Name")) {
            name = value;
        } else if (key == QLatin1String("Comment")) {
            summary = value;
        } else if (key == QLatin1String("Icon")) {
            icon = value;
        } else if (key == QLatin1String("Exec")) {
            exec = value;
        } else if (key == QLatin1String("Categories")) {
            categories = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        }
    }
    file.close();

    if (name.isEmpty())
        return false;

    entry.id = QFileInfo(path).completeBaseName();
    entry.name = name;
    entry.summary = summary;
    entry.exec = exec;
    entry.categories = categories;

    if (icon.isEmpty() || icon.startsWith(QLatin1Char('/'))) {
        entry.iconName = icon;
        entry.iconPath = icon;
    } else {
        // Reuse the same pixmaps fallback as desktopIcon().
        for (const QString &ext :
             {QStringLiteral(""), QStringLiteral(".png"), QStringLiteral(".svg"),
              QStringLiteral(".xpm")}) {
            const QString candidate =
                QStringLiteral("/usr/share/pixmaps/") + icon + ext;
            if (QFileInfo::exists(candidate)) {
                entry.iconPath = candidate;
                entry.iconName.clear();
                return true;
            }
        }
        entry.iconName = icon;
    }
    return true;
}