#pragma once

#include "backends/AbstractBackend.h"

/**
 * Backend that operates on the distro's *native* package tooling:
 *
 *   - Debian / Ubuntu / Mint : apt + dpkg
 *   - Fedora / RHEL          : dnf + rpm
 *   - Arch / Manjaro         : pacman
 *
 * The concrete tool is chosen at first use by probing the executables
 * /usr/bin/apt-get, /usr/bin/dnf and /usr/bin/pacman in order. All mutating
 * commands are prefixed with "pkexec" (@ref AbstractBackend::privilegePrefix)
 * so the user gets a clean policykit authorisation dialog.
 *
 * @note The backend never blocks. Every install/remove/refresh runs through
 *       the shared QProcess machinery in AbstractBackend.
 */
class NativeBackend final : public AbstractBackend
{
    Q_OBJECT

public:
    explicit NativeBackend(QObject *parent = nullptr);
    ~NativeBackend() override;
    Q_DISABLE_COPY_MOVE(NativeBackend)

    // overrides ------------------------------------------------------------
    QString backendName() const override;
    QString displayName() const override;
    bool isAvailable() const override;
    QStringList supportedRemotes() const override;

    void installPackage(const QString &packageId) override;
    void removePackage(const QString &packageId) override;
    void refresh() override;

    // native-specific helpers ----------------------------------------------
    /**
     * Install a locally downloaded .deb / .rpm file (the package manager
     * fetches any missing dependencies from the configured repositories).
     * @param filePath absolute path to the local package file.
     */
    void installLocalFile(const QString &filePath);

private:
    enum class Tool { Apt, Dnf, Pacman, Unknown };
    Tool detectTool() const;
    QString toolExecutable(Tool tool) const;
};
