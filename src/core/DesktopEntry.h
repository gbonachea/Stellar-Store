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
    bool hidden = false;
    bool noDisplay = false;
};

/**
 * Directories that may contain application launchers, in priority order
 * (user entries first, then Flatpak/Snap exports, then the system paths).
 */
QStringList desktopApplicationDirectories();

/**
 * All visible .desktop launchers installed on the system, deduplicated by
 * desktop id. User entries win over system entries with the same id.
 */
QStringList installedDesktopFiles();

/**
 * "flatpak", "snap", "user" or "system" depending on where @p path lives.
 */
QString desktopEntryOrigin(const QString &path);

/** Absolute path of the .desktop matching @p appId, or empty. */
QString findDesktopFile(const QString &appId);

/**
 * Resolve "Icon=" of @p desktopFile to an absolute path when possible
 * (including legacy /usr/share/pixmaps layout) or a theme icon name.
 */
QString desktopIcon(const QString &desktopFile);

/** Parse a .desktop file's useful fields. Returns false when unreadable. */
bool parseDesktopEntry(const QString &path, DesktopEntry &entry);
