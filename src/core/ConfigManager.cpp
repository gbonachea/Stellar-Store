#include "ConfigManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

class ConfigManager::Private
{
public:
    explicit Private()
        : settings(ConfigManager::configFilePath(), QSettings::IniFormat)
    {
    }

    QSettings settings;

    static bool flatpakBinaryPresent()
    {
        return QFileInfo::exists(QStringLiteral("/usr/bin/flatpak"));
    }

    static bool snapBinaryPresent()
    {
        return QFileInfo::exists(QStringLiteral("/usr/bin/snap"));
    }

    static bool cacheTtlDefault()
    {
        return 24;
    }
};

QString ConfigManager::configFilePath()
{
    static const QString path =
        QDir::homePath() + QStringLiteral("/.config/LightStore/config.ini");
    return path;
}

ConfigManager *ConfigManager::instance()
{
    static ConfigManager *self = new ConfigManager;
    return self;
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->settings.sync();
}

ConfigManager::~ConfigManager() = default;

int ConfigManager::cacheTtlHours() const
{
    return d->settings.value(QStringLiteral("cache_ttl_hours"),
                             Private::cacheTtlDefault()).toInt();
}

void ConfigManager::setCacheTtlHours(int hours)
{
    if (hours < 1)
        hours = 1;
    d->settings.setValue(QStringLiteral("cache_ttl_hours"), hours);
    d->settings.sync();
}

bool ConfigManager::flatpakEnabled() const
{
    return d->settings.value(QStringLiteral("enable_flatpak"),
                             Private::flatpakBinaryPresent()).toBool();
}

void ConfigManager::setFlatpakEnabled(bool enabled)
{
    d->settings.setValue(QStringLiteral("enable_flatpak"), enabled);
    d->settings.sync();
}

bool ConfigManager::snapEnabled() const
{
    return d->settings.value(QStringLiteral("enable_snap"),
                             Private::snapBinaryPresent()).toBool();
}

void ConfigManager::setSnapEnabled(bool enabled)
{
    d->settings.setValue(QStringLiteral("enable_snap"), enabled);
    d->settings.sync();
}

QString ConfigManager::normalizeGithubRepository(const QString &input)
{
    QString ref = input.trimmed();
    if (ref.isEmpty())
        return QString();

    // Accept full URLs: https://github.com/owner/name(.git)(/)
    if (ref.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || ref.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        const QUrl url(ref);
        const QString host = url.host().toLower();
        if (host != QLatin1String("github.com") && host != QLatin1String("www.github.com"))
            return QString();
        ref = url.path();
    }

    // Strip query/fragment leftovers and surrounding slashes.
    const int queryPos = ref.indexOf(QLatin1Char('?'));
    if (queryPos >= 0)
        ref.truncate(queryPos);
    const int fragmentPos = ref.indexOf(QLatin1Char('#'));
    if (fragmentPos >= 0)
        ref.truncate(fragmentPos);

    while (ref.startsWith(QLatin1Char('/')))
        ref.remove(0, 1);
    while (ref.endsWith(QLatin1Char('/')))
        ref.chop(1);
    if (ref.endsWith(QLatin1String(".git"), Qt::CaseInsensitive))
        ref.chop(4);

    const QStringList parts = ref.split(QLatin1Char('/'));
    if (parts.size() != 2)
        return QString();
    if (parts.at(0).isEmpty() || parts.at(1).isEmpty())
        return QString();

    // GitHub owner/repo names are limited to these characters.
    static const QRegularExpression valid(
        QStringLiteral("^[A-Za-z0-9_.-]+$"));
    if (!valid.match(parts.at(0)).hasMatch() || !valid.match(parts.at(1)).hasMatch())
        return QString();

    return parts.at(0) + QLatin1Char('/') + parts.at(1);
}

QStringList ConfigManager::githubRepositories() const
{
    const QStringList stored =
        d->settings.value(QStringLiteral("github_repositories")).toStringList();

    // Settings written by older releases may contain unnormalized values.
    QStringList cleaned;
    for (const QString &r : stored) {
        const QString normalized = normalizeGithubRepository(r);
        if (!normalized.isEmpty() && !cleaned.contains(normalized))
            cleaned << normalized;
    }
    return cleaned;
}

void ConfigManager::setGithubRepositories(const QStringList &repos)
{
    QStringList cleaned;
    for (const QString &r : repos) {
        const QString normalized = normalizeGithubRepository(r);
        if (!normalized.isEmpty() && !cleaned.contains(normalized))
            cleaned << normalized;
    }
    if (cleaned == githubRepositories())
        return;
    if (cleaned.isEmpty())
        d->settings.remove(QStringLiteral("github_repositories"));
    else
        d->settings.setValue(QStringLiteral("github_repositories"), cleaned);
    d->settings.sync();
    Q_EMIT githubRepositoriesChanged();
}

bool ConfigManager::addGithubRepository(const QString &repo)
{
    const QString normalized = normalizeGithubRepository(repo);
    if (normalized.isEmpty())
        return false;

    auto repos = githubRepositories();
    if (repos.contains(normalized))
        return true;
    repos << normalized;
    setGithubRepositories(repos);
    return true;
}

void ConfigManager::removeGithubRepository(const QString &repo)
{
    const QString normalized = normalizeGithubRepository(repo);
    if (normalized.isEmpty())
        return;

    auto repos = githubRepositories();
    if (repos.removeAll(normalized) > 0)
        setGithubRepositories(repos);
}

bool ConfigManager::flatpakAvailableOnSystem() const
{
    return Private::flatpakBinaryPresent();
}

bool ConfigManager::snapAvailableOnSystem() const
{
    return Private::snapBinaryPresent();
}

void ConfigManager::setFlatpakAvailableOnSystem(bool available)
{
    d->settings.setValue(QStringLiteral("system/flatpak_available"), available);
    d->settings.sync();
}

void ConfigManager::setSnapAvailableOnSystem(bool available)
{
    d->settings.setValue(QStringLiteral("system/snap_available"), available);
    d->settings.sync();
}

QByteArray ConfigManager::windowGeometry() const
{
    return d->settings.value(QStringLiteral("window/geometry")).toByteArray();
}

void ConfigManager::setWindowGeometry(const QByteArray &geometry)
{
    if (geometry.isEmpty())
        return;
    d->settings.setValue(QStringLiteral("window/geometry"), geometry);
    d->settings.sync();
}

void ConfigManager::flush()
{
    d->settings.sync();
}
