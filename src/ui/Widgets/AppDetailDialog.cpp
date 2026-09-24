#include "ui/Widgets/AppDetailDialog.h"

#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "core/ConfigManager.h"
#include "core/IconCache.h"

namespace {
bool nativeToolAvailable()
{
    return QFileInfo::exists(QStringLiteral("/usr/bin/apt-get"))
        || QFileInfo::exists(QStringLiteral("/usr/bin/dnf"))
        || QFileInfo::exists(QStringLiteral("/usr/bin/pacman"));
}
} // namespace

class AppDetailDialog::Private
{
public:
    AppInfo info;

    QLabel *iconLabel = nullptr;
    QLabel *nameLabel = nullptr;
    QLabel *summaryLabel = nullptr;
    QLabel *descriptionLabel = nullptr;

    QRadioButton *nativeRadio = nullptr;
    QRadioButton *flatpakRadio = nullptr;
    QRadioButton *snapRadio = nullptr;
    QPushButton *installButton = nullptr;
    QPushButton *removeButton = nullptr;

    QNetworkAccessManager *nam = nullptr;
};

AppDetailDialog::AppDetailDialog(const AppInfo &info, QWidget *parent)
    : QDialog(parent)
    , d(std::make_unique<Private>())
{
    d->info = info;
    setWindowTitle(info.name.isEmpty() ? tr("Detalles de la aplicación")
                                       : info.name);
    resize(760, 560);
    d->nam = new QNetworkAccessManager(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    // ------------------------------------------------------------- header
    auto *header = new QHBoxLayout;
    header->setSpacing(14);

    d->iconLabel = new QLabel(this);
    d->iconLabel->setFixedSize(96, 96);
    d->iconLabel->setAlignment(Qt::AlignCenter);
    auto applyPixmap = [this](const QString &localPath) {
        QPixmap pixmap(localPath);
        if (pixmap.isNull())
            return false;
        d->iconLabel->setPixmap(
            pixmap.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        d->iconLabel->setText(QString());
        return true;
    };

    if (!info.icon.isNull()) {
        d->iconLabel->setPixmap(QPixmap::fromImage(info.icon)
                                    .scaled(96, 96, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));
    } else {
        // Local/remote icon resolution: resolveIcon returns the persisted
        // file instantly and queues the remote download (DEP-11/Flathub)
        // when the URL is only known, so it must run before the theme
        // lookup and placeholder shadow the real artwork.
        const QString cached = IconCache::instance()->resolveIcon(info);
        if (!applyPixmap(cached) && !info.iconName.isEmpty()) {
            const QPixmap themePixmap =
                QIcon::fromTheme(info.iconName).pixmap(96, 96);
            if (!themePixmap.isNull())
                d->iconLabel->setPixmap(themePixmap);
        }
    }
    if (d->iconLabel->pixmap().isNull()) {
        static QPixmap s_placeholder(QStringLiteral(":/icons/picture"));
        if (s_placeholder.isNull())
            d->iconLabel->setText(QStringLiteral("□"));
        else
            d->iconLabel->setPixmap(
                s_placeholder.scaled(96, 96, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation));
    }

    // Remote icons land asynchronously; apply them whenever the download
    // finishes. This must be connected unconditionally: native components
    // always reach the placeholder through the theme lookup, but their
    // real icon is a remote URL that resolveIcon() above started fetching.
    connect(IconCache::instance(), &IconCache::iconReady, this,
            [this, applyPixmap](const QString &appId, const QString &localPath) {
                if (appId != d->info.id)
                    return;
                applyPixmap(localPath);
            });

    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(4);

    d->nameLabel = new QLabel(info.name, this);
    QFont titleFont = d->nameLabel->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 5);
    titleFont.setBold(true);
    d->nameLabel->setFont(titleFont);

    d->summaryLabel = new QLabel(info.summary, this);
    d->summaryLabel->setWordWrap(true);
    d->summaryLabel->setStyleSheet(QStringLiteral("color:#9aa0a9;"));

    titleCol->addWidget(d->nameLabel);
    titleCol->addWidget(d->summaryLabel);
    titleCol->addStretch(1);

    header->addWidget(d->iconLabel);
    header->addLayout(titleCol, 1);
    root->addLayout(header);

    // ------------------------------------------------------ description
    d->descriptionLabel = new QLabel(this);
    d->descriptionLabel->setWordWrap(true);
    d->descriptionLabel->setTextFormat(Qt::PlainText);
    d->descriptionLabel->setText(info.description.isEmpty()
                                     ? tr("Sin descripción disponible.")
                                     : info.description);

    auto *descScroll = new QScrollArea(this);
    descScroll->setWidgetResizable(true);
    descScroll->setFrameShape(QFrame::NoFrame);
    descScroll->setFixedHeight(120);
    descScroll->setWidget(d->descriptionLabel);
    root->addWidget(descScroll);

    // ------------------------------------------------------ screenshots
    if (!info.screenshotUrls.isEmpty()) {
        auto *shotTitle = new QLabel(tr("Capturas"), this);
        shotTitle->setStyleSheet(QStringLiteral("font-weight:600;"));
        root->addWidget(shotTitle);

        auto *shotRow = new QWidget(this);
        auto *shotLayout = new QHBoxLayout(shotRow);
        shotLayout->setContentsMargins(0, 0, 0, 0);
        shotLayout->setSpacing(8);
        shotLayout->addStretch(1);

        const int maxShots = qMin(5, info.screenshotUrls.size());
        for (int i = 0; i < maxShots; ++i) {
            QLabel *shot = new QLabel(tr("Cargando…"), shotRow);
            shot->setFixedSize(340, 190);
            shot->setAlignment(Qt::AlignCenter);
            shot->setStyleSheet(
                QStringLiteral("border:1px solid #3a3d45;border-radius:6px;"
                               "background:#25282e;color:#9aa0a9;"));
            shotLayout->addWidget(shot);

            const QUrl url(info.screenshotUrls.at(i));
            if (url.isLocalFile()) {
                QPixmap pixmap(url.toLocalFile());
                if (pixmap.isNull())
                    shot->setText(tr("Sin captura"));
                else
                    shot->setPixmap(pixmap.scaled(338, 188, Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation));
            } else {
                QNetworkReply *reply = d->nam->get(QNetworkRequest(url));
                connect(reply, &QNetworkReply::finished, this,
                        [this, reply, shot] {
                            shot->setText(QString());
                            QPixmap pixmap;
                            if (reply->error() == QNetworkReply::NoError
                                && pixmap.loadFromData(reply->readAll()))
                                shot->setPixmap(
                                    pixmap.scaled(338, 188, Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation));
                            else
                                shot->setText(tr("Sin captura"));
                            reply->deleteLater();
                        });
            }
        }

        auto *shotScroll = new QScrollArea(this);
        shotScroll->setWidgetResizable(true);
        shotScroll->setFrameShape(QFrame::NoFrame);
        shotScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        shotScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        shotScroll->setFixedHeight(212);
        shotScroll->setWidget(shotRow);
        root->addWidget(shotScroll);
    }

    root->addStretch(1);

    // ------------------------------------------------------- backend picker
    auto *backendBox = new QGroupBox(tr("Tipo de paquete"), this);
    auto *backendLayout = new QHBoxLayout(backendBox);

    d->nativeRadio = new QRadioButton(tr("Nativo"), backendBox);
    d->flatpakRadio = new QRadioButton(tr("Flatpak"), backendBox);
    d->snapRadio = new QRadioButton(tr("Snap"), backendBox);
    backendLayout->addWidget(d->nativeRadio);
    backendLayout->addWidget(d->flatpakRadio);
    backendLayout->addWidget(d->snapRadio);

    const bool nativeOn = nativeToolAvailable()
                          && !info.packageNames.isEmpty();
    const bool flatpakOn = QFileInfo::exists(QStringLiteral("/usr/bin/flatpak"));
    const bool snapOn = QFileInfo::exists(QStringLiteral("/usr/bin/snap"));

    d->nativeRadio->setEnabled(nativeOn);
    d->nativeRadio->setToolTip(nativeOn
        ? QString()
        : tr("Esta aplicación no publica un paquete nativo en los "
             "repositorios; usa Flatpak o Snap."));

    const bool flatpakOk = flatpakOn && ConfigManager::instance()->flatpakEnabled();
    d->flatpakRadio->setEnabled(flatpakOk);
    d->flatpakRadio->setToolTip(flatpakOk
        ? QString()
        : tr("Flatpak no está disponible o está deshabilitado en Ajustes."));

    const bool snapOk = snapOn && ConfigManager::instance()->snapEnabled();
    d->snapRadio->setEnabled(snapOk);
    d->snapRadio->setToolTip(snapOk
        ? QString()
        : tr("Snap no está disponible o está deshabilitado en Ajustes."));

    if (d->nativeRadio->isEnabled())
        d->nativeRadio->setChecked(true);
    else if (d->flatpakRadio->isEnabled())
        d->flatpakRadio->setChecked(true);
    else if (d->snapRadio->isEnabled())
        d->snapRadio->setChecked(true);

    d->installButton = new QPushButton(tr("Instalar"), this);
    d->installButton->setDefault(true);
    d->installButton->setEnabled(nativeOn || flatpakOk || snapOk);

    d->removeButton = new QPushButton(tr("Desinstalar"), this);
    d->removeButton->setStyleSheet(QStringLiteral("color:#d96a6a;"));

    if (d->info.installed) {
        d->installButton->setText(tr("Abrir"));
        d->installButton->setEnabled(true);
        d->removeButton->setEnabled(true);
        backendBox->setVisible(false);
    } else {
        d->removeButton->setVisible(false);
    }

    auto *actionLayout = new QHBoxLayout;
    actionLayout->addWidget(backendBox);
    actionLayout->addStretch(1);
    actionLayout->addWidget(d->removeButton);
    actionLayout->addWidget(d->installButton);
    root->addLayout(actionLayout);

    auto pickBackend = [this] {
        QString backend = QStringLiteral("native");
        if (d->flatpakRadio->isChecked())
            backend = QStringLiteral("flatpak");
        else if (d->snapRadio->isChecked())
            backend = QStringLiteral("snap");

        QString target = d->info.id;
        if (backend == QLatin1String("native")
            && !d->info.packageNames.isEmpty())
            target = d->info.packageNames.first();
        return qMakePair(target, backend);
    };

    connect(d->installButton, &QPushButton::clicked, this, [this, pickBackend] {
        if (d->info.installed) {
            emit openRequested(d->info);
            close();
            return;
        }
        const auto [target, backend] = pickBackend();
        emit installRequested(target, backend);
        close();
    });

    connect(d->removeButton, &QPushButton::clicked, this, [this] {
        emit removeRequested(d->info);
        close();
    });
}

AppDetailDialog::~AppDetailDialog() = default;