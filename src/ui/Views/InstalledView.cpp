#include "ui/Views/InstalledView.h"

#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "core/DesktopEntry.h"
#include "ui/Widgets/AppCardWidget.h"
#include "ui/Widgets/FlowLayout.h"

class InstalledView::Private
{
public:
    QList<AppInfo> apps;
    QScrollArea *scroll = nullptr;
    QWidget *content = nullptr;
    FlowLayout *grid = nullptr;
};

InstalledView::InstalledView(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    d->scroll = new QScrollArea(this);
    d->scroll->setWidgetResizable(true);
    root->addWidget(d->scroll);

    d->content = new QWidget(this);
    d->content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *contentLayout = new QVBoxLayout(d->content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    d->grid = new FlowLayout;
    d->grid->setContentsMargins(16, 16, 16, 16);
    d->grid->setSpacing(12);
    contentLayout->addLayout(d->grid);

    d->scroll->setWidget(d->content);

    rebuild();
}

InstalledView::~InstalledView() = default;

void InstalledView::setInstalledApps(const QList<AppInfo> &apps)
{
    d->apps = apps;
    rebuild();
}

void InstalledView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    qWarning() << "InstalledView::showEvent visible=" << isVisible()
               << "apps=" << d->apps.size()
               << "children=" << d->content->children().size()
               << "scrollWidget=" << d->scroll->widget();
}

void InstalledView::rebuild()
{
    // Clear existing cards.
    QLayoutItem *item;
    while ((item = d->grid->takeAt(0)) != nullptr) {
        if (item->widget())
            delete item->widget();
        delete item;
    }

    for (const AppInfo &installedApp : std::as_const(d->apps)) {
        AppInfo info = installedApp;
        info.installed = true;

        QString desktopFile = info.desktopFile;
        if (desktopFile.isEmpty())
            desktopFile = findDesktopFile(info.id);
        if (desktopFile.isEmpty() && !info.packageNames.isEmpty())
            desktopFile = findDesktopFile(info.packageNames.constFirst());

        if (!desktopFile.isEmpty()) {
            DesktopEntry entry;
            if (parseDesktopEntry(desktopFile, entry)) {
                if (info.name.isEmpty() || info.name == info.id)
                    info.name = entry.name;
                if (info.summary.isEmpty())
                    info.summary = entry.summary;
                if (info.iconPath.isEmpty() && !entry.iconPath.isEmpty())
                    info.iconPath = entry.iconPath;
                if (info.iconName.isEmpty() && !entry.iconName.isEmpty())
                    info.iconName = entry.iconName;
            }
            info.desktopFile = desktopFile;
        }

        if (info.name.isEmpty())
            info.name = info.id;
        if (info.summary.isEmpty())
            info.summary = tr("Instalado");

        auto *card = new LightStore::AppCardWidget(d->content);
        card->setAppInfo(info);

        connect(card, &LightStore::AppCardWidget::openRequested,
                this, &InstalledView::openRequested);
        connect(card, &LightStore::AppCardWidget::removeRequested,
                this, &InstalledView::removeRequested);
        d->grid->addWidget(card);
    }

    if (d->apps.isEmpty()) {
        auto *empty = new QLabel(
            tr("No se han detectado aplicaciones instaladas."), d->content);
        empty->setWordWrap(true);
        d->grid->addWidget(empty);
    }
}