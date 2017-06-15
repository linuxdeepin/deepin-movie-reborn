#ifndef _DMR_PLAYLIST_MODEL_H
#define _DMR_PLAYLIST_MODEL_H 


#include <QtWidgets>
#undef Bool
#include <mpv/qthelper.hpp>

namespace dmr {
using namespace mpv::qt;
class MpvProxy;

struct PlayItemInfo {
    QFileInfo info;
    QPixmap thumbnail;
    int duration;
};

class PlaylistModel: public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int current READ current WRITE changeCurrent NOTIFY currentChanged)
public:
    friend class MpvProxy;

    PlaylistModel(Handle h);
    void clear();
    void remove(int pos);
    void append(const QFileInfo&);
    void appendAndPlay(const QFileInfo&);

    void playNext();
    void playPrev();

    int count() const;
    const QList<PlayItemInfo>& items() const { return _infos; }
    int current() const;
    const PlayItemInfo& currentInfo() const;

    void switchPosition(int p1, int p2);

public slots:
    void changeCurrent(int);

signals:
    void countChanged();
    void currentChanged();
    void itemRemoved(int);

private:
    int _count {0};
    int _current {-1};
    QList<PlayItemInfo> _infos;
    Handle _handle;

    struct PlayItemInfo calculatePlayInfo(const QFileInfo& fi);
};

}

#endif /* ifndef _DMR_PLAYLIST_MODEL_H */

