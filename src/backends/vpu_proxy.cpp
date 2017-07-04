#include "config.h"

#include "vpu_proxy.h"

#include "vpuapi.h"
#include "regdefine.h"
#include "vpuhelper.h"
#include "vdi/vdi.h"
#include "vdi/vdi_osal.h"
#include "mixer.h"
#include "vpuio.h"
#include "vpuapifunc.h"
#include <stdio.h>
#include <memory.h>

#ifdef SUPPORT_FFMPEG_DEMUX 
#if defined (__cplusplus)
extern "C" {
#endif

#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/avutil.h>

#if defined (__cplusplus)
}
#endif
#endif

#include <time.h>



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

//#undef DEBUG
#define DEBUG
#ifdef DEBUG
#define debug(format, args...)  printf("%s,%s:%d" format "\n", __FILE__, __func__, __LINE__, ##args)
#else
#define debug(format, args...)
#endif

static unsigned long rpcc()
{
        unsigned long result;
        asm volatile ("rtc %0" : "=r"(result));
        return result;
} 


namespace dmr {
VpuProxy::VpuProxy(QWidget *parent)
{
}


bool VpuProxy::init()
{
    InitLog();

    memset( &decConfig, 0x00, sizeof( decConfig) );
    decConfig.coreIdx = 0;

    strcpy(decConfig.bitstreamFileName, "/home/lily/mindenki.mkv");
    decConfig.outNum = 0;

    //printf("Enter Bitstream Mode(0: Interrupt mode, 1: Rollback mode, 2: PicEnd mode): ");
    decConfig.bitstreamMode = 0;

    decConfig.maxWidth = 0;
    decConfig.maxHeight = 0;
    decConfig.mp4DeblkEnable = 0;
    decConfig.iframeSearchEnable = 0;
    decConfig.skipframeMode = 0; // 1:PB skip, 2:B skip

    if( decConfig.outNum < 0 )
    {
        decConfig.checkeos = 0;
        decConfig.outNum = 0;
    }
    else
    {
        decConfig.checkeos = 1;
    }
}

void VpuProxy::play()
{
    init();
    loop();
}

int VpuProxy::loop()
{
	DecHandle		handle		= {0};
	DecOpenParam	decOP		= {0};
	DecInitialInfo	initialInfo = {0};
	DecOutputInfo	outputInfo	= {0};
	DecParam		decParam	= {0};
	BufInfo			bufInfo	 = {0};
	vpu_buffer_t	vbStream	= {0};

	FrameBuffer		fbPPU[MAX_ROT_BUF_NUM];
	FrameBufferAllocInfo fbAllocInfo;
	SecAxiUse		secAxiUse = {0};
	RetCode			ret = RETCODE_SUCCESS;		
	int				i = 0, saveImage = 0, decodefinish = 0, err=0;
	int				framebufSize = 0, framebufWidth = 0, framebufHeight = 0, rotbufWidth = 0, rotbufHeight = 0, framebufFormat = FORMAT_420, mapType;
	int				framebufStride = 0, rotStride = 0, regFrameBufCount = 0;
	int				frameIdx = 0, ppIdx=0, decodeIdx=0;
	int				kbhitRet = 0,  totalNumofErrMbs = 0;
	int				dispDoneIdx = -1;
	BYTE *			pYuv	 =	NULL;
	osal_file_t		fpYuv	 =	NULL;
	int				seqInited, seqFilled, reUseChunk;
	int				hScaleFactor, vScaleFactor, scaledWidth, scaledHeight;
	int			 randomAccess = 0, randomAccessPos = 0;
	int				ppuEnable = 0;
	int				int_reason = 0;
	int				bsfillSize = 0;
	int				size;
	int				instIdx, coreIdx;
	TiledMapConfig mapCfg;
	DRAMConfig dramCfg = {0};
	DecConfigParam decConfig;
	Rect		   rcPrevDisp;
	frame_queue_item_t* display_queue = NULL;

	AVFormatContext *ic;
	AVPacket pkt1, *pkt=&pkt1;
	AVCodecContext *ctxVideo;
	int idxVideo;
	int	chunkIdx = 0;

	BYTE *chunkData = NULL;
	int chunkSize = 0;
	BYTE *seqHeader = NULL;
	int seqHeaderSize = 0;
	BYTE *picHeader = NULL;
	int picHeaderSize = 0;

	const char *filename;
#if defined(SUPPORT_DEC_SLICE_BUFFER) || defined(SUPPORT_DEC_RESOLUTION_CHANGE)
	DecBufInfo decBufInfo;
#endif

	av_register_all();

	VLOG(INFO, "ffmpeg library version is codec=0x%x, format=0x%x\n", avcodec_version(), avformat_version());
	

	filename = decConfig.bitstreamFileName;
	instIdx = decConfig.instNum;
	coreIdx = decConfig.coreIdx;

		
	ic = avformat_alloc_context();
	if (!ic)
		return 0;

	ic->flags |= CODEC_FLAG_TRUNCATED; /* we do not send complete frames */
	err = avformat_open_input(&ic, filename, NULL,  NULL);
	if (err < 0)
	{
		VLOG(ERR, "%s: could not open file\n", filename);
		av_free(ic);
		return 0;
	}

	err = avformat_find_stream_info(ic,  NULL);
	if (err < 0) 
	{
		VLOG(ERR, "%s: could not find stream information\n", filename);
		goto ERR_DEC_INIT;
	}

	av_dump_format(ic, 0, filename, 0);

	// find video stream index
	idxVideo = -1;
	idxVideo = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (idxVideo < 0) 
	{
		err = -1;
		VLOG(ERR, "%s: could not find video stream information\n", filename);
		goto ERR_DEC_INIT;
	}

	ctxVideo = ic->streams[idxVideo]->codec;  

	seqHeader = osal_malloc(ctxVideo->extradata_size+MAX_CHUNK_HEADER_SIZE);	// allocate more buffer to fill the vpu specific header.
	if (!seqHeader)
	{
		VLOG(ERR, "fail to allocate the seqHeader buffer\n");
		goto ERR_DEC_INIT;
	}
	memset(seqHeader, 0x00, ctxVideo->extradata_size+MAX_CHUNK_HEADER_SIZE);

	picHeader = osal_malloc(MAX_CHUNK_HEADER_SIZE);
	if (!picHeader)
	{
		VLOG(ERR, "fail to allocate the picHeader buffer\n");
		goto ERR_DEC_INIT;
	}
	memset(picHeader, 0x00, MAX_CHUNK_HEADER_SIZE);

	if (strlen(decConfig.yuvFileName))
		saveImage = 1;
	else
		saveImage = 0;

	if (strlen(decConfig.yuvFileName)) 
	{
		fpYuv = osal_fopen(decConfig.yuvFileName, "wb");
		if (!fpYuv) 
		{
			VLOG(ERR, "Can't open %s \n", decConfig.yuvFileName);
			goto ERR_DEC_INIT;
		}
	}


	ret = VPU_Init(coreIdx);
	if (ret != RETCODE_SUCCESS && 
		ret != RETCODE_CALLED_BEFORE) 
	{
		VLOG(ERR, "VPU_Init failed Error code is 0x%x \n", ret );
		goto ERR_DEC_INIT;
	}

	CheckVersion(coreIdx);


	decOP.bitstreamFormat = fourCCToCodStd(ctxVideo->codec_tag);
	if (decOP.bitstreamFormat == -1)
		decOP.bitstreamFormat = codecIdToCodStd(ctxVideo->codec_id);

	if (decOP.bitstreamFormat == -1)
	{
		VLOG(ERR, "can not support video format in VPU tag=%c%c%c%c, codec_id=0x%x \n", ctxVideo->codec_tag>>0, ctxVideo->codec_tag>>8, ctxVideo->codec_tag>>16, ctxVideo->codec_tag>>24, ctxVideo->codec_id );
		goto ERR_DEC_INIT;
	}

	vbStream.size = STREAM_BUF_SIZE; //STREAM_BUF_SIZE;	
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


	decOP.tiled2LinearEnable = (decConfig.mapType>>4)&0x1;
	mapType = decConfig.mapType & 0xf;
	if (mapType) 
	{
		decOP.wtlEnable = decConfig.wtlEnable;
		if (decOP.wtlEnable)
		{
            decConfig.rotAngle;
            decConfig.mirDir;
            decConfig.useRot = 0;
            decConfig.useDering = 0;
			decOP.mp4DeblkEnable = 0;
            decOP.tiled2LinearEnable = 0;
		}
	}

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

	seqInited = 0;
	seqFilled = 0;
	bsfillSize = 0;
	reUseChunk = 0;
	display_queue = frame_queue_init(MAX_REG_FRAME);
	init_VSYNC_flag();
	while(1)
	{
        //FIXME: quit when flag set

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

		av_init_packet(pkt);

		err = av_read_frame(ic, pkt);
		if (err < 0) 
		{
			if (pkt->stream_index == idxVideo)
				chunkIdx++;	

			if (err==AVERROR_EOF || url_feof(ic->pb)) 
			{
				bsfillSize = VPU_GBU_SIZE*2;
				chunkSize = 0;					
				VPU_DecUpdateBitstreamBuffer(handle, STREAM_END_SIZE);	//tell VPU to reach the end of stream. starting flush decoded output in VPU
				goto FLUSH_BUFFER;
			}
			continue;
		}

		if (pkt->stream_index != idxVideo)
			continue;

		
		if (randomAccess)
		{
			int tot_frame;
			int pos_frame;

			tot_frame = (int)ic->streams[idxVideo]->nb_frames;
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

		chunkData = pkt->data;
		chunkSize = pkt->size;

		
		if (!seqInited && !seqFilled)
		{
			seqHeaderSize = BuildSeqHeader(seqHeader, decOP.bitstreamFormat, ic->streams[idxVideo]);	// make sequence data as reference file header to support VPU decoder.
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
						goto ERR_DEC_OPEN;
					}
						
					bsfillSize += size;
				}
				break;
			}
			seqFilled = 1;
		}

		
		// Build and Fill picture Header data which is dedicated for VPU 
		picHeaderSize = BuildPicHeader(picHeader, decOP.bitstreamFormat, ic->streams[idxVideo], pkt);				
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
				goto ERR_DEC_OPEN;
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
					goto ERR_DEC_OPEN;
				}

				bsfillSize += size;
			}
			break;
		}		
		
//#define DUMP_ES_DATA
//#define DUMP_ES_WITH_SIZE
#ifdef DUMP_ES_DATA
		{
			osal_file_t fpDump;
			if (chunkIdx == 0)
				fpDump = osal_fopen("dump.dat", "wb");
			else
				fpDump = osal_fopen("dump.dat", "a+b");
			if (fpDump)
			{
				if (chunkIdx == 0) 
				{
					osal_fwrite(&seqHeaderSize, 4, 1, fpDump);
					osal_fwrite(seqHeader, seqHeaderSize, 1, fpDump);
				}
				if (picHeaderSize) 
				{
					osal_fwrite(&picHeaderSize, 4, 1, fpDump);
					osal_fwrite(picHeader, picHeaderSize, 1, fpDump);	
				}
				osal_fwrite(&chunkSize, 4, 1, fpDump);
				osal_fwrite(chunkData, chunkSize, 1, fpDump);					
				osal_fclose(fpDump);
			}
		}
#endif
		av_free_packet(pkt);

		chunkIdx++;


		if (!seqInited)
		{ 
			ConfigSeqReport(coreIdx, handle, decOP.bitstreamFormat);
			if (decOP.bitstreamMode == BS_MODE_PIC_END)
			{
				ret = VPU_DecGetInitialInfo(handle, &initialInfo);
				if (ret != RETCODE_SUCCESS) 
				{
					if (ret == RETCODE_MEMORY_ACCESS_VIOLATION)
						PrintMemoryAccessViolationReason(coreIdx, NULL);
					VLOG(ERR, "VPU_DecGetInitialInfo failed Error code is 0x%x \n", ret);
					goto ERR_DEC_OPEN;					
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
						goto ERR_DEC_OPEN;
					}
				}
				else
				{
					// After VPU generate the BIT_EMPTY interrupt. HOST should feed the bitstream up to 1024 in case of seq_init
					if (bsfillSize < VPU_GBU_SIZE*2)
						continue;
				}
				//while((kbhitRet = osal_kbhit()) == 0) 
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
							seqInited = 1;
							break;
						}
					}
				}
				if(int_reason & (1<<INT_BIT_BIT_BUF_EMPTY) || int_reason == -1) 
				{
					bsfillSize = 0;
					continue; // go to take next chunk.
				}
				if (seqInited)
				{
					ret = VPU_DecCompleteSeqInit(handle, &initialInfo);	
					if (ret != RETCODE_SUCCESS)
					{
						if (ret == RETCODE_MEMORY_ACCESS_VIOLATION)
							PrintMemoryAccessViolationReason(coreIdx, NULL);
						if (initialInfo.seqInitErrReason & (1<<31)) // this case happened only ROLLBACK mode
							VLOG(ERR, "Not enough header : Parser has to feed right size of a sequence header  \n");
						VLOG(ERR, "VPU_DecCompleteSeqInit failed Error code is 0x%x \n", ret );
						goto ERR_DEC_OPEN;
					}			
				}
				else
				{
					VLOG(ERR, "VPU_DecGetInitialInfo failed Error code is 0x%x \n", ret);
					goto ERR_DEC_OPEN;
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
						goto ERR_DEC_INIT;
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
						goto ERR_DEC_INIT;
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
			sw_mixer_close((coreIdx*MAX_NUM_VPU_CORE)+instIdx);
			if (!ppuEnable)
				sw_mixer_open((coreIdx*MAX_NUM_VPU_CORE)+instIdx, framebufStride, framebufHeight);
			else
				sw_mixer_open((coreIdx*MAX_NUM_VPU_CORE)+instIdx, rotStride, rotbufHeight);

			// the size of pYuv should be aligned 8 byte. because of C&M HPI bus system constraint.
			pYuv = (BYTE*)osal_malloc(framebufSize);
			if (!pYuv) 
			{
				VLOG(ERR, "Fail to allocation memory for display buffer\n");
				goto ERR_DEC_INIT;
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
				goto ERR_DEC_OPEN;
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
					goto ERR_DEC_OPEN;
				}

				ppIdx = 0;

				if (decConfig.useRot)
				{
					VPU_DecGiveCommand(handle, SET_ROTATION_ANGLE, &(decConfig.rotAngle));
					VPU_DecGiveCommand(handle, SET_MIRROR_DIRECTION, &(decConfig.mirDir));
				}

				VPU_DecGiveCommand(handle, SET_ROTATOR_STRIDE, &rotStride);
			
			}


			InitMixerInt();

			seqInited = 1;			

		}

FLUSH_BUFFER:		
		
		if((int_reason & (1<<INT_BIT_BIT_BUF_EMPTY)) != (1<<INT_BIT_BIT_BUF_EMPTY) && (int_reason & (1<<INT_BIT_DEC_FIELD)) != (1<<INT_BIT_DEC_FIELD))
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
				goto ERR_DEC_OPEN;
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
					continue;
			}
		}



		//while((kbhitRet = osal_kbhit()) == 0) 
        while (1)
		{
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
					goto ERR_DEC_OPEN;
				}
				break;
			}


			if (int_reason & (1<<INT_BIT_PIC_RUN)) 
				break;				
		}			

		if(int_reason & (1<<INT_BIT_BIT_BUF_EMPTY)) 
		{
			bsfillSize = 0;
			continue; // go to take next chunk.
		}
		if(int_reason & (1<<INT_BIT_DEC_FIELD)) 
		{
			bsfillSize = 0;
			continue; // go to take next chunk.
		}		


		ret = VPU_DecGetOutputInfo(handle, &outputInfo);
		if (ret != RETCODE_SUCCESS) 
		{
			VLOG(ERR,  "VPU_DecGetOutputInfo failed Error code is 0x%x \n", ret);
			if (ret == RETCODE_MEMORY_ACCESS_VIOLATION)
				PrintMemoryAccessViolationReason(coreIdx, &outputInfo);
			goto ERR_DEC_OPEN;
		}

		if ((outputInfo.decodingSuccess & 0x01) == 0)
		{
			VLOG(ERR, "VPU_DecGetOutputInfo decode fail framdIdx %d \n", frameIdx);
			VLOG(TRACE, "#%d, indexFrameDisplay %d || picType %d || indexFrameDecoded %d\n", 
				frameIdx, outputInfo.indexFrameDisplay, outputInfo.picType, outputInfo.indexFrameDecoded );
		}		

		VLOG(TRACE, "#%d:%d, indexDisplay %d || picType %d || indexDecoded %d || rdPtr=0x%x || wrPtr=0x%x || chunkSize = %d, consume=%d\n", 
			instIdx, frameIdx, outputInfo.indexFrameDisplay, outputInfo.picType, outputInfo.indexFrameDecoded, outputInfo.rdPtr, outputInfo.wrPtr, chunkSize+picHeaderSize, outputInfo.consumedByte);

		SaveDecReport(coreIdx, handle, &outputInfo, decOP.bitstreamFormat, ((initialInfo.picWidth+15)&~15)/16);
		if (outputInfo.chunkReuseRequired) // reuse previous chunk. that would be 1 once framebuffer is full.
			reUseChunk = 1;		

		if (outputInfo.indexFrameDisplay == -1)
			decodefinish = 1;


		if (!ppuEnable) 
		{
			if (decodefinish)
				break;		

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
				continue;
			}
		}
		else
		{
			if (decodefinish)
			{
				if (decodeIdx ==  0)
					break;
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
				continue;
			}

			if (decodeIdx == 0) // if PP has been enabled, the first picture is saved at next time.
			{
				// save rotated dec width, height to display next decoding time.
				if (outputInfo.indexFrameDisplay >= 0)
					frame_queue_enqueue(display_queue, outputInfo.indexFrameDisplay);
				rcPrevDisp = outputInfo.rcDisplay;
				decodeIdx++;
				continue;

			}
		}

		decodeIdx++;

		if (outputInfo.indexFrameDisplay >= 0)
			frame_queue_enqueue(display_queue, outputInfo.indexFrameDisplay);

		if (!saveImage)
		{			
			if (!ppuEnable) 
				DisplayMixer(&outputInfo.dispFrame, framebufStride, framebufHeight);
			else
			{
				ppIdx = (ppIdx+1)%MAX_ROT_BUF_NUM;	
				DisplayMixer(&outputInfo.dispFrame, rotStride, rotbufHeight);
			}
#ifdef FORCE_SET_VSYNC_FLAG
			set_VSYNC_flag();
#endif
		}
		else // store image
		{
			if (ppuEnable) 
				ppIdx = (ppIdx+1)%MAX_ROT_BUF_NUM;

			if (!SaveYuvImageHelperFormat(coreIdx, fpYuv, &outputInfo.dispFrame, mapCfg, pYuv, 
				(ppuEnable)?rcPrevDisp:outputInfo.rcDisplay, decOP.cbcrInterleave, framebufFormat, decOP.frameEndian))
				goto ERR_DEC_OPEN;

		
			sw_mixer_draw((coreIdx*MAX_NUM_VPU_CORE)+instIdx, 0, 0, outputInfo.dispFrame.stride, outputInfo.dispFrame.height, framebufFormat, pYuv, 0);	
#ifdef FORCE_SET_VSYNC_FLAG
			set_VSYNC_flag();
#endif
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

		if (decConfig.outNum && frameIdx == (decConfig.outNum-1)) 
			break;

		if (decodefinish)
			break;		
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

	if( fpYuv )
		osal_fclose( fpYuv );

	if ( picHeader )
		free(picHeader);
	
//	avformat_close_input(&ic);	

	sw_mixer_close((coreIdx*MAX_NUM_VPU_CORE)+instIdx);

	VPU_DeInit(coreIdx);
	return 1;
}

}
