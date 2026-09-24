#include "ui/Views/HomeView.h"

#include <QGridLayout>
#include <QHash>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "ui/Widgets/AppCardWidget.h"
#include "ui/Widgets/FlowLayout.h"

class HomeView::Private
{
public:
    QList<AppInfo> apps;
    QList<AppInfo> githubApps;
    QString search;

    QScrollArea *scroll = nullptr;
    QWidget *content = nullptr;
    QHash<QString, LightStore::AppCardWidget *> cardsById;
};

HomeView::HomeView(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    d->scroll = new QScrollArea(this);
    d->scroll->setWidgetResizable(true);
    root->addWidget(d->scroll);

    rebuild();
}

HomeView::~HomeView() = default;

void HomeView::showApps(const QList<AppInfo> &apps)
{
    d->apps = apps;
    rebuild();
}

void HomeView::setGithubApps(const QList<AppInfo> &apps)
{
    d->githubApps = apps;
    rebuild();
}

void HomeView::setSearchText(const QString &searchText)
{
    d->search = searchText.trimmed();
    rebuild();
}

void HomeView::updateInstalledFlags(const QList<AppInfo> &apps)
{
    for (const AppInfo &app : std::as_const(apps)) {
        const auto it = d->cardsById.find(app.id);
        if (it != d->cardsById.end())
            it.value()->setInstalled(app.installed);
    }
}

void HomeView::rebuild()
{
    if (d->scroll->widget() == d->content)
        d->scroll->setWidget(nullptr);
    delete d->content;
    d->content = new QWidget(this);
    d->cardsById.clear();

    auto *layout = new QVBoxLayout(d->content);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    auto addSection = [this, layout](const QString &title,
                                     const QList<AppInfo> &source) {
        QList<AppInfo> filtered;
        for (const AppInfo &app : source) {
            if (!d->search.isEmpty()
                && !app.name.contains(d->search, Qt::CaseInsensitive))
                continue;
            filtered << app;
        }
        if (filtered.isEmpty())
            return;

        auto *titleLabel = new QLabel(title, d->content);
        titleLabel->setStyleSheet(QStringLiteral("font-weight:600;font-size:15px;"));
        layout->addWidget(titleLabel);

        auto *grid = new FlowLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(12);
        for (const AppInfo &app : std::as_const(filtered)) {
            auto *card = new LightStore::AppCardWidget(d->content);
            card->setAppInfo(app);
            d->cardsById.insert(app.id, card);
            connect(card, &LightStore::AppCardWidget::installRequested,
                    this, &HomeView::installRequested);
            connect(card, &LightStore::AppCardWidget::openRequested,
                    this, &HomeView::openRequested);
            connect(card, &LightStore::AppCardWidget::removeRequested,
                    this, &HomeView::removeRequested);
            connect(card, &LightStore::AppCardWidget::detailsRequested,
                    this, &HomeView::detailsRequested);
            grid->addWidget(card);
        }
        layout->addLayout(grid);
    };

    // The Home screen only needs a handful of tiles; a full catalog is built
    // lazily by CatalogView. Keeping this capped keeps startup light even when
    // the AppStream pool holds thousands of components.
    addSection(tr("Destacadas"), d->apps.mid(0, 8));
    addSection(tr("Más Valoradas"), d->apps.mid(8, 24));
    addSection(tr("Novedades de GitHub"), d->githubApps);

    if (layout->count() == 0) {
        auto *empty = new QLabel(
            tr("Aún no hay aplicaciones. Usa el botón de refresco."), d->content);
        empty->setWordWrap(true);
        layout->addWidget(empty);
    }

    d->scroll->setWidget(d->content);
}