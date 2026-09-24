#include "ui/Views/InstalledView.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "core/DesktopEntry.h"
#include "ui/Widgets/AppCardWidget.h"
#include "ui/Widgets/FlowLayout.h"

class InstalledView::Private
{
public:
    QStringList installed;
    QScrollArea *scroll = nullptr;
    QWidget *content = nullptr;
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

    rebuild();
}

InstalledView::~InstalledView() = default;

void InstalledView::setInstalledPackages(const QStringList &packages)
{
    d->installed = packages;
    rebuild();
}

void InstalledView::rebuild()
{
    if (d->scroll->widget() == d->content)
        d->scroll->setWidget(nullptr);
    delete d->content;
    d->content = new QWidget(this);

    auto *grid = new FlowLayout(d->content);
    grid->setContentsMargins(16, 16, 16, 16);
    grid->setSpacing(12);

    int i = 0;
    for (const QString &pkg : std::as_const(d->installed)) {
        const QString desktopFile = findDesktopFile(pkg);
        if (desktopFile.isEmpty())
            continue;

        AppInfo info;
        info.id = pkg;
        info.name = pkg;
        info.summary = tr("Instalado");
        info.installed = true;
        const QString icon = desktopIcon(desktopFile);
        if (icon.startsWith(QLatin1Char('/')))
            info.iconPath = icon;
        else
            info.iconName = icon;

        auto *card = new LightStore::AppCardWidget(d->content);
        card->setAppInfo(info);

        connect(card, &LightStore::AppCardWidget::openRequested,
                this, &InstalledView::openRequested);
        connect(card, &LightStore::AppCardWidget::removeRequested,
                this, [this](const AppInfo &info) {
                    emit removeRequested(info.id);
                });
        grid->addWidget(card);
        ++i;
    }

    if (i == 0) {
        auto *empty = new QLabel(
            tr("No se han detectado paquetes instalados."), d->content);
        empty->setWordWrap(true);
        grid->addWidget(empty);
    }

    d->scroll->setWidget(d->content);
}