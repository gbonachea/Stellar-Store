#include "backends/SnapBackend.h"

#include <QFileInfo>
#include <QRegularExpression>

SnapBackend::SnapBackend(QObject *parent)
    : AbstractBackend(parent)
{
}

SnapBackend::~SnapBackend() = default;

QString SnapBackend::backendName() const
{
    return QStringLiteral("snap");
}

QString SnapBackend::displayName() const
{
    return QStringLiteral("Snap");
}

bool SnapBackend::isAvailable() const
{
    return QFileInfo::exists(QStringLiteral("/usr/bin/snap"));
}

QStringList SnapBackend::supportedRemotes() const
{
    return QStringList() << QStringLiteral("Snap Store");
}

void SnapBackend::installPackage(const QString &packageId)
{
    runProcess(QStringLiteral("snap"),
               QStringList() << QStringLiteral("install") << packageId);
}

void SnapBackend::removePackage(const QString &packageId)
{
    runProcess(QStringLiteral("snap"),
               QStringList() << QStringLiteral("remove") << packageId);
}

void SnapBackend::refresh()
{
    runListCommand(QStringLiteral("snap"), QStringList() << QStringLiteral("list"),
                   [this](const QStringList &rows) {
                       QStringList names;
                       for (const QString &row : rows) {
                           const QString name =
                               row.split(QRegularExpression(QStringLiteral("\\s+")),
                                         Qt::SkipEmptyParts)
                                   .value(0);
                           if (!name.isEmpty() && name != QLatin1String("Name"))
                               names << name;
                       }
                       emit packagesRefreshed(names);
                   });
}
