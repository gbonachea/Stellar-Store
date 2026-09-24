#pragma once

#include <QDateTime>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include <memory>

#include "backends/AbstractBackend.h"
#include "core/AppStreamManager.h"

namespace LightStore {

/**
 * One tile in the catalog / home grid.
 *
 * Shows the 64x64 icon, the localized name, a one-line summary and an action
 * button that mutates the package through the owning backend. The widget
 * never talks to the backend itself " TextureOfDoom?" -- it only forwards
 * install / remove requests; the view wires them to the right backend.
 */
class AppCardWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit AppCardWidget(QWidget *parent = nullptr);
    ~AppCardWidget() override;
    Q_DISABLE_COPY_MOVE(AppCardWidget)

    const AppInfo &appInfo() const;

public slots:
    void setAppInfo(const AppInfo &info);

    /**
     * Cheap in-place toggle of the installed state without re-resolving the
     * icon or rebuilding the backend combo. Views call this after a backend
     * refresh instead of recreating every card in the catalog.
     */
    void setInstalled(bool installed);
    void setProgress(int percent);
    void setBusy(bool busy);

signals:
    /**
     * @param info    the app the user wants to install.
     * @param backend selected package type: "native", "flatpak", "snap",
     *                "github" or an empty string to let the owner decide.
     */
    void installRequested(const AppInfo &info, const QString &backend);
    void openRequested(const AppInfo &info);
    void removeRequested(const AppInfo &info);
    void detailsRequested(const AppInfo &info);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void onInstallClicked();
    void onRemoveClicked();
    void showPlaceholder();

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LightStore
