#pragma once

#include <QString>
#include <QWidget>

#include <memory>

class QDragEnterEvent;
class QDropEvent;

/**
 * "Instalador local" screen: drag & drop a .deb / .rpm file onto the window
 * and press Install; the matching backend installs it.
 */
class LocalInstallerView final : public QWidget
{
    Q_OBJECT

public:
    explicit LocalInstallerView(QWidget *parent = nullptr);
    ~LocalInstallerView() override;
    Q_DISABLE_COPY_MOVE(LocalInstallerView)

public slots:
    void setLocalFile(const QString &filePath);
    void installDroppedFile(const QString &filePath);

signals:
    void installRequested(const QString &filePath);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void onInstallClicked();
    void updateStatus();

    class Private;
    std::unique_ptr<Private> d;
};