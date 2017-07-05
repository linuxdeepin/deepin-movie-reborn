#ifndef _DMR_VPU_PROXY_H
#define _DMR_VPU_PROXY_H 

#include <QtWidgets>
#include "vpuhelper.h"

#define MAX_FILE_PATH   256

typedef struct
{
        char yuvFileName[MAX_FILE_PATH];
        char bitstreamFileName[MAX_FILE_PATH];
        int     bitFormat;
    int avcExtension; // 0:AVC, 1:MVC   // if CDB is enabled 0:MP 1:MVC 2:BP
        int rotAngle;
        int mirDir;
        int useRot;
        int     useDering;
        int outNum;
        int checkeos;
        int mp4DeblkEnable;
        int iframeSearchEnable;
        int skipframeMode;
        int reorder;
        int     mpeg4Class;
        int mapType;
        int seqInitMode;
        int instNum;
        int bitstreamMode;

      int cacheOption;
      int cacheBypass;
      int cacheDual;
    LowDelayInfo lowDelayInfo;
        int wtlEnable;
    int maxWidth;
    int maxHeight;
        int coreIdx;
        int frameDelay;
        int cmd;
        int userFbAlloc;
        int runFilePlayTest;

} DecConfigParam;

namespace dmr {

class VpuDecoder : public QThread
{
    Q_OBJECT
public:
    VpuDecoder();
    ~VpuDecoder();

protected:
    void run() override;
    bool init();
    int loop();

signals:
    void frame(const QImage &);

private:
	DecConfigParam	decConfig;
};


class VpuProxy: public QWidget {
    Q_OBJECT
public:
    VpuProxy(QWidget *parent = 0);


public slots:
    void play();

private:
    QLabel *_canvas {0};
};
}

#endif /* ifndef _DMR_VPU_PROXY_H */




