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
#include <QSysInfo>
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
    const QString dir = QStringLiteral("/tmp/lightstore");
    QDir().mkpath(dir);
    return dir;
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

bool isForeignArchitecture(const QString &fileName)
{
    const QString cpu = QSysInfo::currentCpuArchitecture().toLower();
    const QString lower = fileName.toLower();

    const bool isX86 = cpu.contains(QLatin1String("x86"))
                       || cpu.contains(QLatin1String("amd64"));
    if (isX86) {
        return lower.contains(QLatin1String("aarch64"))
            || lower.contains(QLatin1String("arm64"))
            || lower.contains(QLatin1String("armhf"))
            || lower.contains(QLatin1String("armv7"))
            || lower.contains(QLatin1String("arm"))
            || lower.contains(QLatin1String("riscv"));
    }
    if (cpu.contains(QLatin1String("arm"))
        || cpu.contains(QLatin1String("aarch64"))) {
        return lower.contains(QLatin1String("amd64"))
            || lower.contains(QLatin1String("x86_64"))
            || lower.contains(QLatin1String("i386"))
            || lower.contains(QLatin1String("i686"));
    }
    return false;
}

int assetScore(const QString &fileName)
{
    const QString lower = fileName.toLower();
    int score = 0;

    const bool isDeb = lower.endsWith(kDebSuffix);
    const bool isRpm = lower.endsWith(kRpmSuffix);
    const bool isAppImage = lower.endsWith(kAppImageSuffix.toLower());

    // Prefer the format the local native package manager understands.
    if (QFileInfo::exists(QStringLiteral("/usr/bin/apt-get"))) {
        if (isDeb)
            score += 100;
        else if (isAppImage)
            score += 20;
        else if (isRpm)
            score += 10;
    } else if (QFileInfo::exists(QStringLiteral("/usr/bin/dnf"))
               || QFileInfo::exists(QStringLiteral("/usr/bin/pacman"))) {
        if (isRpm)
            score += 100;
        else if (isAppImage)
            score += 20;
        else if (isDeb)
            score += 10;
    } else {
        if (isAppImage)
            score += 100;
        else if (isDeb || isRpm)
            score += 10;
    }

    if (lower.contains(QLatin1String("amd64"))
        || lower.contains(QLatin1String("x86_64")))
        score += 30;
    if (isForeignArchitecture(lower))
        score -= 200;

    // Never select signatures, debug symbols or checksum side-cars.
    static const QStringList noise = {
        QStringLiteral("sha256"), QStringLiteral("sha512"),
        QStringLiteral("checksum"), QStringLiteral("debug"),
        QStringLiteral("symbols"), QStringLiteral("source"),
    };
    for (const QString &word : noise) {
        if (lower.contains(word))
            score -= 100;
    }

    return score;
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
    bool refetchPending = false;
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
    // Ignore overlapping refreshes: the in-flight request already covers
    // every configured repository, but remember that another pass was asked
    // for so newly added repositories are not lost.
    if (busy()) {
        d->refetchPending = true;
        return;
    }

    const QStringList repos = ConfigManager::instance()->githubRepositories();
    d->activeReplies.clear();
    d->totalFetches = repos.size();
    d->completed = 0;

    emit fetchStarted();

    if (repos.isEmpty()) {
        emit allFetchesFinished();
        return;
    }

    for (const QString &repo : repos)
        processRepo(repo, false);
}

GitHubFetcher::Asset GitHubFetcher::preferredAsset(const QList<Asset> &assets)
{
    if (assets.isEmpty())
        return Asset();

    Asset best = assets.constFirst();
    int bestScore = assetScore(best.fileName);
    for (int i = 1; i < assets.size(); ++i) {
        const Asset &candidate = assets.at(i);
        const int score = assetScore(candidate.fileName);
        if (score > bestScore) {
            best = candidate;
            bestScore = score;
        }
    }
    return best;
}

void GitHubFetcher::processRepo(const QString &repoToken, bool isSingle)
{
    if (repoToken.isEmpty())
        return;

    const QStringList parts = repoToken.split(QStringLiteral("/"));
    if (parts.size() != 2 || parts.at(0).isEmpty() || parts.at(1).isEmpty()) {
        emit errorOccurred(QStringLiteral("Invalid repository token: %1").arg(repoToken));
        finishFetch(isSingle);
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
        const QString releaseTag = root.value(QStringLiteral("tag_name")).toString();
        const QString releaseName = root.value(QStringLiteral("name")).toString();
        const QString releaseNotes = root.value(QStringLiteral("body")).toString();
        const QString ownerAvatar =
            root.value(QStringLiteral("owner")).toObject()
                .value(QStringLiteral("avatar_url")).toString();

        for (const QJsonValue &v : assets) {
            const QJsonObject asset = v.toObject();
            Asset a;
            a.repo = repoToken;
            a.fileName = asset.value(QStringLiteral("name")).toString();
            a.url = QUrl(asset.value(QStringLiteral("url")).toString());
            a.direct = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
            a.sizeBytes = asset.value(QStringLiteral("size")).toVariant().toLongLong();
            a.releaseTag = releaseTag;
            a.releaseName = releaseName;
            a.releaseNotes = releaseNotes;
            a.ownerAvatar = ownerAvatar;

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
    if (!done)
        return;

    emit allFetchesFinished();

    if (d->refetchPending) {
        d->refetchPending = false;
        fetchLatestReleases();
    }
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
    const QString appId = fi.completeBaseName();
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
