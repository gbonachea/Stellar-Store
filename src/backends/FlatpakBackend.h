#pragma once

#include "backends/AbstractBackend.h"

/**
 * Flatpak backend.
 *
 * Wraps the "flatpak" CLI (system-wide Flathub style installation). Commands
 * run through "pkexec" when mutating the system; user-scope installs use the
 * plain binary. Everything stays asynchronous via AbstractBackend.
 */
class FlatpakBackend final : public AbstractBackend
{
    Q_OBJECT

public:
    explicit FlatpakBackend(QObject *parent = nullptr);
    ~FlatpakBackend() override;
    Q_DISABLE_COPY_MOVE(FlatpakBackend)

    QString backendName() const override;
    QString displayName() const override;
    bool isAvailable() const override;
    QStringList supportedRemotes() const override;

    void installPackage(const QString &packageId) override;
    void removePackage(const QString &packageId) override;
    void refresh() override;
};
