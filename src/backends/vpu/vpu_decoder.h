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

struct AVPacketQueue {
    QQueue<AVPacket> data;
    QMutex lock;
    int capacity {100}; //right now, measure as number of pakcets, maybe should be measured
                        // as duration or data size
    QWaitCondition empty_cond;
    QWaitCondition full_cond;

    AVPacket deque();
    void put(AVPacket v);
    void flush();
    int size();
    bool full();
};

struct VideoFrame
{
    uchar *data;
    int stride;
    int height;
    int width;
    double pts;
};

struct VideoPacketQueue {
    QQueue<VideoFrame> data;
    QMutex lock;
    int capacity {25}; 
    QWaitCondition empty_cond;
    QWaitCondition full_cond;

    VideoFrame deque();
    void put(VideoFrame v);
    void flush();
    int size();
};

class AudioDecoder: public QThread
{
    Q_OBJECT
public:
    AudioDecoder(AVStream* st, AVCodecContext *ctx);
    virtual ~AudioDecoder();

    qreal getVolume() const;
    bool setVolume(qreal value);
    bool setMute(bool value);
    bool isMuted();

    static void context_state_callback(pa_context *c, void *userdata);
    static void stream_state_callback(pa_stream *s, void *userdata);
    static void stream_write_callback(pa_stream *s, size_t length, void *userdata);
    static void success_callback(pa_stream *s, int success, void *userdata);
    static void sink_info_callback(pa_context *c, const pa_sink_input_info *i, 
            int is_last, void *userdata);
    static void context_subscribe_callback(pa_context *c, pa_subscription_event_type_t type,
            uint32_t idx, void *userdata);

signals:
    void muteChanged(bool muted);
    void volumeChanged(qreal val);

protected:
    void run() override;

private:
    AVCodecContext *_audioCtx {nullptr};
    AVStream *_audioSt {nullptr};
    AVAudioResampleContext *_avrCtx {nullptr};
    
    QMutex _lock;
    QWaitCondition _cond;

    pa_sink_input_info _info {0};
    pa_threaded_mainloop *_pa_loop {0};
    pa_context *_pa_ctx {0};
    pa_stream *_pa_stream {0};
    size_t _pulse_available_size {0};

    double _audioCurrentTime {0.0};
    double _lastPts {0.0};

    int decodeFrames(AVPacket *pkt, uint8_t *audio_buf, int buf_size);
	bool init(); //init pulse
    void deinit();

    bool waitToFinished(pa_operation *op);
};

class VpuDecoder: public QThread
{
public:
    VpuDecoder(AVStream *st, AVCodecContext *ctx);
    ~VpuDecoder();

    void updateViewportSize(QSize sz);
    QSize viewportSize() const { return _viewportSize; }
    bool firstFrameStarted();

protected:
    void run() override;
    bool init();
    int loop();

    double synchronize_video(AVFrame *src_frame, double pts);

    int seqInit();
    int flushVideoBuffer(AVPacket* pkt);
    int buildVideoPacket(AVPacket* pkt);
    int sendFrame(AVPacket* pkt);

private:
    QSize _viewportSize;
    QImage _frameImage;

    QMutex _convertLock;

	AVCodecContext *ctxVideo {0};
    AVStream *videoSt {0};
    bool _firstFrameSent {false};

    double _timePassed {0.0};
    double _videoClock {0.0};

    int drop_count {0};
    bool last_dropped {false};
    double last_drop_pts {0.0};


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

class VpuMainThread: public QThread
{
    Q_OBJECT
public:
    VpuMainThread(const QString& name);
    ~VpuMainThread();

    //return if video stream of the file can be hardware decoded
    bool isHardwareSupported();

    // get referencing clock (right now is audio clock)
    double getClock();
    VideoPacketQueue& frames();

    AudioDecoder* audioThread() { return _audioThread; }
    VpuDecoder* videoThread() { return _videoThread; }

    void seekForward(int secs);
    void seekBackward(int secs);

    int64_t duration() const { return _duration; }
    int64_t elapsed() const { return _elapsed; }

public slots:
    void stop(); 

signals:
    void elapsedChanged();
    void volumeChanged();
    void muteChanged();

protected:
	AVFormatContext *ic {0};
	AVCodecContext *ctxVideo {0};
	AVCodecContext *ctxAudio {0};
	AVCodecContext *ctxSubtitle {0};
	int idxVideo {-1};
    int idxAudio {-1};
    int idxSubtitle {-1};

    int64_t _duration {0};
    int64_t _elapsed {0};

    QAtomicInt _seekPending {0};
    int64_t _seekPos {0};
    int _seekFlags {0};
    double _lastSeekTime {0.0};

    QMutex _lock;

    AudioDecoder *_audioThread {0};
    VpuDecoder *_videoThread {0};
    QAtomicInt _quitFlags {0};

    QFileInfo _fileInfo;

    void run() override;
    void close();
    int openMediaFile();
};

}

#endif /* ifndef _DMR_VPU_DECODER_H */

