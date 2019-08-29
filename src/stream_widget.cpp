#include "stream_widget.h"

StreamWidget::StreamWidget(QProcess* rclone, QProcess* player, const QString& remote, const QString& stream, QWidget* parent)
    : QWidget(parent)
    , mRclone(rclone)
    , mPlayer(player)
{
    ui.setupUi(this);

    ui.remote->setText(remote);
    ui.stream->setText(stream);
    ui.info->setText(remote);

    ui.details->setVisible(false);

    ui.output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    ui.output->setVisible(false);

    QObject::connect(ui.showDetails, &QToolButton::toggled, this, [=](bool checked)
    {
        ui.details->setVisible(checked);
        ui.showDetails->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    });

    QObject::connect(ui.showOutput, &QToolButton::toggled, this, [=](bool checked)
    {
        ui.output->setVisible(checked);
        ui.showOutput->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    });

    ui.cancel->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCloseButton));

    QObject::connect(ui.cancel, &QToolButton::clicked, this, [=]()
    {
        if (mRunning)
        {
            int button = QMessageBox::question(
                        this,
                        "Stop",
                        QString("Do you want to stop %1 stream?").arg(remote),
                        QMessageBox::Yes | QMessageBox::No);
            if (button == QMessageBox::Yes)
            {
                cancel();
            }
        }
        else
        {
            emit closed();
        }
    });

    QObject::connect(mRclone, &QProcess::readyRead, this, [=]()
    {
        while (mRclone->canReadLine())
        {
            ui.output->appendPlainText(mRclone->readLine().trimmed());
        }
    });

    QObject::connect(mRclone, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [=]()
    {
        mRclone->deleteLater();
        mRunning = false;
        emit finished();
        emit closed();
    });

    ui.showDetails->setStyleSheet("QToolButton { border: 0; color: green; }");
    ui.showDetails->setText("Streaming");
}

StreamWidget::~StreamWidget()
{
}

void StreamWidget::cancel()
{
    if (!mRunning)
    {
        return;
    }

    mPlayer->terminate();
    mRclone->kill();
    mRclone->waitForFinished();
}
