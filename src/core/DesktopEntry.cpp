#include "core/DesktopEntry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

namespace {

QString stripDesktopSuffix(const QString &id)
{
    return id.endsWith(QLatin1String(".desktop"))
        ? id.left(id.size() - int(QStringLiteral(".desktop").size()))
        : id;
}

void appendUnique(QStringList &list, const QString &directory)
{
    if (directory.isEmpty())
        return;
    const QString clean = QDir::cleanPath(directory);
    if (!list.contains(clean))
        list << clean;
}

} // namespace

QStringList desktopApplicationDirectories()
{
    QStringList dirs;

    // User launchers first so they shadow system entries with the same id.
    appendUnique(dirs,
                 QStandardPaths::writableLocation(
                     QStandardPaths::ApplicationsLocation));
    appendUnique(dirs, QDir::homePath()
                           + QStringLiteral("/.local/share/flatpak/exports/share/applications"));
    appendUnique(dirs, QStringLiteral("/var/lib/flatpak/exports/share/applications"));
    appendUnique(dirs, QStringLiteral("/var/lib/snapd/desktop/applications"));
    appendUnique(dirs, QStringLiteral("/usr/local/share/applications"));
    appendUnique(dirs, QStringLiteral("/usr/share/applications"));

    // Honour custom XDG data directories too.
    QString dataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
    if (dataDirs.isEmpty())
        dataDirs = QStringLiteral("/usr/local/share:/usr/share");
    const QStringList parts =
        dataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &part : parts)
        appendUnique(dirs, part + QStringLiteral("/applications"));

    return dirs;
}

QStringList installedDesktopFiles()
{
    QStringList files;
    QSet<QString> seenIds;

    for (const QString &dirPath : desktopApplicationDirectories()) {
        const QDir dir(dirPath);
        if (!dir.exists())
            continue;
        const QFileInfoList entries =
            dir.entryInfoList(QStringList() << QStringLiteral("*.desktop"),
                              QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &fi : entries) {
            const QString id = fi.completeBaseName().toLower();
            if (id.isEmpty() || seenIds.contains(id))
                continue;

            DesktopEntry entry;
            if (!parseDesktopEntry(fi.absoluteFilePath(), entry))
                continue;
            if (entry.hidden || entry.noDisplay)
                continue;

            seenIds.insert(id);
            files << fi.absoluteFilePath();
        }
    }

    return files;
}

QString desktopEntryOrigin(const QString &path)
{
    const QString clean = QDir::fromNativeSeparators(path);
    if (clean.contains(QStringLiteral("/flatpak/"))
        || clean.contains(QStringLiteral("/flatpak/exports/")))
        return QStringLiteral("flatpak");
    if (clean.contains(QStringLiteral("/snapd/"))
        || clean.contains(QStringLiteral("/snap/")))
        return QStringLiteral("snap");
    if (clean.startsWith(QDir::homePath()))
        return QStringLiteral("user");
    return QStringLiteral("system");
}

QString findDesktopFile(const QString &appId)
{
    const QString stripped = stripDesktopSuffix(appId);
    for (const QString &dir : desktopApplicationDirectories()) {
        const QString path =
            QDir(dir).filePath(appId + QLatin1String(".desktop"));
        if (QFileInfo::exists(path))
            return path;
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
    bool inDesktopEntry = true;
    while (!file.atEnd()) {
        const QString line =
            QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inDesktopEntry = (line == QLatin1String("[Desktop Entry]"));
            continue;
        }
        if (!inDesktopEntry)
            continue;
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
    bool hidden = false;
    bool noDisplay = false;
    bool inDesktopEntry = false;

    while (!file.atEnd()) {
        const QString raw = QString::fromUtf8(file.readLine());
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            inDesktopEntry = (line == QLatin1String("[Desktop Entry]"));
            continue;
        }
        if (!inDesktopEntry)
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
        } else if (key == QLatin1String("Hidden")) {
            hidden = (value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
        } else if (key == QLatin1String("NoDisplay")) {
            noDisplay = (value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
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
    entry.hidden = hidden;
    entry.noDisplay = noDisplay;

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
