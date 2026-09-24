#pragma once

#include <QCheckBox>
#include <QSpinBox>
#include <QWidget>

#include <memory>

class QLabel;
class QPushButton;
class QShowEvent;

/**
 * "Ajustes" screen: cache TTL, toggles for the Flatpak / Snap remotes and
 * the GitHub repository list. Values round-trip through ConfigManager. Also
 * shows how much disk the on-demand icon cache occupies, with a button to
 * clear it.
 */
class SettingsView final : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsView(QWidget *parent = nullptr);
    ~SettingsView() override;
    Q_DISABLE_COPY_MOVE(SettingsView)

public slots:
    void loadFromConfig();
    void saveToConfig();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void refreshIconCacheInfo();

    class Private;
    std::unique_ptr<Private> d;
};
