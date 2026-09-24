#pragma once

#include <QString>
#include <QStringList>

struct DesktopEntry
{
    QString id;
    QString name;
    QString summary;
    QString iconName;
    QString iconPath;
    QString exec;
    QStringList categories;
};

/** Absolute path of the .desktop matching @p appId, or empty. */
QString findDesktopFile(const QString &appId);

/**
 * Resolve "Icon=" of @p desktopFile to an absolute path when possible
 * (including legacy /usr/share/pixmaps layout) or a theme icon name.
 */
QString desktopIcon(const QString &desktopFile);

/** Parse a .desktop file's useful fields. Returns false when unreadable. */
bool parseDesktopEntry(const QString &path, DesktopEntry &entry);