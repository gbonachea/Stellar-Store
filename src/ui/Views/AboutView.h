#pragma once

#include <QWidget>

#include <memory>

/**
 * "Acerca de" screen: project name, version, description and package
 * sources. Reached from the sidebar like every other screen.
 */
class AboutView final : public QWidget
{
    Q_OBJECT

public:
    explicit AboutView(QWidget *parent = nullptr);
    ~AboutView() override;
    Q_DISABLE_COPY_MOVE(AboutView)

private:
    class Private;
    std::unique_ptr<Private> d;
};