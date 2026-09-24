#include "ui/Views/AboutView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

class AboutView::Private
{
public:
    QLabel *title = nullptr;
    QLabel *version = nullptr;
    QLabel *description = nullptr;
    QLabel *sources = nullptr;
};

AboutView::AboutView(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(8);

    d->title = new QLabel(QStringLiteral("Stellar Store"), this);
    QFont titleFont = d->title->font();
    titleFont.setPointSize(titleFont.pointSize() + 10);
    titleFont.setBold(true);
    d->title->setFont(titleFont);

    d->version = new QLabel(
        tr("Versión %1").arg(QStringLiteral(APP_VERSION)), this);

    d->description = new QLabel(
        tr("Tienda de software ultraligera para Linux, escrita en "
           "C++17 con Qt Widgets (6.x / 5.15). Permite buscar e "
           "instalar aplicaciones desde varios orígenes en una sola "
           "interfaz simple y rápida."),
        this);
    d->description->setWordWrap(true);
    d->description->setTextInteractionFlags(Qt::TextSelectableByMouse);

    d->sources = new QLabel(
        tr("Fuentes de paquetes:\n"
           "  · AppStream (información y catálogo)\n"
           "  · apt / dnf / pacman (paquetes nativos)\n"
           "  · Flatpak\n"
           "  · Snap\n"
           "  · GitHub Releases"),
        this);
    d->sources->setTextInteractionFlags(Qt::TextSelectableByMouse);

    root->addWidget(d->title);
    root->addWidget(d->version);
    root->addSpacing(12);
    root->addWidget(d->description);
    root->addSpacing(8);
    root->addWidget(d->sources);
    root->addStretch(1);
}

AboutView::~AboutView() = default;