#pragma once

#include "backends/AbstractBackend.h"

/**
 * Snap backend.
 *
 * Wraps the "snap" CLI. Mutating operations use "pkexec". Everything remains
 * asynchronous via AbstractBackend.
 */
class SnapBackend final : public AbstractBackend
{
    Q_OBJECT

public:
    explicit SnapBackend(QObject *parent = nullptr);
    ~SnapBackend() override;
    Q_DISABLE_COPY_MOVE(SnapBackend)

    QString backendName() const override;
    QString displayName() const override;
    bool isAvailable() const override;
    QStringList supportedRemotes() const override;

    void installPackage(const QString &packageId) override;
    void removePackage(const QString &packageId) override;
    void refresh() override;
};
