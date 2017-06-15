#include "playlist_model.h"

namespace dmr {

PlaylistModel::PlaylistModel(Handle h)
    :_handle{h}
{
}

void PlaylistModel::clear()
{
    _infos.clear();
    QList<QVariant> args = { "playlist-clear" };
    qDebug () << args;
    command(_handle, args);
}

void PlaylistModel::remove(int pos)
{
    if (pos < 0 || pos >= count()) return;

    _infos.removeAt(pos);
    emit itemRemoved(pos);

    QList<QVariant> args = { "playlist-remove", pos };
    qDebug () << args;
    command(_handle, args);
}

void PlaylistModel::playNext()
{
    if (!count()) return;

    QList<QVariant> args = { "playlist-next", "weak" };
    qDebug () << args;
    command(_handle, args);
}

void PlaylistModel::playPrev()
{
    if (!count()) return;

    QList<QVariant> args = { "playlist-prev", "weak" };
    qDebug () << args;
    command(_handle, args);
}

//TODO: what if loadfile failed
void PlaylistModel::append(const QFileInfo& fi)
{
    if (!fi.exists()) return;

    _infos.append(calculatePlayInfo(fi));

    QList<QVariant> args = { "loadfile", fi.canonicalFilePath(), "append" };
    qDebug () << args;
    command(_handle, args);
}

void PlaylistModel::appendAndPlay(const QFileInfo& fi)
{
    if (!fi.exists()) return;
    _infos.append(calculatePlayInfo(fi));

    QList<QVariant> args = { "loadfile", fi.canonicalFilePath(), "append-play" };
    qDebug () << args;
    command(_handle, args);
}

void PlaylistModel::changeCurrent(int pos)
{
    if (pos < 0 || pos >= count()) return;

    set_property(_handle, "playlist-pos", pos);
}

void PlaylistModel::switchPosition(int p1, int p2)
{
}

const PlayItemInfo& PlaylistModel::currentInfo() const
{
    Q_ASSERT (_infos.size() > 0);

    return _infos[_current];
}

int PlaylistModel::count() const
{
    return _infos.count();
}

int PlaylistModel::current() const
{
    return _current;
}

struct PlayItemInfo PlaylistModel::calculatePlayInfo(const QFileInfo& fi)
{
    PlayItemInfo pif = {
        .info = fi
    };

    return pif;
}

}


