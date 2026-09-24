#include "backends/FlatpakBackend.h"

#include <QFileInfo>

FlatpakBackend::FlatpakBackend(QObject *parent)
    : AbstractBackend(parent)
{
}

FlatpakBackend::~FlatpakBackend() = default;

QString FlatpakBackend::backendName() const
{
    return QStringLiteral("flatpak");
}

QString FlatpakBackend::displayName() const
{
    return QStringLiteral("Flatpak");
}

bool FlatpakBackend::isAvailable() const
{
    return QFileInfo::exists(QStringLiteral("/usr/bin/flatpak"));
}

QStringList FlatpakBackend::supportedRemotes() const
{
    return QStringList() << QStringLiteral("Flathub");
}

void FlatpakBackend::installPackage(const QString &packageId)
{
    runProcess(QStringLiteral("flatpak"),
               QStringList() << QStringLiteral("install")
                             << QStringLiteral("-y")
                             << QStringLiteral("flathub") << packageId);
}

void FlatpakBackend::removePackage(const QString &packageId)
{
    runProcess(QStringLiteral("flatpak"),
               QStringList() << QStringLiteral("uninstall")
                             << QStringLiteral("-y") << packageId);
}

void FlatpakBackend::refresh()
{
    runListCommand(QStringLiteral("flatpak"),
                   QStringList() << QStringLiteral("list")
                                 << QStringLiteral("--app")
                                 << QStringLiteral("--columns=application"),
                   [this](const QStringList &list) {
                       emit packagesRefreshed(list);
                   });
}
