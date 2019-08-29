#include "main_window.h"
#include "remote_widget.h"
#include "utils.h"
#include "job_widget.h"
#include "mount_widget.h"
#include "stream_widget.h"
#include "preferences_dialog.h"
#ifdef Q_OS_OSX
#include "osx_helper.h"
#endif

MainWindow::MainWindow()
{
    ui.setupUi(this);

    mSystemTray.setIcon(qApp->windowIcon());
    {
        auto settings = GetSettings();
        if (settings->contains("MainWindow/geometry"))
        {
            restoreGeometry(settings->value("MainWindow/geometry").toByteArray());
        }
        SetRclone(settings->value("Settings/rclone").toString());
        SetRcloneConf(settings->value("Settings/rcloneConf").toString());

        mAlwaysShowInTray = settings->value("Settings/alwaysShowInTray", false).toBool();
        mCloseToTray = settings->value("Settings/closeToTray", false).toBool();
        mNotifyFinishedTransfers = settings->value("Settings/notifyFinishedTransfers", true).toBool();

        mSystemTray.setVisible(mAlwaysShowInTray);
    }

    QObject::connect(ui.preferences, &QAction::triggered, this, [=]()
    {
        PreferencesDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
        {
            auto settings = GetSettings();
            settings->setValue("Settings/rclone", dialog.getRclone().trimmed());
            settings->setValue("Settings/rcloneConf", dialog.getRcloneConf().trimmed());
            settings->setValue("Settings/stream", dialog.getStream());
#ifndef Q_OS_WIN32
            settings->setValue("Settings/mount", dialog.getMount());
#endif
            settings->setValue("Settings/alwaysShowInTray", dialog.getAlwaysShowInTray());
            settings->setValue("Settings/closeToTray", dialog.getCloseToTray());
            settings->setValue("Settings/notifyFinishedTransfers", dialog.getNotifyFinishedTransfers());
            settings->setValue("Settings/showFolderIcons", dialog.getShowFolderIcons());
            settings->setValue("Settings/showFileIcons", dialog.getShowFileIcons());
            settings->setValue("Settings/rowColors", dialog.getRowColors());
            settings->setValue("Settings/showHidden", dialog.getShowHidden());
            SetRclone(dialog.getRclone());
            SetRcloneConf(dialog.getRcloneConf());
            mFirstTime = true;
            rcloneGetVersion();

            mAlwaysShowInTray = dialog.getAlwaysShowInTray();
            mCloseToTray = dialog.getCloseToTray();
            mNotifyFinishedTransfers = dialog.getNotifyFinishedTransfers();

            mSystemTray.setVisible(mAlwaysShowInTray);
        }
    });

    QObject::connect(ui.quit, &QAction::triggered, this, [=]()
    {
        mCloseToTray = false;
        close();
    });

    QObject::connect(ui.about, &QAction::triggered, this, [=]()
    {
        QMessageBox::about(
            this,
            "Rclone Browser",
            QString(
                R"(<h3>GUI for <a href="http://rclone.org/">rclone</a>, )" RCLONE_BROWSER_VERSION "</h3>"
                R"(<p>Copyright &copy; 2019</p>)"
                R"(<p>Original version by Martins Mozeiko</p>)"
                R"(<p>Web: <a href="https://mmozeiko.github.io/RcloneBrowser">https://mmozeiko.github.io/RcloneBrowser</a></p>)"
                R"(<p>Further improvements by DinCahill</p>)"
                R"(<p>Web: <a href="https://github.com/DinCahill/RcloneBrowser">https://github.com/DinCahill/RcloneBrowser</a></p>)"
                R"(<p>Latest fixes and packaging by kapitainsky</p>)"
                R"(<p>Web: <a href="https://github.com/kapitainsky/RcloneBrowser">https://github.com/kapitainsky/RcloneBrowser</a></p>)"
            )
        );
    });
    QObject::connect(ui.aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);

    QObject::connect(ui.remotes, &QListWidget::currentItemChanged, this, [=](QListWidgetItem* current)
    {
        ui.open->setEnabled(current != NULL);
    });
    QObject::connect(ui.remotes, &QListWidget::itemActivated, ui.open, &QPushButton::clicked);

    QObject::connect(ui.config, &QPushButton::clicked, this, &MainWindow::rcloneConfig);
    QObject::connect(ui.refresh, &QPushButton::clicked, this, &MainWindow::rcloneListRemotes);

    QObject::connect(ui.open, &QPushButton::clicked, this, [=]()
    {
        auto item = ui.remotes->selectedItems().front();
        QString type = item->data(Qt::UserRole).toString();
        QString name = item->text();
        bool isLocal = type == "local";

        auto remote =  new RemoteWidget(&mIcons, name, isLocal, ui.tabs);
        QObject::connect(remote, &RemoteWidget::addMount, this, &MainWindow::addMount);
        QObject::connect(remote, &RemoteWidget::addStream, this, &MainWindow::addStream);
        QObject::connect(remote, &RemoteWidget::addTransfer, this, &MainWindow::addTransfer);

        int index = ui.tabs->addTab(remote, name);
        ui.tabs->setCurrentIndex(index);
    });

    QObject::connect(ui.tabs, &QTabWidget::tabCloseRequested, ui.tabs, &QTabWidget::removeTab);

    ui.tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    ui.tabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    ui.tabs->tabBar()->setTabButton(1, QTabBar::RightSide, nullptr);
    ui.tabs->tabBar()->setTabButton(1, QTabBar::LeftSide, nullptr);
    ui.tabs->setCurrentIndex(0);

    QObject::connect(&mSystemTray, &QSystemTrayIcon::activated, this, [=](QSystemTrayIcon::ActivationReason reason)
    {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
        {
            showNormal();
            mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_OSX
            osxShowDockIcon();
#endif
        }
    });

    QObject::connect(&mSystemTray, &QSystemTrayIcon::messageClicked, this, [=]()
    {
        showNormal();
        mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_OSX
        osxShowDockIcon();
#endif

        ui.tabs->setCurrentIndex(1);
        if (mLastFinished)
        {
            mLastFinished->showDetails();
            ui.jobsArea->ensureWidgetVisible(mLastFinished);
        }
    });

    QMenu* trayMenu = new QMenu(this);
    QObject::connect(trayMenu->addAction("&Show"), &QAction::triggered, this, [=]()
    {
        showNormal();
        mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_OSX
        osxShowDockIcon();
#endif
    });
    QObject::connect(trayMenu->addAction("&Quit"), &QAction::triggered, this, &QWidget::close);
    mSystemTray.setContextMenu(trayMenu);

    mStatusMessage = new QLabel();
    ui.statusBar->addWidget(mStatusMessage);
    ui.statusBar->setStyleSheet("QStatusBar::item { border: 0; }");

    QTimer::singleShot(0, ui.remotes, SLOT(setFocus()));

    QString rclone = GetRclone();
    if (rclone.isEmpty())
    {
        rclone = QStandardPaths::findExecutable("rclone");
        if (rclone.isEmpty())
        {
            QMessageBox::information(this, "Error", "Cannot check rclone version!\nPlease verify rclone location.");
            emit ui.preferences->trigger();
        }
        else
        {
            auto settings = GetSettings();
            settings->setValue("Settings/rclone", rclone);
            SetRclone(rclone);
        }
    }
    else
    {
        rcloneGetVersion();
    }
}

MainWindow::~MainWindow()
{
    auto settings = GetSettings();
    settings->setValue("MainWindow/geometry", saveGeometry());
}

void MainWindow::rcloneGetVersion()
{
    bool firstTime = mFirstTime;
    mFirstTime = false;

    QProcess* p = new QProcess();

    QObject::connect(p, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [=](int code, QProcess::ExitStatus)
    {
        if (code == 0)
        {
            QString version = p->readAllStandardOutput().trimmed();
            mStatusMessage->setText(version + " in " + QDir::toNativeSeparators(GetRclone()));
            rcloneListRemotes();
        }
        else
        {
            if (p->error() != QProcess::FailedToStart)
            {
                if (getConfigPassword(p))
                {
                    rcloneGetVersion();
                }
                else
                {
                    close();
                }
                p->deleteLater();
                return;
            }

            if (firstTime)
            {
                if (p->error() == QProcess::FailedToStart)
                {
                    QMessageBox::information(this, "Error", "Wrong rclone executable or rclone not found!\nPlease select its location in next dialog.");
                }
                else
                {
                    QMessageBox::information(this, "Error", "Cannot check rclone version!\nPlease verify rclone location.");
                }
                emit ui.preferences->trigger();
            }
        }
        p->deleteLater();
    });

    UseRclonePassword(p);
    p->start(GetRclone(), QStringList() << "version" << "--ask-password=false", QIODevice::ReadOnly);
}

void MainWindow::rcloneConfig()
{
#if defined(Q_OS_WIN32) && (QT_VERSION < QT_VERSION_CHECK(5, 7, 0))
    QProcess::startDetached(GetRclone(), QStringList() << "config" << GetRcloneConf());
    return;
#else

    QProcess* p = new QProcess(this);

    QObject::connect(p, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [=](int code, QProcess::ExitStatus)
    {
        if (code == 0)
        {
            emit rcloneListRemotes();
        }
        p->deleteLater();
    });
#endif

#if defined(Q_OS_WIN32)

#if QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
    p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args)
    {
        args->flags |= CREATE_NEW_CONSOLE;
        args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
    });
    p->setProgram(GetRclone());
    p->setArguments(QStringList() << "config" << GetRcloneConf());
#endif

#elif defined(Q_OS_OSX)
    auto tmp = new QFile("/tmp/rclone_config.command");
    tmp->open(QIODevice::WriteOnly);
    QTextStream(tmp) << "#!/bin/sh\n" << GetRclone() << " config " << GetRcloneConf().join(" ") << "\n";
    tmp->close();
    tmp->setPermissions(
        QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser |
        QFileDevice::ReadGroup | QFileDevice::ExeGroup |
        QFileDevice::ReadOther | QFileDevice::ExeOther);
    p->setProgram("open");
    p->setArguments(QStringList() << tmp->fileName());
#else
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString terminal = env.value("TERMINAL");
    if (terminal.isEmpty())
    {
        terminal = QStandardPaths::findExecutable("x-terminal-emulator");
        if (terminal.isEmpty())
        {
            QMessageBox::critical(this, "Error",
                "Not sure how to launch terminal!\n"
                "Please set path to terminal executable in $TERMINAL environment variable.", QMessageBox::Ok);
            return;
        }
        p->setArguments(QStringList() << "-e" << GetRclone() << "config" << GetRcloneConf());
    }
    else
    {
        p->setArguments(QStringList() << "-e" << (GetRclone() + " config " + GetRcloneConf().join(" ")));
    }

    p->setProgram(terminal);
#endif

#if !defined(Q_OS_WIN32) || (QT_VERSION >= QT_VERSION_CHECK(5, 7, 0))
    UseRclonePassword(p);
    p->start(QIODevice::NotOpen);
#endif
}

void MainWindow::rcloneListRemotes()
{
    ui.remotes->clear();

    QProcess* p = new QProcess();

    QObject::connect(p, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [=](int code, QProcess::ExitStatus)
    {
        if (code == 0)
        {
            QStyle* style = qApp->style();
            int size = 2 * style->pixelMetric(QStyle::PM_ListViewIconSize);
            ui.remotes->setIconSize(QSize(size, size));

            QString bytes = p->readAllStandardOutput().trimmed();
            QStringList items = bytes.split('\n');
            for (const QString& line : items)
            {
                if (line.isEmpty())
                {
                    continue;
                }

                QStringList parts = line.split(':');
                if (parts.count() != 2)
                {
                    continue;
                }

                QString name = parts[0].trimmed();
                QString type = parts[1].trimmed();
                QString tooltip = type;

                QString path = ":/remotes/images/" + type.replace(' ', '_') + ".png";
                QIcon icon(QFile(path).exists() ? path : ":/remotes/images/unknown.png");

                QListWidgetItem* item = new QListWidgetItem(icon, name);
                item->setData(Qt::UserRole, type);
                item->setToolTip(tooltip);
                ui.remotes->addItem(item);
            }
        }
        else
        {
            if (p->error() != QProcess::FailedToStart)
            {
                if (getConfigPassword(p))
                {
                    rcloneListRemotes();
                }
            }
        }
        p->deleteLater();
    });

    UseRclonePassword(p);
    p->start(GetRclone(), QStringList() << "listremotes" << GetRcloneConf() << "--long" << "--ask-password=false", QIODevice::ReadOnly);
}

bool MainWindow::getConfigPassword(QProcess* p)
{
    QString output = p->readAllStandardError().trimmed();
    if (output.indexOf("RCLONE_CONFIG_PASS") > 0)
    {
        bool ok;
        QString password = QInputDialog::getText(
            this, qApp->applicationDisplayName(),
            "Enter password for .rclone.conf configuration file:",
            QLineEdit::Password, QString(), &ok);
        if (ok)
        {
            SetRclonePassword(password);
            return true;
        }
    }
    else if (output.indexOf("unknown command \"listremotes\"") > 0)
    {
        QMessageBox::critical(this, qApp->applicationDisplayName(), "It seems rclone version you are using is too old.\nPlease upgrade to at least version 1.34!");
        return false;
    }
    return false;
}

bool MainWindow::canClose()
{
    if (mJobCount == 0)
    {
        return true;
    }

    bool wasVisible = isVisible();

    ui.tabs->setCurrentIndex(1);
    showNormal();

    int button = QMessageBox::question(
        this,
        "Rclone Browser",
        QString("There are %1 job(s) running.\n"
                "Do you want to stop them and quit?").arg(mJobCount),
                QMessageBox::Yes | QMessageBox::No);

    if (!wasVisible)
    {
        hide();
    }

    if (button == QMessageBox::Yes)
    {
        for (int i=0; i<ui.jobs->count(); i++)
        {
            QWidget* widget = ui.jobs->itemAt(i)->widget();
            if (auto mount = qobject_cast<MountWidget*>(widget))
            {
                mount->cancel();
            }
            else if (auto transfer = qobject_cast<JobWidget*>(widget))
            {
                transfer->cancel();
            }
            else if (auto stream = qobject_cast<StreamWidget*>(widget))
            {
                stream->cancel();
            }
        }
        return true;
    }

    return false;
}

void MainWindow::closeEvent(QCloseEvent* ev)
{
    if (mCloseToTray && isVisible())
    {
#ifdef Q_OS_OSX
        osxHideDockIcon();
#endif
        mSystemTray.show();
        hide();
        ev->ignore();
        return;
    }

    if (canClose())
    {
        QApplication::quit();
    }
    else
    {
        ev->ignore();
    }
}

void MainWindow::addTransfer(const QString& message, const QString& source, const QString& dest, const QStringList& args)
{
    QProcess* transfer = new QProcess(this);
    transfer->setProcessChannelMode(QProcess::MergedChannels);

    auto widget = new JobWidget(transfer, message, args, source, dest);

    auto line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    QObject::connect(widget, &JobWidget::finished, this, [=](const QString& info)
    {
        if (mNotifyFinishedTransfers)
        {
            qApp->alert(this);
            mLastFinished = widget;
            mSystemTray.showMessage("Transfer finished", info);
        }

        if (--mJobCount == 0)
        {
            ui.tabs->setTabText(1, "Jobs");
        }
        else
        {
            ui.tabs->setTabText(1, QString("Jobs (%1)").arg(mJobCount));
        }
    });

    QObject::connect(widget, &JobWidget::closed, this, [=]()
    {
        if (widget == mLastFinished)
        {
            mLastFinished = nullptr;
        }
        ui.jobs->removeWidget(widget);
        ui.jobs->removeWidget(line);
        widget->deleteLater();
        delete line;
        if (ui.jobs->count() == 2)
        {
            ui.noJobsAvailable->show();
        }
    });

    if (ui.jobs->count() == 2)
    {
        ui.noJobsAvailable->hide();
    }

    ui.jobs->insertWidget(0, widget);
    ui.jobs->insertWidget(1, line);
    ui.tabs->setTabText(1, QString("Jobs (%1)").arg(++mJobCount));

    UseRclonePassword(transfer);
    transfer->start(GetRclone(), GetRcloneConf() + args, QIODevice::ReadOnly);
}

void MainWindow::addMount(const QString& remote, const QString& folder)
{
    QProcess* mount = new QProcess(this);
    mount->setProcessChannelMode(QProcess::MergedChannels);

    auto widget = new MountWidget(mount, remote, folder);

    auto line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    QObject::connect(widget, &MountWidget::finished, this, [=]()
    {
        if (--mJobCount == 0)
        {
            ui.tabs->setTabText(1, "Jobs");
        }
        else
        {
            ui.tabs->setTabText(1, QString("Jobs (%1)").arg(mJobCount));
        }
    });

    QObject::connect(widget, &MountWidget::closed, this, [=]()
    {
        ui.jobs->removeWidget(widget);
        ui.jobs->removeWidget(line);
        widget->deleteLater();
        delete line;
        if (ui.jobs->count() == 2)
        {
            ui.noJobsAvailable->show();
        }
    });

    if (ui.jobs->count() == 2)
    {
        ui.noJobsAvailable->hide();
    }

    ui.jobs->insertWidget(0, widget);
    ui.jobs->insertWidget(1, line);
    ui.tabs->setTabText(1, QString("Jobs (%1)").arg(++mJobCount));

    auto settings = GetSettings();
    QString opt = settings->value("Settings/mount").toString();

    QStringList args;
    args << "mount";
    args << "--vfs-cache-mode";
    args << "writes";
    args.append(GetRcloneConf());
    if (!opt.isEmpty())
    {
        args.append(opt.split(' '));
    }
    args << remote << folder;

    UseRclonePassword(mount);
    mount->start(GetRclone(), args, QIODevice::ReadOnly);
}

void MainWindow::addStream(const QString& remote, const QString& stream)
{
    auto player = new QProcess();
    auto rclone = new QProcess();
    rclone->setStandardOutputProcess(player);

    QObject::connect(player, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [=](int status, QProcess::ExitStatus)
    {
        player->deleteLater();
        if (status != 0 && player->error() == QProcess::FailedToStart)
        {
            QMessageBox::critical(this, "Error", QString("Failed to start '%1' player process").arg(stream));
            auto settings = GetSettings();
            settings->remove("Settings/streamConfirmed");
        }
    });

    auto widget = new StreamWidget(rclone, player, remote, stream);

    auto line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    QObject::connect(widget, &StreamWidget::finished, this, [=]()
    {
        if (--mJobCount == 0)
        {
            ui.tabs->setTabText(1, "Jobs");
        }
        else
        {
            ui.tabs->setTabText(1, QString("Jobs (%1)").arg(mJobCount));
        }
    });

    QObject::connect(widget, &StreamWidget::closed, this, [=]()
    {
        ui.jobs->removeWidget(widget);
        ui.jobs->removeWidget(line);
        widget->deleteLater();
        delete line;
        if (ui.jobs->count() == 2)
        {
            ui.noJobsAvailable->show();
        }
    });

    if (ui.jobs->count() == 2)
    {
        ui.noJobsAvailable->hide();
    }

    ui.jobs->insertWidget(0, widget);
    ui.jobs->insertWidget(1, line);
    ui.tabs->setTabText(1, QString("Jobs (%1)").arg(++mJobCount));

    player->start(stream, QProcess::ReadOnly);
    UseRclonePassword(rclone);
    rclone->start(GetRclone(), QStringList() << "cat" << GetRcloneConf() << remote, QProcess::WriteOnly);
}
