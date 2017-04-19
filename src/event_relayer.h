#ifndef _DMR_EVENT_RELAYER_H
#define _DMR_EVENT_RELAYER_H 

#include <QWindow>
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>
#include <QPoint>

namespace dmr {

class EventRelayer: public QObject, public QAbstractNativeEventFilter 
{
    Q_OBJECT
public:
    EventRelayer(QWindow* src, QWindow *dest);
    ~EventRelayer();

signals:
    void targetNeedsUpdatePosition(const QPoint& p);

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *) override;

private:
    QWindow *_source, *_target;
};

}

#endif /* ifndef _DMR_EVENT_RELAYER_H */
