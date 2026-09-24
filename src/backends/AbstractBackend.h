#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

/**
 * Common behaviour for every package backend in LightStore.
 *
 * A backend wraps the process that actually installs / removes / lists
 * software for one package technology, so the rest of the store talks to a
 * single interface regardless of whether the underlying tool is apt/dnf/pacman,
 * flatpak or snap.
 *
 * Contract for subclasses:
 *   - @ref backendName      : stable identifier ("native", "flatpak", ...).
 *   - @ref displayName      : human readable name ("Paquetes nativos", ...).
 *   - @ref isAvailable      : whether the tool binary exists on this system.
 *   - @ref installPackage   : install the given package id.
 *   - @ref removePackage    : uninstall the given package id.
 *   - @ref refresh          : fetch the list of currently installed packages.
 *
 * All heavy lifting is asynchronous. Subclasses start a QProcess and stream
 * progress/output through the signals below; the GUI thread is never blocked.
 */
class AbstractBackend : public QObject
{
    Q_OBJECT

public:
    explicit AbstractBackend(QObject *parent = nullptr);
    ~AbstractBackend() override;
    Q_DISABLE_COPY_MOVE(AbstractBackend)

    // ------------------------------------------------------------ identity
    virtual QString backendName() const = 0;
    virtual QString displayName() const = 0;
    virtual bool isAvailable() const = 0;

    /**
     * True when this store is the installation client for the apps of its
     * catalog (i.e. install/remove goes through LightStore itself). Cards use
     * it to hide the install button when a backend merely lists foreign apps.
     */
    static bool managesInstall();

    /**
     * URLs of additional remotes this backend can reach (e.g. Flathub).
     * Used by the catalog to label "remote-sourced" packages.
     */
    virtual QStringList supportedRemotes() const = 0;

    // ------------------------------------------------------------ actions
    virtual void installPackage(const QString &packageId) = 0;
    virtual void removePackage(const QString &packageId) = 0;
    virtual void refresh() = 0;

    /**
     * Abort the currently running QProcess (if the backend is mid-operation).
     */
    void killActiveProcess();

    /**
     * True when a QProcess owned by this backend is still running.
     */
    bool busy() const;

signals:
    /**
     * One fresh line of output from the underlying tool (for the log view).
     */
    void processOutput(const QString &line);

    /**
     * Coarse progress 0..100, when the tool reports meaningful percentages.
     */
    void progressChanged(int percent);

    /**
     * A command finished.
     * @param success  exit code == 0.
     * @param message  human readable result ("Listo.", "Error: …").
     * @param command  the exact command line that was executed.
     */
    void operationFinished(bool success, const QString &message,
                           const QString &command);

    /**
     * The installed-package list changed after a @ref refresh.
     */
    void packagesRefreshed(const QStringList &installedPackages);

    /** Any user-facing error from this backend. */
    void errorOccurred(const QString &message);

protected:
    // ------------------------------------------------------------ helpers
    /**
     * Text that must be prepended to every command to gain root rights
     * ("pkexec", "sudo -n", "doas" or empty when not needed).
     */
    QString privilegePrefix() const;

    /**
     * Change the privilege prefix used by this backend.
     */
    void setPrivilegePrefix(const QString &prefix);

    /**
     * Filter/trim a raw installed-package list coming from the tool into a
     * clean, de-duplicated QStringList ready for the catalog model.
     */
    QStringList normalizePackageList(const QStringList &rawList);

    /**
     * True when @p packageId was found in the last @ref normalizePackageList
     * result (cheap lookup used by the model to mark installed items).
     */
    bool packageExists(const QString &packageId) const;

    /**
     * Launch @p program with @p args through QProcess, fully asynchronous.
     * Output is streamed via @ref processOutput, completion via
     * @ref operationFinished. Subclasses simply call this from their
     * install/remove/refresh implementations.
     */
    void runProcess(const QString &program, const QStringList &args);

    /**
     * Like @ref runProcess but additionally collects the full standard-output
     * body and hands the normalized (trimmed, de-duplicated) line list to
     * @p onDone once the process finishes. Made for read-only listing
     * commands ("what is installed?").
     */
    void runListCommand(const QString &program, const QStringList &args,
                        const std::function<void(const QStringList &)> &onDone);

    /**
     * Core process launcher shared by @ref runProcess and
     * @ref runListCommand.
     */
    void startProcess(const QString &program, const QStringList &args);

    /**
     * Parse one line of tool output for a progress indicator ("35%", "n/m")
     * and emit @ref progressChanged when a meaningful value is found.
     */
    void emitProgressFromLine(const QString &line);

    /**
     * Forget the last emitted progress value (call before a new operation).
     */
    void resetProgress();

protected:
    /**
     * PIMPL storage shared with derived backends. Subclasses are granted
     * access because they all funnel through the same QProcess machinery.
     */
    class Private;
    std::unique_ptr<Private> d;
};
