#include "core/GitHubFetcher.h"

#include "core/ConfigManager.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

#include <algorithm>

namespace {

const auto kDebSuffix        = QStringLiteral(".deb");
const auto kRpmSuffix        = QStringLiteral(".rpm");
const auto kAppImageSuffix   = QStringLiteral(".AppImage");
const auto kUserAgent        = QStringLiteral("StellarStore/1.0 (+https://example.org)");

QString stagingDirectory()
{
    return QStringLiteral("/tmp/lightstore");
}

QString binDirectory()
{
    const QString bin = QDir::homePath() + QStringLiteral("/.local/bin");
    QDir().mkpath(bin);
    return bin;
}

QString desktopDirectory()
{
    const QString dir = QDir::homePath() + QStringLiteral("/.local/share/applications");
    QDir().mkpath(dir);
    return dir;
}

bool isSupportedSuffix(const QString &fileName)
{
    return fileName.endsWith(QStringLiteral(".deb"), Qt::CaseInsensitive)
        || fileName.endsWith(QStringLiteral(".rpm"), Qt::CaseInsensitive)
        || fileName.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive);
}

QString choiceForFileName(const QString &fileName)
{
    if (fileName.endsWith(kDebSuffix, Qt::CaseInsensitive) ||
        fileName.endsWith(kRpmSuffix, Qt::CaseInsensitive))
        return stagingDirectory();
    if (fileName.endsWith(kAppImageSuffix, Qt::CaseInsensitive))
        return binDirectory();
    return QString();
}

} // namespace

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------
class GitHubFetcher::Private
{
public:
    QHash<QString, int> activeReplies;          // repo -> in-flight reply count
    int totalFetches = 0;                       // scalar counter
    int completed = 0;
};

// ---------------------------------------------------------------------------
// GitHubFetcher
// ---------------------------------------------------------------------------
GitHubFetcher::GitHubFetcher(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    m_manager = new QNetworkAccessManager(this);
}

GitHubFetcher::~GitHubFetcher()
{
    delete d;
    d = nullptr;
}

void GitHubFetcher::fetchLatestReleases()
{
    const QStringList repos = ConfigManager::instance()->githubRepositories();
    d->activeReplies.clear();
    d->totalFetches = repos.size();
    d->completed = 0;

    if (repos.isEmpty()) {
        emit allFetchesFinished();
        return;
    }

    for (const QString &repo : repos)
        processRepo(repo, false);
}

void GitHubFetcher::processRepo(const QString &repoToken, bool isSingle)
{
    if (repoToken.isEmpty())
        return;

    const QStringList parts = repoToken.split(QStringLiteral("/"));
    if (parts.size() != 2 || parts.at(0).isEmpty() || parts.at(1).isEmpty()) {
        if (!isSingle)
            --d->activeReplies[repoToken];
        emit errorOccurred(QStringLiteral("Invalid repository token: %1").arg(repoToken));
        return;
    }

    const QString url = QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
                            .arg(parts.at(0), parts.at(1));

    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    request.setRawHeader(QByteArrayLiteral("Accept"),
                                          (QByteArrayLiteral("application/vnd.github+json")));

    QNetworkReply *reply = m_manager->get(request);
    d->activeReplies[repoToken]++;

    connect(reply, &QNetworkReply::finished, this, [this, reply, repoToken, isSingle] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(QStringLiteral("GitHub request failed for %1: %2")
                                   .arg(repoToken, reply->errorString()));
            finishFetch(isSingle);
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit errorOccurred(QStringLiteral("Unable to parse GitHub JSON for %1").arg(repoToken));
            finishFetch(isSingle);
            return;
        }

        const QJsonObject root = doc.object();
        const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();

        for (const QJsonValue &v : assets) {
            const QJsonObject asset = v.toObject();
            Asset a;
            a.repo = repoToken;
            a.fileName = asset.value(QStringLiteral("name")).toString();
            a.url = QUrl(asset.value(QStringLiteral("url")).toString());
            a.direct = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
            a.sizeBytes = asset.value(QStringLiteral("size")).toVariant().toLongLong();

            if (!a.fileName.isEmpty() && isSupportedSuffix(a.fileName))
                emit assetDiscovered(a);
        }

        finishFetch(isSingle);
    });
}

void GitHubFetcher::finishFetch(bool isSingle)
{
    Q_UNUSED(isSingle)

    ++d->completed;
    const bool done = (d->completed >= d->totalFetches) || (isSingle && d->totalFetches == 1);
    if (done)
        emit allFetchesFinished();
}

void GitHubFetcher::downloadAsset(const Asset &asset)
{
    const QString destDir = choiceForFileName(asset.fileName);
    if (destDir.isEmpty())
        return;

    const QString destPath = destDir + QStringLiteral("/") + asset.fileName;
    downloadToFile(asset, destPath);
}

void GitHubFetcher::downloadToFile(const Asset &asset, const QString &destinationPath)
{
    if (asset.direct.isEmpty() && asset.url.isEmpty())
        return;

    QNetworkRequest request(asset.direct.isEmpty() ? asset.url : asset.direct);
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);

    QFile *file = new QFile(destinationPath);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete file;
        emit downloadFinished(asset.repo, asset.fileName, QString(), false);
        return;
    }

    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, asset](qint64 got, qint64 total) {
                emit downloadProgress(asset.repo, asset.fileName, got, total);
            });
    connect(reply, &QNetworkReply::readyRead, this, [reply, file] {
        file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, asset, destinationPath] {
        file->flush();
        file->close();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        if (ok)
            postDownload(asset, destinationPath);
        emit downloadFinished(asset.repo, asset.fileName, ok ? destinationPath : QString(), ok);
        delete file;
        reply->deleteLater();
    });
}

void GitHubFetcher::postDownload(const Asset &asset, const QString &localPath)
{
    if (!asset.fileName.endsWith(kAppImageSuffix, Qt::CaseInsensitive))
        return;

    const QFileInfo fi(localPath);
    if (!fi.exists())
        return;

    // Ensure the AppImage is executable.
    QFile::setPermissions(localPath,
                          QFile::permissions(localPath) | QFileDevice::ExeUser);

    // Create a .desktop launcher so it shows up in the system menus.
    const QString appId = fi.baseName();
    QFile desktop(desktopDirectory() + QStringLiteral("/%1.desktop").arg(appId));
    if (desktop.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream out(&desktop);
        out << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=" << appId << "\n"
            << "Exec=" << localPath << "\n"
            << "Categories=Utility;\n"
            << "Terminal=false\n";
        desktop.close();
    }
}

bool GitHubFetcher::busy() const
{
    return d->completed < d->totalFetches;
}
