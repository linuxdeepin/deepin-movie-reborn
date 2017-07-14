#include "config.h"

#include "vpu_decoder.h"
#include "options.h"

#include "vpuapi.h"
#include "regdefine.h"
#include "vpuhelper.h"
#include "vdi/vdi.h"
#include "vdi/vdi_osal.h"
#include "vpuio.h"
#include "vpuapifunc.h"
#include <galUtil.h>
#include <stdio.h>
#include <memory.h>

#ifdef SUPPORT_FFMPEG_DEMUX 
#if defined (__cplusplus)
extern "C" {
#endif

#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavresample/avresample.h>


#if defined (__cplusplus)
}
#endif
#endif

#include <time.h>

static double _audioClock  = 0.0;

#define Q2C(qstr) ((qstr).toUtf8().constData())

enum {
        STD_AVC_DEC = 0,
        STD_VC1_DEC,
        STD_MP2_DEC,
        STD_MP4_DEC,
        STD_H263_DEC,
        STD_DV3_DEC,
        STD_RVx_DEC,
        STD_AVS_DEC,
        STD_THO_DEC,
        STD_VP3_DEC,
        STD_VP8_DEC,
        STD_MP4_ENC = 12,
        STD_H263_ENC,
        STD_AVC_ENC
};


//#define ENC_SOURCE_FRAME_DISPLAY
#define ENC_RECON_FRAME_DISPLAY
#define VPU_ENC_TIMEOUT       5000 
#define VPU_DEC_TIMEOUT       5000
//#define VPU_WAIT_TIME_OUT	10		//should be less than normal decoding time to give a chance to fill stream. if this value happens some problem. we should fix VPU_WaitInterrupt function
#define VPU_WAIT_TIME_OUT       1000
//#define PARALLEL_VPU_WAIT_TIME_OUT 1 	//the value of timeout is 1 means we want to keep a waiting time to give a chance of an interrupt of the next core.
#define PARALLEL_VPU_WAIT_TIME_OUT 0 	//the value of timeout is 0 means we just check interrupt flag. do not wait any time to give a chance of an interrupt of the next core.
#if PARALLEL_VPU_WAIT_TIME_OUT > 0 
#undef VPU_DEC_TIMEOUT
#define VPU_DEC_TIMEOUT       1000
#endif


#define MAX_CHUNK_HEADER_SIZE 1024
#define MAX_DYNAMIC_BUFCOUNT	3
#define NUM_FRAME_BUF			19
#define MAX_ROT_BUF_NUM			2
#define EXTRA_FRAME_BUFFER_NUM	1

#define ENC_SRC_BUF_NUM			2
#define STREAM_BUF_SIZE		 0x300000  // max bitstream size
//#define STREAM_BUF_SIZE                0x100000

//#define STREAM_FILL_SIZE    (512 * 16)  //  4 * 1024 | 512 | 512+256( wrap around test )
#define STREAM_FILL_SIZE    0x2000  //  4 * 1024 | 512 | 512+256( wrap around test )

#define STREAM_END_SIZE			0
#define STREAM_END_SET_FLAG		0
#define STREAM_END_CLEAR_FLAG	-1
#define STREAM_READ_SIZE    (512 * 16)

#define HAVE_HW_MIXER

#define FORCE_SET_VSYNC_FLAG
//#define TEST_USER_FRAME_BUFFER

#ifdef TEST_USER_FRAME_BUFFER
#define TEST_MULTIPLE_CALL_REGISTER_FRAME_BUFFER
#endif

static unsigned long rpcc()
{
        unsigned long result;
        asm volatile ("rtc %0" : "=r"(result));
        return result;
} 

typedef enum {
    YUV444, YUV422, YUV420, NV12, NV21, YUV400, YUYV, YVYU, UYVY, VYUY, YYY, RGB_PLANAR, RGB32, RGB24, RGB16, YUV2RGB_COLOR_FORMAT_MAX 
} yuv2rgb_color_format;

// inteleave : 0 (chroma separate mode), 1 (cbcr interleave mode), 2 (crcb interleave mode)
static yuv2rgb_color_format convert_vpuapi_format_to_yuv2rgb_color_format(int yuv_format, int interleave) 
{
	//typedef enum { YUV444, YUV422, YUV420, NV12, NV21,  YUV400, YUYV, YVYU, UYVY, VYUY, YYY, RGB_PLANAR, RGB32, RGB24, RGB16 } yuv2rgb_color_format;
	yuv2rgb_color_format format;

	switch(yuv_format)
	{
	case FORMAT_400: format = YUV400; break;
	case FORMAT_444: format = YUV444; break;
	case FORMAT_224:
	case FORMAT_422: format = YUV422; break;
	case FORMAT_420: 
		if (interleave == 0)
			format = YUV420; 
		else if (interleave == 1)
			format = NV12;				
		else
			format = NV21; 
		break;
	default:
		format = YUV2RGB_COLOR_FORMAT_MAX; 
	}

	return format;
}

//software convert
static void vpu_yuv2rgb(int width, int height, yuv2rgb_color_format format,
        unsigned char *src, unsigned char *rgba, int cbcr_reverse)
{
#define vpu_clip(var) ((var>=255)?255:(var<=0)?0:var)
	int j, i;
	int c, d, e;

	unsigned char* line = rgba;
	unsigned char* cur;
	unsigned char* y = NULL;
	unsigned char* u = NULL;
	unsigned char* v = NULL;
	unsigned char* misc = NULL;

	int frame_size_y;
	int frame_size_uv;
	int frame_size;
	int t_width;

	frame_size_y = width*height;

	if( format == YUV444 || format == RGB_PLANAR)
		frame_size_uv = width*height;
	else if( format == YUV422 )
		frame_size_uv = (width*height)>>1;
	else if( format == YUV420 || format == NV12 || format == NV21 )
		frame_size_uv = (width*height)>>2;
	else 
		frame_size_uv = 0;

	if( format == YUYV || format == YVYU  || format == UYVY  || format == VYUY )
		frame_size = frame_size_y*2;
	else if( format == RGB32 )
		frame_size = frame_size_y*4;
	else if( format == RGB24 )
		frame_size = frame_size_y*3;
	else if( format == RGB16 )
		frame_size = frame_size_y*2;
	else
		frame_size = frame_size_y + frame_size_uv*2; 

	t_width = width;


	if( format == YUYV || format == YVYU  || format == UYVY  || format == VYUY ) {
		misc = src;
	}
	else if( format == NV12 || format == NV21) {	
		y = src;
		misc = src + frame_size_y;
	}
	else if( format == RGB32 || format == RGB24 || format == RGB16 ) {
		misc = src;
	}
	else {
		y = src;
		u = src + frame_size_y;
		v = src + frame_size_y + frame_size_uv;		
	}

	if( format == YUV444 ){

		for( j = 0 ; j < height ; j++ ){
			cur = line;
			for( i = 0 ; i < width ; i++ ){
				c = y[j*width+i] - 16;
				d = u[j*width+i] - 128;
				e = v[j*width+i] - 128;

				if (!cbcr_reverse) {
					d = u[j*width+i] - 128;
					e = v[j*width+i] - 128;
				} else {
					e = u[j*width+i] - 128;
					e = v[j*width+i] - 128;
				}
				(*cur) = vpu_clip(( 298 * c           + 409 * e + 128) >> 8);cur++;
				(*cur) = vpu_clip(( 298 * c - 100 * d - 208 * e + 128) >> 8);cur++;
				(*cur) = vpu_clip(( 298 * c + 516 * d           + 128) >> 8);cur++;
				(*cur) = 0; cur++;
			}
			line += t_width<<2;
		}
	}
	else if( format == YUV422){
		for( j = 0 ; j < height ; j++ ){
			cur = line;
			for( i = 0 ; i < width ; i++ ){
				c = y[j*width+i] - 16;
				d = u[j*(width>>1)+(i>>1)] - 128;
				e = v[j*(width>>1)+(i>>1)] - 128;

				if (!cbcr_reverse) {
					d = u[j*(width>>1)+(i>>1)] - 128;
					e = v[j*(width>>1)+(i>>1)] - 128;
				} else {
					e = u[j*(width>>1)+(i>>1)] - 128;
					d = v[j*(width>>1)+(i>>1)] - 128;
				}

				(*cur) = vpu_clip(( 298 * c           + 409 * e + 128) >> 8);cur++;
				(*cur) = vpu_clip(( 298 * c - 100 * d - 208 * e + 128) >> 8);cur++;
				(*cur) = vpu_clip(( 298 * c + 516 * d           + 128) >> 8);cur++;
				(*cur) = 0; cur++;
			}
			line += t_width<<2;
		}
	}
	else if( format == YUYV || format == YVYU  || format == UYVY  || format == VYUY )
	{
		unsigned char* t = misc;
		for( j = 0 ; j < height ; j++ ){
			cur = line;
			for( i = 0 ; i < width ; i+=2 ){
				switch( format) {
				case YUYV:
					c = *(t  ) - 16;
					if (!cbcr_reverse) {
						d = *(t+1) - 128;
						e = *(t+3) - 128;
					} else {
						e = *(t+1) - 128;
						d = *(t+3) - 128;
					}
					break;
				case YVYU:
					c = *(t  ) - 16;
					if (!cbcr_reverse) {
						d = *(t+3) - 128;
						e = *(t+1) - 128;
					} else {
						e = *(t+3) - 128;
						d = *(t+1) - 128;
					}
					break;
				case UYVY:
					c = *(t+1) - 16;
					if (!cbcr_reverse) {
						d = *(t  ) - 128;
						e = *(t+2) - 128;
					} else {
						e = *(t  ) - 128;
						d = *(t+2) - 128;
					}
					break;
				case VYUY:
					c = *(t+1) - 16;
					if (!cbcr_reverse) {
						d = *(t+2) - 128;
						e = *(t  ) - 128;
					} else {
						e = *(t+2) - 128;
						d = *(t  ) - 128;
					}
					break;
				default: // like YUYV
					c = *(t  ) - 16;
					if (!cbcr_reverse) {
						d = *(t+1) - 128;
						e = *(t+3) - 128;
					} else {
						e = *(t+1) - 128;
						d = *(t+3) - 128;
					}
					break;
				}

				(*cur) = vpu_clip(( 298 * c           + 409 * e + 128) >> 8);cur++;
				(*cur) = vpu_clip(( 298 * c - 100 * d - 208 * e + 128) >> 8);cur++;
				(*cur) = vpu_clip(( 298 * c + 516 * d           + 128) >> 8);cur++;
				(*cur) = 0;cur++;

				switch( format) {
				case YUYV:
				case YVYU:
					c = *(t+2) - 16;
					break;

				case VYUY:
				case UYVY:
					c = *(t+3) - 16;
					break;
				default: // like YUYV
					c = *(t+2) - 16;
					break;
				}

				(*cur) = vpu_clip(( 298 * c           + 409 * e + 128) >> 8);cur++;
				(*cur) = vpu_clip(( 298 * c - 100 * d - 208 * e + 128) >> 8);cur++;
				(*cur) = vpu_clip(( 298 * c + 516 * d           + 128) >> 8);cur++;
				(*cur) = 0; cur++;

				t += 4;
			}
			line += t_width<<2;
		}
	}
	else if( format == YUV420 || format == NV12 || format == NV21){
		for( j = 0 ; j < height ; j++ ){
			cur = line;
			for( i = 0 ; i < width ; i++ ){
				c = y[j*width+i] - 16;
				if (format == YUV420) {
					if (!cbcr_reverse) {
						d = u[(j>>1)*(width>>1)+(i>>1)] - 128;
						e = v[(j>>1)*(width>>1)+(i>>1)] - 128;					
					} else {
						e = u[(j>>1)*(width>>1)+(i>>1)] - 128;
						d = v[(j>>1)*(width>>1)+(i>>1)] - 128;	
					}
				}
				else if (format == NV12) {
					if (!cbcr_reverse) {
						d = misc[(j>>1)*width+(i>>1<<1)  ] - 128;
						e = misc[(j>>1)*width+(i>>1<<1)+1] - 128;					
					} else {
						e = misc[(j>>1)*width+(i>>1<<1)  ] - 128;
						d = misc[(j>>1)*width+(i>>1<<1)+1] - 128;	
					}
				}
				else { // if (m_color == NV21)
					if (!cbcr_reverse) {
						d = misc[(j>>1)*width+(i>>1<<1)+1] - 128;
						e = misc[(j>>1)*width+(i>>1<<1)  ] - 128;					
					} else {
						e = misc[(j>>1)*width+(i>>1<<1)+1] - 128;
						d = misc[(j>>1)*width+(i>>1<<1)  ] - 128;		
					}
				}
				(*cur) = vpu_clip(( 298 * c           + 409 * e + 128) >> 8);cur++;
				(*cur) = vpu_clip(( 298 * c - 100 * d - 208 * e + 128) >> 8);cur++;
				(*cur) = vpu_clip(( 298 * c + 516 * d           + 128) >> 8);cur++;
				(*cur) = 0; cur++;
			}
			line += t_width<<2;
		}
	}
	else if( format == RGB_PLANAR ){
		for( j = 0 ; j < height ; j++ ){
			cur = line;
			for( i = 0 ; i < width ; i++ ){
				(*cur) = y[j*width+i];cur++;
				(*cur) = u[j*width+i];cur++;
				(*cur) = v[j*width+i];cur++;
				(*cur) = 0; cur++;
			}
			line += t_width<<2;
		}
	}
	else if( format == RGB32 ){
		for( j = 0 ; j < height ; j++ ){
			cur = line;
			for( i = 0 ; i < width ; i++ ){
				(*cur) = misc[j*width*4+i];cur++;	// R
				(*cur) = misc[j*width*4+i+1];cur++;	// G
				(*cur) = misc[j*width*4+i+2];cur++;	// B
				(*cur) = misc[j*width*4+i+3];cur++;	// A
			}
			line += t_width<<2;
		}
	}
	else if( format == RGB24 ){
		for( j = 0 ; j < height ; j++ ){
			cur = line;
			for( i = 0 ; i < width ; i++ ){
				(*cur) = misc[j*width*3+i];cur++;	// R
				(*cur) = misc[j*width*3+i+1];cur++;	// G
				(*cur) = misc[j*width*3+i+2];cur++;	// B
				(*cur) = 0; cur++;
			}
			line += t_width<<2;
		}
	}
	else if( format == RGB16 ){
		for( j = 0 ; j < height ; j++ ){
			cur = line;
			for( i = 0 ; i < width ; i++ ){
				int tmp = misc[j*width*2+i]<<8 | misc[j*width*2+i+1];
				(*cur) = ((tmp>>11)&0x1F<<3);cur++; // R(5bit)
				(*cur) = ((tmp>>5 )&0x3F<<2);cur++; // G(6bit)
				(*cur) = ((tmp    )&0x1F<<3);cur++; // B(5bit)
				(*cur) = 0; cur++;
			}
			line += t_width<<2;
		}
	}
	else { // YYY
		for( j = 0 ; j < height ; j++ ){
			cur = line;
			for( i = 0 ; i < width ; i++ ){
				(*cur) = y[j*width+i]; cur++;
				(*cur) = y[j*width+i]; cur++;
				(*cur) = y[j*width+i]; cur++;
				(*cur) = 0; cur++;
			}
			line += t_width<<2;
		}	
	}
}


namespace dmr {

static AVPacketQueue audioPackets;
static AVPacketQueue videoPackets;

static VideoPacketQueue videoFrames;


struct GALSurface {
    gcoSURF                 surf {0};
    gceSURF_FORMAT          format {0};
    gctUINT                 width {0};
    gctUINT                 height {0};
    gctINT                  stride {0};
    gctUINT32               phyAddr[3];
    gctPOINTER              lgcAddr[3];

    static GALSurface* create(gcoHAL hal, int w, int h, gceSURF_FORMAT fmt)
    {
        GALSurface *s = new GALSurface;
        memset(s->phyAddr, 0, sizeof(s->phyAddr));
        memset(s->lgcAddr, 0, sizeof(s->lgcAddr));

        gcoSURF surf = gcvNULL;
        gceSTATUS status;
        status = gcoSURF_Construct(hal, w, h, 1, gcvSURF_BITMAP, fmt, gcvPOOL_DEFAULT, &surf);
        if (status < 0) {
            fprintf(stderr, "*ERROR* Failed to construct SURFACE object (status = %d)\n", status);
            return NULL;
        }
        s->surf = surf;

        gctUINT aligned_w, aligned_h;
        gcmVERIFY_OK(gcoSURF_GetAlignedSize(s->surf, &aligned_w, &aligned_h, &s->stride)); 
        if (w != aligned_w || h != aligned_h) {
            fprintf(stderr, "gcoSURF width and height is not aligned !\n");
            s->surf = NULL;
            gcoSURF_Destroy(surf);
            delete s;
            return NULL;
        }
        
        gcmVERIFY_OK(gcoSURF_GetSize(s->surf, &s->width, &s->height, gcvNULL)); 
        gcmVERIFY_OK(gcoSURF_GetFormat(s->surf, gcvNULL, &s->format));

        fprintf(stderr, "create surface w %d stride %d  h %d\n", s->width, s->stride, s->height);
        return s;
    }

    void copyFromFb(int width, int height, FrameBuffer fb)
    {
        Q_ASSERT (_locked);

        if (this->format == gcvSURF_I420)  {
            unsigned long yaddr = fb.bufY;
            unsigned long cbaddr = fb.bufCb;
            unsigned long craddr = fb.bufCr;

            //fprintf(stderr, "%s: (%x, %x, %x) -> (%x, %x, %x)\n", __func__,
                    //phyAddr[0], phyAddr[1], phyAddr[2],
                    //yaddr, cbaddr, craddr);

            dma_copy_in_vmem(phyAddr[0], (gctUINT32)yaddr, width*height);
            dma_copy_in_vmem(phyAddr[1], (gctUINT32)cbaddr, width*height/4);
            dma_copy_in_vmem(phyAddr[2], (gctUINT32)craddr, width*height/4);
        }
    }

    void lock()
    {
        gceSTATUS status;
        if (!_locked) {
            gcmVERIFY_OK(gcoSURF_Lock(surf, phyAddr, lgcAddr));
            //fprintf(stderr, "%s: %s %x %x\n", __func__, "phy", phyAddr[0], lgcAddr[0]);
            //fprintf(stderr, "%s: %s %x %x\n", __func__, "phy", phyAddr[1], lgcAddr[1]);
            //fprintf(stderr, "%s: %s %x %x\n", __func__, "phy", phyAddr[2], lgcAddr[2]);
        }
    }

    void unlock()
    {
        gceSTATUS status;
        if (_locked && lgcAddr[0]) {
            gcmVERIFY_OK(gcoSURF_Unlock(surf, lgcAddr[0]));
            memset(lgcAddr, 0, sizeof(lgcAddr));
            memset(phyAddr, 0, sizeof(phyAddr));
            _locked = false;
        }
    }

    ~GALSurface() 
    {
        if (surf) {
            unlock();

            gceSTATUS status;
            if (gcmIS_ERROR(gcoSURF_Destroy(surf))) {
                fprintf(stderr, "Destroy Surf failed:%#x\n", status);
            }
        }
    }

private:
    bool _locked {false};

};

class SurfaceScopedLock
{
public:
    GALSurface *s;
    SurfaceScopedLock(GALSurface *s): s{s} {
        s->lock();
    }

    ~SurfaceScopedLock() {
        s->unlock();
    }
};

class GALConverter: public QObject 
{
public:
    GALConverter() 
    {
        if (!init()) {
            fprintf(stderr, "GALConverter init failed\n");
        }
    }

    ~GALConverter()
    {
        if (g_hal != gcvNULL) {
            gcoHAL_Commit(g_hal, gcvTRUE);
        }

        if (_dstSurf) delete _dstSurf;
        if (_srcSurf) delete _srcSurf;

        if (g_Contiguous != gcvNULL) {
            /* Unmap the contiguous memory. */
            gcmVERIFY_OK(gcoHAL_UnmapMemory(g_hal,
                        g_ContiguousPhysical, g_ContiguousSize,
                        g_Contiguous));
        }

        if (g_hal != gcvNULL) {
            gcoHAL_Commit(g_hal, gcvTRUE);
            gcoHAL_Destroy(g_hal);
            g_hal = NULL;
        }

        if (g_os != gcvNULL) {
            gcoOS_Destroy(g_os);
            g_os = NULL;
        }
    }

    bool init() 
    {
        if (_init) return true;

        gceSTATUS status;

        /* Construct the gcoOS object. */
        status = gcoOS_Construct(gcvNULL, &g_os);
        if (status < 0) {
            fprintf(stderr, "*ERROR* Failed to construct OS object (status = %d)\n", status);
            return gcvFALSE;
        }

        /* Construct the gcoHAL object. */
        status = gcoHAL_Construct(gcvNULL, g_os, &g_hal);
        if (status < 0) {
            fprintf(stderr, "*ERROR* Failed to construct GAL object (status = %d)\n", status);
            return gcvFALSE;
        }


        status = gcoHAL_QueryVideoMemory(g_hal,
                NULL, NULL,
                NULL, NULL,
                &g_ContiguousPhysical, &g_ContiguousSize);
        if (gcmIS_ERROR(status)) {
            fprintf(stderr, "gcoHAL_QueryVideoMemory failed %d.", status);
            return gcvFALSE;
        }


        /* Map the contiguous memory. */
        if (g_ContiguousSize > 0) {
            status = gcoHAL_MapMemory(g_hal, g_ContiguousPhysical, g_ContiguousSize, &g_Contiguous);
            if (gcmIS_ERROR(status)) {
                fprintf(stderr, "gcoHAL_MapMemory failed %d.", status);
                return gcvFALSE;
            }
        }

        status = gcoHAL_Get2DEngine(g_hal, &g_2d);
        if (status < 0) {
            fprintf(stderr, "*ERROR* Failed to get 2D engine object (status = %d)\n", status);
            return gcvFALSE;
        }

        if (!gcoHAL_IsFeatureAvailable(g_hal, gcvFEATURE_YUV420_SCALER)) {
            fprintf(stderr, "YUV420 scaler is not supported.\n");
            return gcvFALSE;
        }


        fprintf(stderr, "%s\n", __func__);
        _init = true;
        return gcvTRUE;
    }

    gctBOOL fake_convert()
    {
        gctUINT8 horKernel = 1, verKernel = 1;
        gcsRECT srcRect;
        gceSTATUS status;
        gcsRECT dstRect = {0, 0, _dstSurf->width, _dstSurf->height};

        srcRect.left = 0;
        srcRect.top = 0;
        srcRect.right = _srcSurf->width;
        srcRect.bottom = _srcSurf->height;

        fprintf(stderr, "%s: (%d, %d, %d, %d) dst (%d, %d, %d, %d)\n", __func__,
                dstRect.left, dstRect.top, dstRect.right, dstRect.bottom,
                srcRect.left, srcRect.top, srcRect.right, srcRect.bottom);

        {
            SurfaceScopedLock dstLock(_dstSurf);
            SurfaceScopedLock scoped(_srcSurf);

            // set clippint rect
            gcmONERROR(gco2D_SetClipping(g_2d, &dstRect));
            gcmONERROR(gcoSURF_SetDither(_dstSurf->surf, gcvTRUE));

            // set kernel size
            status = gco2D_SetKernelSize(g_2d, horKernel, verKernel);
            if (status != gcvSTATUS_OK) {
                fprintf(stderr, "2D set kernel size failed:%#x\n", status);
                return gcvFALSE;
            }

            status = gco2D_EnableDither(g_2d, gcvTRUE);
            if (status != gcvSTATUS_OK) {
                fprintf(stderr, "enable gco2D_EnableDither failed:%#x\n", status);
                return gcvFALSE;
            }

            status = gcoSURF_FilterBlit(_srcSurf->surf, _dstSurf->surf, &srcRect, &dstRect, &dstRect);
            if (status != gcvSTATUS_OK) {
                fprintf(stderr, "2D FilterBlit failed:%#x\n", status);
                return gcvFALSE;
            }

            status = gco2D_EnableDither(g_2d, gcvFALSE);
            if (status != gcvSTATUS_OK) {
                fprintf(stderr, "disable gco2D_EnableDither failed:%#x\n", status);
                return gcvFALSE;
            }

            //gcmONERROR(gco2D_Flush(g_2d));
            fprintf(stderr, "%s: flushed\n", __func__);
            gcmONERROR(gcoHAL_Commit(g_hal, gcvTRUE));
            fprintf(stderr, "%s: commit done\n", __func__);
        }

        return gcvTRUE;

OnError:
        fprintf(stderr, "%s: convert failed\n", __func__);
        return gcvFALSE;
    }

    //FIXME: what if original format is not I420
    gctBOOL convertYUV2RGBScaled(const FrameBuffer& fb)
    {
        gctUINT8 horKernel = 1, verKernel = 1;
        gcsRECT srcRect;
        gceSTATUS status;
        gcsRECT dstRect = {0, 0, _dstSurf->width, _dstSurf->height};

        srcRect.left = 0;
        srcRect.top = 0;
        srcRect.right = _srcSurf->width;
        srcRect.bottom = _srcSurf->height;

        fprintf(stderr, "%s: (%d, %d, %d, %d) dst (%d, %d, %d, %d)\n", __func__,
                dstRect.left, dstRect.top, dstRect.right, dstRect.bottom,
                srcRect.left, srcRect.top, srcRect.right, srcRect.bottom);

        SurfaceScopedLock dstLock(_dstSurf);

        SurfaceScopedLock scoped(_srcSurf);
        _srcSurf->copyFromFb(fb.stride, fb.height, fb);


        // set clippint rect
        gcmONERROR(gco2D_SetClipping(g_2d, &dstRect));
        gcmONERROR(gcoSURF_SetDither(_dstSurf->surf, gcvTRUE));

        // set kernel size
        status = gco2D_SetKernelSize(g_2d, horKernel, verKernel);
        if (status != gcvSTATUS_OK) {
            fprintf(stderr, "2D set kernel size failed:%#x\n", status);
            return gcvFALSE;
        }

        status = gco2D_EnableDither(g_2d, gcvTRUE);
        if (status != gcvSTATUS_OK) {
            fprintf(stderr, "enable gco2D_EnableDither failed:%#x\n", status);
            return gcvFALSE;
        }

        status = gcoSURF_FilterBlit(_srcSurf->surf, _dstSurf->surf, &srcRect, &dstRect, &dstRect);
        if (status != gcvSTATUS_OK) {
            fprintf(stderr, "2D FilterBlit failed:%#x\n", status);
            return gcvFALSE;
        }

        status = gco2D_EnableDither(g_2d, gcvFALSE);
        if (status != gcvSTATUS_OK) {
            fprintf(stderr, "disable gco2D_EnableDither failed:%#x\n", status);
            return gcvFALSE;
        }

        //gcmONERROR(gco2D_Flush(g_2d));
        gcmONERROR(gcoHAL_Commit(g_hal, gcvTRUE));
        fprintf(stderr, "%s: commit done\n", __func__);

        return gcvTRUE;

OnError:
        fprintf(stderr, "%s: convert failed\n", __func__);
        return gcvFALSE;
    }

    bool updateDestSurface(int w, int h)
    {
        if (_dstSurf == nullptr) {
            _dstSurf = GALSurface::create(g_hal, w, h, gcvSURF_A8R8G8B8);
        } else {
            //update
        }
    }

    bool updateSrcSurface(int w, int h)
    {
        if (_srcSurf == nullptr) {
            _srcSurf = GALSurface::create(g_hal, w, h, gcvSURF_I420);
        } else {
            //update
        }
    }

    void copyRGBData(uchar* bits, gctUINT stride, gctUINT height)
    {
        SurfaceScopedLock lock(_dstSurf);
        fprintf(stderr, "%s copy (%d, %d)\n", __func__, _dstSurf->stride, _dstSurf->height);
        if (stride == _dstSurf->stride) {
            gctUINT h = min(height, _dstSurf->height);
            dma_copy_from_vmem(bits, _dstSurf->phyAddr[0], stride * h);
        } else {
            fprintf(stderr, "%s: unmatched stride\n", __func__);
            //TODO:
        }
    }


public:
    bool _init {false};
    GALSurface *_dstSurf {0};
    GALSurface *_srcSurf {0};

    gcoOS       g_os {gcvNULL};
    gcoHAL      g_hal{gcvNULL};
    gco2D       g_2d {gcvNULL};

    gctPHYS_ADDR g_ContiguousPhysical;
    gctSIZE_T    g_ContiguousSize;
    gctPOINTER   g_Contiguous;

};

static GALConverter *galConverter = NULL;


VpuDecoder::VpuDecoder(AVStream *st, AVCodecContext *ctx)
    :QThread(0), videoSt(st), ctxVideo(ctx)
{
    init();
}

VpuDecoder::~VpuDecoder() 
{
    delete galConverter;
    fprintf(stderr, "%s: release gal\n", __func__);
}

void VpuDecoder::run() 
{
    loop();
    fprintf(stderr, "%s: decoder quit\n", __func__);
}

bool VpuDecoder::init()
{
    InitLog();

    memset(&decConfig, 0x00, sizeof( decConfig) );
    decConfig.coreIdx = 0;


    //printf("Enter Bitstream Mode(0: Interrupt mode, 1: Rollback mode, 2: PicEnd mode): ");
    decConfig.bitstreamMode = 0;

    decConfig.maxWidth = 0;
    decConfig.maxHeight = 0;
    decConfig.mp4DeblkEnable = 0;
    decConfig.iframeSearchEnable = 0;
    decConfig.skipframeMode = 0; // 1:PB skip, 2:B skip
    decConfig.checkeos = 1;

    fprintf(stderr, "init done\n");

}



// return -1 = error, 0 = continue, 1 = success
int VpuDecoder::seqInit()
{
    if (_seqInited)
        return 1;

	RetCode			ret =  RETCODE_SUCCESS;		
	SecAxiUse		secAxiUse = {0};

    ConfigSeqReport(coreIdx, handle, decOP.bitstreamFormat);
    if (decOP.bitstreamMode == BS_MODE_PIC_END)
    {
        ret = VPU_DecGetInitialInfo(handle, &initialInfo);
        if (ret != RETCODE_SUCCESS) 
        {
            if (ret == RETCODE_MEMORY_ACCESS_VIOLATION)
                PrintMemoryAccessViolationReason(coreIdx, NULL);
            VLOG(ERR, "VPU_DecGetInitialInfo failed Error code is 0x%x \n", ret);
            return -1;
        }
        VPU_ClearInterrupt(coreIdx);
    }
    else
    {
        if((int_reason & (1<<INT_BIT_BIT_BUF_EMPTY)) != (1<<INT_BIT_BIT_BUF_EMPTY))
        {
            ret = VPU_DecIssueSeqInit(handle);
            if (ret != RETCODE_SUCCESS)
            {
                VLOG(ERR, "VPU_DecIssueSeqInit failed Error code is 0x%x \n", ret);
                return -1;
            }
        }
        else
        {
            // After VPU generate the BIT_EMPTY interrupt. HOST should feed the bitstream up to 1024 in case of seq_init
            if (bsfillSize < VPU_GBU_SIZE*2)
                return 0;
                //continue;
        }

        while (1)
        {
            int_reason = VPU_WaitInterrupt(coreIdx, VPU_DEC_TIMEOUT);

            if (int_reason)
                VPU_ClearInterrupt(coreIdx);

            if(int_reason & (1<<INT_BIT_BIT_BUF_EMPTY)) 
                break;


            CheckUserDataInterrupt(coreIdx, handle, 1, decOP.bitstreamFormat, int_reason);

            if (int_reason)
            {
                if (int_reason & (1<<INT_BIT_SEQ_INIT)) 
                {
                    _seqInited = 1;
                    break;
                }
            }
        }

        if(int_reason & (1<<INT_BIT_BIT_BUF_EMPTY) || int_reason == -1) 
        {
            bsfillSize = 0;
            return 0;
            //continue; // go to take next chunk.
        }
        if (_seqInited)
        {
            ret = VPU_DecCompleteSeqInit(handle, &initialInfo);	
            if (ret != RETCODE_SUCCESS)
            {
                if (ret == RETCODE_MEMORY_ACCESS_VIOLATION)
                    PrintMemoryAccessViolationReason(coreIdx, NULL);
                if (initialInfo.seqInitErrReason & (1<<31)) // this case happened only ROLLBACK mode
                    VLOG(ERR, "Not enough header : Parser has to feed right size of a sequence header  \n");
                VLOG(ERR, "VPU_DecCompleteSeqInit failed Error code is 0x%x \n", ret );
                return -1;
            }			
        }
        else
        {
            VLOG(ERR, "VPU_DecGetInitialInfo failed Error code is 0x%x \n", ret);
            return -1;
        }
    }


    SaveSeqReport(coreIdx, handle, &initialInfo, decOP.bitstreamFormat);	

    if (decOP.bitstreamFormat == STD_VP8)		
    {
        // For VP8 frame upsampling infomration
        static const int scale_factor_mul[4] = {1, 5, 5, 2};
        static const int scale_factor_div[4] = {1, 4, 3, 1};
        hScaleFactor = initialInfo.vp8ScaleInfo.hScaleFactor;
        vScaleFactor = initialInfo.vp8ScaleInfo.vScaleFactor;
        scaledWidth = initialInfo.picWidth * scale_factor_mul[hScaleFactor] / scale_factor_div[hScaleFactor];
        scaledHeight = initialInfo.picHeight * scale_factor_mul[vScaleFactor] / scale_factor_div[vScaleFactor];
        framebufWidth = ((scaledWidth+15)&~15);
        if (IsSupportInterlaceMode(decOP.bitstreamFormat, &initialInfo))
            framebufHeight = ((scaledHeight+31)&~31); // framebufheight must be aligned by 31 because of the number of MB height would be odd in each filed picture.
        else
            framebufHeight = ((scaledHeight+15)&~15);

        rotbufWidth = (decConfig.rotAngle == 90 || decConfig.rotAngle == 270) ?
            ((scaledHeight+15)&~15) : ((scaledWidth+15)&~15);
        rotbufHeight = (decConfig.rotAngle == 90 || decConfig.rotAngle == 270) ?
            ((scaledWidth+15)&~15) : ((scaledHeight+15)&~15);				
    }
    else
    {
        if (decConfig.maxWidth)
        {
            if (decConfig.maxWidth < initialInfo.picWidth)
            {
                VLOG(ERR, "maxWidth is too small\n");
                return -1;
            }
            framebufWidth = ((decConfig.maxWidth+15)&~15);
        }
        else
            framebufWidth = ((initialInfo.picWidth+15)&~15);

        if (decConfig.maxHeight)
        {
            if (decConfig.maxHeight < initialInfo.picHeight)
            {
                VLOG(ERR, "maxHeight is too small\n");
                return -1;
            }

            if (IsSupportInterlaceMode(decOP.bitstreamFormat, &initialInfo))
                framebufHeight = ((decConfig.maxHeight+31)&~31); // framebufheight must be aligned by 31 because of the number of MB height would be odd in each filed picture.
            else
                framebufHeight = ((decConfig.maxHeight+15)&~15);
        }
        else
        {
            if (IsSupportInterlaceMode(decOP.bitstreamFormat, &initialInfo))
                framebufHeight = ((initialInfo.picHeight+31)&~31); // framebufheight must be aligned by 31 because of the number of MB height would be odd in each filed picture.
            else
                framebufHeight = ((initialInfo.picHeight+15)&~15);
        }
        rotbufWidth = (decConfig.rotAngle == 90 || decConfig.rotAngle == 270) ? 
            ((initialInfo.picHeight+15)&~15) : ((initialInfo.picWidth+15)&~15);
        rotbufHeight = (decConfig.rotAngle == 90 || decConfig.rotAngle == 270) ? 
            ((initialInfo.picWidth+15)&~15) : ((initialInfo.picHeight+15)&~15);
    }

    rotStride = rotbufWidth;
    framebufStride = framebufWidth;
    framebufFormat = FORMAT_420;	
    framebufSize = VPU_GetFrameBufSize(framebufStride, framebufHeight, mapType, framebufFormat, &dramCfg);

    galConverter = new GALConverter;
    //galConverter->updateDestSurface(850, 600, gcvSURF_A8R8G8B8);
    //galConverter->updateSrcSurface(framebufWidth, framebufHeight);
    //galConverter->fake_convert();

    // the size of pYuv should be aligned 8 byte. because of C&M HPI bus system constraint.
    pYuv = (BYTE*)osal_malloc(framebufSize);
    if (!pYuv) 
    {
        VLOG(ERR, "Fail to allocation memory for display buffer\n");
        return -1;
    }

    secAxiUse.useBitEnable  = USE_BIT_INTERNAL_BUF;
    secAxiUse.useIpEnable   = USE_IP_INTERNAL_BUF;
    secAxiUse.useDbkYEnable = USE_DBKY_INTERNAL_BUF;
    secAxiUse.useDbkCEnable = USE_DBKC_INTERNAL_BUF;
    secAxiUse.useBtpEnable  = USE_BTP_INTERNAL_BUF;
    secAxiUse.useOvlEnable  = USE_OVL_INTERNAL_BUF;

    VPU_DecGiveCommand(handle, SET_SEC_AXI, &secAxiUse);


    regFrameBufCount = initialInfo.minFrameBufferCount + EXTRA_FRAME_BUFFER_NUM;

#ifdef SUPPORT_DEC_RESOLUTION_CHANGE
    decBufInfo.maxDecMbX = framebufWidth/16;
    decBufInfo.maxDecMbY = ((framebufHeight + 31 ) & ~31)/16;
    decBufInfo.maxDecMbNum = decBufInfo.maxDecMbX*decBufInfo.maxDecMbY;
#endif
#if defined(SUPPORT_DEC_SLICE_BUFFER) || defined(SUPPORT_DEC_RESOLUTION_CHANGE)
    // Register frame buffers requested by the decoder.
    ret = VPU_DecRegisterFrameBuffer(handle, NULL, regFrameBufCount, framebufStride, framebufHeight, mapType, &decBufInfo); // frame map type (can be changed before register frame buffer)
#else
    // Register frame buffers requested by the decoder.
    ret = VPU_DecRegisterFrameBuffer(handle, NULL, regFrameBufCount, framebufStride, framebufHeight, mapType); // frame map type (can be changed before register frame buffer)
#endif
    if (ret != RETCODE_SUCCESS) 
    {
        VLOG(ERR, "VPU_DecRegisterFrameBuffer failed Error code is 0x%x \n", ret);
        return -1;
    }

    VPU_DecGiveCommand(handle, GET_TILEDMAP_CONFIG, &mapCfg);

    if (ppuEnable) 
    {
        ppIdx = 0;

        fbAllocInfo.format          = framebufFormat;
        fbAllocInfo.cbcrInterleave  = decOP.cbcrInterleave;
        if (decOP.tiled2LinearEnable)
            fbAllocInfo.mapType = LINEAR_FRAME_MAP;
        else
            fbAllocInfo.mapType = mapType;

        fbAllocInfo.stride  = rotStride;
        fbAllocInfo.height  = rotbufHeight;
        fbAllocInfo.num     = MAX_ROT_BUF_NUM;
        fbAllocInfo.endian  = decOP.frameEndian;
        fbAllocInfo.type    = FB_TYPE_PPU;
        ret = VPU_DecAllocateFrameBuffer(handle, fbAllocInfo, fbPPU);
        if( ret != RETCODE_SUCCESS )
        {
            VLOG(ERR, "VPU_DecAllocateFrameBuffer fail to allocate source frame buffer is 0x%x \n", ret );
            return -1;
        }

        ppIdx = 0;

        if (decConfig.useRot)
        {
            VPU_DecGiveCommand(handle, SET_ROTATION_ANGLE, &(decConfig.rotAngle));
            VPU_DecGiveCommand(handle, SET_MIRROR_DIRECTION, &(decConfig.mirDir));
        }

        VPU_DecGiveCommand(handle, SET_ROTATOR_STRIDE, &rotStride);

    }

    _seqInited = 1;			
    return 1;
}

void VpuDecoder::updateViewportSize(QSize sz)
{
    if (_viewportSize != sz) {
        _viewportSize = sz;
    }
}

bool VpuDecoder::firstFrameStarted()
{
    return _firstFrameSent;
}

// return -1 to quit
int VpuDecoder::sendFrame(AVPacket *pkt)
{
    bool use_gal = CommandLineManager::get().useGAL();

    QImage img;

    if (use_gal) {
        img = QImage(_viewportSize.width(), _viewportSize.height(), QImage::Format_RGB32);

        galConverter->updateDestSurface(_viewportSize.width(), _viewportSize.height());
        galConverter->updateSrcSurface(outputInfo.dispFrame.stride, outputInfo.dispFrame.height);

        galConverter->convertYUV2RGBScaled(outputInfo.dispFrame);
        auto stride = galConverter->_dstSurf->stride;
        galConverter->copyRGBData(img.bits(), img.bytesPerLine(), img.height());

    } else {
        img = QImage(outputInfo.dispFrame.stride, outputInfo.dispFrame.height, 
                QImage::Format_RGB32);
        //sw coversion
        vdi_read_memory(coreIdx, outputInfo.dispFrame.bufY, pYuv, framebufSize, decOP.frameEndian);
        yuv2rgb_color_format color_format = 
            convert_vpuapi_format_to_yuv2rgb_color_format(framebufFormat, 0);
        vpu_yuv2rgb(outputInfo.dispFrame.stride, outputInfo.dispFrame.height,
                color_format, pYuv, img.bits(), 1);
    }

    double pts = 0.0;

    if(pkt->dts != AV_NOPTS_VALUE) {
        pts = pkt->dts;
        //pts = av_frame_get_best_effort_timestamp(pFrame);
    } else {
        pts = 0;
    }
    pts *= av_q2d(videoSt->time_base);
    pts = synchronize_video(NULL, pts);

    VideoFrame vf;
    vf.img = img;
    vf.pts = pts;
    videoFrames.put(vf);

    if (!_firstFrameSent) _firstFrameSent = true;

    //qDebug() << QTime::currentTime().toString("ss.zzz");
    fprintf(stderr, "%s: timestamp %s\n", __func__, QTime::currentTime().toString("ss.zzz").toUtf8().constData());

    auto total = CommandLineManager::get().debugFrameCount();
    if (total > 0 && frameIdx > total) return -1;

#ifdef FORCE_SET_VSYNC_FLAG
    set_VSYNC_flag();
#endif

    return 0;
}

/// build video packet for vpu
int VpuDecoder::buildVideoPacket(AVPacket* pkt)
{
    int size;

    if (!_seqInited && !seqFilled)
    {
        seqHeaderSize = BuildSeqHeader(seqHeader, decOP.bitstreamFormat, videoSt);	// make sequence data as reference file header to support VPU decoder.
        switch(decOP.bitstreamFormat)
        {
        case STD_THO:
        case STD_VP3:
            break;
        default:
            {
                size = WriteBsBufFromBufHelper(coreIdx, handle, &vbStream, seqHeader, seqHeaderSize, decOP.streamEndian);
                if (size < 0)
                {
                    VLOG(ERR, "WriteBsBufFromBufHelper failed Error code is 0x%x \n", size );
                    return -1;
                }
                    
                bsfillSize += size;
            }
            break;
        }
        seqFilled = 1;
    }
		
    // Build and Fill picture Header data which is dedicated for VPU 
    picHeaderSize = BuildPicHeader(picHeader, decOP.bitstreamFormat, videoSt, pkt);
    switch(decOP.bitstreamFormat)
    {
    case STD_THO:
    case STD_VP3:
        break;
    default:
        size = WriteBsBufFromBufHelper(coreIdx, handle, &vbStream, picHeader, picHeaderSize, decOP.streamEndian);
        if (size < 0)
        {
            VLOG(ERR, "WriteBsBufFromBufHelper failed Error code is 0x%x \n", size );
            return -1;
        }	
        bsfillSize += size;
        break;
    }

    switch(decOP.bitstreamFormat)
    {
    case STD_VP3:
    case STD_THO:
        break;
    default:
        {
            if (decOP.bitstreamFormat == STD_RV)
            {
                int cSlice = chunkData[0] + 1;
                int nSlice =  chunkSize - 1 - (cSlice * 8);
                chunkData += (1+(cSlice*8));
                chunkSize = nSlice;
            }

            size = WriteBsBufFromBufHelper(coreIdx, handle, &vbStream, chunkData, chunkSize, decOP.streamEndian);
            if (size <0)
            {
                VLOG(ERR, "WriteBsBufFromBufHelper failed Error code is 0x%x \n", size );
                return -1;
            }

            bsfillSize += size;
        }
        break;
    }		
}

int VpuDecoder::flushVideoBuffer(AVPacket *pkt)
{
	int				decodefinish = 0;
	int				dispDoneIdx = -1;
	Rect		   rcPrevDisp;
	RetCode			ret =  RETCODE_SUCCESS;		

    if((int_reason & (1<<INT_BIT_BIT_BUF_EMPTY)) != (1<<INT_BIT_BIT_BUF_EMPTY) &&
            (int_reason & (1<<INT_BIT_DEC_FIELD)) != (1<<INT_BIT_DEC_FIELD))
    {
        if (ppuEnable) 
        {
            VPU_DecGiveCommand(handle, SET_ROTATOR_OUTPUT, &fbPPU[ppIdx]);

            if (decConfig.useRot)
            {
                VPU_DecGiveCommand(handle, ENABLE_ROTATION, 0);
                VPU_DecGiveCommand(handle, ENABLE_MIRRORING, 0);
            }

            if (decConfig.useDering)
                VPU_DecGiveCommand(handle, ENABLE_DERING, 0);			
        }

        ConfigDecReport(coreIdx, handle, decOP.bitstreamFormat);

        // Start decoding a frame.
        ret = VPU_DecStartOneFrame(handle, &decParam);
        if (ret != RETCODE_SUCCESS) 
        {
            VLOG(ERR,  "VPU_DecStartOneFrame failed Error code is 0x%x \n", ret);
            return -1;
        }
    }
    else
    {
        if(int_reason & (1<<INT_BIT_DEC_FIELD))
        {
            VPU_ClearInterrupt(coreIdx);
            int_reason = 0;
        }
        // After VPU generate the BIT_EMPTY interrupt. HOST should feed the bitstreams than 512 byte.
        if (decOP.bitstreamMode != BS_MODE_PIC_END)
        {
            if (bsfillSize < VPU_GBU_SIZE)
                return 0;// continue;
        }
    }

    while (1)
    {
        if (_quitFlags.load()) 
            break;

        int_reason = VPU_WaitInterrupt(coreIdx, VPU_DEC_TIMEOUT);
        if (int_reason == (Uint32)-1 ) // timeout
        {
            VPU_SWReset(coreIdx, SW_RESET_SAFETY, handle);				
            break;
        }		

        CheckUserDataInterrupt(coreIdx, handle, outputInfo.indexFrameDecoded, decOP.bitstreamFormat, int_reason);
        if(int_reason & (1<<INT_BIT_DEC_FIELD))	
        {
            if (decOP.bitstreamMode == BS_MODE_PIC_END)
            {
                PhysicalAddress rdPtr, wrPtr;
                int room;
                VPU_DecGetBitstreamBuffer(handle, &rdPtr, &wrPtr, &room);
                if (rdPtr-decOP.bitstreamBuffer < (PhysicalAddress)(chunkSize+picHeaderSize+seqHeaderSize-8))	// there is full frame data in chunk data.
                    VPU_DecSetRdPtr(handle, rdPtr, 0);		//set rdPtr to the position of next field data.
                else
                {
                    // do not clear interrupt until feeding next field picture.
                    break;
                }
            }
        }

        if (int_reason)
            VPU_ClearInterrupt(coreIdx);

        if(int_reason & (1<<INT_BIT_BIT_BUF_EMPTY)) 
        {
            if (decOP.bitstreamMode == BS_MODE_PIC_END)
            {
                VLOG(ERR, "Invalid operation is occurred in pic_end mode \n");
                return -1;
            }
            break;
        }


        if (int_reason & (1<<INT_BIT_PIC_RUN)) 
            break;				
    }			

    if(int_reason & (1<<INT_BIT_BIT_BUF_EMPTY)) 
    {
        bsfillSize = 0;
        return 0; // continue; // go to take next chunk.
    }
    if(int_reason & (1<<INT_BIT_DEC_FIELD)) 
    {
        bsfillSize = 0;
        return 0; // continue; // go to take next chunk.
    }


    ret = VPU_DecGetOutputInfo(handle, &outputInfo);
    if (ret != RETCODE_SUCCESS) 
    {
        VLOG(ERR,  "VPU_DecGetOutputInfo failed Error code is 0x%x \n", ret);
        if (ret == RETCODE_MEMORY_ACCESS_VIOLATION)
            PrintMemoryAccessViolationReason(coreIdx, &outputInfo);
        return -1;
    }

    if ((outputInfo.decodingSuccess & 0x01) == 0)
    {
        VLOG(ERR, "VPU_DecGetOutputInfo decode fail framdIdx %d \n", frameIdx);
        VLOG(TRACE, "#%d, indexFrameDisplay %d || picType %d || indexFrameDecoded %d\n", 
            frameIdx, outputInfo.indexFrameDisplay, outputInfo.picType, outputInfo.indexFrameDecoded );
    }		

    VLOG(TRACE, "#%d:%d, indexDisplay %d || picType %d || indexDecoded %d || rdPtr=0x%x || wrPtr=0x%x || chunkSize = %d, consume=%d\n", 
        instIdx, frameIdx, outputInfo.indexFrameDisplay, outputInfo.picType, outputInfo.indexFrameDecoded, outputInfo.rdPtr, outputInfo.wrPtr, chunkSize+picHeaderSize, outputInfo.consumedByte);

    //SaveDecReport(coreIdx, handle, &outputInfo, decOP.bitstreamFormat, ((initialInfo.picWidth+15)&~15)/16);
    if (outputInfo.chunkReuseRequired) // reuse previous chunk. that would be 1 once framebuffer is full.
        reUseChunk = 1;		

    if (outputInfo.indexFrameDisplay == -1)
        decodefinish = 1;


    if (!ppuEnable) 
    {
        if (decodefinish)
            _quitFlags.store(1); // break;

        if (outputInfo.indexFrameDisplay == -3 ||
            outputInfo.indexFrameDisplay == -2 ) // BIT doesn't have picture to be displayed 
        {
            if (check_VSYNC_flag())
            {
                clear_VSYNC_flag();

                if (frame_queue_dequeue(display_queue, &dispDoneIdx) == 0)
                    VPU_DecClrDispFlag(handle, dispDoneIdx);					
            }
#if defined(CNM_FPGA_PLATFORM) && defined(FPGA_LX_330)
#else
            if (outputInfo.indexFrameDecoded == -1)	// VPU did not decode a picture because there is not enough frame buffer to continue decoding
            {
                // if you can't get VSYN interrupt on your sw layer. this point is reasonable line to set VSYN flag.
                // but you need fine tune EXTRA_FRAME_BUFFER_NUM value not decoder to write being display buffer.
                if (frame_queue_count(display_queue) > 0)
                    set_VSYNC_flag();
            }
#endif			
            return 0; // continue;
        }
    }
    else
    {
        if (decodefinish)
        {
            if (decodeIdx ==  0)
                _quitFlags.store(1); // break;
            // if PP feature has been enabled. the last picture is in PP output framebuffer.									
        }

        if (outputInfo.indexFrameDisplay == -3 ||
            outputInfo.indexFrameDisplay == -2 ) // BIT doesn't have picture to be displayed
        {
            if (check_VSYNC_flag())
            {
                clear_VSYNC_flag();

                if (frame_queue_dequeue(display_queue, &dispDoneIdx) == 0)
                    VPU_DecClrDispFlag(handle, dispDoneIdx);					
            }
#if defined(CNM_FPGA_PLATFORM) && defined(FPGA_LX_330)
#else
            if (outputInfo.indexFrameDecoded == -1)	// VPU did not decode a picture because there is not enough frame buffer to continue decoding
            {
                // if you can't get VSYN interrupt on your sw layer. this point is reasonable line to set VSYN flag.
                // but you need fine tuning EXTRA_FRAME_BUFFER_NUM value not decoder to write being display buffer.
                if (frame_queue_count(display_queue) > 0)
                    set_VSYNC_flag();
            }
#endif			
            return 0; // continue;
        }

        if (decodeIdx == 0) // if PP has been enabled, the first picture is saved at next time.
        {
            // save rotated dec width, height to display next decoding time.
            if (outputInfo.indexFrameDisplay >= 0)
                frame_queue_enqueue(display_queue, outputInfo.indexFrameDisplay);
            rcPrevDisp = outputInfo.rcDisplay;
            decodeIdx++;
            return 0; // continue;

        }
    }

    decodeIdx++;

    if (outputInfo.indexFrameDisplay >= 0)
        frame_queue_enqueue(display_queue, outputInfo.indexFrameDisplay);

    {
        if (ppuEnable) 
            ppIdx = (ppIdx+1)%MAX_ROT_BUF_NUM;

        if (sendFrame(pkt) < 0) 
            _quitFlags.store(1); // break;
    }
    
    if (check_VSYNC_flag())
    {
        clear_VSYNC_flag();

        if (frame_queue_dequeue(display_queue, &dispDoneIdx) == 0)
            VPU_DecClrDispFlag(handle, dispDoneIdx);			
    }

    // save rotated dec width, height to display next decoding time.
    rcPrevDisp = outputInfo.rcDisplay;

    if (outputInfo.numOfErrMBs) 
    {
        totalNumofErrMbs += outputInfo.numOfErrMBs;
        VLOG(ERR, "Num of Error Mbs : %d, in Frame : %d \n", outputInfo.numOfErrMBs, frameIdx);
    }

    frameIdx++;

    if (decodefinish)
        _quitFlags.store(1); // break;

    return 0;
}

double VpuDecoder::synchronize_video(AVFrame *src_frame, double pts) 
{

    double frame_delay;

    if(pts != 0) {
        /* if we have pts, set video clock to it */
        _videoClock = pts;
    } else {
        /* if we aren't given a pts, set it to the clock */
        pts = _videoClock;
    }
#if 0
    /* update the video clock */
    frame_delay = av_q2d(ctxVideo->time_base);
    /* if we are repeating a frame, adjust clock accordingly */
    frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
    _videoClock += frame_delay;
#endif
    fprintf(stderr, "%s: pts = %f\n", __func__, pts);
    return pts;
}

int VpuDecoder::loop()
{
    int i = 0;
	RetCode			ret =  RETCODE_SUCCESS;		
	int			    randomAccess = 0, randomAccessPos = 0;
    AVPacket *pkt = 0;

	seqHeader = osal_malloc(ctxVideo->extradata_size+MAX_CHUNK_HEADER_SIZE);	// allocate more buffer to fill the vpu specific header.
	if (!seqHeader) {
		VLOG(ERR, "fail to allocate the seqHeader buffer\n");
		goto ERR_DEC_INIT;
	}
	memset(seqHeader, 0x00, ctxVideo->extradata_size+MAX_CHUNK_HEADER_SIZE);

	picHeader = osal_malloc(MAX_CHUNK_HEADER_SIZE);
	if (!picHeader) {
		VLOG(ERR, "fail to allocate the picHeader buffer\n");
		goto ERR_DEC_INIT;
	}
	memset(picHeader, 0x00, MAX_CHUNK_HEADER_SIZE);

	ret = VPU_Init(coreIdx);
	if (ret != RETCODE_SUCCESS && ret != RETCODE_CALLED_BEFORE) {
		VLOG(ERR, "VPU_Init failed Error code is 0x%x \n", ret );
		goto ERR_DEC_INIT;
	}

	CheckVersion(coreIdx);


	decOP.bitstreamFormat = fourCCToCodStd(ctxVideo->codec_tag);
	if (decOP.bitstreamFormat == -1)
		decOP.bitstreamFormat = codecIdToCodStd(ctxVideo->codec_id);

	if (decOP.bitstreamFormat == -1)
	{
		VLOG(ERR, "can not support video format in VPU tag=%c%c%c%c, codec_id=0x%x \n",
                ctxVideo->codec_tag>>0, ctxVideo->codec_tag>>8, ctxVideo->codec_tag>>16,
                ctxVideo->codec_tag>>24, ctxVideo->codec_id );
		goto ERR_DEC_INIT;
	}

	vbStream.size = STREAM_BUF_SIZE; 
	vbStream.size = ((vbStream.size+1023)&~1023);
	if (vdi_allocate_dma_memory(coreIdx, &vbStream) < 0)
	{
		VLOG(ERR, "fail to allocate bitstream buffer\n" );
		goto ERR_DEC_INIT;
	}


	decOP.bitstreamBuffer = vbStream.phys_addr; 
	decOP.bitstreamBufferSize = vbStream.size;
	decOP.mp4DeblkEnable = 0;

	decOP.mp4Class = fourCCToMp4Class(ctxVideo->codec_tag);
	if (decOP.mp4Class == -1)
        decOP.mp4Class = codecIdToMp4Class(ctxVideo->codec_id);


	decOP.tiled2LinearEnable = 0;
	mapType = LINEAR_FRAME_MAP;

	decOP.cbcrInterleave = CBCR_INTERLEAVE;
	if (mapType == TILED_FRAME_MB_RASTER_MAP ||
		mapType == TILED_FIELD_MB_RASTER_MAP) {
			decOP.cbcrInterleave = 1;
	}
	decOP.bwbEnable = VPU_ENABLE_BWB;
	decOP.frameEndian  = VPU_FRAME_ENDIAN;
	decOP.streamEndian = VPU_STREAM_ENDIAN;
	decOP.bitstreamMode = decConfig.bitstreamMode;

	if (decConfig.useRot || decConfig.useDering || decOP.tiled2LinearEnable) 
		ppuEnable = 1;
	else
		ppuEnable = 0;

	ret = VPU_DecOpen(&handle, &decOP);
	if (ret != RETCODE_SUCCESS) 
	{
		VLOG(ERR, "VPU_DecOpen failed Error code is 0x%x \n", ret );
		goto ERR_DEC_INIT;
	}  	
	ret = VPU_DecGiveCommand(handle, GET_DRAM_CONFIG, &dramCfg);
	if( ret != RETCODE_SUCCESS )
	{
		VLOG(ERR, "VPU_DecGiveCommand[GET_DRAM_CONFIG] failed Error code is 0x%x \n", ret );
		goto ERR_DEC_OPEN;
	}


	seqFilled = 0;
	bsfillSize = 0;
	reUseChunk = 0;
	display_queue = frame_queue_init(MAX_REG_FRAME);
	init_VSYNC_flag();

	while(1) {
        if (_quitFlags.load()) {
            break;
        }

		seqHeaderSize = 0;
		picHeaderSize = 0;

		if (decOP.bitstreamMode == BS_MODE_PIC_END)
		{
			if (reUseChunk)
			{
				reUseChunk = 0;
				goto FLUSH_BUFFER;			
			}
			VPU_DecSetRdPtr(handle, decOP.bitstreamBuffer, 1);	
		}

        pkt = videoPackets.deque();

        //FIXME:
#if 0
        if (err < 0) {
            if (pkt->stream_index == idxVideo)
                chunkIdx++;	

            if (err==AVERROR_EOF || url_feof(ic->pb)) {
                bsfillSize = VPU_GBU_SIZE*2;
                chunkSize = 0;					
                VPU_DecUpdateBitstreamBuffer(handle, STREAM_END_SIZE);	//tell VPU to reach the end of stream. starting flush decoded output in VPU
                goto FLUSH_BUFFER;
            }
            continue;
        }

		
		if (randomAccess)
		{
			int tot_frame;
			int pos_frame;

			tot_frame = (int)videoSt->nb_frames;
			pos_frame = (tot_frame/100) * randomAccessPos;
			if (pos_frame < ctxVideo->frame_number)
				continue;			
			else
			{
				randomAccess = 0;

				if (decOP.bitstreamMode != BS_MODE_PIC_END)
				{
					//clear all frame buffer except current frame
					if (frame_queue_check_in_queue(display_queue, i) == 0)
						VPU_DecClrDispFlag(handle, i);
									
					//Clear all display buffer before Bitstream & Frame buffer flush
					ret = VPU_DecFrameBufferFlush(handle);
					if( ret != RETCODE_SUCCESS )
					{
						VLOG(ERR, "VPU_DecGetBitstreamBuffer failed Error code is 0x%x \n", ret );
						goto ERR_DEC_OPEN;
					}

				}
			}
		}
#endif

		chunkData = pkt->data;
		chunkSize = pkt->size;

        if (buildVideoPacket(pkt) < 0)
            goto ERR_DEC_OPEN;
		
        av_packet_free(&pkt);
		//av_free_packet(pkt);

		chunkIdx++;


        switch (seqInit()) {
            case 1: break;
            case -1: goto ERR_DEC_INIT;
            case 0: continue;
        }

FLUSH_BUFFER:		
        if (flushVideoBuffer(pkt) < 0) {
            goto ERR_DEC_OPEN;
        }
		
	}	// end of while

	if (totalNumofErrMbs) 
		VLOG(ERR, "Total Num of Error MBs : %d\n", totalNumofErrMbs);

ERR_DEC_OPEN:
	if (VPU_IsBusy(coreIdx))
	{
		VPU_DecUpdateBitstreamBuffer(handle, STREAM_END_SIZE);
		while(VPU_IsBusy(coreIdx)) 
			;
	}
	// Now that we are done with decoding, close the open instance.
	VPU_DecClose(handle);
	if (display_queue)
	{
		frame_queue_dequeue_all(display_queue);
		frame_queue_deinit(display_queue);
	}

	VLOG(INFO, "\nDec End. Tot Frame %d\n", frameIdx);

ERR_DEC_INIT:	
	if (vbStream.size)
		vdi_free_dma_memory(coreIdx, &vbStream);

	if (seqHeader)
		free(seqHeader);

	if( pYuv )
		free( pYuv );

	if ( picHeader )
		free(picHeader);
	
	VPU_DeInit(coreIdx);

	return 1;
}

//----------------------------------------------------------------------

int AudioDecoder::decodeFrames(AVPacket* pkt, uint8_t *audio_buf, int buf_size) 
{
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame frame;

    int len1;

    audio_pkt_data = pkt->data;
    audio_pkt_size = pkt->size;
    while(audio_pkt_size > 0) {
        int got_frame = 0;
        fprintf(stderr, "%s: decode audio\n", __func__);
        len1 = avcodec_decode_audio4(_audioCtx, &frame, &got_frame, pkt);
        if(len1 < 0) {
            char buf[1024] = {0,};
            av_make_error_string(buf, 1023, len1);
            /* if error, skip frame */
            fprintf(stderr, "%s: decode audio error %s\n", __func__,  buf);
            audio_pkt_size = 0;
            break;
        }
        audio_pkt_data += len1;
        audio_pkt_size -= len1;
        if(got_frame) {
            int out_nb_samples = avresample_available(_avrCtx)
                + (avresample_get_delay(_avrCtx) + frame.nb_samples); // upper bound
            uint8_t *out_data = NULL;
            int out_linesize = 0;
            av_samples_alloc(&out_data, &out_linesize, frame.channels, out_nb_samples, AV_SAMPLE_FMT_S16, 0);
            int nr_read_samples = avresample_convert(_avrCtx, &out_data, out_linesize,
                    out_nb_samples, frame.data, frame.linesize[0], frame.nb_samples);
            if (nr_read_samples < out_nb_samples) {
                fprintf(stderr, "still has samples needs to be read\n");
            }

            if (frame.pts != AV_NOPTS_VALUE) {
                _audioClock = av_q2d(_audioCtx->time_base) * frame.pts;
            } else if(pkt->pts != AV_NOPTS_VALUE) {
                _audioClock = av_q2d(_audioCtx->time_base) * pkt->pts;
            }
            fprintf(stderr, "%s: update audio clock %f\n", __func__, _audioClock);

            //assert(out_linesize == nr_read_samples*4);
            char *inbuf = (char*)out_data;
            int error = 0;
            if (pa_simple_write(_pa, inbuf, out_linesize, &error) < 0) {
                fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
            }

            av_frame_unref(&frame);
            //int data_size = av_samples_get_buffer_size(NULL, _audioCtx->channels,
                    //frame.nb_samples, _audioCtx->sample_fmt, 1);
            //int n = 2 * _audioCtx->channels;
            //_audioClock += (double)data_size / (double)(n * _audioCtx->sample_rate);
            //fprintf(stderr, "%s: play audio, audio clock %f\n", __func__, _audioClock);
            av_freep(&out_data);
        }
    }

    av_packet_free(&pkt);
}

AudioDecoder::AudioDecoder(AVCodecContext *ctx)
    :QThread(0), _audioCtx(ctx)
{
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16NE;
    ss.channels = ctx->channels;
    ss.rate = ctx->sample_rate;

    _avrCtx = avresample_alloc_context();
    av_opt_set_int(_avrCtx, "in_channel_layout", _audioCtx->channel_layout, 0);
    av_opt_set_int(_avrCtx, "in_sample_fmt", _audioCtx->sample_fmt, 0);
    av_opt_set_int(_avrCtx, "in_sample_rate", _audioCtx->sample_rate, 0);
    av_opt_set_int(_avrCtx, "out_channel_layout", _audioCtx->channel_layout, 0);
    av_opt_set_int(_avrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int(_avrCtx, "out_sample_rate", _audioCtx->sample_rate, 0);
    avresample_open(_avrCtx);

    fprintf(stderr, "channels %d, rate %d, fmt %d\n", _audioCtx->channels,
                    ss.rate, _audioCtx->sample_fmt);
    int error = 0;
    /* Create a new playback stream */
    if (!(_pa = pa_simple_new(NULL, Q2C(tr("Deepin Movie")), PA_STREAM_PLAYBACK, NULL, "playback",
                    &ss, NULL, NULL, &error))) {
        fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
        _pa = nullptr;
    }
}

void AudioDecoder::stop() 
{ 
    _quitFlags.storeRelease(1); 
    audioPackets.flush();
}

void AudioDecoder::run()
{
    for (;;) {
        if (_quitFlags.load()) break;

        auto* pkt = audioPackets.deque();
        decodeFrames(pkt, 0, 0);
    }
}

AudioDecoder::~AudioDecoder()
{
    avresample_close(_avrCtx);
    avresample_free(&_avrCtx);

    if (_pa) {
        pa_simple_flush(_pa, NULL);
        pa_simple_free(_pa);
    }
}

//------------------------------------------------------------------------
//

static int open_codec_context(int *stream_idx,
        AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;
    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        qWarning() << "Could not find " << av_get_media_type_string(type)
            << " stream in input file";
        return ret;
    }

    stream_index = ret;
    st = fmt_ctx->streams[stream_index];
    *dec_ctx = st->codec;
    dec = avcodec_find_decoder((*dec_ctx)->codec_id);
    if(avcodec_open2(*dec_ctx, dec, NULL) < 0) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }
#if 0
#if LIBAVFORMAT_VERSION_MAJOR >= 57 && LIBAVFORMAT_VERSION_MINOR <= 25
    *dec_ctx = st->codec;
    dec = avcodec_find_decoder((*dec_ctx)->codec_id);
#else
    /* find decoder for the stream */
    dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(type));
        return AVERROR(EINVAL);
    }
    /* Allocate a codec context for the decoder */
    *dec_ctx = avcodec_alloc_context3(dec);
    if (!*dec_ctx) {
        fprintf(stderr, "Failed to allocate the %s codec context\n",
                av_get_media_type_string(type));
        return AVERROR(ENOMEM);
    }

    if(avcodec_copy_context(*dec_ctx, fmt_ctx->streams[stream_index]->codec) != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        return -1; // Error copying codec context
    }

    if(avcodec_open2(*dec_ctx, dec, NULL) < 0) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }
    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                av_get_media_type_string(type));
        return ret;
    }
#endif
#endif
    *stream_idx = stream_index;
    return 0;
}

VpuMainThread::VpuMainThread(const QString& name)
    :QThread(0), _fileInfo(name)
{
    if (openMediaFile() >= 0) {
        _audioThread = new AudioDecoder(ctxAudio);
        _videoThread = new VpuDecoder(ic->streams[idxVideo], ctxVideo);
    }
}

VpuMainThread::~VpuMainThread()
{
}

int VpuMainThread::openMediaFile()
{
    int err;

	av_register_all();
	
	err = avformat_open_input(&ic, Q2C(_fileInfo.absoluteFilePath()), NULL,  NULL);
	if (err < 0) {
		VLOG(ERR, "%s: could not open file\n", Q2C(_fileInfo.absoluteFilePath()));
		av_free(ic);
		return 0;
	}
	ic->flags |= CODEC_FLAG_TRUNCATED; /* we do not send complete frames */

	err = avformat_find_stream_info(ic,  NULL);
	if (err < 0) {
		VLOG(ERR, "%s: could not find stream information\n", Q2C(_fileInfo.absoluteFilePath()));
        goto _error;
	}

	av_dump_format(ic, 0, Q2C(_fileInfo.absoluteFilePath()), 0);

	// find video stream index
    if (open_codec_context(&idxVideo, &ctxVideo, ic, AVMEDIA_TYPE_VIDEO) < 0) {
		err = -1;
		VLOG(ERR, "%s: could not find video stream information\n", Q2C(_fileInfo.absoluteFilePath()));
        goto _error;
	}

    if (open_codec_context(&idxAudio, &ctxAudio, ic, AVMEDIA_TYPE_AUDIO) < 0) {
		err = -1;
		VLOG(ERR, "%s: could not find audio stream information\n", Q2C(_fileInfo.absoluteFilePath()));
        goto _error;
	}

    //if (open_codec_context(&idxSubtitle, &ctxSubtitle, ic, AVMEDIA_TYPE_SUBTITLE) < 0) {
		//err = -1;
		//VLOG(ERR, "%s: could not find sub stream information\n", Q2C(_fileInfo.absoluteFilePath()));
        //goto _error;
	//}
    return 0;

_error:
    //FIXME: should not free ctxVideo
    return -1;
}

void VpuMainThread::close()
{
    if (ctxVideo) avcodec_close(ctxVideo);
    if (ctxAudio) avcodec_close(ctxAudio);
    if (ctxSubtitle) avcodec_close(ctxSubtitle);
    if (ic) avformat_close_input(&ic);
}

//FIXME: need impl!
bool VpuMainThread::isHardwareSupported()
{
    return true;
}

int VpuMainThread::decodeAudio(AVPacket* pkt)
{
    AVPacket *dst = av_packet_alloc();
    av_packet_ref(dst, pkt);
    audioPackets.put(dst);
    av_free_packet(pkt);
}

double VpuMainThread::getClock()
{
    return _audioClock;
}

VideoPacketQueue& VpuMainThread::frames()
{
    return videoFrames;
}

void VpuMainThread::run() 
{
    int err;
	AVPacket  *pkt = 0;

	AVPacket pkt1; 
    pkt=&pkt1;

    connect(_audioThread, &QThread::finished, [=]() { _quitFlags.store(1); });
    connect(_videoThread, &QThread::finished, [=]() { _quitFlags.store(1); });

    bool audio_started = false;
    _videoThread->start();
    _audioThread->start();

	while(1) {
        if (_quitFlags.load()) {
            break;
        }

        av_init_packet(pkt);
		err = av_read_frame(ic, pkt);
        if (err < 0) {
            break;
        }

		if (pkt->stream_index == idxAudio) {
            decodeAudio(pkt);
			continue;
        }

		if (pkt->stream_index == idxVideo) {
            AVPacket *dst = av_packet_alloc();
            av_packet_ref(dst, pkt);
            videoPackets.put(dst);
            av_free_packet(pkt);

            continue;
        }

        // subtitle
        av_free_packet(pkt);
    }

    if (_audioThread) {
        _audioThread->stop();
        int tries = 10;
        while (tries--) {
            _audioThread->wait(100);
        }
        //FIXME: connect finished signal?
        _audioThread->deleteLater();
    }

    if (_videoThread) {
        _videoThread->stop();
        int tries = 10;
        while (tries--) {
            _videoThread->wait(100);
        }
        _videoThread->deleteLater();
    }
    fprintf(stderr, "%s: decoder quit\n", __func__);
}

}
