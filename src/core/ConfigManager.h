#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

/**
 * Centralized runtime configuration, persisted to an INI file located at
 * ~/.config/LightStore/config.ini via QSettings.
 *
 * All settings live in one singleton instance. Modifications are flushed to
 * disk immediately so a crash can never lose user preferences.
 */
class ConfigManager : public QObject
{
    Q_OBJECT

public:
    /**
     * Absolute path of the persistent configuration file.
     */
    static QString configFilePath();

    /**
     * Access the shared process-wide instance.
     */
    static ConfigManager *instance();

    // --- cache TTL -------------------------------------------------------
    /**
     * Cache TTL (in hours). When the AppStream cache is older than this,
     * a background refresh is triggered. Default: 24.
     */
    int cacheTtlHours() const;
    void setCacheTtlHours(int hours);

    // --- Flatpak backend ------------------------------------------------
    /**
     * Whether the Flatpak backend is enabled. On first run the value falls
     * back to the auto-detected availability of /usr/bin/flatpak.
     */
    bool flatpakEnabled() const;
    void setFlatpakEnabled(bool enabled);

    // --- Snap backend ----------------------------------------------------
    bool snapEnabled() const;
    void setSnapEnabled(bool enabled);

    // --- GitHub repositories ---------------------------------------------
    /**
     * List of repositories in "owner/name" form that are polled for the
     * latest releases on the Home screen and in the Catalog.
     */
    QStringList githubRepositories() const;
    void setGithubRepositories(const QStringList &repos);
    void addGithubRepository(const QString &repo);
    void removeGithubRepository(const QString &repo);

    // --- runtime auto-detection flags (not user-edited) ------------------
    /**
     * True when /usr/bin/flatpak was present when the app was first run.
     * Stored so the UI can keep a consistent "available" indicator.
     */
    bool flatpakAvailableOnSystem() const;
    bool snapAvailableOnSystem() const;
    void setFlatpakAvailableOnSystem(bool available);
    void setSnapAvailableOnSystem(bool available);

    // --- window geometry -------------------------------------------------
    /**
     * Last saved main-window geometry/state (from QWidget::saveGeometry).
     * Empty when never stored yet, in which case the caller keeps its
     * default size.
     */
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    // --- persistence -----------------------------------------------------
    /** Flush all pending changes to disk. */
    void flush();

private:
    ConfigManager(QObject *parent = nullptr);
    ~ConfigManager() override;
    Q_DISABLE_COPY_MOVE(ConfigManager)

    class Private;
    std::unique_ptr<Private> d;
};
