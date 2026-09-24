#pragma once

#include <QDialog>
#include <QString>

#include <memory>

#include "core/AppStreamManager.h"

/**
 * Detail dialog for one application. Shows the icon, name, summary,
 * description, screenshots (downloaded asynchronously when remote) and lets
 * the user choose which package backend to use (native / Flatpak / Snap)
 * before installing.
 */
class AppDetailDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AppDetailDialog(const AppInfo &info, QWidget *parent = nullptr);
    ~AppDetailDialog() override;
    Q_DISABLE_COPY_MOVE(AppDetailDialog)

signals:
    void installRequested(const QString &appId, const QString &backend);
    void openRequested(const AppInfo &info);
    void removeRequested(const AppInfo &info);

private:
    class Private;
    std::unique_ptr<Private> d;
};