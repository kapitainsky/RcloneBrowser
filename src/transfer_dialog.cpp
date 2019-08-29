#include "transfer_dialog.h"
#include "utils.h"

TransferDialog::TransferDialog(bool isDownload, const QString& remote, const QDir& path, bool isFolder, QWidget* parent)
    : QDialog(parent)
    , mIsDownload(isDownload)
{
    ui.setupUi(this);
    resize(0, 0);
    setWindowTitle(isDownload ? "Download" : "Upload");

    QStyle* style = qApp->style();
    ui.buttonSourceFile->setIcon(style->standardIcon(QStyle::SP_FileIcon));
    ui.buttonSourceFolder->setIcon(style->standardIcon(QStyle::SP_DirIcon));
    ui.buttonDest->setIcon(style->standardIcon(QStyle::SP_DirIcon));

    QPushButton* dryRun = ui.buttonBox->addButton("&Dry run", QDialogButtonBox::AcceptRole);
    ui.buttonBox->addButton("&Run", QDialogButtonBox::AcceptRole);

    QObject::connect(ui.buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [=]()
    {
        ui.cbSyncDelete->setCurrentIndex(0);
        ui.checkSkipNewer->setChecked(false);
        ui.checkSkipNewer->setChecked(false);
        ui.checkCompare->setChecked(true);
        ui.cbCompare->setCurrentIndex(0);
        ui.checkVerbose->setChecked(false);
        ui.checkSameFilesystem->setChecked(false);
        ui.checkDontUpdateModified->setChecked(false);
        ui.spinTransfers->setValue(4);
        ui.spinCheckers->setValue(8);
        ui.textBandwidth->clear();
        ui.textMinSize->clear();
        ui.textMinAge->clear();
        ui.textMaxAge->clear();
        ui.spinMaxDepth->setValue(0);
        ui.spinConnectTimeout->setValue(60);
        ui.spinIdleTimeout->setValue(300);
        ui.spinRetries->setValue(3);
        ui.spinLowLevelRetries->setValue(10);
        ui.checkDeleteExcluded->setChecked(false);
        ui.textExclude->clear();
        ui.textExtra->clear();
    });
    ui.buttonBox->button(QDialogButtonBox::RestoreDefaults)->click();

    QObject::connect(dryRun, &QPushButton::clicked, this, [=]()
    {
        mDryRun = true;
    });
    QObject::connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(ui.buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QObject::connect(ui.buttonSourceFile, &QToolButton::clicked, this, [=]()
    {
        QString file = QFileDialog::getOpenFileName(this, "Choose file to upload");
        if (!file.isEmpty())
        {
            ui.textSource->setText(QDir::toNativeSeparators(file));
        }
    });

    QObject::connect(ui.buttonSourceFolder, &QToolButton::clicked, this, [=]()
    {
        QString folder = QFileDialog::getExistingDirectory(this, "Choose folder to upload");
        if (!folder.isEmpty())
        {
            ui.textSource->setText(QDir::toNativeSeparators(folder));
            ui.textDest->setText(remote + ":" + path.filePath(QFileInfo(folder).fileName()));
        }
    });
    
    QObject::connect(ui.buttonDest, &QToolButton::clicked, this, [=]()
    {
        QString folder = QFileDialog::getExistingDirectory(this, "Choose destination folder");
        if (!folder.isEmpty())
        {
            if (isFolder)
            {
                ui.textDest->setText(QDir::toNativeSeparators(folder + "/" + path.dirName()));
            }
            else
            {
                ui.textDest->setText(QDir::toNativeSeparators(folder));
            }
        }
    });

    auto settings = GetSettings();
    settings->beginGroup("Transfer");
    ReadSettings(settings.get(), this);
    settings->endGroup();

    ui.buttonSourceFile->setVisible(!isDownload);
    ui.buttonSourceFolder->setVisible(!isDownload);
    ui.buttonDest->setVisible(isDownload);

    (isDownload ? ui.textSource : ui.textDest)->setText(remote + ":" + path.path());
}

TransferDialog::~TransferDialog()
{
    if (result() == QDialog::Accepted)
    {
        auto settings = GetSettings();
        settings->beginGroup("Transfer");
        WriteSettings(settings.get(), this);
        settings->remove("textSource");
        settings->remove("textDest");
        settings->endGroup();
    }
}

void TransferDialog::setSource(const QString& path)
{
    ui.textSource->setText(QDir::toNativeSeparators(path));
}

QString TransferDialog::getMode() const
{
    if (ui.rbCopy->isChecked())
    {
        return "Copy";
    }
    else if (ui.rbMove->isChecked())
    {
        return "Move";
    }
    else if (ui.rbSync->isChecked())
    {
        return "Sync";
    }

    return QString();
}

QString TransferDialog::getSource() const
{
    return ui.textSource->text();
}

QString TransferDialog::getDest() const
{
    return ui.textDest->text();
}

QStringList TransferDialog::getOptions() const
{
    QString mode;

    QStringList list;
    if (ui.rbCopy->isChecked())
    {
        list << "copy";
        mode = "Copy";
    }
    else if (ui.rbMove->isChecked())
    {
        list << "move";
        mode = "Move";
    }
    else if (ui.rbSync->isChecked())
    {
        list << "sync";
        mode = "Sync";
    }

    if (mDryRun)
    {
        list << "--dry-run";
    }
    if (ui.rbSync->isChecked())
    {
        switch (ui.cbSyncDelete->currentIndex())
        {
        case 0:
            list << "--delete-during";
            break;
        case 1:
            list << "--delete-after";
            break;
        case 2:
            list << "--delete-before";
            break;
        }
    }
    if (ui.checkSkipNewer->isChecked())
    {
        list << "--update";
    }
    if (ui.checkSkipExisting->isChecked())
    {
        list << "--ignore-existing";
    }
    if (ui.checkCompare->isChecked())
    {
        switch (ui.cbCompare->currentIndex())
        {
        case 1:
            list << "--checksum";
            break;
        case 2:
            list << "--ignore-size";
            break;
        case 3:
            list << "--size-only";
            break;
        case 4:
            list << "--checksum" << "--ignore-size";
            break;
        }
    }
    if (ui.checkVerbose->isChecked())
    {
        list << "--verbose";
    }
    if (ui.checkSameFilesystem->isChecked())
    {
        list << "--one-file-system";
    }
    if (ui.checkDontUpdateModified->isChecked())
    {
        list << "--no-update-modtime";
    }
    list << "--transfers" << ui.spinTransfers->text();
    list << "--checkers" << ui.spinCheckers->text();
    if (!ui.textBandwidth->text().isEmpty())
    {
        list << "--bwlimit" << ui.textBandwidth->text();
    }
    if (!ui.textMinSize->text().isEmpty())
    {
        list << "--min-size" << ui.textMinSize->text();
    }
    if (!ui.textMinAge->text().isEmpty())
    {
        list << "--min-age" << ui.textMinAge->text();
    }
    if (!ui.textMaxAge->text().isEmpty())
    {
        list << "--max-age" << ui.textMaxAge->text();
    }
    if (ui.spinMaxDepth->value() != 0)
    {
        list << "--max-depth" << ui.spinMaxDepth->text();
    }
    list << "--contimeout" << (ui.spinConnectTimeout->text() + "s");
    list << "--timeout" << (ui.spinIdleTimeout->text() + "s");
    list << "--retries" << ui.spinRetries->text();
    list << "--low-level-retries" << ui.spinLowLevelRetries->text();

    if (ui.checkDeleteExcluded->isChecked())
    {
        list << "--delete-excluded";
    }

    QString excluded = ui.textExclude->toPlainText().trimmed();
    if (!excluded.isEmpty())
    {
        for (auto line : excluded.split('\n'))
        {
            list << "--exclude" << line;
        }
    }

    QString extra = ui.textExtra->text().trimmed();
    if (!extra.isEmpty())
    {
        for (auto arg : extra.split(' '))
        {
            list << arg;
        }
    }

    list << GetDriveSharedWithMe();

    list << "--stats" << "1s";

    list << ui.textSource->text();
    list << ui.textDest->text();

    return list;
}

void TransferDialog::done(int r)
{
    if (r == QDialog::Accepted)
    {
        if (mIsDownload)
        {
            if (ui.textDest->text().isEmpty())
            {
                QMessageBox::warning(this, "Warning", "Please enter destination!");
                return;
            }
        }
        else
        {
            if (ui.textSource->text().isEmpty())
            {
                QMessageBox::warning(this, "Warning", "Please enter source!");
                return;
            }
        }
    }
    QDialog::done(r);
}
