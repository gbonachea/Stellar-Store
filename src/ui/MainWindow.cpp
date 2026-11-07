#include "ui/MainWindow.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QProcess>
#include <QSet>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "backends/AbstractBackend.h"
#include "backends/FlatpakBackend.h"
#include "backends/NativeBackend.h"
#include "backends/SnapBackend.h"
#include "core/AppStreamManager.h"
#include "core/ConfigManager.h"
#include "core/DesktopEntry.h"
#include "core/GitHubFetcher.h"
#include "ui/Views/AboutView.h"
#include "ui/Views/CatalogView.h"
#include "ui/Views/HomeView.h"
#include "ui/Views/InstalledView.h"
#include "ui/Views/LocalInstallerView.h"
#include "ui/Views/SettingsView.h"
#include "ui/Widgets/AppDetailDialog.h"
#include "ui/Widgets/ProcessDialog.h"

namespace LightStore {

class MainWindow::Private
{
public:
    QWidget *sidebar = nullptr;
    QButtonGroup *navGroup = nullptr;
    QList<QPushButton *> navButtons;
    QStackedWidget *stack = nullptr;
    QLineEdit *search = nullptr;
    QPushButton *refreshButton = nullptr;
    QProgressBar *busyIndicator = nullptr;

    NativeBackend *native = nullptr;
    FlatpakBackend *flatpak = nullptr;
    SnapBackend *snap = nullptr;

    AppStreamManager *stream = nullptr;
    GitHubFetcher *github = nullptr;

    HomeView *home = nullptr;
    CatalogView *catalog = nullptr;
    InstalledView *installed = nullptr;
    LocalInstallerView *local = nullptr;
    SettingsView *settings = nullptr;
    AboutView *about = nullptr;

    ProcessDialog *procDialog = nullptr;

    QList<AppInfo> githubApps;
    QHash<QString, QList<GitHubFetcher::Asset>> githubAssets;
    QSet<QString> pendingGithubRepos;

    QList<AppInfo> appstreamApps;
    QList<AppInfo> masterApps;
    QSet<QString> installedIds;
    QSet<QString> nativeIds;
    QSet<QString> flatpakIds;
    QSet<QString> snapIds;
    int pendingRefreshes = 0;
    bool structureBuilt = false;
};

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    setWindowTitle(QStringLiteral("Stellar Store"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/stellarstore")));

    // Restore last window size/position; fall back to the default size on
    // first run (restoreGeometry returns false when nothing was saved yet).
    if (!restoreGeometry(ConfigManager::instance()->windowGeometry()))
        resize(1180, 760);

    // --------------------------------------------------------- header
    d->search = new QLineEdit(this);
    d->search->setPlaceholderText(QStringLiteral("Buscar aplicaciones…"));
    d->search->setClearButtonEnabled(true);

    d->refreshButton = new QPushButton(this);
    d->refreshButton->setIcon(QIcon(QStringLiteral(":/icons/reload")));
    d->refreshButton->setIconSize(QSize(20, 20));
    d->refreshButton->setToolTip(QStringLiteral("Refrescar catálogo"));
    d->refreshButton->setFixedSize(36, 36);

    d->busyIndicator = new QProgressBar(this);
    d->busyIndicator->setRange(0, 0);
    d->busyIndicator->setFixedWidth(120);
    d->busyIndicator->setVisible(false);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(12, 8, 12, 8);
    header->setSpacing(8);
    header->addWidget(d->search, 1);
    header->addWidget(d->refreshButton);
    header->addWidget(d->busyIndicator);

    // -------------------------------------------------------- sidebar
    d->sidebar = new QWidget(this);
    d->sidebar->setObjectName(QStringLiteral("sidebar"));
    d->sidebar->setFixedWidth(216);

    auto *sidebarLayout = new QVBoxLayout(d->sidebar);
    sidebarLayout->setContentsMargins(10, 18, 10, 12);
    sidebarLayout->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("S T E L L A R   S T O R E"),
                             d->sidebar);
    title->setObjectName(QStringLiteral("navTitle"));
    title->setWordWrap(true);
    sidebarLayout->addWidget(title);
    sidebarLayout->addSpacing(14);

    d->navGroup = new QButtonGroup(this);
    d->navGroup->setExclusive(true);

    const QStringList sections = {
        QStringLiteral("Inicio"), QStringLiteral("Catálogo"),
        QStringLiteral("Instaladas"), QStringLiteral("Instalador local"),
        QStringLiteral("Ajustes"), QStringLiteral("Acerca de"),
    };
    for (int i = 0; i < sections.size(); ++i) {
        auto *btn = new QPushButton(sections.at(i), d->sidebar);
        btn->setObjectName(QStringLiteral("navButton"));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        d->navGroup->addButton(btn, i);
        d->navButtons.append(btn);
        sidebarLayout->addWidget(btn);
    }
    sidebarLayout->addStretch(1);

    auto *brandIcon = new QLabel(d->sidebar);
    QPixmap brandPixmap(QStringLiteral(":/icons/stellarstore"));
    if (!brandPixmap.isNull()) {
        brandIcon->setPixmap(brandPixmap.scaled(
            120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    brandIcon->setAlignment(Qt::AlignLeft);
    brandIcon->setObjectName(QStringLiteral("brandIcon"));
    sidebarLayout->addWidget(brandIcon, 0, Qt::AlignLeft);
    sidebarLayout->addSpacing(8);

    d->sidebar->setStyleSheet(QStringLiteral(
        "#sidebar { background:#181b21; border-right:1px solid #2b2f38; }"
        "#navTitle { color:#6d7685; font-size:10px; font-weight:600;"
        "            letter-spacing:3px; padding-left:10px; }"
        "QPushButton#navButton {"
        "  text-align:left; padding:12px 16px; margin:2px 4px;"
        "  border:none; border-radius:8px; background:transparent;"
        "  color:#b9c0ca; font-size:14px;"
        "}"
        "QPushButton#navButton:hover { background:#242932; color:#ffffff; }"
        "QPushButton#navButton:checked { background:#2f81f7;"
        "                                color:#ffffff; font-weight:600; }"));

    // ----------------------------------------------------- stacked UI
    d->stack = new QStackedWidget(this);
    d->home = new HomeView(this);
    d->catalog = new CatalogView(this);
    d->installed = new InstalledView(this);
    d->local = new LocalInstallerView(this);
    d->settings = new SettingsView(this);
    d->about = new AboutView(this);

    d->stack->addWidget(d->home);
    d->stack->addWidget(d->catalog);
    d->stack->addWidget(d->installed);
    d->stack->addWidget(d->local);
    d->stack->addWidget(d->settings);
    d->stack->addWidget(d->about);

    auto *center = new QHBoxLayout;
    center->setContentsMargins(0, 0, 0, 0);
    center->setSpacing(0);
    center->addWidget(d->sidebar);
    center->addWidget(d->stack, 1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addLayout(header);
    root->addLayout(center, 1);

    connect(d->navGroup,
            QOverload<int>::of(&QButtonGroup::idClicked),
            d->stack, &QStackedWidget::setCurrentIndex);
    if (!d->navButtons.isEmpty())
        d->navButtons.first()->setChecked(true);

    // ------------------------------------------------------- backends
    d->stream = new AppStreamManager(this);
    d->github = new GitHubFetcher(this);
    d->native = new NativeBackend(this);
    d->flatpak = new FlatpakBackend(this);
    d->snap = new SnapBackend(this);

    d->procDialog = new ProcessDialog(this);

    // -------------------------------------------------------- helpers
    auto baseName = [](const QString &id) {
        if (id.endsWith(QLatin1String(".desktop")))
            return id.left(id.size() - 8);
        return id;
    };

    // Real Flatpak id for an app. Legacy ids like "firefox.desktop" map to
    // "org.mozilla.firefox"; prefer an exact match and then a suffix match on
    // the "….base" form so we never hand Flatpak an invalid (1-dot) id.
    auto matchesFlatpakId = [baseName](const QString &id,
                                        const QSet<QString> &flatpakInstalled) {
        const QString base = baseName(id);
        if (flatpakInstalled.contains(id))
            return id;
        if (flatpakInstalled.contains(base))
            return base;
        for (const QString &fid : std::as_const(flatpakInstalled)) {
            if (fid.endsWith(QStringLiteral(".") + base, Qt::CaseInsensitive))
                return fid;
        }
        return QString();
    };

    // Id we can hand to the Flatpak backend for install (Flatpak refuses ids
    // with fewer than two dots).
    auto flatpakTarget = [baseName](const AppInfo &info) {
        if (!info.flatpakId.isEmpty())
            return info.flatpakId;
        if (info.id.count(QLatin1Char('.')) >= 2)
            return info.id;
        return baseName(info.id);
    };

    auto annotate = [this, baseName, matchesFlatpakId] {
        // Installed package names do not always match the component id (the
        // distro may ship a differently named package). Presence of the real
        // launcher is the most reliable "is installed" signal for desktop
        // components, so build a lookup of installed desktop ids.
        QSet<QString> desktopIds;
        const QStringList launchers = installedDesktopFiles();
        desktopIds.reserve(launchers.size());
        for (const QString &path : launchers)
            desktopIds.insert(QFileInfo(path).completeBaseName().toLower());

        QList<AppInfo> annotated = d->masterApps;
        for (AppInfo &app : annotated) {
            app.installed = false;
            for (const QString &pkg : app.packageNames) {
                if (d->installedIds.contains(pkg)) {
                    app.installed = true;
                    break;
                }
            }
            if (!app.installed && !matchesFlatpakId(app.id, d->installedIds).isEmpty())
                app.installed = true;
            if (!app.installed && desktopIds.contains(baseName(app.id).toLower()))
                app.installed = true;
        }
        return annotated;
    };

    // Compose the catalog from AppStream metadata plus the GitHub releases.
    // Keeping both lists separate lets each source refresh independently
    // without losing the entries provided by the other one.
    auto rebuildMaster = [this] {
        QList<AppInfo> combined = d->appstreamApps;
        QSet<QString> ids;
        ids.reserve(combined.size() + d->githubApps.size());
        for (const AppInfo &app : std::as_const(combined))
            ids.insert(app.id);
        for (const AppInfo &app : std::as_const(d->githubApps)) {
            if (ids.contains(app.id))
                continue;
            ids.insert(app.id);
            combined.append(app);
        }
        d->masterApps = combined;
    };

    // Build the "Instaladas" list from real .desktop launchers, not from an
    // exact package-name match. This is what makes Flatpak/Snap/AppImage and
    // components whose package owns a differently named launcher visible.
    auto rebuildInstalled = [this](const QList<AppInfo> &annotated) {
        QList<AppInfo> entries;
        QSet<QString> seen;
        seen.reserve(annotated.size() + 256);

        auto desktopKey = [](const QString &path) {
            return QFileInfo(path).completeBaseName().toLower();
        };

        for (const AppInfo &app : annotated) {
            if (!app.installed)
                continue;

            QString desktopFile = findDesktopFile(app.id);
            if (desktopFile.isEmpty()) {
                for (const QString &pkg : app.packageNames) {
                    desktopFile = findDesktopFile(pkg);
                    if (!desktopFile.isEmpty())
                        break;
                }
            }

            AppInfo entry = app;
            QString key = app.id.toLower();
            if (!desktopFile.isEmpty()) {
                DesktopEntry de;
                if (parseDesktopEntry(desktopFile, de)) {
                    entry.id = de.id;
                    if (entry.name.isEmpty() || entry.name == app.id)
                        entry.name = de.name;
                    if (entry.summary.isEmpty())
                        entry.summary = de.summary;
                    if (entry.iconPath.isEmpty() && !de.iconPath.isEmpty())
                        entry.iconPath = de.iconPath;
                    if (entry.iconName.isEmpty() && !de.iconName.isEmpty())
                        entry.iconName = de.iconName;
                    if (entry.categories.isEmpty())
                        entry.categories = de.categories;
                }
                entry.desktopFile = desktopFile;
                entry.origin = desktopEntryOrigin(desktopFile);
                key = desktopKey(desktopFile);
            }

            if (key.isEmpty() || seen.contains(key))
                continue;
            seen.insert(key);
            entry.installed = true;
            entries.append(entry);
        }

        // Launchers that do not map to any AppStream component still count as
        // installed applications (AppImages, locally built tools, etc.).
        for (const QString &desktopFile : installedDesktopFiles()) {
            const QString key = desktopKey(desktopFile);
            if (key.isEmpty() || seen.contains(key))
                continue;

            DesktopEntry de;
            if (!parseDesktopEntry(desktopFile, de))
                continue;

            AppInfo info;
            info.id = de.id;
            info.name = de.name;
            info.summary = de.summary.isEmpty() ? tr("Instalado") : de.summary;
            info.categories = de.categories;
            info.iconName = de.iconName;
            info.iconPath = de.iconPath;
            info.desktopFile = desktopFile;
            info.origin = desktopEntryOrigin(desktopFile);
            info.installed = true;

            seen.insert(key);
            entries.append(info);
        }

        std::sort(entries.begin(), entries.end(),
                  [](const AppInfo &a, const AppInfo &b) {
                      return a.name.localeAwareCompare(b.name) < 0;
                  });
        d->installed->setInstalledApps(entries);
    };

    // Cheap path used after backend refreshes: flip the buttons of the already
    // created cards instead of recreating the whole catalog (thousands of
    // widgets otherwise block the GUI and balloon memory on every action).
    // CatalogView stores the fresh, annotated list; it only rebuilds its
    // (bounded) visible chunk if the page was already opened.
    auto applyAnnotations = [this, annotate, rebuildInstalled] {
        if (!d->structureBuilt)
            return;
        const QList<AppInfo> annotated = annotate();
        d->home->updateInstalledFlags(annotated);
        d->catalog->setApps(annotated);
        rebuildInstalled(annotated);
    };

    auto absorbInstalled = [this] {
        d->installedIds = d->nativeIds;
        for (const QString &p : std::as_const(d->flatpakIds))
            d->installedIds.insert(p);
        for (const QString &p : std::as_const(d->snapIds))
            d->installedIds.insert(p);
    };

    auto refreshInstalledStatus = [this, absorbInstalled, applyAnnotations] {
        if (d->pendingRefreshes > 0)
            return;
        auto *cfg = ConfigManager::instance();
        if (d->native->isAvailable())
            ++d->pendingRefreshes, d->native->refresh();
        if (d->flatpak->isAvailable() && cfg->flatpakEnabled())
            ++d->pendingRefreshes, d->flatpak->refresh();
        if (d->snap->isAvailable() && cfg->snapEnabled())
            ++d->pendingRefreshes, d->snap->refresh();
        if (d->pendingRefreshes == 0) {
            absorbInstalled();
            applyAnnotations();
        }
    };

    connect(d->settings, &SettingsView::backendSettingsChanged, this,
            [this, refreshInstalledStatus] {
                refreshInstalledStatus();
                d->stream->startLoading();
            });

    auto openInstalledApp = [this](const AppInfo &info) {
        QString base = info.id;
        if (base.endsWith(QLatin1String(".desktop")))
            base.chop(int(QStringLiteral(".desktop").size()));

        QString desktopFile = info.desktopFile;
        if (desktopFile.isEmpty())
            desktopFile = findDesktopFile(info.id);
        if (desktopFile.isEmpty()) {
            for (const QString &pkg : info.packageNames) {
                desktopFile = findDesktopFile(pkg);
                if (!desktopFile.isEmpty())
                    break;
            }
        }
        if (desktopFile.isEmpty())
            desktopFile = findDesktopFile(base);

        if (!desktopFile.isEmpty()
            && QProcess::startDetached(
                QStringLiteral("gio"),
                QStringList() << QStringLiteral("launch") << desktopFile))
            return;

        if (QProcess::startDetached(QStringLiteral("gtk-launch"),
                                    QStringList() << base))
            return;

        d->procDialog->setStatusMessage(
            tr("No se pudo abrir %1.").arg(info.name.isEmpty() ? base : info.name));
        d->procDialog->show();
    };

    // ------------------------------------------------- metadata wiring
    connect(d->stream, &AppStreamManager::dataReady,
            this, [this, annotate, rebuildMaster, rebuildInstalled](
                      const QList<AppInfo> &apps) {
                d->appstreamApps = apps;
                rebuildMaster();
                const QList<AppInfo> annotated = annotate();
                d->home->showApps(annotated);
                d->catalog->setApps(annotated);
                d->structureBuilt = true;
                rebuildInstalled(annotated);
                d->busyIndicator->setVisible(false);
            });
    connect(d->stream, &AppStreamManager::refreshStarted,
            this, [this] { d->busyIndicator->setVisible(true); });
    connect(d->stream, &AppStreamManager::loadFailed,
            d->procDialog, &ProcessDialog::setStatusMessage);
    connect(d->stream, &AppStreamManager::loadFailed,
            this, [this] { d->busyIndicator->setVisible(false); });
    connect(d->stream, &AppStreamManager::refreshCacheFailed, this, [this] {
        d->procDialog->setStatusMessage(
            tr("No se pudo actualizar el índice de repositorios."));
        d->busyIndicator->setVisible(false);
    });

    connect(d->refreshButton, &QPushButton::clicked, this,
            [this, refreshInstalledStatus] {
                d->stream->requestCacheRefresh();
                d->github->fetchLatestReleases();
                refreshInstalledStatus();
            });

    // -------------------------------------------------- github wiring
    auto downloadGithubAsset = [this](const QString &repo) {
        const auto assets = d->githubAssets.value(repo);
        if (assets.isEmpty())
            return false;

        const GitHubFetcher::Asset asset =
            GitHubFetcher::preferredAsset(assets);
        d->procDialog->setStatusMessage(
            tr("Descargando %1…").arg(asset.fileName));
        d->github->downloadAsset(asset);
        return true;
    };

    connect(d->github, &GitHubFetcher::fetchStarted, this, [this] {
        // A new pass replaces the previous snapshot; otherwise repeated
        // refreshes would accumulate duplicate cards.
        d->githubApps.clear();
        d->githubAssets.clear();
        d->home->setGithubApps(d->githubApps);
    });

    connect(d->github, &GitHubFetcher::assetDiscovered, this,
            [this](const GitHubFetcher::Asset &asset) {
                d->githubAssets[asset.repo].append(asset);
            });

    connect(d->github, &GitHubFetcher::allFetchesFinished, this,
            [this, rebuildMaster, applyAnnotations, downloadGithubAsset] {
                d->githubApps.clear();

                QStringList repos = d->githubAssets.keys();
                std::sort(repos.begin(), repos.end());

                for (const QString &repo : repos) {
                    const auto assets = d->githubAssets.value(repo);
                    if (assets.isEmpty())
                        continue;

                    const GitHubFetcher::Asset asset =
                        GitHubFetcher::preferredAsset(assets);

                    AppInfo info;
                    info.id = QStringLiteral("github:") + repo;
                    info.name = asset.releaseName.trimmed();
                    if (info.name.isEmpty()
                        || info.name == asset.releaseTag)
                        info.name = repo;
                    info.summary = asset.releaseTag.isEmpty()
                        ? tr("Última versión de %1").arg(repo)
                        : tr("%1 — %2").arg(repo, asset.releaseTag);
                    info.description = asset.releaseNotes;
                    info.categories = QStringList() << QStringLiteral("Utility");
                    info.origin = QStringLiteral("github");
                    info.repository = repo;
                    info.releaseTag = asset.releaseTag;
                    info.iconUrl = asset.ownerAvatar;
                    d->githubApps.append(info);

                    if (d->pendingGithubRepos.remove(repo))
                        downloadGithubAsset(repo);
                }

                if (!d->pendingGithubRepos.isEmpty()) {
                    QStringList pending = d->pendingGithubRepos.values();
                    pending.sort();
                    d->procDialog->setStatusMessage(
                        tr("No se encontraron binarios compatibles en: %1")
                            .arg(pending.join(QStringLiteral(", "))));
                    d->pendingGithubRepos.clear();
                }

                rebuildMaster();
                d->home->setGithubApps(d->githubApps);

                // The catalog already exists: fold the GitHub apps in and
                // re-apply installed flags without waiting for AppStream.
                applyAnnotations();
            });

    connect(d->github, &GitHubFetcher::errorOccurred, this,
            [this](const QString &message) {
                d->procDialog->setStatusMessage(message);
                if (!d->pendingGithubRepos.isEmpty()) {
                    d->procDialog->show();
                    d->procDialog->raise();
                }
            });

    connect(d->github, &GitHubFetcher::downloadFinished, this,
            [this, refreshInstalledStatus](const QString &repo,
                                           const QString &fileName,
                                           const QString &localPath, bool ok) {
                if (!ok) {
                    d->procDialog->setStatusMessage(
                        tr("Error descargando %1/%2").arg(repo, fileName));
                    d->procDialog->show();
                    return;
                }

                const QString lower = localPath.toLower();
                if ((lower.endsWith(QLatin1String(".deb"))
                     || lower.endsWith(QLatin1String(".rpm")))
                    && d->native->isAvailable()) {
                    d->procDialog->setStatusMessage(
                        tr("Instalando %1…").arg(fileName));
                    d->native->installLocalFile(localPath);
                } else {
                    d->procDialog->setStatusMessage(
                        tr("Descargado: %1/%2 → %3")
                            .arg(repo, fileName, localPath));
                    // AppImages get a fresh .desktop launcher, so the
                    // installed list has to be rebuilt.
                    refreshInstalledStatus();
                }
                d->procDialog->show();
            });

    connect(ConfigManager::instance(),
            &ConfigManager::githubRepositoriesChanged,
            this, [this] { d->github->fetchLatestReleases(); });

    // --------------------------------------------------- search wiring
    connect(d->search, &QLineEdit::textChanged,
            d->catalog, &CatalogView::setSearchText);
    connect(d->search, &QLineEdit::textChanged,
            d->home, &HomeView::setSearchText);

    // ------------------------------------------------ operation wiring
    auto showProcess = [this] {
        d->procDialog->show();
        d->procDialog->raise();
    };

    ConfigManager *cfg = ConfigManager::instance();
    auto flatpakReady = [this, cfg] {
        return d->flatpak->isAvailable() && cfg->flatpakEnabled();
    };
    auto snapReady = [this, cfg] {
        return d->snap->isAvailable() && cfg->snapEnabled();
    };

    // Route one app to the backend selected on its card ("native",
    // "flatpak", "snap", "github"); an empty backend falls back to the
    // best available option.
auto installApp = [this, showProcess, flatpakReady, snapReady, flatpakTarget,
                   downloadGithubAsset](
                      const AppInfo &info, const QString &backend) {
        showProcess();

        if (backend == QLatin1String("github")) {
            if (!downloadGithubAsset(info.repository)) {
                d->procDialog->setStatusMessage(
                    tr("Buscando la versión de %1 en GitHub…")
                        .arg(info.repository));
                d->pendingGithubRepos.insert(info.repository);
                d->github->fetchLatestReleases();
            }
            return;
        }
        if (backend == QLatin1String("native")
            || backend.isEmpty()) {
            if (!info.packageNames.isEmpty() && d->native->isAvailable()) {
                const QString pkg = info.packageNames.constFirst();
                d->procDialog->setStatusMessage(tr("Instalando %1…").arg(pkg));
                d->native->installPackage(pkg);
                return;
            }
        }
        if (backend == QLatin1String("flatpak")
            || backend.isEmpty()) {
            if (flatpakReady() && !info.flatpakId.isEmpty()) {
                const QString fid = flatpakTarget(info);
                d->procDialog->setStatusMessage(
                    tr("Instalando %1 vía Flatpak…").arg(fid));
                d->flatpak->installPackage(fid);
                return;
            }
        }
        if (backend == QLatin1String("snap")
            || backend.isEmpty()) {
            if (snapReady() && !info.snapId.isEmpty()) {
                d->procDialog->setStatusMessage(
                    tr("Instalando %1 vía Snap…").arg(info.snapId));
                d->snap->installPackage(info.snapId);
                return;
            }
        }
        if (backend.isEmpty() && info.origin == QLatin1String("github")) {
            if (!downloadGithubAsset(info.repository)) {
                d->procDialog->setStatusMessage(
                    tr("Buscando la versión de %1 en GitHub…")
                        .arg(info.repository));
                d->pendingGithubRepos.insert(info.repository);
                d->github->fetchLatestReleases();
            }
            return;
        }
        d->procDialog->setStatusMessage(
            tr("No hay backend disponible para %1; usa Ver detalles.")
                .arg(info.name));
    };

    // Routes one app to the backend that manages its removal. The choice is
    // based on what is *actually installed* (native package list vs. Flatpak
    // app list), so legacy ids like "firefox.desktop" map to the real
    // "org.mozilla.firefox" instead of failing with id errors.
    auto removeApp = [this, showProcess, flatpakReady, snapReady, baseName,
                      matchesFlatpakId](const AppInfo &info) {
        showProcess();
        const QString base = baseName(info.id);
        QString nativePkg =
            info.packageNames.isEmpty() ? base : info.packageNames.first();

        // AppStream components do not always publish PackageName. Ask the
        // package manager which package owns the launcher instead of giving
        // up on removal.
        if (!d->nativeIds.contains(nativePkg)
            && !info.desktopFile.isEmpty()
            && d->native->isAvailable()) {
            const QString owner =
                d->native->packageOwningFile(info.desktopFile);
            if (!owner.isEmpty() && d->nativeIds.contains(owner))
                nativePkg = owner;
        }

        if (d->nativeIds.contains(nativePkg) && d->native->isAvailable()) {
            d->procDialog->setStatusMessage(tr("Desinstalando %1…").arg(nativePkg));
            d->native->removePackage(nativePkg);
            return;
        }
        const QString fid = matchesFlatpakId(info.id, d->flatpakIds);
        if (!fid.isEmpty() && flatpakReady()) {
            d->procDialog->setStatusMessage(
                tr("Desinstalando %1 vía Flatpak…").arg(fid));
            d->flatpak->removePackage(fid);
            return;
        }
        if (info.origin == QLatin1String("flatpak") && flatpakReady()) {
            const QString target = base.isEmpty() ? info.id : base;
            d->procDialog->setStatusMessage(
                tr("Desinstalando %1 vía Flatpak…").arg(target));
            d->flatpak->removePackage(target);
            return;
        }

        QString snapName = base;
        if (snapName.contains(QLatin1Char('_')))
            snapName = snapName.section(QLatin1Char('_'), 0, 0);
        if (snapName.startsWith(QLatin1String("snap.")))
            snapName = snapName.mid(5).section(QLatin1Char('.'), 0, 0);

        if ((d->snapIds.contains(snapName)
             || info.origin == QLatin1String("snap"))
            && snapReady()) {
            d->procDialog->setStatusMessage(
                tr("Desinstalando %1 vía Snap…").arg(snapName));
            d->snap->removePackage(snapName);
            return;
        }
        d->procDialog->setStatusMessage(
            tr("No se pudo desinstalar %1; no está instalado en ningún "
               "gestor conocido.")
                .arg(info.name.isEmpty() ? info.id : info.name));
    };

    // ------------------------------------------------ detail dialog
    auto openDetail = [this, showProcess, openInstalledApp, removeApp, installApp](
                      const AppInfo &info) {
        auto *dialog = new AppDetailDialog(info, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
connect(dialog, &AppDetailDialog::installRequested, this,
            [this, dialog, showProcess, installApp](const AppInfo &app,
                                                    const QString &backend) {
                showProcess();
                installApp(app, backend);
                dialog->close();
            });
connect(dialog, &AppDetailDialog::removeRequested, this,
            [this, dialog, removeApp](const AppInfo &info) {
                removeApp(info);
                dialog->close();
            });
        connect(dialog, &AppDetailDialog::openRequested,
                this, openInstalledApp);
        dialog->open();
    };

    connect(d->home, &HomeView::detailsRequested, this, openDetail);
    connect(d->catalog, &CatalogView::detailsRequested, this, openDetail);
    connect(d->home, &HomeView::openRequested, this, openInstalledApp);
    connect(d->catalog, &CatalogView::openRequested, this, openInstalledApp);
    connect(d->installed, &InstalledView::openRequested, this, openInstalledApp);

    connect(d->home, &HomeView::installRequested, this,
            [installApp](const AppInfo &info, const QString &backend) {
                installApp(info, backend);
            });
    connect(d->home, &HomeView::removeRequested, this,
            [removeApp](const AppInfo &info) { removeApp(info); });
    connect(d->catalog, &CatalogView::installRequested, this,
            [installApp](const AppInfo &info, const QString &backend) {
                installApp(info, backend);
            });
    connect(d->catalog, &CatalogView::removeRequested, this,
            [removeApp](const AppInfo &info) { removeApp(info); });
    connect(d->installed, &InstalledView::removeRequested, this,
            [removeApp](const AppInfo &info) { removeApp(info); });
    connect(d->local, &LocalInstallerView::installRequested, this,
            [this, showProcess](const QString &filePath) {
                showProcess();
                d->procDialog->setStatusMessage(
                    tr("Instalando %1…").arg(QFileInfo(filePath).fileName()));
                d->native->installLocalFile(filePath);
            });

    // ------------------------------------------------- process feedback
    const auto wireBackend =
        [this, refreshInstalledStatus, absorbInstalled, applyAnnotations](
            AbstractBackend *backend) {
        connect(backend, &AbstractBackend::processOutput,
                d->procDialog, &ProcessDialog::appendOutput);
        connect(backend, &AbstractBackend::progressChanged,
                d->procDialog, &ProcessDialog::setProgress);
        connect(backend, &AbstractBackend::operationFinished, this,
                [this, refreshInstalledStatus, absorbInstalled,
                 applyAnnotations](bool ok, const QString &message,
                                   const QString &command) {
                    Q_UNUSED(command)
                    d->procDialog->setStatusMessage(
                        ok ? tr("Listo: %1").arg(message)
                           : tr("Error: %1").arg(message));
                    if (d->pendingRefreshes > 0) {
                        --d->pendingRefreshes;
                        if (d->pendingRefreshes == 0) {
                            absorbInstalled();
                            applyAnnotations();
                        }
                    } else if (ok) {
                        refreshInstalledStatus();
                    }
                });
        connect(backend, &AbstractBackend::errorOccurred, this,
                [this](const QString &message) {
                    d->procDialog->setStatusMessage(tr("Error: %1").arg(message));
                    d->procDialog->show();
                    d->procDialog->raise();
                });
    };
    wireBackend(d->native);
    wireBackend(d->flatpak);
    wireBackend(d->snap);
    connect(d->procDialog, &ProcessDialog::cancelRequested, this, [this] {
        if (d->native->busy())
            d->native->killActiveProcess();
        else if (d->flatpak->busy())
            d->flatpak->killActiveProcess();
        else if (d->snap->busy())
            d->snap->killActiveProcess();
        d->procDialog->setStatusMessage(tr("Operación cancelada."));
    });

    // ------------------------------------------------ installed status
    connect(d->native, &AbstractBackend::packagesRefreshed, this,
            [this, absorbInstalled, applyAnnotations](
                const QStringList &packages) {
                d->nativeIds.clear();
                d->nativeIds.reserve(packages.size());
                for (const QString &p : packages)
                    d->nativeIds.insert(p);
                absorbInstalled();
                applyAnnotations();
            });
    connect(d->flatpak, &AbstractBackend::packagesRefreshed, this,
            [this, absorbInstalled, applyAnnotations](
                const QStringList &packages) {
                d->flatpakIds.clear();
                d->flatpakIds.reserve(packages.size());
                for (const QString &p : packages)
                    d->flatpakIds.insert(p);
                absorbInstalled();
                applyAnnotations();
            });
    connect(d->snap, &AbstractBackend::packagesRefreshed, this,
            [this, absorbInstalled, applyAnnotations](
                const QStringList &packages) {
                d->snapIds.clear();
                d->snapIds.reserve(packages.size());
                for (const QString &p : packages)
                    d->snapIds.insert(p);
                absorbInstalled();
                applyAnnotations();
            });

    // ------------------------------------------------------- bootstrap
    QTimer::singleShot(250, this, [this, refreshInstalledStatus] {
        d->stream->startLoading();
        d->github->fetchLatestReleases();
        refreshInstalledStatus();
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Remember the window geometry across sessions.
    ConfigManager::instance()->setWindowGeometry(saveGeometry());
    event->accept();
}

} // namespace LightStore
