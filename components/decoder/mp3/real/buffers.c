/* ***** BEGIN LICENSE BLOCK ***** 
 * Version: RCSL 1.0/RPSL 1.0 
 *  
 * Portions Copyright (c) 1995-2002 RealNetworks, Inc. All Rights Reserved. 
 *      
 * The contents of this file, and the files included with this file, are 
 * subject to the current version of the RealNetworks Public Source License 
 * Version 1.0 (the "RPSL") available at 
 * http://www.helixcommunity.org/content/rpsl unless you have licensed 
 * the file under the RealNetworks Community Source License Version 1.0 
 * (the "RCSL") available at http://www.helixcommunity.org/content/rcsl, 
 * in which case the RCSL will apply. You may also obtain the license terms 
 * directly from RealNetworks.  You may not use this file except in 
 * compliance with the RPSL or, if you have a valid RCSL with RealNetworks 
 * applicable to this file, the RCSL.  Please see the applicable RPSL or 
 * RCSL for the rights, obligations and limitations governing use of the 
 * contents of the file.  
 *  
 * This file is part of the Helix DNA Technology. RealNetworks is the 
 * developer of the Original Code and owns the copyrights in the portions 
 * it created. 
 *  
 * This file, and the files included with this file, is distributed and made 
 * available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER 
 * EXPRESS OR IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS ALL SUCH WARRANTIES, 
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS 
 * FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. 
 * 
 * Technology Compatibility Kit Test Suite(s) Location: 
 *    http://www.helixcommunity.org/content/tck 
 * 
 * Contributor(s): 
 *  
 * ***** END LICENSE BLOCK ***** */ 

/**************************************************************************************
 * Fixed-point MP3 decoder
 * Jon Recker (jrecker@real.com), Ken Cooke (kenc@real.com)
 * June 2003
 *
 * buffers.c - allocation and freeing of internal MP3 decoder buffers
 *
 * All memory allocation for the codec is done in this file, so if you don't want 
 *  to use other the default system malloc() and free() for heap management this is 
 *  the only file you'll need to change.
 **************************************************************************************/

#define USE_STATIC      1

#include "coder.h"

#if USE_STATIC
MP3DecInfo mp3DecInfo; 
SubbandInfo sbi;
IMDCTInfo mi;
HuffmanInfo hi;
DequantInfo di;
ScaleFactorInfo sfi;
SideInfo si;
FrameHeader fh;
#else
#include "stdlib.h" 
#endif

/**************************************************************************************
 * Function:    ClearBuffer
 *
 * Description: fill buffer with 0's
 *
 * Inputs:      pointer to buffer
 *              number of bytes to fill with 0
 *
 * Outputs:     cleared buffer
 *
 * Return:      none
 *
 * Notes:       slow, platform-independent equivalent to memset(buf, 0, nBytes)
 **************************************************************************************/
static void ClearBuffer(void *buf, int nBytes)
{
	int i;
	unsigned char *cbuf = (unsigned char *)buf;

	for (i = 0; i < nBytes; i++)
		cbuf[i] = 0;

	return;
}

/**************************************************************************************
 * Function:    AllocateBuffers
 *
 * Description: allocate all the memory needed for the MP3 decoder
 *
 * Inputs:      none
 *
 * Outputs:     none
 *
 * Return:      pointer to MP3DecInfo structure (initialized with pointers to all 
 *                the internal buffers needed for decoding, all other members of 
 *                MP3DecInfo structure set to 0)
 *
 * Notes:       if one or more mallocs fail, function frees any buffers already
 *                allocated before returning
 **************************************************************************************/
MP3DecInfo *AllocateBuffers(void)
{
#if USE_STATIC
	MP3DecInfo *mp3DecInfoPtr;
        mp3DecInfoPtr = (MP3DecInfo*)&mp3DecInfo;
        ClearBuffer((void*)&mp3DecInfo, sizeof(MP3DecInfo));

        mp3DecInfo.FrameHeaderPS =     (void*)&fh;
        mp3DecInfo.SideInfoPS =        (void*)&si;
        mp3DecInfo.ScaleFactorInfoPS = (void*)&sfi;
        mp3DecInfo.HuffmanInfoPS =     (void*)&hi;
        mp3DecInfo.DequantInfoPS =     (void*)&di;
        mp3DecInfo.IMDCTInfoPS =       (void*)&mi;
        mp3DecInfo.SubbandInfoPS =     (void*)&sbi;

        ClearBuffer((void*)&fh,  sizeof(FrameHeader));
        ClearBuffer((void*)&si,  sizeof(SideInfo));
        ClearBuffer((void*)&sfi, sizeof(ScaleFactorInfo));
        ClearBuffer((void*)&hi,  sizeof(HuffmanInfo));
        ClearBuffer((void*)&di,  sizeof(DequantInfo));
        ClearBuffer((void*)&mi,  sizeof(IMDCTInfo));
        ClearBuffer((void*)&sbi, sizeof(SubbandInfo));

        return mp3DecInfoPtr;
#else
	MP3DecInfo *mp3DecInfo;
	FrameHeader *fh;
	SideInfo *si;
	ScaleFactorInfo *sfi;
	HuffmanInfo *hi;
	DequantInfo *di;
	IMDCTInfo *mi;
	SubbandInfo *sbi;

	mp3DecInfo = (MP3DecInfo *)malloc(sizeof(MP3DecInfo));
	if (!mp3DecInfo)
		return 0;
	ClearBuffer(mp3DecInfo, sizeof(MP3DecInfo));
	
	fh =  (FrameHeader *)     malloc(sizeof(FrameHeader));
	si =  (SideInfo *)        malloc(sizeof(SideInfo));
	sfi = (ScaleFactorInfo *) malloc(sizeof(ScaleFactorInfo));
	hi =  (HuffmanInfo *)     malloc(sizeof(HuffmanInfo));
	di =  (DequantInfo *)     malloc(sizeof(DequantInfo));
	mi =  (IMDCTInfo *)       malloc(sizeof(IMDCTInfo));
	sbi = (SubbandInfo *)     malloc(sizeof(SubbandInfo));

	mp3DecInfo->FrameHeaderPS =     (void *)fh;
	mp3DecInfo->SideInfoPS =        (void *)si;
	mp3DecInfo->ScaleFactorInfoPS = (void *)sfi;
	mp3DecInfo->HuffmanInfoPS =     (void *)hi;
	mp3DecInfo->DequantInfoPS =     (void *)di;
	mp3DecInfo->IMDCTInfoPS =       (void *)mi;
	mp3DecInfo->SubbandInfoPS =     (void *)sbi;

	if (!fh || !si || !sfi || !hi || !di || !mi || !sbi) {
		FreeBuffers(mp3DecInfo);	/* safe to call - only frees memory that was successfully allocated */
		return 0;
	}
        
	/* important to do this - DSP primitives assume a bunch of state variables are 0 on first use */
	ClearBuffer(fh,  sizeof(FrameHeader));
	ClearBuffer(si,  sizeof(SideInfo));
	ClearBuffer(sfi, sizeof(ScaleFactorInfo));
	ClearBuffer(hi,  sizeof(HuffmanInfo));
	ClearBuffer(di,  sizeof(DequantInfo));
	ClearBuffer(mi,  sizeof(IMDCTInfo));
	ClearBuffer(sbi, sizeof(SubbandInfo));

	return mp3DecInfo;
#endif
}

#if !USE_STATIC
#define SAFE_FREE(x)	{if (x)	free(x);	(x) = 0;}	/* helper macro */
#endif

/**************************************************************************************
 * Function:    FreeBuffers
 *
 * Description: frees all the memory used by the MP3 decoder
 *
 * Inputs:      pointer to initialized MP3DecInfo structure
 *
 * Outputs:     none
 *
 * Return:      none
 *
 * Notes:       safe to call even if some buffers were not allocated (uses SAFE_FREE)
 **************************************************************************************/
void FreeBuffers(MP3DecInfo *mp3DecInfoArg)
{
#if !USE_STATIC
	if (!mp3DecInfoArg)
		return;

	SAFE_FREE(mp3DecInfoArg->FrameHeaderPS);
	SAFE_FREE(mp3DecInfoArg->SideInfoPS);
	SAFE_FREE(mp3DecInfoArg->ScaleFactorInfoPS);
	SAFE_FREE(mp3DecInfoArg->HuffmanInfoPS);
	SAFE_FREE(mp3DecInfoArg->DequantInfoPS);
	SAFE_FREE(mp3DecInfoArg->IMDCTInfoPS);
	SAFE_FREE(mp3DecInfoArg->SubbandInfoPS);

	SAFE_FREE(mp3DecInfoArg);
#endif
}
