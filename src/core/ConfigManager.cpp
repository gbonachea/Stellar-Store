#include "ConfigManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

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

QStringList ConfigManager::githubRepositories() const
{
    return d->settings.value(QStringLiteral("github_repositories")).toStringList();
}

void ConfigManager::setGithubRepositories(const QStringList &repos)
{
    QStringList cleaned;
    for (const QString &r : repos) {
        const auto parts = r.split(QLatin1Char('/'));
        if (parts.size() == 2 && !parts.at(0).isEmpty() && !parts.at(1).isEmpty())
            cleaned << r;
    }
    cleaned.removeDuplicates();
    d->settings.setValue(QStringLiteral("github_repositories"), cleaned);
    d->settings.sync();
}

void ConfigManager::addGithubRepository(const QString &repo)
{
    auto repos = githubRepositories();
    if (!repos.contains(repo))
        repos << repo;
    setGithubRepositories(repos);
}

void ConfigManager::removeGithubRepository(const QString &repo)
{
    auto repos = githubRepositories();
    if (repos.removeAll(repo) > 0)
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
