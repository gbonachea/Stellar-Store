#include "core/AppStreamManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>

#include <AppStreamQt/icon.h>
#include <AppStreamQt/bundle.h>
#include <AppStreamQt/image.h>
#include <AppStreamQt/metadata.h>
#include <AppStreamQt/pool.h>
#include <AppStreamQt/screenshot.h>

#include "core/ConfigManager.h"
#include "core/DesktopEntry.h"

// ----------------------------------------------------------------------------
// AppInfo
// ----------------------------------------------------------------------------
QString AppInfo::tagsAsStringListSeparatedByComma() const
{
    return categories.join(QStringLiteral(", "));
}

// ----------------------------------------------------------------------------
// Popularity
// ----------------------------------------------------------------------------

/**
 * Rough "most popular first" ordering for the Home screen. AppStream metadata
 * on Debian/Ubuntu does not ship global ratings, so the score combines a
 * curated list of well-known applications with metadata richness
 * (description / screenshots / categories). Unknown apps sort below the
 * curated set, alphabetically.
 */
namespace {
int popularityScore(const AppInfo &info)
{
    static const QStringList famous = {
        QStringLiteral("firefox"),      QStringLiteral("chromium"),
        QStringLiteral("libreoffice"),  QStringLiteral("vlc"),
        QStringLiteral("gimp"),         QStringLiteral("audacity"),
        QStringLiteral("telegram"),     QStringLiteral("discord"),
        QStringLiteral("spotify"),      QStringLiteral("gedit"),
        QStringLiteral("kate"),         QStringLiteral("thunderbird"),
        QStringLiteral("okular"),       QStringLiteral("evince"),
        QStringLiteral("blender"),      QStringLiteral("inkscape"),
        QStringLiteral("krita"),        QStringLiteral("rhythmbox"),
        QStringLiteral("transmission"), QStringLiteral("virtualbox"),
        QStringLiteral("steam"),        QStringLiteral("obs-studio"),
        QStringLiteral("kdenlive"),     QStringLiteral("simple-scan"),
        QStringLiteral("gnome calculator"),
    };

    const QString lower = info.name.toLower();
    int score = 0;
    for (int i = 0; i < famous.size(); ++i) {
        if (lower.contains(famous.at(i))) {
            score += (famous.size() - i) * 1000;
            break;
        }
    }

    if (!info.description.isEmpty())
        score += 300;
    score += info.screenshotUrls.size() * 120;
    score += info.categories.size() * 20;
    if (!info.icon.isNull() || !info.iconPath.isEmpty())
        score += 50;
    return score;
}

} // namespace

// ----------------------------------------------------------------------------
// Worker
// ----------------------------------------------------------------------------

/**
 * Runs on a worker thread. Loads the AppStream pool and extracts the small
 * amounts of metadata the store actually needs (id, name, summary, categories,
 * native package name, first usable icon). No AppStream types leak back into
 * the calling thread — only this plain struct does.
 *
 * @param systemPool true to load the system pool, false to load only metadata
 *                   owned by the current user (~/.local/share/appstream).
 * @return The full list of desktop applications found in the pool.
 */
static QList<AppInfo> loadPoolWorker(bool systemPool, bool excludeFlatpak = false)
{
    QList<AppInfo> result;

    AppStream::Pool pool;
    pool.setLocale(QLocale::system().name());

    if (systemPool) {
        // Standard locations include the OS catalog, Flatpak data and the
        // per-user metadata installed for the current account.
        pool.setLoadStdDataLocations(true);
        if (excludeFlatpak)
            pool.removeFlags(AppStream::Pool::FlagLoadFlatpak);
    } else {
        // Load only the current user's own metadata. Disabling the standard
        // locations is required: the default load would duplicate the system
        // pool and lose the ability to tell both scopes apart.
        pool.setLoadStdDataLocations(false);
        bool hasUserData = false;
        const QString dataHome = QStandardPaths::writableLocation(
            QStandardPaths::GenericDataLocation);
        const QString metainfoDir = dataHome + QStringLiteral("/metainfo");
        const QString appstreamDir = dataHome + QStringLiteral("/appstream");
        if (QFileInfo::exists(metainfoDir)) {
            pool.addExtraDataLocation(metainfoDir,
                                      AppStream::Metadata::FormatStyleMetainfo);
            hasUserData = true;
        }
        if (QFileInfo::exists(appstreamDir)) {
            pool.addExtraDataLocation(appstreamDir,
                                      AppStream::Metadata::FormatStyleCatalog);
            hasUserData = true;
        }
        if (!hasUserData)
            return result;
    }

    if (!pool.load())
        return result;

    const auto components = pool.components();
    result.reserve(components.size());

    for (const AppStream::Component &cpt : components) {
        // Only ship ordinary desktop and console applications. Drivers,
        // fonts, input methods and operating systems are noise for a store.
        const AppStream::Component::Kind kind = cpt.kind();
        if (kind != AppStream::Component::KindDesktopApp
            && kind != AppStream::Component::KindConsoleApp) {
            continue;
        }

        AppInfo info;
        info.id = cpt.id();
        info.name = cpt.name();
        info.summary = cpt.summary();
        info.description = cpt.description();
        info.homepage = cpt.url(AppStream::Component::UrlKindHomepage).toString();
        info.categories = cpt.categories();
        info.packageNames = cpt.packageNames();
        for (const AppStream::Bundle &bundle : cpt.bundles()) {
            if (bundle.kind() == AppStream::Bundle::KindFlatpak) {
                const QStringList parts = bundle.id().split(QLatin1Char('/'));
                info.flatpakId = parts.size() >= 2 && parts.first() == QLatin1String("app")
                    ? parts.at(1) : bundle.id();
            } else if (bundle.kind() == AppStream::Bundle::KindSnap) {
                info.snapId = bundle.id();
            }
        }
        info.origin = cpt.origin();
        info.iconName = cpt.id();

        // Deliberately do NOT decode icons here: decoding one QImage per
        // component is what bloats startup and RAM on huge catalogs. Cards
        // decode their icon lazily from iconPath/iconName, so only the tiles
        // actually on screen ever touch the image files. Remote icons are
        // remembered by URL and downloaded on demand into IconCache.
        const QList<AppStream::Icon> icons = cpt.icons();
        for (const AppStream::Icon &icn : icons) {
            if (icn.kind() == AppStream::Icon::KindRemote) {
                const QUrl remoteUrl = icn.url();
                if (info.iconUrl.isEmpty() && remoteUrl.isValid()
                    && (remoteUrl.scheme() == QStringLiteral("http")
                        || remoteUrl.scheme() == QStringLiteral("https"))) {
                    info.iconUrl = remoteUrl.toString();
                }
            } else if (icn.kind() == AppStream::Icon::KindCached) {
                const QString fileName = icn.url().toLocalFile();
                if (!fileName.isEmpty() && QFileInfo::exists(fileName)) {
                    info.iconPath = fileName;
                    break;
                }
            } else if (icn.kind() == AppStream::Icon::KindStock) {
                info.iconName = icn.name();
                info.iconPath.clear();
                break;
            }
        }

        // Resolve the real launcher once: the store uses it for icons,
        // launching and for finding the package that owns the app.
        info.desktopFile = findDesktopFile(info.id);

        // Native components frequently ship no usable AppStream icon (only a
        // theme name or a stale cache path). Fall back to the real Icon= of
        // the installed .desktop launcher so system apps still show artwork.
        if (info.iconPath.isEmpty() && info.iconUrl.isEmpty()
            && !info.desktopFile.isEmpty()) {
            const QString icon = desktopIcon(info.desktopFile);
            if (icon.startsWith(QLatin1Char('/')))
                info.iconPath = icon;
            else if (!icon.isEmpty())
                info.iconName = icon;
        }

        // Collect up to 4 screenshot images for the detail view.
        const auto screenshots = cpt.screenshotsAll();
        for (const AppStream::Screenshot &shot : screenshots) {
            const auto images = shot.images();
            for (const AppStream::Image &img : images) {
                const QUrl url = img.url();
                if (url.isValid() && !url.isEmpty())
                    info.screenshotUrls << url.toString();
                if (info.screenshotUrls.size() >= 4)
                    break;
            }
            if (info.screenshotUrls.size() >= 4)
                break;
        }

        info.popularity = popularityScore(info);

        result.append(info);
    }

    // Order from most popular to least popular.
    std::stable_sort(result.begin(), result.end(),
                     [](const AppInfo &a, const AppInfo &b) {
                         if (a.popularity != b.popularity)
                             return a.popularity > b.popularity;
                         return a.name.localeAwareCompare(b.name) < 0;
                     });

    return result;
}

// ----------------------------------------------------------------------------
// Private
// ----------------------------------------------------------------------------
namespace {

struct PoolLoadResult
{
    QList<AppInfo> systemApps;
    QList<AppInfo> userApps;
};

/**
 * libappstream keeps non-thread-safe global state (profiling and GObject
 * bookkeeping), so loading two pools in parallel from the global thread pool
 * corrupts it and crashes inside as_pool_get_components(). Read both scopes
 * one after the other on the same worker thread instead.
 */
PoolLoadResult loadAllPoolsWorker()
{
    PoolLoadResult loaded;
    loaded.systemApps = loadPoolWorker(/*systemPool=*/true);
    // AppStream can prefer a Flatpak component over the OS component with
    // the same ID. Load the OS catalog separately as well so its package
    // names survive when the combined view is assembled.
    loaded.systemApps.append(loadPoolWorker(/*systemPool=*/true, /*excludeFlatpak=*/true));
    loaded.userApps = loadPoolWorker(/*systemPool=*/false);
    return loaded;
}

QList<AppInfo> mergedAppLists(const QList<AppInfo> &systemApps,
                              const QList<AppInfo> &userApps)
{
    QList<AppInfo> result;
    QHash<QString, int> indexById;
    indexById.reserve(systemApps.size() + userApps.size());
    QList<AppInfo> allApps = systemApps;
    allApps.append(userApps);
    for (const AppInfo &userApp : allApps) {
        if (userApp.id.isEmpty())
            continue;
        QString key = userApp.id.toLower();
        if (key.endsWith(QLatin1String(".desktop")))
            key.chop(8);
        const auto it = indexById.constFind(key);
        if (it == indexById.cend()) {
            indexById.insert(key, result.size());
            result.append(userApp);
            continue;
        }

        // Same component in both scopes: keep the richer system metadata but
        // merge package names so installed detection works either way.
        AppInfo &existing = result[it.value()];
        for (const QString &pkg : userApp.packageNames) {
            if (!existing.packageNames.contains(pkg))
                existing.packageNames << pkg;
        }
        if (existing.desktopFile.isEmpty())
            existing.desktopFile = userApp.desktopFile;
        if (existing.flatpakId.isEmpty())
            existing.flatpakId = userApp.flatpakId;
        if (existing.snapId.isEmpty())
            existing.snapId = userApp.snapId;
        if (existing.homepage.isEmpty())
            existing.homepage = userApp.homepage;
    }

    // Legacy distribution IDs often use a short desktop filename (e.g.
    // blender.desktop), while Flatpak uses a reverse-DNS ID
    // (org.blender.Blender). Match only when the short ID, final bundle-ID
    // segment and display name all agree, and the native match is unique.
    QHash<QString, QList<int>> nativeByShortId;
    QHash<QString, QList<int>> nativeByProject;
    QHash<QString, QList<int>> nativeByPackage;
    QHash<QString, int> bundleProjectCounts;
    auto projectKey = [](const AppInfo &app) {
        QUrl url(app.homepage);
        if (!url.isValid() || url.host().isEmpty() || app.name.trimmed().isEmpty())
            return QString();
        QString host = url.host().toLower();
        if (host.startsWith(QLatin1String("www.")))
            host.remove(0, 4);
        QString path = url.path();
        while (path.endsWith(QLatin1Char('/')))
            path.chop(1);
        return app.name.trimmed().toCaseFolded() + QLatin1Char('|')
            + host + path;
    };
    auto normalizedToken = [](const QString &text) {
        QString token;
        for (const QChar c : text.toCaseFolded()) {
            if (c.isLetterOrNumber())
                token.append(c);
        }
        return token;
    };
    for (int i = 0; i < result.size(); ++i) {
        const AppInfo &app = result.at(i);
        QString id = app.id.toLower();
        if (id.endsWith(QLatin1String(".desktop")))
            id.chop(8);
        if (!id.contains(QLatin1Char('.')) && !app.packageNames.isEmpty())
            nativeByShortId[id].append(i);
        for (const QString &package : app.packageNames) {
            QString base = package.toLower();
            for (const QString &suffix : {QStringLiteral("-gtk"), QStringLiteral("-qt"),
                                          QStringLiteral("-qt6")}) {
                if (base.endsWith(suffix)) {
                    base.chop(suffix.size());
                    break;
                }
            }
            const QString token = normalizedToken(base);
            if (token.size() >= 5 && !nativeByPackage[token].contains(i))
                nativeByPackage[token].append(i);
        }
        const QString project = projectKey(app);
        if (!project.isEmpty()) {
            if (!app.packageNames.isEmpty())
                nativeByProject[project].append(i);
            else if (!app.flatpakId.isEmpty())
                ++bundleProjectCounts[project];
        }
    }
    QSet<int> merged;
    for (int i = 0; i < result.size(); ++i) {
        const AppInfo &bundle = result.at(i);
        if (bundle.flatpakId.isEmpty() || !bundle.packageNames.isEmpty())
            continue;
        const QString shortId = bundle.flatpakId.section(QLatin1Char('.'), -1).toLower();
        QList<int> matches = nativeByShortId.value(shortId);
        if (matches.size() != 1) {
            const QString project = projectKey(bundle);
            if (!project.isEmpty() && bundleProjectCounts.value(project) == 1)
                matches = nativeByProject.value(project);
        }
        if (matches.size() != 1) {
            QSet<int> candidates;
            for (const QString &part : bundle.flatpakId.split(QLatin1Char('.'))) {
                const QString token = normalizedToken(part);
                if (token.size() < 5)
                    continue;
                for (int candidate : nativeByPackage.value(token)) {
                    if (result.at(candidate).name.trimmed().compare(
                            bundle.name.trimmed(), Qt::CaseInsensitive) == 0)
                        candidates.insert(candidate);
                }
            }
            matches = candidates.values();
        }
        if (matches.size() != 1 || matches.first() == i)
            continue;
        AppInfo &native = result[matches.first()];
        if (native.name.trimmed().compare(bundle.name.trimmed(), Qt::CaseInsensitive) != 0
            || (!projectKey(native).isEmpty() && !projectKey(bundle).isEmpty()
                && projectKey(native) != projectKey(bundle))
            || (!native.flatpakId.isEmpty() && native.flatpakId != bundle.flatpakId))
            continue;
        native.flatpakId = bundle.flatpakId;
        if (native.snapId.isEmpty())
            native.snapId = bundle.snapId;
        if (native.description.isEmpty())
            native.description = bundle.description;
        if (native.iconUrl.isEmpty() && native.iconPath.isEmpty())
            native.iconUrl = bundle.iconUrl;
        if (native.screenshotUrls.isEmpty())
            native.screenshotUrls = bundle.screenshotUrls;
        merged.insert(i);
    }
    QList<AppInfo> unique;
    unique.reserve(result.size() - merged.size());
    for (int i = 0; i < result.size(); ++i) {
        if (!merged.contains(i))
            unique.append(result.at(i));
    }
    return unique;
}
} // namespace

class AppStreamManager::Private
{
public:
    QList<AppInfo> apps;
    QList<AppInfo> userApps;
    bool isBusy = false;
    bool reloadQueued = false;
    bool refreshQueued = false;

    bool refreshRunning = false;
    bool refreshHadFailure = false;
    int refreshStep = 0;
    QStringList refreshPrograms;
    QList<QStringList> refreshArguments;
};

// ----------------------------------------------------------------------------
// Class
// ----------------------------------------------------------------------------
AppStreamManager::AppStreamManager(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    qRegisterMetaType<QList<AppInfo>>("QList<AppInfo>");
}

AppStreamManager::~AppStreamManager() = default;

QList<AppInfo> AppStreamManager::apps() const
{
    return d->apps;
}

QList<AppInfo> AppStreamManager::userApps() const
{
    return d->userApps;
}

void AppStreamManager::startLoading()
{
    if (d->isBusy || d->refreshRunning)
        return;

    // Decide whether a background cache refresh is warranted. We deliberately
    // do NOT block the GUI here; the refresh (appstreamcli refresh --source=os)
    // runs detached and the pool load is dispatched to the global pool.
    const int ttlHours = ConfigManager::instance()->cacheTtlHours();
    const QStringList locations = {
        QDir::homePath() + QStringLiteral("/.cache/appstream"),
        QStringLiteral("/var/cache/app-info/xmls"),
        QStringLiteral("/var/cache/app-info")
    };

    bool needsRefresh = ttlHours <= 0;
    if (!needsRefresh) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 limit = qint64(ttlHours) * 3600;
        bool anyFresh = false;
        for (const auto &loc : locations) {
            const QFileInfo fi(loc);
            if (fi.exists()) {
                const qint64 age = now - fi.lastModified().toSecsSinceEpoch();
                if (age <= limit) {
                    anyFresh = true;
                    break;
                }
            }
        }
        // If none of the well-known cache locations exist yet, treat the
        // metadata as stale on first run so we try to bootstrap it.
        needsRefresh = !anyFresh;
    }

    // Always tell the UI we are loading so the busy indicator shows up even
    // when the cache is warm. System and user pools are loaded together.
    emit refreshStarted();
    loadAllPools();

    if (needsRefresh) {
        // A TTL-triggered refresh must not prompt for root credentials; it
        // only regenerates the AppStream cache from already-available data.
        runCacheRefresh(/*includeSystemIndex=*/false);
    }
}

void AppStreamManager::requestCacheRefresh(bool includeSystemIndex)
{
    // If a load or refresh is already running, remember the request and run a
    // fresh refresh as soon as the current work finishes instead of silently
    // ignoring the click (otherwise the busy indicator never appears).
    if (d->isBusy || d->refreshRunning) {
        d->refreshQueued = true;
        return;
    }
    emit refreshStarted();
    runCacheRefresh(includeSystemIndex);
}

void AppStreamManager::runCacheRefresh(bool includeSystemIndex)
{
    if (d->refreshRunning) {
        d->refreshQueued = true;
        return;
    }

    d->refreshRunning = true;
    d->refreshHadFailure = false;
    d->refreshStep = 0;
    d->refreshPrograms.clear();
    d->refreshArguments.clear();

    const bool hasPkexec = QFileInfo::exists(QStringLiteral("/usr/bin/pkexec"));

    // Refresh the real package index first, so a manual "Refrescar" click
    // actually discovers new AppStream metadata published by the distro.
    if (includeSystemIndex && hasPkexec) {
        if (QFileInfo::exists(QStringLiteral("/usr/bin/apt-get"))) {
            d->refreshPrograms << QStringLiteral("pkexec");
            d->refreshArguments << (QStringList()
                                    << QStringLiteral("apt-get")
                                    << QStringLiteral("update"));
        } else if (QFileInfo::exists(QStringLiteral("/usr/bin/dnf"))) {
            d->refreshPrograms << QStringLiteral("pkexec");
            d->refreshArguments << (QStringList()
                                    << QStringLiteral("dnf")
                                    << QStringLiteral("makecache"));
        } else if (QFileInfo::exists(QStringLiteral("/usr/bin/pacman"))) {
            d->refreshPrograms << QStringLiteral("pkexec");
            d->refreshArguments << (QStringList()
                                    << QStringLiteral("pacman")
                                    << QStringLiteral("-Sy")
                                    << QStringLiteral("--noconfirm"));
        }
    }

    // Flatpak keeps its own AppStream catalog; refresh it when available.
    if (QFileInfo::exists(QStringLiteral("/usr/bin/flatpak"))) {
        d->refreshPrograms << QStringLiteral("flatpak");
        d->refreshArguments << (QStringList()
                                << QStringLiteral("update")
                                << QStringLiteral("--appstream"));
    }

    // "appstreamcli refresh-index" no longer exists in AppStream 1.x.
    // "refresh --source=os" regenerates the OS metadata cache in exactly the
    // same way the apt post-invoke hook does, so the pool and this store see
    // identical data afterwards.
    d->refreshPrograms << QStringLiteral("appstreamcli");
    d->refreshArguments << (QStringList()
                            << QStringLiteral("refresh")
                            << QStringLiteral("--source=os"));

    runNextRefreshStep();
}

void AppStreamManager::runNextRefreshStep()
{
    if (d->refreshStep >= d->refreshPrograms.size()) {
        d->refreshRunning = false;
        if (d->refreshHadFailure)
            emit refreshCacheFailed();
        else
            emit refreshCacheSucceeded();

        // The refreshed caches may hold newer components than the pool we
        // already loaded; reflect that in the UI.
        loadAllPools();

        if (d->refreshQueued) {
            d->refreshQueued = false;
            emit refreshStarted();
            runCacheRefresh(/*includeSystemIndex=*/true);
        }
        return;
    }

    const int index = d->refreshStep;
    const QString program = d->refreshPrograms.at(index);
    const QStringList args = d->refreshArguments.at(index);

    QProcess *step = new QProcess(this);
    connect(step, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, step](int code, QProcess::ExitStatus) {
                step->deleteLater();
                if (code != 0)
                    d->refreshHadFailure = true;
                ++d->refreshStep;
                runNextRefreshStep();
            });
    connect(step, &QProcess::errorOccurred, this,
            [this, step](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                step->deleteLater();
                d->refreshHadFailure = true;
                ++d->refreshStep;
                runNextRefreshStep();
            });
    step->start(program, args);
}

void AppStreamManager::loadAllPools()
{
    // Never run two pool loads at the same time: libappstream shares global
    // state between Pool instances. Queue a reload instead and let the
    // in-flight worker finish first.
    if (d->isBusy) {
        d->reloadQueued = true;
        return;
    }

    using Watcher = QFutureWatcher<PoolLoadResult>;
    d->isBusy = true;
    Watcher *watcher = new Watcher(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const PoolLoadResult loaded = watcher->result();
        watcher->deleteLater();

        d->apps = loaded.systemApps;
        d->userApps = loaded.userApps;
        d->isBusy = false;

        // A refresh finished while this load was running: its results are
        // newer, so read the pools again instead of publishing stale data.
        if (d->reloadQueued) {
            d->reloadQueued = false;
            loadAllPools();
            return;
        }

        emit dataReady(mergedAppLists(d->apps, d->userApps));

        if (d->refreshQueued && !d->refreshRunning) {
            d->refreshQueued = false;
            emit refreshStarted();
            runCacheRefresh(/*includeSystemIndex=*/true);
        }
    });

    watcher->setFuture(QtConcurrent::run(&loadAllPoolsWorker));
}

#include "moc_AppStreamManager.cpp"
