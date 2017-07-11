#ifndef _DMR_VPU_DECODER_H
#define _DMR_VPU_DECODER_H 

#include <QtGui>
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>

#include "vpuhelper.h"

#define MAX_FILE_PATH   256
#define MAX_ROT_BUF_NUM			2

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

template<class T>
struct PacketQueue: QObject {
    QQueue<T> data;
    QMutex lock;
    int capacity {10}; //right now, measure as number of pakcets, maybe should be measured
                        // as duration or data size
    QWaitCondition empty_cond;
    QWaitCondition full_cond;

    T deque();
    void put(const T& v);
};

template<class T>
T PacketQueue<T>::deque()
{
    T ret;
    {
        QMutexLocker l(&lock);
        if (data.count() == 0) {
            empty_cond.wait(l.mutex());
        }
        ret = data.dequeue();
    }

    full_cond.wakeAll();
    return ret;
}

template<class T>
void PacketQueue<T>::put(const T& v)
{
    {
        QMutexLocker l(&lock);
        if (data.count() >= capacity) {
            full_cond.wait(l.mutex());
        }
        data.enqueue(v);
    }
    empty_cond.wakeAll();
}

class AudioDecoder: public QThread
{
public:
    AudioDecoder(AVCodecContext *ctx);

    void stop() { _quitFlags.storeRelease(1); }

protected:
    void run() override;

private:
    AVCodecContext *_audioCtx {nullptr};
    QAtomicInt _quitFlags {0};
    pa_simple *_pa {nullptr};

    int decodeFrames(AVPacket *pkt, uint8_t *audio_buf, int buf_size);
};

class VpuDecoder: public QThread
{
    Q_OBJECT
public:
    VpuDecoder(const QString& name);
    ~VpuDecoder();

    //return if video stream of the file can be hardware decoded
    bool isHardwareSupported();

    void stop() { _quitFlags.storeRelease(1); }
    void updateViewportSize(QSize sz);


protected:
    void run() override;
    bool init();
    int loop();

    int decodeAudio(AVPacket* pkt);

    int seqInit();
    int flushVideoBuffer();
    int buildVideoPacket(AVPacket* pkt);
    int sendFrame();

    int openMediaFile();

signals:
    void frame(const QImage &);

private:
    QSize _viewportSize;
    QFileInfo _fileInfo;

	DecConfigParam	decConfig;
    QAtomicInt _quitFlags {0};
    int _seqInited {0};
    int seqFilled {0};

	frame_queue_item_t* display_queue {0};
	DecHandle		handle		{0};
	DecOpenParam	decOP		{0};
	DecInitialInfo	initialInfo {0};
	DecOutputInfo	outputInfo	{0};
	DecParam		decParam	{0};
	BufInfo			bufInfo	    {0};
	vpu_buffer_t	vbStream	{0};

	AVFormatContext *ic {0};
	AVCodecContext *ctxVideo {0};
	AVCodecContext *ctxAudio {0};
	AVCodecContext *ctxSubtitle {0};
	int idxVideo {-1};
    int idxAudio {-1};
    int idxSubtitle {-1};

	int	chunkIdx {0};
	BYTE *seqHeader {0};
	int seqHeaderSize {0};
	BYTE *picHeader {0};
	int picHeaderSize {0};
	BYTE *chunkData {0};
	int chunkSize {0};
	int	reUseChunk;
	int	totalNumofErrMbs {0};
	int	int_reason {0};

#if defined(SUPPORT_DEC_SLICE_BUFFER) || defined(SUPPORT_DEC_RESOLUTION_CHANGE)
	DecBufInfo decBufInfo;
#endif
	BYTE *			pYuv {0};
	FrameBuffer		fbPPU[MAX_ROT_BUF_NUM];
	FrameBufferAllocInfo fbAllocInfo;
	int				framebufSize  {0}, framebufWidth  {0};
    int             framebufHeight  {0}, rotbufWidth  {0}, rotbufHeight  {0};
    int             framebufFormat  {FORMAT_420}, mapType;
	int				framebufStride  {0}, rotStride  {0}, regFrameBufCount  {0};
	int				frameIdx  {0};
    int ppIdx {0}, decodeIdx {0};
	int				hScaleFactor, vScaleFactor, scaledWidth, scaledHeight;
	int				ppuEnable  {0};
	int				bsfillSize  {0};
	int				instIdx {0}, coreIdx {0};
	TiledMapConfig mapCfg;
	DRAMConfig dramCfg  {0};
};

}

#endif /* ifndef _DMR_VPU_DECODER_H */

