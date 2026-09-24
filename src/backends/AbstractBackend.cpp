#include "backends/AbstractBackend.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

#include <memory>

/**
 * Shared machinery for every package backend in LightStore.
 *
 * A backend wraps the QProcess that actually installs / removes / lists
 * software for one package technology. Subclasses decide WHAT command to
 * run (apt/dnf/pacman, flatpak, snap); this base decides HOW: a single
 * reusable QProcess that never blocks the GUI thread.
 */
class AbstractBackend::Private
{
public:
    std::unique_ptr<QProcess> proc;
    bool busyFlag = false;
    QByteArray pendingStdout;
    QString privilegePrefix;
    QString lastCommand;

    QStringList listBuffer;
    std::function<void(const QStringList &)> listCallback;

    QStringList knownInstalled;
    int lastProgress = -1;
};

namespace {
// Emits progressChanged whenever the tool prints a percentage ("35%") or an
// "n/m" step counter. No percent available → bar stays indeterminate.
int percentFromLine(const QString &line)
{
    static const QRegularExpression rePct(
        QStringLiteral("(\\d{1,3})\\s*%"));
    const auto pct = rePct.match(line);
    if (pct.hasMatch())
        return qBound(0, pct.captured(1).toInt(), 100);

    static const QRegularExpression reSlash(
        QStringLiteral("(\\d{1,4})\\s*/\\s*(\\d{1,4})"));
    const auto slash = reSlash.match(line);
    if (slash.hasMatch()) {
        const int cur = slash.captured(1).toInt();
        const int total = slash.captured(2).toInt();
        if (total > 0)
            return qBound(0, cur * 100 / total, 100);
    }
    return -1;
}
} // namespace

AbstractBackend::AbstractBackend(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

AbstractBackend::~AbstractBackend() = default;

void AbstractBackend::emitProgressFromLine(const QString &line)
{
    const int pct = percentFromLine(line);
    if (pct >= 0 && pct != d->lastProgress) {
        d->lastProgress = pct;
        emit progressChanged(pct);
    }
}

void AbstractBackend::resetProgress()
{
    d->lastProgress = -1;
}

bool AbstractBackend::managesInstall()
{
    // LightStore installs/removes the apps of its own catalog, so by
    // design the store is the installation client. Per-app gating is
    // done by the caller through AppInfo::installed.
    return true;
}

bool AbstractBackend::busy() const
{
    return d->busyFlag;
}

QString AbstractBackend::privilegePrefix() const
{
    return d->privilegePrefix;
}

void AbstractBackend::setPrivilegePrefix(const QString &prefix)
{
    d->privilegePrefix = prefix;
}

void AbstractBackend::killActiveProcess()
{
    if (d->proc)
        d->proc->kill();
}

QStringList AbstractBackend::normalizePackageList(const QStringList &rawList)
{
    QStringList out;
    for (QString pkg : rawList) {
        pkg = pkg.trimmed();
        if (pkg.isEmpty())
            continue;
        const int colon = pkg.indexOf(QLatin1Char(':'));
        if (colon > 0)
            pkg = pkg.mid(colon + 1);
        if (!out.contains(pkg))
            out << pkg;
    }
    return out;
}

bool AbstractBackend::packageExists(const QString &packageId) const
{
    return QFileInfo::exists(packageId);
}

void AbstractBackend::runProcess(const QString &program,
                                 const QStringList &args)
{
    d->listCallback = {};
    startProcess(program, args);
}

void AbstractBackend::runListCommand(
    const QString &program, const QStringList &args,
    const std::function<void(const QStringList &)> &onDone)
{
    d->listBuffer.clear();
    d->listCallback = onDone;
    startProcess(program, args);
}

void AbstractBackend::startProcess(const QString &program,
                                   const QStringList &args)
{
    if (d->busyFlag) {
        emit processOutput(tr("Operación en curso; se ha ignorado: %1 %2")
                               .arg(program, args.join(QLatin1Char(' '))));
        return;
    }
    if (!d->proc) {
        d->proc = std::make_unique<QProcess>(this);
        QObject::connect(d->proc.get(), &QProcess::readyReadStandardOutput,
                         this, [this] {
            d->pendingStdout += d->proc->readAllStandardOutput();
            int nl = d->pendingStdout.lastIndexOf('\n');
            if (nl >= 0) {
                const QByteArray ready = d->pendingStdout.left(nl);
                d->pendingStdout.remove(0, nl + 1);
                const auto lines = ready.split('\n');
                for (const QByteArray &l : lines) {
                    if (l.trimmed().isEmpty())
                        continue;
                    const QString line = QString::fromUtf8(l).trimmed();
                    emit processOutput(line);
                    emitProgressFromLine(line);
                    d->listBuffer << line;
                }
            }
        });
        QObject::connect(d->proc.get(), &QProcess::readyReadStandardError,
                         this, [this] {
            const QByteArray chunk = d->proc->readAllStandardError();
            const auto lines = chunk.split('\n');
            for (const QByteArray &l : lines) {
                if (!l.trimmed().isEmpty()) {
                    emit processOutput(QString::fromUtf8(l).trimmed());
                    emitProgressFromLine(QString::fromUtf8(l).trimmed());
                }
            }
        });
        QObject::connect(d->proc.get(), &QProcess::errorOccurred,
                         this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart && d->busyFlag) {
                d->busyFlag = false;
                d->listCallback = {};
                const QString message = tr("No se pudo iniciar el comando: %1")
                                            .arg(d->lastCommand);
                emit processOutput(message);
                emit AbstractBackend::errorOccurred(message);
                emit operationFinished(
                    false, tr("El comando no se pudo iniciar."), d->lastCommand);
            }
        });
        QObject::connect(d->proc.get(),
                         QOverload<int, QProcess::ExitStatus>::of(
                             &QProcess::finished),
                         this, [this](int exitCode, QProcess::ExitStatus status) {
            d->busyFlag = false;
            const bool ok = (exitCode == 0 && status == QProcess::NormalExit);
            const QString message =
                ok ? tr("Operación completada correctamente.")
                   : tr("El comando terminó con error (código %1).")
                         .arg(exitCode);
            d->lastProgress = 100;
            emit progressChanged(100);
            emit operationFinished(ok, message, d->lastCommand);
            if (ok && d->listCallback) {
                const auto callback = d->listCallback;
                d->listCallback = {};
                callback(normalizePackageList(d->listBuffer));
            }
        });
    }

    QStringList effectiveArgs = args;
    QString effectiveProgram = program;

    // pkexec needs the real tool as its first argument.
    if (!d->privilegePrefix.isEmpty())
        effectiveArgs.prepend(program);

    d->lastCommand =
        effectiveProgram + QLatin1Char(' ') + effectiveArgs.join(QLatin1Char(' '));
    d->busyFlag = true;
    d->lastProgress = -1;
    emit progressChanged(0);
    d->proc->setProcessChannelMode(QProcess::SeparateChannels);
    // A failed launch is reported asynchronously via errorOccurred(FailedToStart);
    // that slot clears busyFlag and surfaces the message to the UI.
    d->proc->start(effectiveProgram, effectiveArgs, QIODevice::ReadWrite);
}
