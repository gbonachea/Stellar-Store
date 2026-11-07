#include "ui/Views/SettingsView.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <functional>

#include "core/ConfigManager.h"
#include "core/IconCache.h"

class SettingsView::Private
{
public:
    QSpinBox *cacheSspin = nullptr;
    QCheckBox *flatpakCheck = nullptr;
    QCheckBox *snapCheck = nullptr;
    QLabel *backendStatus = nullptr;

    QLabel *iconsSizeLabel = nullptr;
    QPushButton *clearIconsButton = nullptr;

    QLineEdit *githubEdit = nullptr;
    QListWidget *githubList = nullptr;
    QPushButton *addButton = nullptr;
    QPushButton *removeButton = nullptr;
    QLabel *githubStatus = nullptr;
    QProcess *backendInstall = nullptr;
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
    d->backendStatus = new QLabel(backendBox);
    d->backendStatus->setWordWrap(true);
    backendLayout->addWidget(d->flatpakCheck);
    backendLayout->addWidget(d->snapCheck);
    backendLayout->addWidget(d->backendStatus);
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
    d->githubEdit->setPlaceholderText(
        tr("usuario/repositorio o https://github.com/usuario/repositorio"));
    d->githubList = new QListWidget(githubBox);
    d->addButton = new QPushButton(tr("Añadir"), githubBox);
    d->removeButton = new QPushButton(tr("Eliminar"), githubBox);
    d->githubStatus = new QLabel(githubBox);
    d->githubStatus->setWordWrap(true);
    d->githubStatus->setStyleSheet(
        QStringLiteral("color:#d96a6a; font-size:12px;"));

    auto *githubButtons = new QHBoxLayout;
    githubButtons->addWidget(d->addButton);
    githubButtons->addWidget(d->removeButton);
    githubButtons->addStretch(1);

    githubLayout->addWidget(d->githubEdit);
    githubLayout->addWidget(d->githubList);
    githubLayout->addLayout(githubButtons);
    githubLayout->addWidget(d->githubStatus);
    root->addWidget(githubBox);
    root->addStretch(1);

    connect(d->cacheSspin, &QSpinBox::valueChanged,
            this, [this](int) { saveToConfig(); });
    connect(d->flatpakCheck, &QCheckBox::toggled,
            this, [this](bool enabled) { setBackendEnabled(true, enabled); });
    connect(d->snapCheck, &QCheckBox::toggled,
            this, [this](bool enabled) { setBackendEnabled(false, enabled); });

    connect(d->clearIconsButton, &QPushButton::clicked, this, [this] {
        IconCache::instance()->clearCache();
        refreshIconCacheInfo();
    });

    connect(d->addButton, &QPushButton::clicked, this, [this] {
        const QString repo = d->githubEdit->text().trimmed();
        if (repo.isEmpty())
            return;

        const QString normalized =
            ConfigManager::normalizeGithubRepository(repo);
        if (normalized.isEmpty()) {
            d->githubStatus->setText(
                tr("Formato no válido. Usa «usuario/repositorio» o la URL "
                   "completa del repositorio de GitHub."));
            return;
        }
        if (!ConfigManager::instance()->addGithubRepository(normalized)) {
            d->githubStatus->setText(tr("No se pudo guardar el repositorio."));
            return;
        }

        d->githubStatus->clear();
        d->githubEdit->clear();
        loadFromConfig();
    });

    connect(d->removeButton, &QPushButton::clicked, this, [this] {
        QListWidgetItem *item = d->githubList->currentItem();
        if (!item)
            return;
        ConfigManager::instance()->removeGithubRepository(item->text());
        d->githubStatus->clear();
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

    const QSignalBlocker cacheBlock(d->cacheSspin);
    const QSignalBlocker flatpakBlock(d->flatpakCheck);
    const QSignalBlocker snapBlock(d->snapCheck);

    if (d->cacheSspin)
        d->cacheSspin->setValue(cfg->cacheTtlHours());
    if (d->flatpakCheck)
        d->flatpakCheck->setChecked(cfg->flatpakEnabled() && cfg->flatpakAvailableOnSystem());
    if (d->snapCheck)
        d->snapCheck->setChecked(cfg->snapEnabled() && cfg->snapAvailableOnSystem());

    if (d->githubList) {
        d->githubList->clear();
        d->githubList->addItems(cfg->githubRepositories());
    }

    refreshIconCacheInfo();
}

void SettingsView::setBackendEnabled(bool flatpak, bool enabled)
{
    if (flatpak) {
        configureFlatpak(enabled);
        return;
    }
    auto *cfg = ConfigManager::instance();
    QCheckBox *check = flatpak ? d->flatpakCheck : d->snapCheck;
    const QString name = flatpak ? QStringLiteral("Flatpak") : QStringLiteral("Snap");
    auto save = [this, cfg, flatpak](bool value) {
        if (flatpak)
            cfg->setFlatpakEnabled(value);
        else
            cfg->setSnapEnabled(value);
        emit backendSettingsChanged();
    };

    if (!enabled || (flatpak ? cfg->flatpakAvailableOnSystem()
                             : cfg->snapAvailableOnSystem())) {
        save(enabled);
        return;
    }

    // A checked box must not imply that a missing executable is available.
    { const QSignalBlocker blocker(check); check->setChecked(false); }
    save(false);
    if (d->backendInstall)
        return;

    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    QString manager;
    QStringList arguments;
    const QString package = flatpak ? QStringLiteral("flatpak") : QStringLiteral("snapd");
    if (!QStandardPaths::findExecutable(QStringLiteral("apt-get")).isEmpty()) {
        manager = QStringLiteral("apt-get");
        arguments = {manager, QStringLiteral("install"), QStringLiteral("-y"), package};
    } else if (!QStandardPaths::findExecutable(QStringLiteral("dnf")).isEmpty()) {
        manager = QStringLiteral("dnf");
        arguments = {manager, QStringLiteral("install"), QStringLiteral("-y"), package};
    } else if (!QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
        manager = QStringLiteral("pacman");
        arguments = {manager, QStringLiteral("-S"), QStringLiteral("--needed"),
                     QStringLiteral("--noconfirm"), package};
    }
    if (pkexec.isEmpty() || manager.isEmpty()) {
        QMessageBox::warning(this, tr("No se puede instalar %1").arg(name),
                             tr("No se encontró pkexec o un gestor de paquetes compatible. Instala %1 manualmente.").arg(package));
        return;
    }
    if (QMessageBox::question(this, tr("Instalar %1").arg(name),
            tr("%1 no está instalado. ¿Deseas instalar el paquete «%2»? Se solicitará autorización de administrador.\n\nEsto no configura repositorios como Flathub ni servicios adicionales.").arg(name, package))
        != QMessageBox::Yes)
        return;

    auto *process = new QProcess(this);
    d->backendInstall = process;
    d->flatpakCheck->setEnabled(false);
    d->snapCheck->setEnabled(false);
    d->backendStatus->setText(tr("Instalando %1; espera la solicitud de autorización…").arg(name));
    auto finish = [this, cfg, check, flatpak, name, save, process](bool success) {
        const bool available = flatpak ? cfg->flatpakAvailableOnSystem()
                                       : cfg->snapAvailableOnSystem();
        d->backendInstall = nullptr;
        d->flatpakCheck->setEnabled(true);
        d->snapCheck->setEnabled(true);
        d->backendStatus->clear();
        { const QSignalBlocker blocker(check); check->setChecked(success && available); }
        save(success && available);
        if (success && available) {
            QMessageBox::information(this, tr("%1 habilitado").arg(name),
                tr("%1 está instalado y habilitado en Stellar Store. Es posible que debas configurar repositorios o servicios por separado.").arg(name));
        } else {
            QMessageBox::warning(this, tr("Instalación no completada"),
                tr("No se pudo instalar %1 o se canceló la autorización. La opción permanece deshabilitada.").arg(name));
        }
        process->deleteLater();
    };
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [finish](int code, QProcess::ExitStatus status) { finish(code == 0 && status == QProcess::NormalExit); });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false);
    });
    process->start(pkexec, arguments);
}

void SettingsView::configureFlatpak(bool enabled)
{
    auto *cfg = ConfigManager::instance();
    const bool previous = cfg->flatpakEnabled() && cfg->flatpakAvailableOnSystem();
    auto restore = [this, previous] {
        const QSignalBlocker blocker(d->flatpakCheck);
        d->flatpakCheck->setChecked(previous);
    };
    restore();

    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    const QString apt = QStandardPaths::findExecutable(QStringLiteral("apt-get"));
    if (pkexec.isEmpty() || apt.isEmpty()) {
        QMessageBox::warning(this, tr("Flatpak"),
            tr("Se necesitan apt-get y pkexec para configurar Flatpak. No se realizaron cambios."));
        return;
    }
    const QString question = enabled
        ? tr("Se instalará el paquete Flatpak y se añadirá Flathub para todo el sistema. Se solicitará autorización de administrador. ¿Continuar?")
        : tr("Se eliminará el repositorio Flathub del sistema y se desinstalará el paquete Flatpak. Las aplicaciones y datos Flatpak pueden permanecer en el equipo. ¿Continuar?");
    if (QMessageBox::question(this, tr("Configurar Flatpak"), question,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    auto *process = new QProcess(this);
    d->backendInstall = process;
    d->flatpakCheck->setEnabled(false);
    d->snapCheck->setEnabled(false);
    d->backendStatus->setText(tr("Configurando Flatpak; puede solicitarse autorización más de una vez…"));

    // Each step is separate: a failure or refused authorization stops the
    // sequence, and no shell is used for privileged commands.
    auto step = std::make_shared<int>(0);
    auto output = std::make_shared<QByteArray>();
    auto runStep = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakRunStep = runStep;
    auto finish = [this, cfg, previous, enabled, process](bool success, const QString &detail) {
        d->backendInstall = nullptr;
        d->flatpakCheck->setEnabled(true);
        d->snapCheck->setEnabled(true);
        d->backendStatus->clear();
        const bool active = success ? enabled : (previous && cfg->flatpakAvailableOnSystem());
        { const QSignalBlocker blocker(d->flatpakCheck); d->flatpakCheck->setChecked(active); }
        cfg->setFlatpakEnabled(active);
        emit backendSettingsChanged();
        if (success)
            QMessageBox::information(this, tr("Flatpak"), enabled
                ? tr("Flatpak y Flathub están configurados para el sistema.")
                : tr("Se han eliminado Flathub del sistema y el paquete Flatpak."));
        else
            QMessageBox::warning(this, tr("Operación incompleta"),
                tr("No se completó la configuración de Flatpak. Los pasos anteriores pueden haberse aplicado.\n%1").arg(detail));
        process->deleteLater();
    };
    *runStep = [enabled, pkexec, apt, process, step, output, weakRunStep, finish] {
        output->clear();
        if (*step == 2) {
            finish(true, {});
            return;
        }
        if (enabled) {
            if (*step == 0)
                process->start(pkexec, {apt, QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("flatpak")});
            else
                process->start(pkexec, {QStringLiteral("/usr/bin/flatpak"), QStringLiteral("remote-add"), QStringLiteral("--system"), QStringLiteral("--if-not-exists"), QStringLiteral("flathub"), QStringLiteral("https://dl.flathub.org/repo/flathub.flatpakrepo")});
        } else if (*step == 0 && QFileInfo::exists(QStringLiteral("/usr/bin/flatpak"))) {
            process->start(QStringLiteral("/usr/bin/flatpak"), {QStringLiteral("remotes"), QStringLiteral("--system"), QStringLiteral("--columns=name")});
        } else if (*step == 0) {
            ++*step;
            if (auto next = weakRunStep.lock())
                (*next)();
        } else {
            process->start(pkexec, {apt, QStringLiteral("remove"), QStringLiteral("-y"), QStringLiteral("flatpak")});
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, this, [process, output] { *output += process->readAllStandardOutput(); });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [enabled, pkexec, process, step, output, runStep, finish](int code, QProcess::ExitStatus status) {
        *output += process->readAllStandardOutput();
        if (status != QProcess::NormalExit || code != 0) {
            const QString detail = QString::fromUtf8(process->readAllStandardError()).trimmed().right(2000);
            finish(false, detail);
            return;
        }
        if (!enabled && *step == 0 && QString::fromUtf8(*output).split(QLatin1Char('\n')).contains(QStringLiteral("flathub"))) {
            // Step -1 marks the remote deletion, followed by package removal.
            *step = -1;
            process->start(pkexec, {QStringLiteral("/usr/bin/flatpak"), QStringLiteral("remote-delete"), QStringLiteral("--system"), QStringLiteral("flathub")});
            return;
        }
        if (*step == -1)
            *step = 1;
        else
            ++*step;
        (*runStep)();
    });
    connect(process, &QProcess::errorOccurred, this, [runStep, finish, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish(false, process->errorString());
        }
    });
    (*runStep)();
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
