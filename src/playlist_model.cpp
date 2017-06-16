#include "playlist_model.h"
#include <libffmpegthumbnailer/videothumbnailer.h>

namespace dmr {

PlaylistModel::PlaylistModel(Handle h)
    :_handle{h}
{
    _thumbnailer.setThumbnailSize(44);
}

void PlaylistModel::clear()
{
    _infos.clear();
    QList<QVariant> args = { "playlist-clear" };
    qDebug () << args;
    command(_handle, args);

    args = { "playlist-remove", "current" };
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
    std::vector<uint8_t> buf;
    _thumbnailer.generateThumbnail(fi.canonicalFilePath().toUtf8().toStdString(),
            ThumbnailerImageType::Png, buf);

    auto img = QImage::fromData(buf.data(), buf.size(), "png");

    QPixmap pm = QPixmap::fromImage(img);

    PlayItemInfo pif = {
        .info = fi,
        .thumbnail = pm.scaled(24, 44),
        .duration = 3600
    };

    Q_ASSERT(!pif.thumbnail.isNull());

    return pif;
}

}


