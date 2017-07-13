#ifndef _DMR_VPU_DECODER_H
#define _DMR_VPU_DECODER_H 

#include <QtGui>
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>

#include "vpuhelper.h"

extern "C" {
#include <libavresample/avresample.h>
}

#define MAX_FILE_PATH   256
#define MAX_ROT_BUF_NUM			2

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0


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
    int capacity {100}; //right now, measure as number of pakcets, maybe should be measured
                        // as duration or data size
    QWaitCondition empty_cond;

    T deque();
    void put(const T& v);
    void flush();
};

template<class T>
void PacketQueue<T>::flush()
{
    T v;
    //QMutexLocker l(&lock);
    //data.enqueue(v);
    //empty_cond.wakeAll();
}

template<class T>
T PacketQueue<T>::deque()
{
    T ret;
    {
        QMutexLocker l(&lock);
        if (data.count() == 0) {
            fprintf(stderr, "queue is empty, block and wait\n");
            empty_cond.wait(l.mutex());
            //FIXME: check quit signal
        }
        ret = data.dequeue();
    }

    return ret;
}

template<class T>
void PacketQueue<T>::put(const T& v)
{
    QMutexLocker l(&lock);
    data.enqueue(v);
    empty_cond.wakeAll();
}

struct VideoFrame
{
    QImage img;
    double pts;
};

class AudioDecoder: public QThread
{
public:
    AudioDecoder(AVCodecContext *ctx);
    virtual ~AudioDecoder();

    void stop();

protected:
    void run() override;

private:
    AVCodecContext *_audioCtx {nullptr};
    QAtomicInt _quitFlags {0};
    pa_simple *_pa {nullptr};
    AVAudioResampleContext *_avrCtx {nullptr};


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

    double synchronize_video(AVFrame *src_frame, double pts);

    int decodeAudio(AVPacket* pkt);

    int seqInit();
    int flushVideoBuffer(AVPacket* pkt);
    int buildVideoPacket(AVPacket* pkt);
    int sendFrame(AVPacket* pkt);

    int openMediaFile();

public slots:
    void video_refresh_timer();

signals:
    void frame(const QImage &);
    void schedule_refresh(int delay); 

private:
    QSize _viewportSize;
    QFileInfo _fileInfo;
    AudioDecoder *_audioThread {0};

    double _audioClock {0.0};
    double _videoClock {0.0};
    double _frameLastPts {0.0};
    double _frameLastDelay {0.0};
    double _frameTimer {0.0};


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

