#include "core/IconCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQueue>
#include <QUrl>

#include <utility>

#include "core/AppStreamManager.h"

namespace {

QString safeKey(const QString &appId)
{
    QString key = appId;
    key.replace(QLatin1Char('/'), QLatin1Char('_'));
    key.replace(QLatin1Char('\\'), QLatin1Char('_'));
    key.replace(QStringLiteral(".."), QStringLiteral("__"));
    return key;
}

} // namespace

// ----------------------------------------------------------------------------
// Private
// ----------------------------------------------------------------------------
class IconCache::Private
{
public:
    QNetworkAccessManager *nam = nullptr;

    // Downloads are capped so a huge catalog scrolled fast never spawns
    // hundreds of parallel requests. Four transfers at a time is enough to
    // fill a screen while staying neighbor-friendly.
    static constexpr int maxActive = 4;
    int active = 0;
    QQueue<QPair<QString, QUrl>> queue;
    QSet<QString> inFlight;
};

// ----------------------------------------------------------------------------
// Class
// ----------------------------------------------------------------------------
IconCache::IconCache(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->nam = new QNetworkAccessManager(this);
}

IconCache::~IconCache() = default;

IconCache *IconCache::instance()
{
    static IconCache *s_cache = new IconCache;
    return s_cache;
}

QString IconCache::cacheDir()
{
    return QDir::homePath() + QStringLiteral("/.cache/LightStore/icons");
}

QString IconCache::filePathFor(const QString &appId) const
{
    return QDir(cacheDir()).filePath(safeKey(appId) + QStringLiteral(".png"));
}

QString IconCache::resolveIcon(const AppInfo &info)
{
    // Prefer the local AppStream icon file when it still exists.
    if (!info.iconPath.isEmpty()) {
        if (QFileInfo::exists(info.iconPath))
            return info.iconPath;
        // The cached file may have been purged between load and render;
        // fall through to the persisted download or a fresh download.
    }

    // A previous on-demand download already lives on disk? Serve it.
    const QString cached = filePathFor(info.id);
    if (QFileInfo::exists(cached))
        return cached;

    if (!info.iconUrl.isEmpty())
        enqueue(info.id, QUrl(info.iconUrl));

    return QString();
}

void IconCache::enqueue(const QString &appId, const QUrl &url)
{
    if (!url.isValid() || d->inFlight.contains(appId))
        return;
    for (const auto &item : std::as_const(d->queue)) {
        if (item.first == appId)
            return;
    }
    d->queue.enqueue(qMakePair(appId, url));
    pump();
}

void IconCache::pump()
{
    while (d->active < IconCache::Private::maxActive && !d->queue.isEmpty()) {
        const auto item = d->queue.dequeue();
        const QString appId = item.first;
        if (d->inFlight.contains(appId))
            continue;
        d->inFlight.insert(appId);
        ++d->active;

        QNetworkReply *reply = d->nam->get(QNetworkRequest(item.second));
        connect(reply, &QNetworkReply::finished, this,
                [this, appId, reply] {
                    --d->active;
                    d->inFlight.remove(appId);

                    const QByteArray data = reply->readAll();
                    const bool ok =
                        reply->error() == QNetworkReply::NoError
                        && !data.isEmpty();
                    reply->deleteLater();

                    if (ok) {
                        QDir().mkpath(cacheDir());
                        QFile out(filePathFor(appId));
                        if (out.open(QIODevice::WriteOnly)) {
                            out.write(data);
                            out.close();
                            emit iconReady(appId, out.fileName());
                        }
                    }
                    pump();
                });
    }
}

qint64 IconCache::cacheSizeBytes() const
{
    qint64 total = 0;
    QDir dir(cacheDir());
    const auto entries =
        dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries)
        total += fi.size();
    return total;
}

void IconCache::clearCache()
{
    QDir dir(cacheDir());
    const QStringList files =
        dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &fileName : files)
        QFile::remove(dir.filePath(fileName));
}

#include "moc_IconCache.cpp"