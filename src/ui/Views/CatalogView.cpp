#include "ui/Views/CatalogView.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QVBoxLayout>

#include "ui/Widgets/AppCardWidget.h"
#include "ui/Widgets/FlowLayout.h"

namespace {

bool isMainCategory(const QString &cat)
{
    static const QSet<QString> mains = {
        QStringLiteral("Development"), QStringLiteral("Game"),
        QStringLiteral("Graphics"),   QStringLiteral("Network"),
        QStringLiteral("AudioVideo"), QStringLiteral("System"),
        QStringLiteral("Utility"),    QStringLiteral("Office"),
        QStringLiteral("Education"),  QStringLiteral("Settings"),
        QStringLiteral("Science"),
    };
    return mains.contains(cat);
}

QString categoryLabel(const QString &cat)
{
    static const QHash<QString, QString> labels = {
        { QStringLiteral("WebBrowser"),        QObject::tr("Navegador web") },
        { QStringLiteral("Mail"),              QObject::tr("Correo") },
        { QStringLiteral("InstantMessaging"),  QObject::tr("Mensajería") },
        { QStringLiteral("Chat"),              QObject::tr("Chat") },
        { QStringLiteral("FileTransfer"),      QObject::tr("Transferencia de archivos") },
        { QStringLiteral("P2P"),               QObject::tr("P2P") },
        { QStringLiteral("RemoteAccess"),      QObject::tr("Acceso remoto") },
        { QStringLiteral("Feed"),              QObject::tr("RSS / Feeds") },
        { QStringLiteral("News"),              QObject::tr("Noticias") },
        { QStringLiteral("Monitor"),           QObject::tr("Monitorización") },
        { QStringLiteral("Security"),          QObject::tr("Seguridad") },
        { QStringLiteral("Audio"),             QObject::tr("Audio") },
        { QStringLiteral("Video"),             QObject::tr("Video") },
        { QStringLiteral("Music"),             QObject::tr("Música") },
        { QStringLiteral("Player"),            QObject::tr("Reproductor") },
        { QStringLiteral("Mixer"),             QObject::tr("Mezcla de audio") },
        { QStringLiteral("Sequencer"),         QObject::tr("Secuenciador") },
        { QStringLiteral("Midi"),              QObject::tr("MIDI") },
        { QStringLiteral("Recorder"),          QObject::tr("Grabadora") },
        { QStringLiteral("TV"),                QObject::tr("Televisión") },
        { QStringLiteral("Photography"),       QObject::tr("Fotografía") },
        { QStringLiteral("RasterGraphics"),    QObject::tr("Gráficos raster") },
        { QStringLiteral("VectorGraphics"),    QObject::tr("Gráficos vectoriales") },
        { QStringLiteral("Viewer"),            QObject::tr("Visor") },
        { QStringLiteral("2DGraphics"),        QObject::tr("Gráficos 2D") },
        { QStringLiteral("3DGraphics"),        QObject::tr("Gráficos 3D") },
        { QStringLiteral("IDE"),               QObject::tr("IDE") },
        { QStringLiteral("Building"),          QObject::tr("Compilación") },
        { QStringLiteral("Debugger"),          QObject::tr("Depurador") },
        { QStringLiteral("RevisionControl"),   QObject::tr("Control de versiones") },
        { QStringLiteral("ProjectManagement"), QObject::tr("Gestión de proyectos") },
        { QStringLiteral("Database"),          QObject::tr("Bases de datos") },
        { QStringLiteral("TextEditor"),        QObject::tr("Editor de texto") },
        { QStringLiteral("ArcadeGame"),        QObject::tr("Arcade") },
        { QStringLiteral("BoardGame"),         QObject::tr("De mesa") },
        { QStringLiteral("CardGame"),          QObject::tr("Cartas") },
        { QStringLiteral("KidsGame"),          QObject::tr("Infantiles") },
        { QStringLiteral("LogicGame"),         QObject::tr("Lógica") },
        { QStringLiteral("RolePlaying"),       QObject::tr("Rol") },
        { QStringLiteral("Simulation"),        QObject::tr("Simulación") },
        { QStringLiteral("Sport"),             QObject::tr("Deportes") },
        { QStringLiteral("StrategyGame"),      QObject::tr("Estrategia") },
        { QStringLiteral("Emulator"),          QObject::tr("Emulador") },
        { QStringLiteral("WordProcessor"),     QObject::tr("Procesador de texto") },
        { QStringLiteral("Spreadsheet"),       QObject::tr("Hoja de cálculo") },
        { QStringLiteral("Presentation"),      QObject::tr("Presentaciones") },
        { QStringLiteral("Finance"),           QObject::tr("Finanzas") },
        { QStringLiteral("Calculator"),        QObject::tr("Calculadora") },
        { QStringLiteral("DesktopSettings"),   QObject::tr("Configuración de escritorio") },
        { QStringLiteral("Accessibility"),     QObject::tr("Accesibilidad") },
        { QStringLiteral("Core"),              QObject::tr("Utilidades del sistema") },
        { QStringLiteral("Utility"),           QObject::tr("Utilidades") },
    };
    return labels.value(cat, cat);
}

} // namespace

class CatalogView::Private
{
public:
    QList<AppInfo> apps;
    QString category;      // main AppStream category id
    QString subCategory;   // sub-category id (may be empty)
    QString search;

    // True after the page has been shown once. Until then setApps only stores
    // the data so opening the tab builds tiles from the freshest list.
    bool hasBuilt = false;

    // Matches the current category/sub-category/search filter. Cards are
    // created progressively (chunked) so opening a huge catalog never blocks
    // the GUI or allocates thousands of widgets at once.
    QList<AppInfo> filtered;
    int builtCount = 0;
    static constexpr int chunkSize = 120;

    QComboBox *combo = nullptr;
    QComboBox *subCombo = nullptr;
    QScrollArea *scroll = nullptr;
    QWidget *content = nullptr;
    FlowLayout *grid = nullptr;
    QLabel *statusLabel = nullptr;
    QHash<QString, LightStore::AppCardWidget *> cardsById;
};

CatalogView::CatalogView(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    d->combo = new QComboBox(this);
    d->combo->addItem(tr("Todas las categorías"), QString());
    d->combo->addItem(tr("Desarrollo"), QStringLiteral("Development"));
    d->combo->addItem(tr("Juegos"), QStringLiteral("Game"));
    d->combo->addItem(tr("Gráficos"), QStringLiteral("Graphics"));
    d->combo->addItem(tr("Internet"), QStringLiteral("Network"));
    d->combo->addItem(tr("Multimedia"), QStringLiteral("AudioVideo"));
    d->combo->addItem(tr("Oficina"), QStringLiteral("Office"));
    d->combo->addItem(tr("Educación"), QStringLiteral("Education"));
    d->combo->addItem(tr("Configuración"), QStringLiteral("Settings"));
    d->combo->addItem(tr("Ciencia"), QStringLiteral("Science"));
    d->combo->addItem(tr("Sistema"), QStringLiteral("System"));
    d->combo->addItem(tr("Utilidades"), QStringLiteral("Utility"));
    d->combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    d->subCombo = new QComboBox(this);
    d->subCombo->addItem(tr("Todas las subcategorías"), QString());
    d->subCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    d->subCombo->setEnabled(false);

    auto *comboRow = new QHBoxLayout;
    comboRow->setContentsMargins(12, 8, 12, 8);
    comboRow->setSpacing(8);
    comboRow->addWidget(d->combo);
    comboRow->addWidget(d->subCombo);
    comboRow->addStretch(1);

    d->scroll = new QScrollArea(this);
    d->scroll->setWidgetResizable(true);

    root->addLayout(comboRow);
    root->addWidget(d->scroll, 1);

    connect(d->combo, &QComboBox::currentIndexChanged, this, [this](int) {
        d->category = d->combo->currentData().toString();
        repopulateSubcategories();
        applyFilter();
    });
    connect(d->subCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        d->subCategory = d->subCombo->currentData().toString();
        applyFilter();
    });

    // Progressive loading: reach the bottom of the scroll area -> build the
    // next chunk of tiles. No full catalog is ever built in one pass.
    connect(d->scroll->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int value) {
                if (d->hasBuilt && d->builtCount < d->filtered.size()
                    && value >= d->scroll->verticalScrollBar()->maximum() - 200) {
                    buildMore();
                }
            });

    // The page starts empty: tiles are only created on first show.
}

CatalogView::~CatalogView() = default;

void CatalogView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (d->hasBuilt)
        return;
    d->hasBuilt = true;
    repopulateSubcategories();
    applyFilter();
}

void CatalogView::setApps(const QList<AppInfo> &apps)
{
    d->apps = apps;
    if (!d->hasBuilt)
        return;
    repopulateSubcategories();
    applyFilter();
}

void CatalogView::setCategoryFilter(const QString &category)
{
    d->category = category;
    if (!d->hasBuilt)
        return;
    repopulateSubcategories();
    applyFilter();
}

void CatalogView::setSearchText(const QString &searchText)
{
    d->search = searchText.trimmed();
    if (d->hasBuilt)
        applyFilter();
}

void CatalogView::repopulateSubcategories()
{
    const QString main = d->combo->currentData().toString();
    const QString previous = d->subCombo->currentData().toString();

    QSet<QString> subs;
    if (!main.isEmpty()) {
        for (const AppInfo &app : std::as_const(d->apps)) {
            if (!app.categories.contains(main))
                continue;
            for (const QString &cat : app.categories) {
                if (cat != main && !isMainCategory(cat))
                    subs.insert(cat);
            }
        }
    }

    d->subCombo->blockSignals(true);
    d->subCombo->clear();
    d->subCombo->addItem(tr("Todas las subcategorías"), QString());

    QStringList sorted(subs.constBegin(), subs.constEnd());
    sorted.sort();
    for (const QString &sub : std::as_const(sorted))
        d->subCombo->addItem(categoryLabel(sub), sub);

    const int index = d->subCombo->findData(previous);
    d->subCombo->setCurrentIndex(index < 0 ? 0 : index);
    d->subCombo->setEnabled(!main.isEmpty());
    d->subCombo->blockSignals(false);

    d->subCategory = d->subCombo->currentData().toString();
}

void CatalogView::applyFilter()
{
    if (d->scroll->widget() == d->content)
        d->scroll->setWidget(nullptr);
    delete d->content;
    d->content = new QWidget(this);
    d->cardsById.clear();
    d->grid = nullptr;

    auto *root = new QVBoxLayout(d->content);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    d->grid = new FlowLayout;
    d->grid->setContentsMargins(16, 16, 16, 16);
    d->grid->setSpacing(12);
    root->addLayout(d->grid);

    d->statusLabel = new QLabel(d->content);
    d->statusLabel->setStyleSheet(
        QStringLiteral("color:#7a8290; font-size:12px;"
                       "padding:2px 16px 10px 16px;"));
    root->addWidget(d->statusLabel);
    root->addStretch(1);

    const QString main = d->category;
    const QString sub = d->subCategory;

    d->filtered.clear();
    for (const AppInfo &app : std::as_const(d->apps)) {
        if (!main.isEmpty() && !app.categories.contains(main))
            continue;
        if (!sub.isEmpty() && !app.categories.contains(sub))
            continue;
        if (!d->search.isEmpty()
            && !app.name.contains(d->search, Qt::CaseInsensitive))
            continue;
        d->filtered.append(app);
    }

    d->builtCount = 0;
    buildMore();

    d->scroll->setWidget(d->content);
}

void CatalogView::buildMore()
{
    if (!d->grid || !d->content)
        return;

    const int end = qMin(d->builtCount + CatalogView::Private::chunkSize,
                         d->filtered.size());
    for (int i = d->builtCount; i < end; ++i) {
        const AppInfo &app = d->filtered.at(i);
        auto *card = new LightStore::AppCardWidget(d->content);
        card->setAppInfo(app);
        d->cardsById.insert(app.id, card);
        connect(card, &LightStore::AppCardWidget::installRequested,
                this, &CatalogView::installRequested);
        connect(card, &LightStore::AppCardWidget::openRequested,
                this, &CatalogView::openRequested);
        connect(card, &LightStore::AppCardWidget::removeRequested,
                this, &CatalogView::removeRequested);
        connect(card, &LightStore::AppCardWidget::detailsRequested,
                this, &CatalogView::detailsRequested);
        d->grid->addWidget(card);
    }
    d->builtCount = end;

    if (d->filtered.isEmpty()) {
        auto *empty = new QLabel(
            tr("Sin resultados para este filtro."), d->content);
        empty->setWordWrap(true);
        d->grid->addWidget(empty);
        d->statusLabel->setVisible(false);
    } else {
        d->statusLabel->setText(
            tr("Mostrando %1 de %2").arg(d->builtCount).arg(d->filtered.size()));
        d->statusLabel->setVisible(d->builtCount < d->filtered.size());
    }
}

void CatalogView::updateInstalledFlags(const QList<AppInfo> &apps)
{
    for (const AppInfo &app : std::as_const(apps)) {
        const auto it = d->cardsById.find(app.id);
        if (it != d->cardsById.end())
            it.value()->setInstalled(app.installed);
    }
}