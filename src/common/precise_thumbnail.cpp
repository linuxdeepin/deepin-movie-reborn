// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "precise_thumbnail.h"

#include <QImage>
#include <QFileInfo>
#include <QDebug>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace dmr {

QPixmap PreciseThumbnail::generate(const QUrl &url, int secs, const QSize &thumbSize, qreal dpr)
{
    qInfo() << "PreciseThumbnail::generate: url =" << url << "secs =" << secs;

    QPixmap pm;
    pm.setDevicePixelRatio(dpr);

    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();

    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, file.toUtf8().constData(), nullptr, nullptr) != 0) {
        qWarning() << "PreciseThumbnail: failed to open input" << file;
        return pm;
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        qWarning() << "PreciseThumbnail: failed to find stream info";
        avformat_close_input(&fmtCtx);
        return pm;
    }

    int videoStream = -1;
    for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = static_cast<int>(i);
            break;
        }
    }
    if (videoStream < 0) {
        qWarning() << "PreciseThumbnail: no video stream found";
        avformat_close_input(&fmtCtx);
        return pm;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(nullptr);
    if (!codecCtx) {
        avformat_close_input(&fmtCtx);
        return pm;
    }
    avcodec_parameters_to_context(codecCtx, fmtCtx->streams[videoStream]->codecpar);

    const AVCodec *codec = avcodec_find_decoder(codecCtx->codec_id);
    if (!codec || avcodec_open2(codecCtx, codec, nullptr) < 0) {
        qWarning() << "PreciseThumbnail: failed to open codec";
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return pm;
    }

    int64_t seekTarget = static_cast<int64_t>(secs) * AV_TIME_BASE;
    if (av_seek_frame(fmtCtx, -1, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
        qWarning() << "PreciseThumbnail: seek failed for" << secs << "seconds";
    }

    // Flush decoder state after seek to discard stale frames
    avcodec_flush_buffers(codecCtx);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool found = false;
    int64_t targetUs = static_cast<int64_t>(secs) * AV_TIME_BASE;
    AVRational timeBase = fmtCtx->streams[videoStream]->time_base;
    AVRational avTimeBase = {1, AV_TIME_BASE};

    // Safety limit: cap the number of decoded frames to avoid
    // excessive reads when seek silently fails or video structure is unusual
    const int maxFrames = 1000;
    int frameCount = 0;

    while (!found && frameCount < maxFrames && av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == videoStream) {
            int ret = avcodec_send_packet(codecCtx, pkt);
            while (ret >= 0) {
                ret = avcodec_receive_frame(codecCtx, frame);
                if (ret < 0) {
                    break;
                }
                frameCount++;

                // Handle missing PTS: fall back to pkt_dts, or skip frame
                int64_t pts = frame->pts;
                if (pts == AV_NOPTS_VALUE) {
                    pts = frame->pkt_dts;
                }
                if (pts == AV_NOPTS_VALUE) {
                    // Cannot determine timestamp, skip this frame
                    continue;
                }

                int64_t frameUs = av_rescale_q(pts, timeBase, avTimeBase);
                if (frameUs >= targetUs) {
                    found = true;
                    break;
                }
            }
        }
        av_packet_unref(pkt);
    }

    if (found) {
        int width = codecCtx->width;
        int height = codecCtx->height;
        struct SwsContext *swsCtx = sws_getContext(
            width, height, codecCtx->pix_fmt,
            width, height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (swsCtx) {
            uint8_t *rgbBuf = static_cast<uint8_t *>(
                av_malloc(static_cast<size_t>(width) * height * 3));
            if (rgbBuf) {
                uint8_t *rgbData[1] = {rgbBuf};
                int rgbLinesize[1] = {width * 3};
                sws_scale(swsCtx, frame->data, frame->linesize, 0, height,
                          rgbData, rgbLinesize);

                QImage img(rgbBuf, width, height, width * 3, QImage::Format_RGB888);
                pm = QPixmap::fromImage(img.scaled(thumbSize * dpr,
                    Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                pm.setDevicePixelRatio(dpr);

                av_free(rgbBuf);
            } else {
                qWarning() << "PreciseThumbnail: av_malloc returned NULL for rgb buffer";
            }
            sws_freeContext(swsCtx);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    qDebug() << "PreciseThumbnail: done, found =" << found
             << "frames decoded =" << frameCount
             << "pm isNull =" << pm.isNull();
    return pm;
}

}
