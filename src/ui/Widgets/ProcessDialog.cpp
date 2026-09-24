#include "ui/Widgets/ProcessDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

/**
 * A non-modal dialog that shows one backend operation as it runs: a coarse
 * progress bar plus a streaming log pane fed through the AbstractBackend
 * signals the owning view wires directly here.
 */
class ProcessDialog::Private
{
public:
    QProgressBar *bar = nullptr;
    QPlainTextEdit *log = nullptr;
    QLabel *status = nullptr;
    QPushButton *cancelButton = nullptr;
    QDialogButtonBox *buttons = nullptr;
};

ProcessDialog::ProcessDialog(QWidget *parent)
    : QDialog(parent)
    , d(std::make_unique<Private>())
{
    setWindowTitle(QStringLiteral("Light Store — progreso"));
    setModal(false);
    resize(560, 420);

    d->status = new QLabel(QStringLiteral("En cola…"), this);
    d->bar = new QProgressBar(this);
    d->bar->setRange(0, 100);
    d->bar->setValue(0);
    d->log = new QPlainTextEdit(this);
    d->log->setReadOnly(true);

    d->buttons = new QDialogButtonBox(
        QDialogButtonBox::Close, this);
    d->cancelButton = new QPushButton(tr("Cancelar"), this);
    d->buttons->addButton(d->cancelButton, QDialogButtonBox::RejectRole);
    connect(d->buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(d->cancelButton, &QPushButton::clicked,
            this, &ProcessDialog::cancelRequested);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(d->status);
    layout->addWidget(d->bar);
    layout->addWidget(d->log, 1);
    layout->addWidget(d->buttons);
}

ProcessDialog::~ProcessDialog() = default;

void ProcessDialog::appendOutput(const QString &line)
{
    if (d->log)
        d->log->appendPlainText(line);
}

void ProcessDialog::setProgress(int percent)
{
    if (d->bar)
        d->bar->setValue(qBound(0, percent, 100));
}

void ProcessDialog::setStatusMessage(const QString &message)
{
    if (d->status)
        d->status->setText(message);
}
