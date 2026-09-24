#pragma once

#include <QList>
#include <QString>
#include <QWidget>

#include <memory>

#include "core/AppStreamManager.h"

/**
 * "Inicio" screen: highlighted / top rated native applications plus the
 * latest GitHub releases. Filled with one AppCardWidget per entry.
 */
class HomeView final : public QWidget
{
    Q_OBJECT

public:
    explicit HomeView(QWidget *parent = nullptr);
    ~HomeView() override;
    Q_DISABLE_COPY_MOVE(HomeView)

public slots:
    void showApps(const QList<AppInfo> &apps);
    void setGithubApps(const QList<AppInfo> &apps);
    void setSearchText(const QString &searchText);

    /**
     * Cheap in-place update: flips the installed button of the matching cards
     * without recreating the whole view (used after backend refreshes).
     */
    void updateInstalledFlags(const QList<AppInfo> &apps);

signals:
    void installRequested(const AppInfo &info, const QString &backend);
    void openRequested(const AppInfo &info);
    void removeRequested(const AppInfo &info);
    void detailsRequested(const AppInfo &info);

private:
    void rebuild();

    class Private;
    std::unique_ptr<Private> d;
};