#pragma once

#include <QStringList>
#include <QWidget>

#include <memory>

#include "core/AppStreamManager.h"

/**
 * "Instaladas" screen: one card per currently installed package, each with
 * an action to remove it through the matching backend.
 */
class InstalledView final : public QWidget
{
    Q_OBJECT

public:
    explicit InstalledView(QWidget *parent = nullptr);
    ~InstalledView() override;
    Q_DISABLE_COPY_MOVE(InstalledView)

public slots:
    void setInstalledPackages(const QStringList &packages);

signals:
    void openRequested(const AppInfo &info);
    void removeRequested(const QString &packageId);

private:
    void rebuild();

    class Private;
    std::unique_ptr<Private> d;
};