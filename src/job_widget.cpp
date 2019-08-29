#include "job_widget.h"
#include "utils.h"

JobWidget::JobWidget(QProcess* process, const QString& info, const QStringList& args, const QString& source, const QString& dest, QWidget* parent)
    : QWidget(parent)
    , mProcess(process)
{
    ui.setupUi(this);

    mArgs.append(QDir::toNativeSeparators(GetRclone()));
    mArgs.append(GetRcloneConf());
    mArgs.append(args);

    ui.source->setText(source);
    ui.dest->setText(dest);
    ui.info->setText(info);

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
                        "Transfer",
                        QString("rclone process is still running. Do you want to cancel it?"),
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

    ui.copy->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileLinkIcon));

    QObject::connect(ui.copy, &QToolButton::clicked, this, [=]()
    {
        QClipboard* clipboard = QGuiApplication::clipboard();
	        clipboard->setText(mArgs.join(" "));
    });

    QObject::connect(mProcess, &QProcess::readyRead, this, [=]()
    {
        QRegExp rxSize(R"(^Transferred:\s+(\S+ \S+) \(([^)]+)\)$)");  // Until rclone 1.42
        QRegExp rxSize2(R"(^Transferred:\s+([0-9.]+)(\S)? \/ (\S+) (\S+), ([0-9%-]+), (\S+ \S+), (\S+) (\S+)$)"); // Starting with rclone 1.43
        QRegExp rxErrors(R"(^Errors:\s+(\S+)$)");
        QRegExp rxChecks(R"(^Checks:\s+(\S+)$)"); // Until rclone 1.42
        QRegExp rxChecks2(R"(^Checks:\s+(\S+) \/ (\S+), ([0-9%-]+)$)"); // Starting with rclone 1.43
        QRegExp rxTransferred(R"(^Transferred:\s+(\S+)$)"); // Until rclone 1.42
        QRegExp rxTransferred2(R"(^Transferred:\s+(\S+) \/ (\S+), ([0-9%-]+)$)"); // Starting with rclone 1.43
        QRegExp rxTime(R"(^Elapsed time:\s+(\S+)$)");
        QRegExp rxProgress(R"(^\*([^:]+):\s*([^%]+)% done.+(ETA: [^)]+)$)"); // Until rclone 1.38
        QRegExp rxProgress2(R"(\*([^:]+):\s*([^%]+)% \/[a-zA-z0-9.]+, [a-zA-z0-9.]+\/s, (\w+)$)"); // Starting with rclone 1.39

        while (mProcess->canReadLine())
        {
            QString line = mProcess->readLine().trimmed();
            if (++mLines == 10000)
            {
                ui.output->clear();
                mLines = 1;
            }
            ui.output->appendPlainText(line);

            if (line.isEmpty())
            {
                for (auto it = mActive.begin(), eit = mActive.end(); it != eit; /* empty */)
                {
                    auto label = it.value();
                    if (mUpdated.contains(label))
                    {
                        ++it;
                    }
                    else
                    {
                        it = mActive.erase(it);
                        ui.progress->removeWidget(label->buddy());
                        ui.progress->removeWidget(label);
                        delete label->buddy();
                        delete label;
                    }
                }
                mUpdated.clear();
                continue;
            }

            if (rxSize.exactMatch(line))
            {
                ui.size->setText(rxSize.cap(1));
                ui.bandwidth->setText(rxSize.cap(2));
            }
            else if (rxSize2.exactMatch(line))
            {
                ui.size->setText(rxSize2.cap(1) + " " + rxSize2.cap(2) + "B" + ", " +  rxSize2.cap(5));
                ui.bandwidth->setText(rxSize2.cap(6));
	        ui.eta->setText(rxSize2.cap(8));
                ui.totalsize->setText(rxSize2.cap(3) + " " + rxSize2.cap(4));
            }
            else if (rxErrors.exactMatch(line))
            {
                ui.errors->setText(rxErrors.cap(1));
            }
            else if (rxChecks.exactMatch(line))
            {
                ui.checks->setText(rxChecks.cap(1));
            }
            else if (rxChecks2.exactMatch(line))
            {
                ui.checks->setText(rxChecks2.cap(1) + " / " + rxChecks2.cap(2) + ", " + rxChecks2.cap(3));
            }
            else if (rxTransferred.exactMatch(line))
            {
                ui.transferred->setText(rxTransferred.cap(1));
            }
            else if (rxTransferred2.exactMatch(line))
            {
                ui.transferred->setText(rxTransferred2.cap(1) + " / " + rxTransferred2.cap(2) + ", " + rxTransferred2.cap(3));
            }
            else if (rxTime.exactMatch(line))
            {
                ui.elapsed->setText(rxTime.cap(1));
            }
            else if (rxProgress.exactMatch(line))
            {
                QString name = rxProgress.cap(1).trimmed();

                auto it = mActive.find(name);

                QLabel* label;
                QProgressBar* bar;
                if (it == mActive.end())
                {
                    label = new QLabel();
                    label->setText(name);

                    bar = new QProgressBar();
                    bar->setMinimum(0);
                    bar->setMaximum(100);
                    bar->setTextVisible(true);

                    label->setBuddy(bar);

                    ui.progress->addRow(label, bar);

                    mActive.insert(name, label);
                }
                else
                {
                    label = it.value();
                    bar = static_cast<QProgressBar*>(label->buddy());
                }

                bar->setValue(rxProgress.cap(2).toInt());
                bar->setToolTip(rxProgress.cap(3));

                mUpdated.insert(label);
            }
            else if (rxProgress2.exactMatch(line))
            {
                QString name = rxProgress2.cap(1).trimmed();

                auto it = mActive.find(name);

                QLabel* label;
                QProgressBar* bar;
                if (it == mActive.end())
                {
                    label = new QLabel();
                    label->setText(name);

                    bar = new QProgressBar();
                    bar->setMinimum(0);
                    bar->setMaximum(100);
                    bar->setTextVisible(true);

                    label->setBuddy(bar);

                    ui.progress->addRow(label, bar);

                    mActive.insert(name, label);
                }
                else
                {
                    label = it.value();
                    bar = static_cast<QProgressBar*>(label->buddy());
                }

                bar->setValue(rxProgress2.cap(2).toInt());
                bar->setToolTip("ETA: " + rxProgress2.cap(3));

                mUpdated.insert(label);
            }
        }
    });

    QObject::connect(mProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [=](int status, QProcess::ExitStatus)
    {
        mProcess->deleteLater();
        for (auto label : mActive)
        {
            ui.progress->removeWidget(label->buddy());
            ui.progress->removeWidget(label);
            delete label->buddy();
            delete label;
        }

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

        emit finished(ui.info->text());
    });

    ui.showDetails->setStyleSheet("QToolButton { border: 0; color: green; }");
    ui.showDetails->setText("Running");
}

JobWidget::~JobWidget()
{
}

void JobWidget::showDetails()
{
    ui.showDetails->setChecked(true);
}

void JobWidget::cancel()
{
    if (!mRunning)
    {
        return;
    }

    mProcess->kill();
    mProcess->waitForFinished();

    emit closed();
}
