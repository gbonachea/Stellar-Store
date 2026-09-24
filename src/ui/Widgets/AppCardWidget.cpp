#include "ui/Widgets/AppCardWidget.h"

#include <QComboBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/ConfigManager.h"
#include "core/IconCache.h"

#include <QtMath>

namespace LightStore {

class AppCardWidget::Private
{
public:
    QLabel *iconLabel = nullptr;
    QLabel *nameLabel = nullptr;
    QLabel *summaryLabel = nullptr;
    QComboBox *backendCombo = nullptr;
    QPushButton *installButton = nullptr;
    QPushButton *removeButton = nullptr;
    bool anyBackend = false;

    AppInfo info;
    QDateTime lastDetailsEmit;

    explicit Private(QWidget *parent)
    {
        auto *layout = new QVBoxLayout(parent);
        layout->setContentsMargins(8, 8, 8, 8);

        iconLabel = new QLabel(parent);
        iconLabel->setFixedSize(64, 64);
        iconLabel->setAlignment(Qt::AlignCenter);

        nameLabel = new QLabel(parent);
        nameLabel->setWordWrap(true);

        summaryLabel = new QLabel(parent);

        backendCombo = new QComboBox(parent);
        backendCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

        installButton = new QPushButton(parent);
        removeButton = new QPushButton(parent);
        removeButton->setStyleSheet(QStringLiteral("color:#d96a6a;"));

        layout->addWidget(iconLabel);
        layout->addWidget(nameLabel);
        layout->addWidget(summaryLabel);
        layout->addWidget(backendCombo);
        layout->addWidget(installButton);
        layout->addWidget(removeButton);
    }
};

AppCardWidget::AppCardWidget(QWidget *parent)
    : QWidget(parent)
    , d(new Private(this))
{
    setFixedWidth(230);
    connect(d->installButton, &QPushButton::clicked,
            this, &AppCardWidget::onInstallClicked);
    connect(d->removeButton, &QPushButton::clicked,
            this, &AppCardWidget::onRemoveClicked);

    // Remote icons land asynchronously; apply them when this card asks for
    // them. Downloads are on-demand and persisted on disk by IconCache.
    connect(IconCache::instance(), &IconCache::iconReady, this,
            [this](const QString &appId, const QString &localPath) {
                if (appId != d->info.id)
                    return;
                QPixmap pixmap(localPath);
                if (!pixmap.isNull()) {
                    d->iconLabel->setPixmap(
                        pixmap.scaled(64, 64, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation));
                    d->iconLabel->setText(QString());
                }
            });
}

AppCardWidget::~AppCardWidget() = default;

void AppCardWidget::setAppInfo(const AppInfo &info)
{
    d->info = info;

    if (!info.name.isEmpty())
        d->nameLabel->setText(info.name);
    if (!info.summary.isEmpty())
        d->summaryLabel->setText(info.summary);

    if (!info.icon.isNull()) {
        d->iconLabel->setPixmap(QPixmap::fromImage(info.icon));
    } else {
        // Local file left over from a previous download, or an on-demand
        // fetch of the remote icon (DEP-11/Flathub catalogs reference their
        // icons by URL). resolveIcon queues the download and returns the
        // cached file instantly once one exists, so this must run BEFORE the
        // theme lookup and the placeholder, otherwise a stock icon name would
        // shadow the real remote artwork.
        const QString cached = IconCache::instance()->resolveIcon(info);
        bool iconShown = false;
        if (!cached.isEmpty()) {
            QPixmap pixmap(cached);
            if (!pixmap.isNull()) {
                d->iconLabel->setPixmap(
                    pixmap.scaled(64, 64, Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation));
                d->iconLabel->setText(QString());
                iconShown = true;
            }
        }
        if (!iconShown && !info.iconName.isEmpty()) {
            const QPixmap pixmap = QIcon::fromTheme(info.iconName).pixmap(64, 64);
            if (!pixmap.isNull()) {
                d->iconLabel->setPixmap(pixmap);
                iconShown = true;
            }
        }
        if (!iconShown)
            showPlaceholder();
    }

    const bool installed = info.installed;

    // Pick the package types this app can realistically use and preselect
    // the same one the owner would route to by default.
    d->backendCombo->clear();
    bool anyBackend = false;
    if (info.origin == QLatin1String("github")) {
        d->backendCombo->addItem(tr("GitHub"), QStringLiteral("github"));
        anyBackend = true;
    } else {
        const bool nativeTool = QFileInfo::exists(QStringLiteral("/usr/bin/apt-get"))
            || QFileInfo::exists(QStringLiteral("/usr/bin/dnf"))
            || QFileInfo::exists(QStringLiteral("/usr/bin/pacman"));
        if (!info.packageNames.isEmpty() && nativeTool) {
            d->backendCombo->addItem(tr("Nativo"), QStringLiteral("native"));
            anyBackend = true;
        }
        auto *cfg = ConfigManager::instance();
        if (QFileInfo::exists(QStringLiteral("/usr/bin/flatpak"))
            && cfg->flatpakEnabled()) {
            d->backendCombo->addItem(tr("Flatpak"), QStringLiteral("flatpak"));
            anyBackend = true;
        }
        if (QFileInfo::exists(QStringLiteral("/usr/bin/snap"))
            && cfg->snapEnabled()) {
            d->backendCombo->addItem(tr("Snap"), QStringLiteral("snap"));
            anyBackend = true;
        }
    }
    for (const char *preferred :
         {"native", "flatpak", "snap", "github"}) {
        const int index = d->backendCombo->findData(QLatin1String(preferred));
        if (index >= 0) {
            d->backendCombo->setCurrentIndex(index);
            break;
        }
    }

    d->anyBackend = anyBackend;
    d->backendCombo->setVisible(!installed && d->anyBackend);
    d->removeButton->setVisible(installed);

    if (installed) {
        d->installButton->setText(tr("Abrir"));
        d->installButton->setEnabled(true);
        d->removeButton->setText(tr("Desinstalar"));
        d->removeButton->setEnabled(true);
    } else {
        d->installButton->setText(tr("Instalar"));
        d->installButton->setEnabled(AbstractBackend::managesInstall());
        d->removeButton->setEnabled(false);
    }
}

void AppCardWidget::setInstalled(bool installed)
{
    d->info.installed = installed;
    d->backendCombo->setVisible(!installed && d->anyBackend);
    d->removeButton->setVisible(installed);

    if (installed) {
        d->installButton->setText(tr("Abrir"));
        d->installButton->setEnabled(true);
        d->removeButton->setText(tr("Desinstalar"));
        d->removeButton->setEnabled(true);
    } else {
        d->installButton->setText(tr("Instalar"));
        d->installButton->setEnabled(AbstractBackend::managesInstall());
        d->removeButton->setEnabled(false);
    }
}

void AppCardWidget::setProgress(int percent)
{
    if (d->installButton)
        d->installButton->setText(QStringLiteral("…%1%").arg(percent));
}

void AppCardWidget::setBusy(bool busy)
{
    if (d->installButton)
        d->installButton->setEnabled(!busy);
    if (d->removeButton)
        d->removeButton->setEnabled(!busy);
}

const AppInfo &AppCardWidget::appInfo() const
{
    return d->info;
}

void AppCardWidget::onInstallClicked()
{
    if (d->info.installed) {
        emit openRequested(d->info);
        return;
    }
    emit installRequested(d->info,
                          d->backendCombo->currentData().toString());
}

void AppCardWidget::onRemoveClicked()
{
    if (d->info.installed)
        emit removeRequested(d->info);
}

void AppCardWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    QWidget::mouseDoubleClickEvent(event);
}

void AppCardWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const bool repeating =
            d->lastDetailsEmit.isValid()
            && d->lastDetailsEmit.msecsTo(QDateTime::currentDateTime()) < 350;
        if (!repeating) {
            d->lastDetailsEmit = QDateTime::currentDateTime();
            emit detailsRequested(d->info);
        }
    }
    QWidget::mousePressEvent(event);
}

void AppCardWidget::showPlaceholder()
{
    static QPixmap s_pixmap(QStringLiteral(":/icons/picture"));
    if (s_pixmap.isNull()) {
        d->iconLabel->setText(QStringLiteral("□"));
        return;
    }
    d->iconLabel->setPixmap(
        s_pixmap.scaled(64, 64, Qt::KeepAspectRatio,
                        Qt::SmoothTransformation));
    d->iconLabel->setText(QString());
}

} // namespace LightStore
