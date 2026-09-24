#include "ui/Views/SettingsView.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/ConfigManager.h"
#include "core/IconCache.h"

class SettingsView::Private
{
public:
    QSpinBox *cacheSspin = nullptr;
    QCheckBox *flatpakCheck = nullptr;
    QCheckBox *snapCheck = nullptr;

    QLabel *iconsSizeLabel = nullptr;
    QPushButton *clearIconsButton = nullptr;

    QLineEdit *githubEdit = nullptr;
    QListWidget *githubList = nullptr;
    QPushButton *addButton = nullptr;
    QPushButton *removeButton = nullptr;
};

SettingsView::SettingsView(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto *cacheBox = new QGroupBox(tr("Caché de metadatos"), this);
    auto *cacheLayout = new QFormLayout(cacheBox);
    d->cacheSspin = new QSpinBox(cacheBox);
    d->cacheSspin->setRange(1, 720);
    d->cacheSspin->setSuffix(tr(" h"));
    cacheLayout->addRow(tr("TTL de caché (horas):"), d->cacheSspin);
    root->addWidget(cacheBox);

    auto *backendBox = new QGroupBox(tr("Backends"), this);
    auto *backendLayout = new QVBoxLayout(backendBox);
    d->flatpakCheck = new QCheckBox(tr("Habilitar Flatpak"), backendBox);
    d->snapCheck = new QCheckBox(tr("Habilitar Snap"), backendBox);
    backendLayout->addWidget(d->flatpakCheck);
    backendLayout->addWidget(d->snapCheck);
    root->addWidget(backendBox);

    auto *iconsBox = new QGroupBox(tr("Caché de iconos"), this);
    auto *iconsLayout = new QVBoxLayout(iconsBox);
    d->iconsSizeLabel = new QLabel(iconsBox);
    d->iconsSizeLabel->setStyleSheet(
        QStringLiteral("color:#9aa0a9; font-size:13px;"));
    d->clearIconsButton = new QPushButton(tr("Limpiar caché"), iconsBox);

    auto *iconsRow = new QHBoxLayout;
    iconsRow->addWidget(d->iconsSizeLabel);
    iconsRow->addStretch(1);
    iconsRow->addWidget(d->clearIconsButton);
    iconsLayout->addLayout(iconsRow);
    root->addWidget(iconsBox);

    auto *githubBox = new QGroupBox(tr("Repositorios de GitHub"), this);
    auto *githubLayout = new QVBoxLayout(githubBox);
    d->githubEdit = new QLineEdit(githubBox);
    d->githubEdit->setPlaceholderText(tr("usuario/repositorio"));
    d->githubList = new QListWidget(githubBox);
    d->addButton = new QPushButton(tr("Añadir"), githubBox);
    d->removeButton = new QPushButton(tr("Eliminar"), githubBox);

    auto *githubButtons = new QHBoxLayout;
    githubButtons->addWidget(d->addButton);
    githubButtons->addWidget(d->removeButton);
    githubButtons->addStretch(1);

    githubLayout->addWidget(d->githubEdit);
    githubLayout->addWidget(d->githubList);
    githubLayout->addLayout(githubButtons);
    root->addWidget(githubBox);
    root->addStretch(1);

    connect(d->cacheSspin, &QSpinBox::valueChanged,
            this, [this](int) { saveToConfig(); });
    connect(d->flatpakCheck, &QCheckBox::toggled,
            this, [this] { saveToConfig(); });
    connect(d->snapCheck, &QCheckBox::toggled,
            this, [this] { saveToConfig(); });

    connect(d->clearIconsButton, &QPushButton::clicked, this, [this] {
        IconCache::instance()->clearCache();
        refreshIconCacheInfo();
    });

    connect(d->addButton, &QPushButton::clicked, this, [this] {
        const QString repo = d->githubEdit->text().trimmed();
        if (repo.isEmpty())
            return;
        ConfigManager::instance()->addGithubRepository(repo);
        d->githubEdit->clear();
        loadFromConfig();
    });

    connect(d->removeButton, &QPushButton::clicked, this, [this] {
        QListWidgetItem *item = d->githubList->currentItem();
        if (!item)
            return;
        ConfigManager::instance()->removeGithubRepository(item->text());
        loadFromConfig();
    });

    loadFromConfig();
}

SettingsView::~SettingsView() = default;

void SettingsView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshIconCacheInfo();
}

void SettingsView::refreshIconCacheInfo()
{
    const qint64 bytes = IconCache::instance()->cacheSizeBytes();
    QString human;
    if (bytes >= 1024 * 1024)
        human = QStringLiteral("%1 MB").arg(bytes / double(1024 * 1024), 0, 'f', 1);
    else if (bytes >= 1024)
        human = QStringLiteral("%1 KB").arg(bytes / double(1024), 0, 'f', 1);
    else
        human = QStringLiteral("%1 B").arg(bytes);

    if (d->iconsSizeLabel)
        d->iconsSizeLabel->setText(
            bytes > 0 ? tr("Iconos descargados: %1").arg(human)
                      : tr("Iconos descargados: no hay caché"));
}

void SettingsView::loadFromConfig()
{
    auto *cfg = ConfigManager::instance();

    if (d->cacheSspin)
        d->cacheSspin->setValue(cfg->cacheTtlHours());
    if (d->flatpakCheck)
        d->flatpakCheck->setChecked(cfg->flatpakEnabled());
    if (d->snapCheck)
        d->snapCheck->setChecked(cfg->snapEnabled());

    if (d->githubList) {
        d->githubList->clear();
        d->githubList->addItems(cfg->githubRepositories());
    }

    refreshIconCacheInfo();
}

void SettingsView::saveToConfig()
{
    auto *cfg = ConfigManager::instance();

    if (d->cacheSspin)
        cfg->setCacheTtlHours(d->cacheSspin->value());
    if (d->flatpakCheck)
        cfg->setFlatpakEnabled(d->flatpakCheck->isChecked());
    if (d->snapCheck)
        cfg->setSnapEnabled(d->snapCheck->isChecked());

    cfg->flush();
}