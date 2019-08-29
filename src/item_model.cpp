#include <algorithm>

#include "item_model.h"
#include "icon_cache.h"
#include "utils.h"

namespace
{
    static void advanceSpinner(QString& text)
    {
        int spinnerPos = (int)((size_t)text.length() - 2);
        QChar current = text[spinnerPos];
        static const QChar spinner[] = { '-', '\\', '|', '/' };
        size_t spinnerCount = sizeof(spinner) / sizeof(*spinner);
        const QChar* found = std::find(spinner, spinner + spinnerCount, current);
        size_t idx = found - spinner;
        size_t next = idx == spinnerCount - 1 ? 0 : idx + 1;
        text[spinnerPos] = spinner[next];
    }

    QString getNiceSize(quint64 size)
    {
        static const char prefix[] = " KMGTPE";
        for (int i = sizeof(prefix) - 2; i >= 0; i--)
        {
            quint64 base = quint64(1) << (i * 10);
            if (size >= 10 * base)
            {
                return QString("%1 %2").arg(size / base).arg(QChar(prefix[i])).trimmed();
            }
        }
        return "0";
    }
}

class ItemSorter
{
public:
    inline ItemSorter(int column, Qt::SortOrder order)
        : mColumn(column)
        , mOrder(order)
    {
        mCompare.setNumericMode(true);
    }

    bool operator()(const Item* a, const Item* b) const
    {
        switch (mColumn)
        {
        case 0:
            if (a->isFolder != b->isFolder)
            {
                return a->isFolder;
            }
            return mOrder == Qt::AscendingOrder
                ? mCompare.compare(a->name, b->name) < 0
                : mCompare.compare(b->name, a->name) < 0;

        case 1:
            if (a->isFolder != b->isFolder)
            {
                return a->isFolder;
            }
            if (a->size == b->size)
            {
                return mOrder == Qt::AscendingOrder
                    ? mCompare.compare(a->name, b->name) < 0
                    : mCompare.compare(b->name, a->name) < 0;
            }
            return mOrder == Qt::AscendingOrder ? a->size < b->size : b->size < a->size;

        case 2:
            if (a->isFolder != b->isFolder)
            {
                return a->isFolder;
            }
            if (a->modified == b->modified)
            {
                return mOrder == Qt::AscendingOrder
                    ? mCompare.compare(a->name, b->name) < 0
                    : mCompare.compare(b->name, a->name) < 0;
            }
            return mOrder == Qt::AscendingOrder ? a->modified < b->modified : b->modified < a->modified;
        }
        Q_ASSERT(false);
        return false;
    }

private:
    QCollator mCompare;
    int mColumn;
    Qt::SortOrder mOrder;
};

ItemModel::ItemModel(IconCache* icons, const QString& remote, QObject* parent)
    : QAbstractItemModel(parent)
    , mRemote(remote)
    , mFixedFont(QFontDatabase::systemFont(QFontDatabase::FixedFont))
    , mRegExpFolder(R"(^[\d-]+ (\d\d\d\d-\d\d-\d\d \d\d:\d\d:\d\d) \s*[\d-]+ (.+)$)")
    , mRegExpFile(R"(^(\d+) (\d\d\d\d-\d\d-\d\d \d\d:\d\d:\d\d)\.\d+ (.+)$)")
{
    QStyle* style = qApp->style();
    mDriveIcon = style->standardIcon(QStyle::SP_DriveNetIcon);
    mFolderIcon = style->standardIcon(QStyle::SP_DirIcon);
    mFileIcon = style->standardIcon(QStyle::SP_FileIcon);

    auto settings = GetSettings();
    mFolderIcons = settings->value("Settings/showFolderIcons", true).toBool();
    mFileIcons = settings->value("Settings/showFileIcons", true).toBool();

    mRoot = new Item();
    mRoot->isFolder = true;
    mRoot->state = Item::Ready;

    QObject::connect(this, &ItemModel::getIcon, icons, &IconCache::getIcon);
    QObject::connect(icons, &IconCache::iconReady, this, [=](Item* item, const QPersistentModelIndex& parent, const QIcon& icon)
    {
        item->state = Item::Ready;
        QString ext = QFileInfo(item->name).suffix();
        if (!mLoadedIcons.contains(ext))
        {
            mLoadedIcons.insert(ext, icon);
        }

        if (item->isDeleted)
        {
            delete item;
            return;
        }

        QModelIndex idx = index(item->num(), 0, parent);
        emit dataChanged(idx, idx, QVector<int>{Qt::DecorationRole});
    });
}

ItemModel::~ItemModel()
{
    delete mRoot;
}

const QDir& ItemModel::path(const QModelIndex& index) const
{
    return get(index)->path;
}

bool ItemModel::isLoading(const QModelIndex& index) const
{
    return get(index)->parent->isLoading();
}

void ItemModel::refresh(const QModelIndex& index)
{
    Item* item = get(index);
    Item* folderItem = item->isFolder ? item : item->parent;
    if (folderItem->isLoading())
    {
        return;
    }
    load(item->isFolder ? index : index.parent(), folderItem);
}

void ItemModel::rename(const QModelIndex& index, const QString& name)
{
    Item* item = get(index);
    item->name = name;
    item->path.setPath(item->parent->path.filePath(item->name));
    emit dataChanged(index, index, QVector<int>{Qt::DisplayRole});
}

bool ItemModel::isTopLevel(const QModelIndex& index) const
{
    return get(index)->parent == mRoot;
}

bool ItemModel::isFolder(const QModelIndex& index) const
{
    return get(index)->isFolder;
}

QModelIndex ItemModel::addRoot(const QString& name, const QString& path)
{
    emit layoutAboutToBeChanged();

    Item* item = new Item();
    item->isFolder = true;
    item->name = name;
    item->path.setPath(path);
    item->parent = mRoot;
    mRoot->childs.append(item);

    emit layoutChanged();

    return createIndex(item->num(), 0, item);
}

QModelIndex ItemModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
    {
        return QModelIndex();
    }

    Item* item = get(parent);
    return createIndex(row, column, item->childs[row]);
}

QModelIndex ItemModel::parent(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return QModelIndex();
    }

    Item* child = get(index);
    if (child->parent == mRoot)
    {
        return QModelIndex();
    }

    return createIndex(child->parent->num(), 0, child->parent);
}

bool ItemModel::hasChildren(const QModelIndex& parent) const
{
    Item* item = get(parent);
    if (item->isFolder)
    {
        if (item->state == Item::Ready)
        {
            return !item->childs.isEmpty();
        }
        return true;
    }
    return false;
}

int ItemModel::rowCount(const QModelIndex& parent) const
{
    Item* item = get(parent);
    if (item->isFolder)
    {
        if (item->state == Item::Unknown)
        {
            const_cast<ItemModel*>(this)->load(parent, item);
        }
    }
    return item->childs.count();
}

int ItemModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 3;
}

void ItemModel::sort(int column, Qt::SortOrder order)
{
    mSortColumn = column;
    mSortOrder = order;
    sort(QModelIndex(), mRoot);
}

QVariant ItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    const Item* item = get(index);

    if (role == Qt::DecorationRole && index.column() == 0)
    {
        if (item->state == Item::Special)
        {
            return QIcon();
        }

        if (item->isFolder)
        {
            if (mFolderIcons)
            {
                return item->parent == mRoot ? mDriveIcon : mFolderIcon;
            }
            return QIcon();
        }

        if (mFileIcons)
        {
            QString ext = QFileInfo(item->name).suffix();
            auto it = mLoadedIcons.find(ext);
            if (it == mLoadedIcons.end())
            {
                return mFileIcon;
            }

            return it.value();
        }

        return QIcon();
    }

    if (role == Qt::TextAlignmentRole)
    {
        if (index.column() == 1)
        {
            return Qt::AlignRight + Qt::AlignVCenter;
        }
        return QVariant();
    }

    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
        case 0:
            return item->name;
        case 1:
            if (item->isFolder || item->state == Item::Special)
            {
                return QString();
            }
            else
            {
                return getNiceSize(item->size);
            }
        case 2:
            return item->modified;
        }
        Q_ASSERT(false);
    }
    return QVariant();
}

QVariant ItemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (section)
        {
        case 0:
            return "Name";
        case 1:
            return "Size";
        case 2:
            return "Modified";
        }
    }

    return QVariant();
}

bool ItemModel::removeRows(int row, int count, const QModelIndex& parent)
{
    if (!hasIndex(row, 0, parent))
    {
        return false;
    }

    Item* item = get(parent);
    if (row + count > item->childs.count())
    {
        return false;
    }

    emit beginRemoveRows(parent, row, row + count - 1);

    for (int i=row; i<row+count; i++)
    {
        Item* node = item->childs.at(i);
        if (node->isLoading() || node->state == Item::LoadingIcon)
        {
            node->isDeleted = true;
        }
        else
        {
            delete node;
        }
    }
    item->childs.remove(row, count);

    emit endRemoveRows();

    return true;
}

Qt::ItemFlags ItemModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(index);

    if (!index.isValid())
    {
        return defaultFlags;
    }

    return Qt::ItemIsDropEnabled | defaultFlags;
}

bool ItemModel::canDropMimeData(const QMimeData* data, Qt::DropAction action,
    int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(row);
    Q_UNUSED(column);
    Q_UNUSED(parent);

    if (action != Qt::CopyAction && action != Qt::MoveAction)
    {
        return false;
    }

    if (!data->hasUrls())
    {
        return false;
    }

    auto urls = data->urls();
    if (urls.count() == 1)
    {
        return urls.front().isLocalFile();
    }

    return false;
}

bool ItemModel::dropMimeData(const QMimeData* data, Qt::DropAction action,
    int row, int column, const QModelIndex& parent)
{
    if (!canDropMimeData(data, action, row, column, parent))
    {
        return false;
    }

    QDir path = QDir(data->urls().front().toLocalFile());
    Item* item = get(parent);

    emit drop(path, item->isFolder ? parent : parent.parent());

    return false;
}

Item* ItemModel::get(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Item*>(index.internalPointer()) : mRoot;
}

void ItemModel::load(const QPersistentModelIndex& parentIndex, Item* parent)
{
    auto lsd = new QProcess(this);
    auto lsl = new QProcess(this);

    auto cache = new QVector<Item*>();

    Item* loading = new Item();
    loading->state = Item::Special;
    loading->name = "... loading [-]";
    loading->parent = parent;

    QTimer* timer = new QTimer(this);

    QObject::connect(timer, &QTimer::timeout, this, [=]()
    {
        advanceSpinner(loading->name);
        auto loadingIndex = createIndex(loading->num(), 0, loading);
        emit dataChanged(loadingIndex, loadingIndex, QVector<int>{Qt::DisplayRole});
    });

    auto rcloneFinished = [=]()
    {
        sender()->deleteLater();

        parent->state = parent->state == Item::Loading1 ? Item::Loading2 : Item::Ready;
        if (parent->state != Item::Ready)
        {
            return;
        }

        timer->stop();
        timer->deleteLater();

        if (parent->isDeleted)
        {
            qDeleteAll(*cache);
            delete cache;
            delete parent;
            return;
        }

        QHash<QString, int> existing;
        for (int i=0; i<parent->childs.count(); i++)
        {
            if (parent->childs[i] != loading)
            {
                existing.insert(parent->childs[i]->name, i);
            }
        }

        QVector<Item*> todo;

        bool modified = false;
        for (auto& item : *cache)
        {
            auto it = existing.find(item->name);
            if (it == existing.end())
            {
                item->path.setPath(parent->path.filePath(item->name));
                if (!item->isFolder && mFileIcons)
                {
                    QString ext = QFileInfo(item->name).suffix();
                    if (!mLoadedIcons.contains(ext))
                    {
                        item->state = Item::LoadingIcon;
                        emit getIcon(item, parentIndex);
                    }
                }
                todo.append(item);
                item = nullptr;
            }
            else
            {
                Item* old = parent->childs[it.value()];
                if (old->isFolder != item->isFolder
                 || old->modified != item->modified
                 || old->size != item->size)
                {
                    old->state = Item::Unknown;
                    old->isFolder = item->isFolder;
                    old->modified = item->modified;
                    old->size = item->size;
                    modified = true;
                    emit dataChanged(createIndex(it.value(), 0, parent),
                                     createIndex(it.value(), 2, parent),
                                     QVector<int>{Qt::DisplayRole});
                }
                existing.erase(it);
            }
        }

        qDeleteAll(*cache);
        delete cache;

        for (int i=0; i<parent->childs.count(); i++)
        {
            if (parent->childs[i] == loading || existing.contains(parent->childs[i]->name))
            {
                emit beginRemoveRows(parentIndex, i, i);
                delete parent->childs.takeAt(i);
                emit endRemoveRows();
                i--;
            }
        }

        if (!todo.isEmpty())
        {
            modified = true;
            emit beginInsertRows(parentIndex, parent->childs.count(), parent->childs.count() + todo.count() - 1);
            parent->childs += todo;
            emit endInsertRows();
        }

        if (modified)
        {
            sort(parentIndex, parent);
        }
    };

    QObject::connect(lsd, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, rcloneFinished);
    QObject::connect(lsl, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, rcloneFinished);

    QObject::connect(lsd, &QProcess::readyRead, this, [=]()
    {
        while (lsd->canReadLine())
        {
            if (mRegExpFolder.exactMatch(lsd->readLine().trimmed()))
            {
                QStringList cap = mRegExpFolder.capturedTexts();

                Item* child = new Item();
                child->isFolder = true;
                child->parent = parent;
                child->name = cap[2];
                child->modified = cap[1];

                cache->append(child);
            }
        }
    });

    QObject::connect(lsl, &QProcess::readyRead, this, [=]()
    {
        while (lsl->canReadLine())
        {
            if (mRegExpFile.exactMatch(lsl->readLine().trimmed()))
            {
                QStringList cap = mRegExpFile.capturedTexts();

                Item* child = new Item();
                child->parent = parent;
                child->name = cap[3];
                child->modified = cap[2];
                child->size = cap[1].toULongLong();

                cache->append(child);
            }
        }
    });

    parent->state = Item::Loading1;

    emit beginInsertRows(parentIndex, 0, 0);
    parent->childs.prepend(loading);
    emit endInsertRows();

    timer->start(100);
    UseRclonePassword(lsd);
    UseRclonePassword(lsl);
	QString path = parent->path.path();
	if (path.trimmed().length() == 0) {
		path = "/";
	}
	else if (path.at(0) != '/') {
		path = "/" + path;
	}

	lsd->start(GetRclone(), QStringList() << "lsd" << GetRcloneConf() << GetDriveSharedWithMe() << GetShowHidden() << mRemote + ":" + path, QIODevice::ReadOnly);
	lsl->start(GetRclone(), QStringList() << "lsl" << GetRcloneConf() << GetDriveSharedWithMe() << GetShowHidden() << "--max-depth" << "1" << mRemote + ":" + path, QIODevice::ReadOnly);
}

void ItemModel::sortRecursive(Item* item, const ItemSorter& sorter)
{
    std::sort(item->childs.begin(), item->childs.end(), sorter);

    for (auto child : item->childs)
    {
        sortRecursive(child, sorter);
    }
}

void ItemModel::sort(const QModelIndex& parent, Item* item)
{
    if (item->childs.isEmpty())
    {
        return;
    }

    QList<QPersistentModelIndex> parents;
    parents << parent;
    emit layoutAboutToBeChanged(parents, QAbstractItemModel::VerticalSortHint);

    QModelIndexList oldList = persistentIndexList();
    QVector<QPair<Item*, int> > oldNodes;
    oldNodes.reserve(oldList.count());
    for (const auto& index : oldList)
    {
        oldNodes.append(qMakePair(get(index), index.column()));
    }

    ItemSorter sorter(mSortColumn, mSortOrder);
    sortRecursive(item, sorter);

    QModelIndexList newList;
    newList.reserve(oldNodes.size());
    for (const auto& node : oldNodes)
    {
        Item* child = node.first;
        int column = node.second;
        int row = child->num();
        newList.append(createIndex(row, column, child));
    }

    changePersistentIndexList(oldList, newList);

    emit layoutChanged(parents, QAbstractItemModel::VerticalSortHint);
}
