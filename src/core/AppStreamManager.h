#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

/**
 * Plain metadata payload that the UI consumes. It contains no AppStream
 * types on purpose: the pool is loaded inside a worker thread and only the
 * plain fields below are shipped back through queued signals, so the GUI
 * thread never touches the AppStreamQt pool object.
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

    /** Native package name(s) this component maps to (may be empty). */
    QStringList packageNames;

    /** Confirmed bundle identifiers from AppStream metadata. */
    QString flatpakId;
    QString snapId;

    /** Project website from AppStream, used to identify matching sources. */
    QString homepage;

    /** Origin indicator: "system", "user", "flatpak", "snap" or "github". */
    QString origin;

    /** Rasterized 64x64 icon (if one could be resolved for this system). */
    QImage icon;

    /** Base icon name hint (stock icon id). */
    QString iconName;

    /** Absolute path to a cached, non-empty icon file (may be empty). */
    QString iconPath;

    /** Remote icon URL (may be empty). Downloaded lazily into IconCache. */
    QString iconUrl;

    /** Screenshot image URLs (remote or local). May be empty. */
    QStringList screenshotUrls;

    /**
     * Heuristic popularity score (higher = more popular). Computed from a
     * curated list of well-known apps plus the richness of the metadata
     * (description, screenshots, categories). Used to order the Home screen.
     */
    int popularity = 0;

    /** True when this is an ordinary desktop/console application. */
    bool isApplication = true;

    /** Absolute path of the launcher that starts this app (may be empty). */
    QString desktopFile;

    /** For GitHub-origin apps: the "owner/name" repository. */
    QString repository;

    /** For GitHub-origin apps: the most recent release tag (e.g. "v1.2.3"). */
    QString releaseTag;

    /** Tags rendered as a single comma-separated string (UI convenience). */
    QString tagsAsStringListSeparatedByComma() const;

    bool installed = false;

    bool isValid() const { return !id.isEmpty(); }
};

Q_DECLARE_METATYPE(AppInfo)
Q_DECLARE_METATYPE(QList<AppInfo>)

/**
 * Asynchronous facade over the system AppStream pool.
 *
 * Loading the metadata pool can take a measurable amount of time, so all
 * AppStream work runs on the global Qt thread pool through QtConcurrent and
 * only the resulting @ref AppInfo list (a copyable, plain payload) comes back
 * to the GUI thread via queued signals.
 *
 * A TTL check decides whether the local AppStream cache is fresh enough to be
 * used directly or must first be refreshed in the background with
 * "appstreamcli refresh --source=os" (the AppStream 1.x replacement for the
 * old apt post-invoke hook). The pool itself is loaded from the system data
 * locations, which on apt-based distros read the DEP-11 YAML catalogs
 * downloaded into /var/lib/app-info by "apt update".
 */
class AppStreamManager final : public QObject
{
    Q_OBJECT

public:
    explicit AppStreamManager(QObject *parent = nullptr);
    ~AppStreamManager() override;
    Q_DISABLE_COPY_MOVE(AppStreamManager)

    /** The last successfully loaded system applications. */
    QList<AppInfo> apps() const;

    /** The last successfully loaded user-scope applications. */
    QList<AppInfo> userApps() const;

public slots:
    /** Kick off the asynchronous load. @sa dataReady. */
    void startLoading();

    /** Force a cache refresh, then reload. */
    void requestCacheRefresh(bool includeSystemIndex = true);

signals:
    /** Emitted when system applications finished loading. */
    void dataReady(const QList<AppInfo> &apps);

    /** A cache refresh was started in the background. */
    void refreshStarted();

    /** Background cache refresh completed successfully. */
    void refreshCacheSucceeded();

    /** Background cache refresh failed (tool missing / non-zero exit). */
    void refreshCacheFailed();

    /** A load failed; @a message describes the problem. */
    void loadFailed(const QString &message);

private:
    /**
     * Run the refresh steps (optional system package index, Flatpak appstream,
     * AppStream OS cache) in sequence and report on
     * @ref refreshCacheSucceeded / @ref refreshCacheFailed.
     */
    void runCacheRefresh(bool includeSystemIndex);
    void runNextRefreshStep();

    /**
     * Load the system and the per-user pools sequentially on one worker
     * thread. libappstream is not safe for concurrent pool loads, so both
     * scopes must be read one after the other.
     */
    void loadAllPools();

    class Private;
    std::unique_ptr<Private> d;
};

Q_DECLARE_METATYPE(AppStreamManager*)
