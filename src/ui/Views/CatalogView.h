#pragma once

#include <QList>
#include <QString>
#include <QWidget>

#include <memory>

#include "core/AppStreamManager.h"

class QShowEvent;

/**
 * Browse every app in the store, filtered by category and/or a search
 * string. A category drop-down sits on top and one AppCardWidget is shown
 * per matching component. Tiles are built lazily and progressively: nothing
 * happens until the page is first shown, and then only ~120 tiles are
 * created at a time as the user scrolls.
 */
class CatalogView final : public QWidget
{
    Q_OBJECT

public:
    explicit CatalogView(QWidget *parent = nullptr);
    ~CatalogView() override;
    Q_DISABLE_COPY_MOVE(CatalogView)

public slots:
    void setApps(const QList<AppInfo> &apps);
    void setCategoryFilter(const QString &category);
    void setSearchText(const QString &searchText);

    /**
     * Cheap in-place update: flips the installed button of matching cards
     * without recreating the whole view (used after backend refreshes).
     */
    void updateInstalledFlags(const QList<AppInfo> &apps);

signals:
    void installRequested(const AppInfo &info, const QString &backend);
    void openRequested(const AppInfo &info);
    void removeRequested(const AppInfo &info);
    void detailsRequested(const AppInfo &info);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void applyFilter();
    void repopulateSubcategories();
    void buildMore();

    class Private;
    std::unique_ptr<Private> d;
};