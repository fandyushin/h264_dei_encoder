#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/BufTab.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Rendezvous.h>

#include <ti/sdo/dmai/ce/Venc1.h>
#include <ti/sdo/codecs/h264enc/ih264venc.h>

#include "video.h"
#include "main.h"

#define NUM_VIDEO_BUFS 4

Void *videoThrFxn(Void *arg)
{
	VideoEnv *envp = (VideoEnv *) arg;

	Venc1_Handle hVe1 = NULL;
	VIDENC1_Params params = Venc1_Params_DEFAULT;
	VIDENC1_DynamicParams dynParams = Venc1_DynamicParams_DEFAULT;
	IH264VENC_Params h264Params = IH264VENC_PARAMS;
	IH264VENC_DynamicParams h264DynParams = H264VENC_TI_IH264VENC_DYNAMICPARAMS;
	VUIParamBuffer VUI_Buffer = H264VENC_TI_VUIPARAMBUFFER;

	BufTab_Handle hVidBufTab = NULL;
	Buffer_Handle hVInBuf, hWOutBuf;
	BufferGfx_Attrs gfxAttrs = BufferGfx_Attrs_DEFAULT;

	ColorSpace_Type colorSpace = ColorSpace_YUV420PSEMI;

	Void *status = THREAD_SUCCESS;

	/* Initialization */
	params->maxWidth = envp->imageWidth;
	params->maxHeight = Dmai_roundUp(envp->imageHeight, CODECHEIGHTALIGN);
	params->inputChromaFormat = XDM_YUV_420SP;
	params->reconChromaFormat = XDM_YUV_420SP;
	params->maxFrameRate = envp->videoFrameRate;
	params->encodingPreset = XDM_USER_DEFINED;
	params->rateControlPreset = IVIDEO_USER_DEFINED;
	params->maxBitRate = 10000000;

	dynParams->targetBitRate = envp->videoBitRate*0.9;
	dynParams->inputWidth = envp->imageWidth;
	dynParams->captureWidth = Dmai_roundUp(BufferGfx_calcLineLength(envp->imageWidth, colorSpace), 32);
	dynParams->inputHeight = envp->imageHeight;
	dynParams->refFrameRate = params->maxFrameRate;
	dynParams->targetFrameRate = params->maxFrameRate;
	dynParams->intraFrameInterval = 0;
	dynParams->interFrameInterval = 0;

	h264Params.videncParams = *params;
	h264Params.videncParams.size = sizeof(IH264VENC_Params);
	h264Params.encQuality = 1;
	h264Params.enableDDRbuff = 1; /* Uses DDR instead of VICP buffers */
	h264Params.enableARM926Tcm = 0;
	h264Params.enableVUIparams = (0x1 << 1);
	h264Params.videncParams.inputContentType = IVIDEO_PROGRESSIVE;

	h264DynParams.videncDynamicParams = *dynParams;
	h264DynParams.videncDynamicParams.size = sizeof(IH264VENC_DynamicParams);

	h264DynParams.VUI_Buffer = &VUI_Buffer;
	h264DynParams.VUI_Buffer->aspectRatioInfoPresentFlag = 1;
	h264DynParams.VUI_Buffer->overscanInfoPresentFlag = 0;
	h264DynParams.VUI_Buffer->videoSignalTypePresentFlag = 0;
	h264DynParams.VUI_Buffer->timingInfoPresentFlag = 1;
	h264DynParams.VUI_Buffer->numUnitsInTicks = 1;
	h264DynParams.VUI_Buffer->timeScale = params->maxFrameRate / 1000;
	h264DynParams.VUI_Buffer->fixedFrameRateFlag = 1; 
	h264DynParams.VUI_Buffer->nalHrdParameterspresentFlag = 1;
	h264DynParams.VUI_Buffer->picStructPresentFlag = 1;

	h264DynParams.idrFrameInterval = 15;

	hVe1 = Venc1_create(envp->hEngine, envp->videoEncoder,
			(IVIDENC1_Params *) &h264Params,
			(IVIDENC1_DynamicParams *) &h264DynParams);
	if (hVe1 == NULL) {
		ERR("Failed to create video encoder: %s\n", envp->videoEncoder);
		cleanup(THREAD_FAILURE);
	}

	/* Store the output buffer size in the environment */
	envp->outBufSize = Venc1_getOutBufSize(hVe1);

	/* Signal that the codec is created and output buffer size available */
	Rendezvous_meet(envp->hRendezvousWriter);

	/* Video BufTab create */
	BufferGfx_calcDimensions(VideoStd_D1_PAL, colorSpace, &gfxAttrs.dim);
	gfxAttrs.dim.width = 704;
	gfxAttrs.dim.height = 576;
	gfxAttrs.dim.lineLength = Dmai_roundUp(BufferGfx_calcLineLength(gfxAttrs.dim.width, colorSpace), 32);
	gfxAttrs.colorSpace = colorSpace;
	bufSize = gfxAttrs.dim.lineLength * gfxAttrs.dim.height * 3 / 2;
	hVidBufTab = BufTab_create(NUM_VIDEO_BUFS, bufSize, BufferGfx_getBufferAttrs(&gfxAttrs));
	if (hVidBufTab == NULL) {
		ERR("Failed to create video buftab\n");
		cleanup(THREAD_FAILURE);
	}

	/* Set input buffer table */
    Venc1_setBufTab(hVe1, hVidBufTab);

	/* Send video buffers to DEI */
	Int nBufId = 0;
	for (nBufId = 0; nBufId < NUM_VIDEO_BUFS; nBufId++) {
		hVInBuf = BufTab_getBuf(hVidBufTab, nBufId);
		if (Fifo_put(envp->hVideoOutFifo, hVInBuf) < 0) {
			ERR("Failed to send buffer to dei thread\n");
			cleanup(THREAD_FAILURE);
		}
	}

	/* Signal that initialization is done and wait for other threads */
	Rendezvous_meet(envp->hRendezvousInit);

	while(1) {

		/* Get buffer from DEI thread */
		if(Fifo_get(envp->hVideoInFifo, &hVInBuf) < 0) {
			ERR("Failed to get buffer from dei thread\n");
			cleanup(THREAD_FAILURE);
		}

		/* Get buffer from Writer thread */
		if(Fifo_get(envp->hWriterOutFifo, &hWOutBuf) < 0) {
			ERR("Failed to get buffer from writer thread\n");
			cleanup(THREAD_FAILURE);
		}

		/* Make sure the whole buffer is used for input */
		BufferGfx_resetDimensions(hVInBuf);

		/* Encode */
		if (Venc1_process(hVe1, hVInBuf, hWOutBuf) < 0) {
			ERR("Failed to encode video buffer\n");
			cleanup(THREAD_FAILURE);
		}

		/* Put buffer to dei thread */
		if (Fifo_put(envp->hVideoOutFifo, hVInBuf) < 0) {
			ERR("Failed to send buffer to dei thread\n");
			cleanup(THREAD_FAILURE);
		}

		/* Put buffer to writer thread */
		if (Fifo_put(envp->hWriterInFifo, hWOutBuf) < 0) {
			ERR("Failed to send buffer to dei thread\n");
			cleanup(THREAD_FAILURE);
		}

	}

cleanup:

	/* Make sure the other threads aren't waiting for us */
	Rendezvous_force(envp->hRendezvousInit);
	Rendezvous_force(envp->hRendezvousWriter);

	/* Make sure the other threads aren't waiting for init to complete */
	Rendezvous_meet(envp->hRendezvousCleanup);

	if (hVidBufTab) {
		BufTab_delete(hVidBufTab);
	}

	if (hVe1) {
		Venc1_delete(hVe1);
	}

	return status;
}