#pragma once

#include <QList>
#include <QShowEvent>
#include <QWidget>

#include <memory>

#include "core/AppStreamManager.h"

/**
 * "Instaladas" screen: one card per currently installed application, each with
 * an action to remove it through the backend that actually owns it.
 */
class InstalledView final : public QWidget
{
    Q_OBJECT

public:
    explicit InstalledView(QWidget *parent = nullptr);
    ~InstalledView() override;
    Q_DISABLE_COPY_MOVE(InstalledView)

public slots:
    void setInstalledApps(const QList<AppInfo> &apps);

signals:
    void openRequested(const AppInfo &info);
    void removeRequested(const AppInfo &info);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void rebuild();

    class Private;
    std::unique_ptr<Private> d;
};
