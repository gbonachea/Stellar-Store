#pragma once

#include <QWidget>

#include <memory>

namespace LightStore {

/**
 * Main application window: sidebar + stacked per-screen widget, plus all the
 * backend / metadata wiring.
 */
class MainWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    Q_DISABLE_COPY_MOVE(MainWindow)

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LightStore
