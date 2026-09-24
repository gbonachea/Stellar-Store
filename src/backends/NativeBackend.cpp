#include "backends/NativeBackend.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

/**
 * Native tool backend — installs / removes / lists packages using the
 * distribution's own package manager, chosen by probing the executables
 * /usr/bin/apt-get, /usr/bin/dnf and /usr/bin/pacman in this order.
 *
 * All commands that modify the system run with "pkexec" (policykit), so the
 * user gets one clean authorisation dialog per operation. Nothing blocks the
 * UI: every call funnels through AbstractBackend::runProcess.
 */
class NativeBackend::Private
{
public:
    enum class Tool { Apt, Dnf, Pacman, Unknown };

    Tool detectTool() const
    {
        for (const char *candidate : {"/usr/bin/apt-get",
                                      "/usr/bin/dnf",
                                      "/usr/bin/pacman"}) {
            if (QFileInfo::exists(QLatin1String(candidate))) {
                if (QLatin1String(candidate).contains(QLatin1String("apt")))
                    return Tool::Apt;
                if (QLatin1String(candidate).contains(QLatin1String("dnf")))
                    return Tool::Dnf;
                return Tool::Pacman;
            }
        }
        return Tool::Unknown;
    }

    QString toolExecutable(Tool tool, bool &viaPkexec,
                           QStringList &effectiveArgs,
                           const QStringList &userArgs)
    {
        viaPkexec = true;
        switch (tool) {
        case Tool::Apt:
            effectiveArgs = QStringList{QLatin1String("apt-get")} + userArgs;
            return QLatin1String("pkexec");
        case Tool::Dnf:
            effectiveArgs = QStringList{QLatin1String("dnf")} + userArgs;
            return QLatin1String("pkexec");
        case Tool::Pacman:
            effectiveArgs = QStringList{QLatin1String("pacman")} + userArgs;
            return QLatin1String("pkexec");
        default:
            break;
        }
        viaPkexec = false;
        return QString();
    }
};

NativeBackend::NativeBackend(QObject *parent)
    : AbstractBackend(parent)
{
}

NativeBackend::~NativeBackend() = default;

QString NativeBackend::backendName() const
{
    return QStringLiteral("native");
}

QString NativeBackend::displayName() const
{
    return QStringLiteral("Paquetes nativos");
}

bool NativeBackend::isAvailable() const
{
    Private probe;
    return probe.detectTool() != Private::Tool::Unknown;
}

QStringList NativeBackend::supportedRemotes() const
{
    return QStringList() << QStringLiteral("repositorios del sistema");
}

void NativeBackend::installPackage(const QString &packageId)
{
    Private probe;
    const auto tool = probe.detectTool();
    if (tool == Private::Tool::Unknown) {
        emit errorOccurred(QStringLiteral(
            "No se ha encontrado gestor nativo (apt-get, dnf o pacman)."));
        return;
    }

    bool viaPkexec = false;
    QStringList args;
    const QString program = probe.toolExecutable(
        tool, viaPkexec, args,
        QStringList() << QStringLiteral("install") << QStringLiteral("-y")
                      << packageId);
    runProcess(program, args);
}

void NativeBackend::removePackage(const QString &packageId)
{
    Private probe;
    const auto tool = probe.detectTool();
    if (tool == Private::Tool::Unknown) {
        emit errorOccurred(QStringLiteral(
            "No se ha encontrado gestor nativo (apt-get, dnf o pacman)."));
        return;
    }

    bool viaPkexec = false;
    QStringList args;
    const QString program = probe.toolExecutable(
        tool, viaPkexec, args,
        QStringList() << QStringLiteral("remove") << QStringLiteral("-y")
                      << packageId);
    runProcess(program, args);
}

void NativeBackend::refresh()
{
    Private probe;
    const auto tool = probe.detectTool();
    if (tool == Private::Tool::Unknown) {
        emit packagesRefreshed(QStringList());
        return;
    }

    QString program;
    QStringList args;
    switch (tool) {
    case Private::Tool::Apt:
        program = QStringLiteral("dpkg-query");
        args = QStringList() << QStringLiteral("-W")
                             << QStringLiteral("-f=${Package}\n");
        break;
    case Private::Tool::Dnf:
        program = QStringLiteral("rpm");
        args = QStringList() << QStringLiteral("-qa")
                             << QStringLiteral("--qf")
                             << QStringLiteral("%{NAME}\n");
        break;
    case Private::Tool::Pacman:
        program = QStringLiteral("pacman");
        args = QStringList() << QStringLiteral("-Qq");
        break;
    default:
        emit packagesRefreshed(QStringList());
        return;
    }

    runListCommand(program, args, [this](const QStringList &list) {
        emit packagesRefreshed(list);
    });
}

void NativeBackend::installLocalFile(const QString &filePath)
{
    Private probe;
    const auto tool = probe.detectTool();
    if (tool == Private::Tool::Unknown) {
        emit errorOccurred(QStringLiteral(
            "No se ha encontrado gestor nativo (apt-get, dnf o pacman)."));
        return;
    }

    bool viaPkexec = false;
    QStringList args;
    QString program;
    switch (tool) {
    case Private::Tool::Apt:
        program = probe.toolExecutable(
            tool, viaPkexec, args,
            QStringList() << QStringLiteral("install")
                          << QStringLiteral("-y") << filePath);
        break;
    case Private::Tool::Dnf:
        program = probe.toolExecutable(
            tool, viaPkexec, args,
            QStringList() << QStringLiteral("install")
                          << QStringLiteral("-y") << filePath);
        break;
    case Private::Tool::Pacman:
        program = probe.toolExecutable(
            tool, viaPkexec, args,
            QStringList() << QStringLiteral("--noconfirm")
                          << QStringLiteral("-U") << filePath);
        break;
    default:
        break;
    }
    runProcess(program, args);
}
