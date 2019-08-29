#include "mount_widget.h"

MountWidget::MountWidget(QProcess* process, const QString& remote, const QString& folder, QWidget* parent)
    : QWidget(parent)
    , mProcess(process)
{
    ui.setupUi(this);

    ui.remote->setText(remote);
    ui.folder->setText(folder);
    ui.info->setText(QString("%1 on %2").arg(remote).arg(folder));

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
                        "Unmount",
                        QString("Do you want to umount %1 folder?").arg(folder),
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

    QObject::connect(mProcess, &QProcess::readyRead, this, [=]()
    {
        while (mProcess->canReadLine())
        {
            ui.output->appendPlainText(mProcess->readLine().trimmed());
        }
    });

    QObject::connect(mProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [=](int status, QProcess::ExitStatus)
    {
        mProcess->deleteLater();
        mRunning = false;
        if (status == 0)
        {
            ui.showDetails->setStyleSheet("QToolButton { border: 0; color: black; }");
            ui.showDetails->setText("Finished");
        }
        else
        {
            ui.showDetails->setStyleSheet("QToolButton { border: 0; color: red; }");
            ui.showDetails->setText("Error");
        }
        ui.cancel->setToolTip("Close");
        emit finished();
    });

    ui.showDetails->setStyleSheet("QToolButton { border: 0; color: green; }");
    ui.showDetails->setText("Mounted");
}

MountWidget::~MountWidget()
{
}

void MountWidget::cancel()
{
    if (!mRunning)
    {
        return;
    }

    QString cmd;
#ifdef Q_OS_OSX
    QProcess::startDetached("umount", QStringList() << ui.folder->text());
#else
    QProcess::startDetached("fusermount", QStringList() << "-u" << ui.folder->text());
#endif
    mProcess->waitForFinished();

    emit closed();
}
