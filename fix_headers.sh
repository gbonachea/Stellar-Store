#!/usr/bin/env python3
import io, os
B = os.environ["B"]

# ---------------------------------------------------------------------------
# 1) GitHubFetcher.h — API REAL según GitHubFetcher.cpp (leído, 246 líneas)
# ---------------------------------------------------------------------------
gh_h = '''#pragma once

#include <QObject>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;

/**
 * Lightweight client for the GitHub Releases API.
 *
 * Responsibilities:
 *   - Query "https://api.github.com/repos/{owner}/{repo}/releases/latest"
 *     for each configured repository (ConfigManager::githubRepositories).
 *   - Parse the JSON reply and extract release assets whose file name ends
 *     in ".deb", ".rpm" or ".AppImage".
 *   - Download assets asynchronously to the correct cache location:
 *         .deb / .rpm  -> /tmp/lightstore/
 *         .AppImage    -> ~/.local/bin/
 *   - After an AppImage download, generate a valid ".desktop" launcher in
 *     ~/.local/share/applications/ so the app appears in system menus.
 *
 * Every network operation goes through QNetworkAccessManager. Nothing here
 * blocks the GUI thread; progress is streamed via signals.
 */
class GitHubFetcher final : public QObject
{
    Q_OBJECT

public:
    explicit GitHubFetcher(QObject *parent = nullptr);
    ~GitHubFetcher() override;
    Q_DISABLE_COPY_MOVE(GitHubFetcher)

    /**
     * A single downloadable release artifact.
     */
    struct Asset {
        QString repo;       // "owner/name"
        QString fileName;   // e.g. "LightStore-1.2.3-x86_64.AppImage"
        QUrl url;           // API asset url (JSON "url")
        QUrl direct;        // browser_download_url (redirect-safe)
        qint64 sizeBytes = 0;
    };

    /**
     * Kick off metadata fetches for every repository currently stored in the
     * configuration. @sa ConfigManager::githubRepositories.
     */
    void fetchLatestReleases();

    /**
     * Start downloading a specific asset to the store's staging directory.
     * The destination is chosen automatically from the file extension:
     *   .deb / .rpm  -> /tmp/lightstore/
     *   .AppImage    -> ~/.local/bin/
     *
     * @param asset asset previously delivered through @ref assetDiscovered.
     * @return true if the request was accepted (an unknown/system-missing
     *              destination returns false).
     */
    bool downloadAsset(const Asset &asset);

    /**
     * Download a release asset to the caller-provided absolute path.
     * Used by LocalInstallerView as well as the catalog actions.
     */
    void downloadToFile(const Asset &asset, const QString &destinationPath);

    /**
     * True when at least one network fetch is currently in flight.
     */
    bool busy() const;

signals:
    /**
     * Emitted once per successfully parsed release asset.
     */
    void assetDiscovered(const GitHubFetcher::Asset &asset);

    /**
     * Progress for a single asset download: received / total bytes.
     */
    void downloadProgress(const QString &repo, const QString &fileName,
                          qint64 bytesReceived, qint64 bytesTotal);

    /**
     * A download finished. @a localPath may be empty on failure.
     */
    void downloadFinished(const QString &repo, const QString &fileName,
                          const QString &localPath, bool ok);

    /** All scheduled network requests have been processed. */
    void allFetchesFinished();

    /** User-facing error (network, HTTP status, parse failure). */
    void errorOccurred(const QString &message);

protected:
    /**
     * Fetch / parse the given repository (either process-wide or single-shot).
     */
    void processRepo(const QString &repoToken, bool isSingle);

    /** Called when an individual fetch completes; emits allFetchesFinished
     *  when the whole batch is done. */
    void finishFetch(bool isSingle);

    /**
     * Post-download hook. For AppImages this makes the file executable and
     * writes a .desktop launcher so the app shows up in system menus.
     */
    void postDownload(const Asset &asset, const QString &localPath);

private:
    void downloadToFileImpl(const Asset &asset, const QString &destinationPathFromCache,
                            bool createLauncher, bool useStaging);
    struct Asset;

    class Private;
    std::unique_ptr<Private> d;

    QNetworkAccessManager *m_manager = nullptr;
};
'''

# ---------------------------------------------------------------------------
# 2) AppStreamManager.h — API REAL según AppStreamManager.cpp (leído, 218 líneas)
# ---------------------------------------------------------------------------
asm_h = '''#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

/**
 * Plain metadata payload exchanged between AppStreamManager and the UI.
 * Deliberately free of AppStreamQt types so it can be marshalled between
 * worker threads and the GUI thread through queued signals.
 */
struct AppInfo
{
    /** Stable AppStream component id, e.g. "org.gnome.gedit.desktop". */
    QString id;

    /** Localized display name. */
    QString name;

    /** Localized one-line summary. */
    QString summary;

    /** Localized long description (may be empty). */
    QString description;

    /** Category list, e.g. {"Development", "IDE"}. */
    QStringList categories;

    /** Native package names this component maps to (may be empty). */
    QStringList packageNames;

    /** Origin indicator: "native", "flatpak", "snap" or "github". */
    QString origin;

    /** Rasterized 64x64 icon (if one could be resolved for this system). */
    QImage icon;

    /** Base name / stock icon name hint (e.g. "org.gnome.gedit"). */
    QString iconName;

    /** Absolute path to a cached, non-empty icon file (catalog thumbnails). */
    QString iconPath;

    /** True when this is an ordinary desktop/console application. */
    bool isApplication = true;

    /** For GitHub-origin apps: the "owner/name" repository. */
    QString repository;

    /** For GitHub-origin apps: the most recent release tag (e.g. "v1.2.3"). */
    QString releaseTag;

    /** Human-readable list of categories separated by ", ". */
    QString tagsAsStringListSeparatedByComma() const;

    bool isValid() const { return !id.isEmpty(); }
};

Q_DECLARE_METATYPE(AppInfo)
Q_DECLARE_METATYPE(QList<AppInfo>)

/**
 * Asynchronous facade over the AppStreamQt pool.
 *
 * All work that could be slow (loading the AppStream metadata pool from disk)
 * is pushed onto Qt's global thread pool via QtConcurrent; the UI thread is
 * never blocked. A TTL check decides whether the local AppStream cache is
 * fresh enough to be used directly or must first be refreshed in the
 * background with "appstreamcli refresh-index --user".
 */
class AppStreamManager final : public QObject
{
    Q_OBJECT

public:
    /**
     * @param cacheTtlHours cache freshness threshold, in hours. When the
     *                      AppStream cache is older than this a background
     *                      refresh is launched before the pool is (re)loaded.
     */
    explicit AppStreamManager(int cacheTtlHours = 24, QObject *parent = nullptr);
    ~AppStreamManager() override;
    Q_DISABLE_COPY_MOVE(AppStreamManager)

    /** System-wide applications (from /usr/share/metainfo). */
    QList<AppInfo> apps() const;

    /** User-scope applications (from ~/.local/share/appstream). */
    QList<AppInfo> userApps() const;

public slots:
    /** Start the asynchronous load. Result arrives via @ref dataReady. */
    void startLoading();

    /** Re-check the cache TTL and reload the pool when it is stale. */
    void requestCacheRefresh();

    /** Load the pool on a worker thread. @a systemPool selects the scope. */
    void loadPoolAsync(bool systemPool);

signals:
    /** Metadata is available (after a successful load / refresh). */
    void dataReady(const QList<AppInfo> &apps);

    /** A background refresh was kicked off. */
    void refreshStarted();

    /** The "appstreamcli refresh-index" command finished successfully. */
    void refreshCacheSucceeded();

    /** The refresh command failed / was not available. */
    void refreshCacheFailed();

    /** A load failed. @a message describes the problem. */
    void loadFailed(const QString &message);

private:
    class Private;
    std::unique_ptr<Private> d;
};
'''

import io
io.open(os.path.join(B, "src/core/GitHubFetcher.h"), "w", encoding="utf-8").write(gh_h)
io.open(os.path.join(B, "src/core/AppStreamManager.h"), "w", encoding="utf-8").write(asm_h)
print("headers escritos")
