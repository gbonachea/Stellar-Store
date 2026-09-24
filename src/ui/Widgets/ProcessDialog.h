#pragma once

#include <QDialog>
#include <QString>

#include <memory>

/**
 * Streaming progress/log dialog used by the install / remove / refresh
 * operations. Owned by the view that starts the process; the view wires the
 * backend's processOutput / progressChanged / operationFinished signals here.
 */
class ProcessDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ProcessDialog(QWidget *parent = nullptr);
    ~ProcessDialog() override;
    Q_DISABLE_COPY_MOVE(ProcessDialog)

public slots:
    void appendOutput(const QString &line);
    void setProgress(int percent);
    void setStatusMessage(const QString &message);

signals:
    /** Ask the owner to kill the currently running backend command. */
    void cancelRequested();

private:
    class Private;
    std::unique_ptr<Private> d;
};
