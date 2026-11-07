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
                             << QStringLiteral("-f=${db:Status-Abbrev} ${Package}\n");
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
        // dpkg-query -W lists every record in the dpkg database, including
        // packages that were removed but left their config files behind
        // (status "rc"). Only "ii" (installed) packages belong in the
        // installed set.
        QStringList installed;
        for (const QString &line : list) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (trimmed.startsWith(QLatin1String("ii "))) {
                const QString pkg = trimmed.mid(3).trimmed();
                if (!pkg.isEmpty() && !installed.contains(pkg))
                    installed << pkg;
            } else if (!trimmed.startsWith(QLatin1String("rc "))
                       && !trimmed.startsWith(QLatin1String("iU "))
                       && !trimmed.contains(QLatin1Char(' '))) {
                // Non-apt backends (or unexpected lines) report bare names.
                if (!installed.contains(trimmed))
                    installed << trimmed;
            }
        }
        emit packagesRefreshed(installed);
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

QString NativeBackend::packageOwningFile(const QString &filePath) const
{
    if (filePath.isEmpty())
        return QString();

    Private probe;
    const auto tool = probe.detectTool();
    if (tool == Private::Tool::Unknown)
        return QString();

    QString program;
    QStringList args;
    switch (tool) {
    case Private::Tool::Apt:
        program = QStringLiteral("dpkg-query");
        args = QStringList() << QStringLiteral("-S") << filePath;
        break;
    case Private::Tool::Dnf:
        program = QStringLiteral("rpm");
        args = QStringList() << QStringLiteral("-qf")
                             << QStringLiteral("--qf")
                             << QStringLiteral("%{NAME}\n")
                             << filePath;
        break;
    case Private::Tool::Pacman:
        program = QStringLiteral("pacman");
        args = QStringList() << QStringLiteral("-Qo") << filePath;
        break;
    default:
        return QString();
    }

    QProcess process;
    process.start(program, args);
    if (!process.waitForFinished(2500))
        return QString();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return QString();

    QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (output.isEmpty())
        return QString();

    if (tool == Private::Tool::Apt) {
        // "package: /path" or "package1, package2: /path"
        const int colon = output.indexOf(QLatin1Char(':'));
        if (colon > 0)
            output.truncate(colon);
        output = output.section(QLatin1Char(','), 0, 0).trimmed();
    } else if (tool == Private::Tool::Pacman) {
        // "/path is owned by package 1.2.3-1"
        const QString marker = QStringLiteral(" is owned by ");
        const int pos = output.indexOf(marker);
        if (pos >= 0) {
            output = output.mid(pos + marker.size());
            output = output.section(QLatin1Char(' '), 0, 0).trimmed();
        }
    } else {
        output = output.section(QLatin1Char('\n'), 0, 0).trimmed();
    }

    return output;
}
