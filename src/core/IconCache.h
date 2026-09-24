#pragma once

#include <QObject>
#include <QString>
#include <QSet>

#include <memory>

struct AppInfo;

/**
 * On-demand, disk-backed icon store.
 *
 * Goals (resource budget in mind):
 *   - Never decode or hold icons for the whole catalog: memory only grows for
 *     the tiles actually on screen.
 *   - Remote (Flathub) icons are downloaded lazily, only when a tile that
 *     needs them becomes visible, with a small bounded number of parallel
 *     transfers.
 *   - Every finished icon is persisted under ~/.cache/LightStore/icons so the
 *     next run (and every later scroll back) is served from disk instantly,
 *     with no re-download and no RAM retained.
 *
 * resolveIcon() is the single entry point widgets use. It returns a local
 * file path immediately when one already exists (AppStream cache or our disk
 * cache) and otherwise queues an async download, emitting iconReady() when
 * the file lands.
 */
class IconCache final : public QObject
{
    Q_OBJECT

public:
    /** Process-wide singleton. */
    static IconCache *instance();

    /** Root directory where downloaded icons are persisted. */
    static QString cacheDir();

    /**
     * Best local path for @a info's icon, or an empty string when none
     * exists yet. When only a remote URL is known, the download is queued
     * here and @ref iconReady fires on completion. Callers render the
     * returned path and also listen for iconReady to fill in later.
     */
    QString resolveIcon(const AppInfo &info);

    /** Total bytes currently stored in the icon cache directory. */
    qint64 cacheSizeBytes() const;

    /** Delete every file in the icon cache directory. */
    void clearCache();

signals:
    /** @a localPath (absolute) is ready for @a appId (an AppInfo::id). */
    void iconReady(const QString &appId, const QString &localPath);

private:
    explicit IconCache(QObject *parent = nullptr);
    ~IconCache() override;
    Q_DISABLE_COPY_MOVE(IconCache)

    QString filePathFor(const QString &appId) const;
    void enqueue(const QString &appId, const QUrl &url);
    void pump();

    class Private;
    std::unique_ptr<Private> d;
};