#include "core/AppStreamManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThreadPool>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>

#include <AppStreamQt/icon.h>
#include <AppStreamQt/image.h>
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
static QList<AppInfo> loadPoolWorker(bool systemPool)
{
    QList<AppInfo> result;

    AppStream::Pool pool;
    pool.setLocale(QLocale::system().name());
    pool.setLoadStdDataLocations(systemPool);

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
        info.categories = cpt.categories();
        info.packageNames = cpt.packageNames();
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

        // Native components frequently ship no usable AppStream icon (only a
        // theme name or a stale cache path). Fall back to the real Icon= of
        // the installed .desktop launcher so system apps still show artwork.
        if (info.iconPath.isEmpty() && info.iconUrl.isEmpty()) {
            const QString desktopFile = findDesktopFile(info.id);
            if (!desktopFile.isEmpty()) {
                const QString icon = desktopIcon(desktopFile);
                if (icon.startsWith(QLatin1Char('/')))
                    info.iconPath = icon;
                else if (!icon.isEmpty())
                    info.iconName = icon;
            }
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
class AppStreamManager::Private
{
public:
    QList<AppInfo> apps;
    QList<AppInfo> userApps;
    bool isBusy = false;
    bool refreshQueued = false;
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
    if (d->isBusy)
        return;
    d->isBusy = true;

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

    // Cache is fresh? Load directly, otherwise kick a background refresh
    // first and load the (possibly stale) pool right away for instant UI.
    // Always tell the UI we are loading so the busy indicator shows up even
    // when the cache is warm.
    emit refreshStarted();
    loadPoolAsync(/*systemPool=*/true);

    if (needsRefresh) {
        // fire-and-forget index refresh; result is not strictly required for
        // a responsive store view, but the user can re-trigger it manually.
        runCacheRefresh();
    }
}

void AppStreamManager::requestCacheRefresh()
{
    // If a load is already running, remember the request and run a fresh
    // load as soon as the current one finishes instead of silently ignoring
    // the click (otherwise the busy indicator never appears).
    if (d->isBusy) {
        d->refreshQueued = true;
        return;
    }
    emit refreshStarted();
    runCacheRefresh();
}

void AppStreamManager::runCacheRefresh()
{
    // "appstreamcli refresh-index" no longer exists in AppStream 1.x.
    // "refresh --source=os" regenerates the OS metadata cache in exactly the
    // same way the apt post-invoke hook does, so the pool and this store see
    // identical data afterwards.
    QProcess *refresh = new QProcess(this);
    connect(refresh, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, refresh](int code, QProcess::ExitStatus) {
                refresh->deleteLater();
                if (code == 0) {
                    emit refreshCacheSucceeded();
                    // The refreshed cache may hold newer components than the
                    // pool we already loaded; reflect that in the UI.
                    loadPoolAsync(/*systemPool=*/true);
                } else {
                    emit refreshCacheFailed();
                }
            });
    connect(refresh, &QProcess::errorOccurred, this, [this, refresh] {
        refresh->deleteLater();
        emit refreshCacheFailed();
    });
    refresh->start(QStringLiteral("appstreamcli"),
                   QStringList() << QStringLiteral("refresh")
                                 << QStringLiteral("--source=os"));
}

void AppStreamManager::loadPoolAsync(bool systemPool)
{
    using Watcher = QFutureWatcher<QList<AppInfo>>;
    d->isBusy = true;
    Watcher *watcher = new Watcher(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, systemPool] {
                const QList<AppInfo> loaded = watcher->result();
                watcher->deleteLater();
                if (systemPool)
                    d->apps = loaded;
                else
                    d->userApps = loaded;

                if (d->refreshQueued) {
                    d->refreshQueued = false;
                    loadPoolAsync(systemPool);
                    return;
                }
                d->isBusy = false;
                emit dataReady(loaded);
            });

    watcher->setFuture(QtConcurrent::run(&loadPoolWorker, systemPool));
}

#include "moc_AppStreamManager.cpp"
