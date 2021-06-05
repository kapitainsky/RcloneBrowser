#pragma once

#include "icon_cache.h"
#include "job_options.h"
#include "pch.h"
#include "ui_main_window.h"

class JobWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow();
  ~MainWindow();

private slots:
  void rcloneGetVersion();
  void rcloneConfig();
  void rcloneListRemotes();
  void listTasks();

  void addTransfer(const QString &message, const QString &source,
                   const QString &dest, const QStringList &args);
  void addMount(const QString &remote, const QString &folder);
  void addStream(const QString &remote, const QString &stream);
  void mountRemote(const QString &remote, const QString &path);

private:
  Ui::MainWindow ui;

  QSystemTrayIcon mSystemTray;
  JobWidget *mLastFinished = nullptr;

  bool mAlwaysShowInTray;
  bool mCloseToTray;
  bool mNotifyFinishedTransfers;

  QLabel *mStatusMessage;

  IconCache mIcons;

  bool mFirstTime = true;
  int mJobCount = 0;

  bool canClose();
  void closeEvent(QCloseEvent *ev) override;
  bool getConfigPassword(QProcess *p);

  void addEmptyJobsMessage();

  void runItem(JobOptionsListWidgetItem *item, bool dryrun = false);
  void editSelectedTask();
  QIcon mUploadIcon;
  QIcon mDownloadIcon;
};
