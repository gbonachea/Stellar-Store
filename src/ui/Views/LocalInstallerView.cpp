#include "ui/Views/LocalInstallerView.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QVBoxLayout>

class LocalInstallerView::Private
{
public:
    QString selectedFile;
    QLabel *dropIcon = nullptr;
    QLabel *statusLabel = nullptr;
    QPushButton *installButton = nullptr;
};

LocalInstallerView::LocalInstallerView(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    setAcceptDrops(true);
    setMinimumHeight(260);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    d->dropIcon = new QLabel(this);
    d->dropIcon->setAlignment(Qt::AlignCenter);
    d->dropIcon->setStyleSheet(QStringLiteral("color:#6C6F77;font-size:40px;"));
    d->dropIcon->setText(QStringLiteral("\u21E9"));

    d->statusLabel = new QLabel(tr("Arrastra aquí un .deb o .rpm"), this);
    d->statusLabel->setAlignment(Qt::AlignCenter);
    d->statusLabel->setWordWrap(true);

    d->installButton = new QPushButton(tr("Instalar fichero"), this);
    d->installButton->setEnabled(false);
    d->installButton->setFixedWidth(220);

    layout->addStretch(1);
    layout->addWidget(d->dropIcon);
    layout->addWidget(d->statusLabel);
    layout->addSpacing(8);
    layout->addWidget(d->installButton, 0, Qt::AlignHCenter);
    layout->addStretch(1);

    connect(d->installButton, &QPushButton::clicked,
            this, &LocalInstallerView::onInstallClicked);
}

LocalInstallerView::~LocalInstallerView() = default;

void LocalInstallerView::setLocalFile(const QString &filePath)
{
    d->selectedFile = filePath;
    d->installButton->setEnabled(!filePath.isEmpty());
    updateStatus();
}

void LocalInstallerView::installDroppedFile(const QString &filePath)
{
    setLocalFile(filePath);
}

void LocalInstallerView::onInstallClicked()
{
    if (d->selectedFile.isEmpty())
        return;
    emit installRequested(d->selectedFile);
}

void LocalInstallerView::updateStatus()
{
    if (d->statusLabel) {
        d->statusLabel->setText(d->selectedFile.isEmpty()
                                    ? tr("Arrastra aquí un .deb o .rpm")
                                    : tr("Fichero: %1").arg(d->selectedFile));
    }
}

void LocalInstallerView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const QString path = event->mimeData()->urls().first().toLocalFile();
        if (path.endsWith(QLatin1String(".deb"), Qt::CaseInsensitive)
            || path.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void LocalInstallerView::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    const QString path = event->mimeData()->urls().first().toLocalFile();
    if (!path.isEmpty())
        installDroppedFile(path);
}