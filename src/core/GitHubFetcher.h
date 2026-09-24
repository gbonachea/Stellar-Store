#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <QNetworkAccessManager>

#include <memory>

class QNetworkReply;
class ConfigManager;

/**
 * Lightweight client for the GitHub Releases API.
 *
 * Responsibilities:
 *   - Query "https://api.github.com/repos/{owner}/{name}/releases/latest"
 *     for every repository configured through ConfigManager::githubRepositories.
 *   - Parse the JSON reply and extract release assets whose file name ends
 *     in ".deb", ".rpm" or ".AppImage".
 *   - Download assets asynchronously to the correct location:
 *         .deb / .rpm  -> /tmp/lightstore/
 *         .AppImage    -> ~/.local/bin/
 *   - After an AppImage download, generate a valid ".desktop" launcher in
 *     ~/.local/share/applications/ so the app appears in system menus.
 *
 * Every network operation goes through QNetworkAccessManager; nothing here
 * blocks the GUI thread and progress is streamed via signals.
 */
class GitHubFetcher final : public QObject
{
    Q_OBJECT

public:
    explicit GitHubFetcher(QObject *parent = nullptr);
    ~GitHubFetcher() override;
    Q_DISABLE_COPY_MOVE(GitHubFetcher)

    /**
     * A single downloadable release asset.
     */
    struct Asset {
        QString repo;        // "owner/name"
        QString fileName;
        QUrl url;
        QUrl direct;         // browser_download_url (redirect-safe)
        qint64 sizeBytes = 0;
    };

    void fetchLatestReleases();
    void downloadAsset(const Asset &asset);
    void downloadToFile(const Asset &asset, const QString &destinationPath);
    bool busy() const;

signals:
    void assetDiscovered(const GitHubFetcher::Asset &asset);
    void downloadProgress(const QString &repo, const QString &fileName,
                          qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(const QString &repo, const QString &fileName,
                          const QString &localPath, bool ok);
    void allFetchesFinished();
    void errorOccurred(const QString &message);

private:
    void processRepo(const QString &repoToken, bool isSingle);
    void finishFetch(bool isSingle);
    void postDownload(const Asset &asset, const QString &localPath);

    class Private;
    Private *d = nullptr;
    QNetworkAccessManager *m_manager = nullptr;
};
