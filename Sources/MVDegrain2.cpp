// Make a motion compensate temporal denoiser
// Copyright(c)2006 A.G.Balakhnin aka Fizick
// See legal notice in Copying.txt for more information

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .

#include "ClipFnc.h"
#include "debugprintf.h"
#include "Interpolation.h"
#include "MVDegrain2.h"
#include "MVFrame.h"
#include	"MVGroupOfFrames.h"
#include "MVPlane.h"
#include "Padding.h"
#include "profile.h"
#include "SuperParams64Bits.h"



// If mvfw is null, mvbw is assumed to be a radius-2 multi-vector clip.
MVDegrain2::MVDegrain2(
	PClip _child, PClip _super, PClip mvbw, PClip mvfw, PClip mvbw2, PClip mvfw2,
	int _thSAD, int _thSADC, int _YUVplanes, int _nLimit, int _nLimitC,
	int _nSCD1, int _nSCD2, bool _isse2, bool _planar, bool _lsb_flag,
	bool mt_flag, IScriptEnvironment* env
)
:	GenericVideoFilter(_child)
,	MVFilter ((! mvfw) ? mvbw : mvfw2, "MDegrain2",    env, (! mvfw) ? 4 : 1, (! mvfw) ? 3 : 0)
,	mvClipF2 ((! mvfw) ? mvbw : mvfw2, _nSCD1, _nSCD2, env, (! mvfw) ? 4 : 1, (! mvfw) ? 3 : 0)
,	mvClipF  ((! mvfw) ? mvbw : mvfw,  _nSCD1, _nSCD2, env, (! mvfw) ? 4 : 1, (! mvfw) ? 1 : 0)
,	mvClipB  ((! mvfw) ? mvbw : mvbw,  _nSCD1, _nSCD2, env, (! mvfw) ? 4 : 1, (! mvfw) ? 0 : 0)
,	mvClipB2 ((! mvfw) ? mvbw : mvbw2, _nSCD1, _nSCD2, env, (! mvfw) ? 4 : 1, (! mvfw) ? 2 : 0)
,	super (_super)
,	lsb_flag (_lsb_flag)
,	height_lsb_mul ((_lsb_flag) ? 2 : 1)
,	DstShort (0)
,	DstInt (0)
{
	thSAD = _thSAD*mvClipB.GetThSCD1()/_nSCD1; // normalize to block SAD
	thSADC = _thSADC*mvClipB.GetThSCD1()/_nSCD1; // chroma
	YUVplanes = _YUVplanes;
	nLimit = _nLimit;
	nLimitC = _nLimitC;

	isse2 = _isse2;
	planar = _planar;

	CheckSimilarity(mvClipB, "mvbw", env);
	CheckSimilarity(mvClipF, "mvfw", env);
	CheckSimilarity(mvClipF2, "mvfw2", env);
	CheckSimilarity(mvClipB2, "mvbw2", env);

	if (mvClipB.GetDeltaFrame() <= 0 || mvClipB.GetDeltaFrame() <= 0)  // 2.5.11.22
		env->ThrowError("MDegrain2: cannot use motion vectors with absolute frame references.");

	const ::VideoInfo &	vi_super = _super->GetVideoInfo ();

    pixelsize = vi.ComponentSize(); // of MVFilter
    pixelsize_super = vi_super.ComponentSize();

	// get parameters of prepared super clip - v2.0
	SuperParams64Bits params;
	memcpy(&params, &vi_super.num_audio_samples, 8);
	int nHeightS = params.nHeight;
	int nSuperHPad = params.nHPad;
	int nSuperVPad = params.nVPad;
	int nSuperPel = params.nPel;
	nSuperModeYUV = params.nModeYUV;
	int nSuperLevels = params.nLevels;
	pRefBGOF = new MVGroupOfFrames(nSuperLevels, nWidth, nHeight, nSuperPel, nSuperHPad, nSuperVPad, nSuperModeYUV, isse2, xRatioUV, yRatioUV, pixelsize_super, mt_flag);
	pRefFGOF = new MVGroupOfFrames(nSuperLevels, nWidth, nHeight, nSuperPel, nSuperHPad, nSuperVPad, nSuperModeYUV, isse2, xRatioUV, yRatioUV, pixelsize_super, mt_flag);
	pRefB2GOF = new MVGroupOfFrames(nSuperLevels, nWidth, nHeight, nSuperPel, nSuperHPad, nSuperVPad, nSuperModeYUV, isse2, xRatioUV, yRatioUV, pixelsize_super, mt_flag);
	pRefF2GOF = new MVGroupOfFrames(nSuperLevels, nWidth, nHeight, nSuperPel, nSuperHPad, nSuperVPad, nSuperModeYUV, isse2, xRatioUV, yRatioUV, pixelsize_super, mt_flag);
	int nSuperWidth  = vi_super.width;
	int nSuperHeight = vi_super.height;

	if (   nHeight != nHeightS
	    || nHeight != vi.height
	    || nWidth  != nSuperWidth-nSuperHPad*2
	    || nWidth  != vi.width
	    || nPel    != nSuperPel)
	{
		env->ThrowError("MDegrain2 : wrong source or super frame size");
	}

   if ( (pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2 && !planar)
   {
		DstPlanes =  new YUY2Planes(nWidth, nHeight * height_lsb_mul);
		SrcPlanes =  new YUY2Planes(nWidth, nHeight);
   }
   dstShortPitch = ((nWidth + 15)/16)*16;
	dstIntPitch = dstShortPitch;
   if (nOverlapX >0 || nOverlapY>0)
   {
		OverWins = new OverlapWindows(nBlkSizeX, nBlkSizeY, nOverlapX, nOverlapY);
		OverWinsUV = new OverlapWindows(nBlkSizeX/xRatioUV, nBlkSizeY/yRatioUV, nOverlapX/xRatioUV, nOverlapY/yRatioUV);
		if (lsb_flag)
		{
			DstInt = new int [dstIntPitch * nHeight];
		}
		else
		{
			DstShort = new unsigned short[dstShortPitch*nHeight];
		}
   }
   // todo function fill and xRatioUV
	switch (nBlkSizeX)
      {
      case 32:
      if (nBlkSizeY==16) {          OVERSLUMALSB = OverlapsLsb_C<32,16>; 
		 if (yRatioUV==2) {	        OVERSCHROMALSB = OverlapsLsb_C<16,8>;  }
		 else {	                    OVERSCHROMALSB = OverlapsLsb_C<16,16>; }
      } else if (nBlkSizeY==32) {    OVERSLUMALSB = OverlapsLsb_C<32,32>;
		 if (yRatioUV==2) {	        OVERSCHROMALSB = OverlapsLsb_C<16,16>;		 }
		 else {	                    OVERSCHROMALSB = OverlapsLsb_C<16,32>;		 }
      } break;
      case 16:
      if (nBlkSizeY==16) {          OVERSLUMALSB = OverlapsLsb_C<16,16>; 
		 if (yRatioUV==2) {	        OVERSCHROMALSB = OverlapsLsb_C<8,8>;   }
		 else {	                    OVERSCHROMALSB = OverlapsLsb_C<8,16>;  }
      } else if (nBlkSizeY==8) {    OVERSLUMALSB = OverlapsLsb_C<16,8>;  
		 if (yRatioUV==2) {	        OVERSCHROMALSB = OverlapsLsb_C<8,4>;   }
		 else {	                    OVERSCHROMALSB = OverlapsLsb_C<8,8>;   }
      } else if (nBlkSizeY==2) {    OVERSLUMALSB = OverlapsLsb_C<16,2>;  
		 if (yRatioUV==2) {	        OVERSCHROMALSB = OverlapsLsb_C<8,1>;   }
		 else {	                    OVERSCHROMALSB = OverlapsLsb_C<8,2>;   }
      }
         break;
      case 4:
                                    OVERSLUMALSB = OverlapsLsb_C<4,4>;   
		 if (yRatioUV==2) {			OVERSCHROMALSB = OverlapsLsb_C<2,2>;  }
		 else {			            OVERSCHROMALSB = OverlapsLsb_C<2,4>;  }
         break;
      case 8:
      default:
      if (nBlkSizeY==8) {           OVERSLUMALSB = OverlapsLsb_C<8,8>;   
		 if (yRatioUV==2) {	        OVERSCHROMALSB = OverlapsLsb_C<4,4>;   }
		 else {	                    OVERSCHROMALSB = OverlapsLsb_C<4,8>;   }
      }else if (nBlkSizeY==4) {     OVERSLUMALSB = OverlapsLsb_C<8,4>;   
		 if (yRatioUV==2) {	        OVERSCHROMALSB = OverlapsLsb_C<4,2>;   }
		 else {	                    OVERSCHROMALSB = OverlapsLsb_C<4,4>;   }
      }
      }

   if ( ((env->GetCPUFlags() & CPUF_SSE2) != 0) & isse2)
   {
	switch (nBlkSizeX)
      {
      case 32:
      if (nBlkSizeY==16) {          OVERSLUMA = Overlaps32x16_sse2;  DEGRAINLUMA = Degrain2_sse2<32,16>;
		 if (yRatioUV==2) {         OVERSCHROMA = Overlaps16x8_sse2; DEGRAINCHROMA = Degrain2_sse2<16,8>;		 }
		 else {                     OVERSCHROMA = Overlaps16x16_sse2;DEGRAINCHROMA = Degrain2_sse2<16,16>;		 }
      } else if (nBlkSizeY==32) {    OVERSLUMA = Overlaps32x32_sse2;  DEGRAINLUMA = Degrain2_sse2<32,32>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps16x16_sse2; DEGRAINCHROMA = Degrain2_sse2<16,16>;		 }
		 else {	                    OVERSCHROMA = Overlaps16x32_sse2; DEGRAINCHROMA = Degrain2_sse2<16,32>;		 }
      } break;
      case 16:
      if (nBlkSizeY==16) {          OVERSLUMA = Overlaps16x16_sse2; DEGRAINLUMA = Degrain2_sse2<16,16>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps8x8_sse2; DEGRAINCHROMA = Degrain2_sse2<8,8>;		 }
		 else {	                    OVERSCHROMA = Overlaps8x16_sse2;DEGRAINCHROMA = Degrain2_sse2<8,16>;		 }
      } else if (nBlkSizeY==8) {    OVERSLUMA = Overlaps16x8_sse2;  DEGRAINLUMA = Degrain2_sse2<16,8>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps8x4_sse2; DEGRAINCHROMA = Degrain2_sse2<8,4>;		 }
		 else {	                    OVERSCHROMA = Overlaps8x8_sse2; DEGRAINCHROMA = Degrain2_sse2<8,8>;		 }
      } else if (nBlkSizeY==2) {    OVERSLUMA = Overlaps16x2_sse2;  DEGRAINLUMA = Degrain2_sse2<16,2>;
		 if (yRatioUV==2) {         OVERSCHROMA = Overlaps8x1_sse2; DEGRAINCHROMA = Degrain2_sse2<8,1>;		 }
		 else {	                    OVERSCHROMA = Overlaps8x2_sse2; DEGRAINCHROMA = Degrain2_sse2<8,2>;		 }
      }
         break;
      case 4:
                                    OVERSLUMA = Overlaps4x4_sse2;    DEGRAINLUMA = Degrain2_mmx<4,4>;
		 if (yRatioUV==2) {			OVERSCHROMA = Overlaps_C<2,2>;	DEGRAINCHROMA = Degrain2_C<2,2>;		 }
		 else {			            OVERSCHROMA = Overlaps_C<2,4>;    DEGRAINCHROMA = Degrain2_C<2,4>;		 }
         break;
      case 8:
      default:
      if (nBlkSizeY==8) {           OVERSLUMA = Overlaps8x8_sse2;    DEGRAINLUMA = Degrain2_sse2<8,8>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps4x4_sse2;  DEGRAINCHROMA = Degrain2_sse2<4,4>;		 }
		 else {	                    OVERSCHROMA = Overlaps4x8_sse2;  DEGRAINCHROMA = Degrain2_sse2<4,8>;		 }
      }else if (nBlkSizeY==4) {     OVERSLUMA = Overlaps8x4_sse2;	DEGRAINLUMA = Degrain2_sse2<8,4>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps4x2_sse2;	DEGRAINCHROMA = Degrain2_sse2<4,2>;		 }
		 else {	                    OVERSCHROMA = Overlaps4x4_sse2;  DEGRAINCHROMA = Degrain2_sse2<4,4>;		 }
      }
      }
   }
   else if ( isse2 )
   {
	switch (nBlkSizeX)
      {
      case 32:
      if (nBlkSizeY==16) {          OVERSLUMA = Overlaps32x16_sse2;  DEGRAINLUMA = Degrain2_sse2<32,16>;
		 if (yRatioUV==2) {         OVERSCHROMA = Overlaps16x8_sse2; DEGRAINCHROMA = Degrain2_sse2<16,8>;		 }
		 else {                     OVERSCHROMA = Overlaps16x16_sse2;DEGRAINCHROMA = Degrain2_sse2<16,16>;		 }
      } else if (nBlkSizeY==32) {    OVERSLUMA = Overlaps32x32_sse2;  DEGRAINLUMA = Degrain2_sse2<32,32>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps16x16_sse2; DEGRAINCHROMA = Degrain2_sse2<16,16>;		 }
		 else {	                    OVERSCHROMA = Overlaps16x32_sse2; DEGRAINCHROMA = Degrain2_sse2<16,32>;		 }
      } break;
      case 16:
      if (nBlkSizeY==16) {          OVERSLUMA = Overlaps16x16_sse2; DEGRAINLUMA = Degrain2_sse2<16,16>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps8x8_sse2; DEGRAINCHROMA = Degrain2_sse2<8,8>;		 }
		 else {	                    OVERSCHROMA = Overlaps8x16_sse2;DEGRAINCHROMA = Degrain2_sse2<8,16>;		 }
      } else if (nBlkSizeY==8) {    OVERSLUMA = Overlaps16x8_sse2;  DEGRAINLUMA = Degrain2_sse2<16,8>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps8x4_sse2; DEGRAINCHROMA = Degrain2_sse2<8,4>;		 }
		 else {	                    OVERSCHROMA = Overlaps8x8_sse2; DEGRAINCHROMA = Degrain2_sse2<8,8>;		 }
      } else if (nBlkSizeY==2) {    OVERSLUMA = Overlaps16x2_sse2;  DEGRAINLUMA = Degrain2_sse2<16,2>;
		 if (yRatioUV==2) {         OVERSCHROMA = Overlaps8x1_sse2; DEGRAINCHROMA = Degrain2_sse2<8,1>;		 }
		 else {	                    OVERSCHROMA = Overlaps8x2_sse2; DEGRAINCHROMA = Degrain2_sse2<8,2>;		 }
      }
         break;
      case 4:
                                    OVERSLUMA = Overlaps4x4_sse2;    DEGRAINLUMA = Degrain2_sse2<4,4>;
		 if (yRatioUV==2) {			OVERSCHROMA = Overlaps_C<2,2>;	DEGRAINCHROMA = Degrain2_C<2,2>;		 }
		 else {			            OVERSCHROMA = Overlaps_C<2,4>;    DEGRAINCHROMA = Degrain2_C<2,4>;		 }
         break;
      case 8:
      default:
      if (nBlkSizeY==8) {           OVERSLUMA = Overlaps8x8_sse2;    DEGRAINLUMA = Degrain2_sse2<8,8>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps4x4_sse2;  DEGRAINCHROMA = Degrain2_sse2<4,4>;		 }
		 else {	                    OVERSCHROMA = Overlaps4x8_sse2;  DEGRAINCHROMA = Degrain2_sse2<4,8>;		 }
      }else if (nBlkSizeY==4) {     OVERSLUMA = Overlaps8x4_sse2;	DEGRAINLUMA = Degrain2_sse2<8,4>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps4x2_sse2;	DEGRAINCHROMA = Degrain2_sse2<4,2>;		 }
		 else {	                    OVERSCHROMA = Overlaps4x4_sse2;  DEGRAINCHROMA = Degrain2_sse2<4,4>;		 }
      }
      }
   }
   else
   {
	switch (nBlkSizeX)
      {
      case 32:
      if (nBlkSizeY==16) {          OVERSLUMA = Overlaps_C<32,16>;  DEGRAINLUMA = Degrain2_C<32,16>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps_C<16,8>; DEGRAINCHROMA = Degrain2_C<16,8>;		 }
		 else {	                    OVERSCHROMA = Overlaps_C<16,16>;DEGRAINCHROMA = Degrain2_C<16,16>;		 }
      } else if (nBlkSizeY==32) {    OVERSLUMA = Overlaps_C<32,32>;   DEGRAINLUMA = Degrain2_C<32,32>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps_C<16,16>;  DEGRAINCHROMA = Degrain2_C<16,16>;		 }
		 else {	                    OVERSCHROMA = Overlaps_C<16,32>;  DEGRAINCHROMA = Degrain2_C<16,32>;		 }
      } break;
      case 16:
      if (nBlkSizeY==16) {          OVERSLUMA = Overlaps_C<16,16>;  DEGRAINLUMA = Degrain2_C<16,16>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps_C<8,8>;  DEGRAINCHROMA = Degrain2_C<8,8>;		 }
		 else {	                    OVERSCHROMA = Overlaps_C<8,16>; DEGRAINCHROMA = Degrain2_C<8,16>;		 }
      } else if (nBlkSizeY==8) {    OVERSLUMA = Overlaps_C<16,8>;   DEGRAINLUMA = Degrain2_C<16,8>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps_C<8,4>;  DEGRAINCHROMA = Degrain2_C<8,4>;		 }
		 else {	                    OVERSCHROMA = Overlaps_C<8,8>;  DEGRAINCHROMA = Degrain2_C<8,8>;		 }
      } else if (nBlkSizeY==2) {    OVERSLUMA = Overlaps_C<16,2>;   DEGRAINLUMA = Degrain2_C<16,2>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps_C<8,1>;  DEGRAINCHROMA = Degrain2_C<8,1>;		 }
		 else {	                    OVERSCHROMA = Overlaps_C<8,2>;  DEGRAINCHROMA = Degrain2_C<8,2>;		 }
      }
         break;
      case 4:
                                    OVERSLUMA = Overlaps_C<4,4>;    DEGRAINLUMA = Degrain2_C<4,4>;
		 if (yRatioUV==2) {			OVERSCHROMA = Overlaps_C<2,2>;  DEGRAINCHROMA = Degrain2_C<2,2>;		 }
		 else {			            OVERSCHROMA = Overlaps_C<2,4>;  DEGRAINCHROMA = Degrain2_C<2,4>;		 }
         break;
      case 8:
      default:
      if (nBlkSizeY==8) {           OVERSLUMA = Overlaps_C<8,8>;    DEGRAINLUMA = Degrain2_C<8,8>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps_C<4,4>;  DEGRAINCHROMA = Degrain2_C<4,4>;		 }
		 else {	                    OVERSCHROMA = Overlaps_C<4,8>;  DEGRAINCHROMA = Degrain2_C<4,8>;		 }
      }else if (nBlkSizeY==4) {     OVERSLUMA = Overlaps_C<8,4>;    DEGRAINLUMA = Degrain2_C<8,4>;
		 if (yRatioUV==2) {	        OVERSCHROMA = Overlaps_C<4,2>;  DEGRAINCHROMA = Degrain2_C<4,2>;		 }
		 else {	                    OVERSCHROMA = Overlaps_C<4,4>;  DEGRAINCHROMA = Degrain2_C<4,4>;		 }
      }
      }
   }

	const int		tmp_size = 32 * 32;
	tmpBlock = new BYTE[tmp_size * height_lsb_mul];
	tmpBlockLsb = (lsb_flag) ? (tmpBlock + tmp_size) : 0;

	if (lsb_flag)
	{
		vi.height <<= 1;
	}
}


MVDegrain2::~MVDegrain2()
{
   if ( (pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2 && !planar)
   {
	delete DstPlanes;
	delete SrcPlanes;
   }
   if (nOverlapX >0 || nOverlapY>0)
   {
	   delete OverWins;
	   delete OverWinsUV;
	   delete [] DstShort;
	   delete [] DstInt;
   }
   delete [] tmpBlock;
   delete pRefBGOF;
   delete pRefFGOF;
   delete pRefB2GOF;
   delete pRefF2GOF;
}




PVideoFrame __stdcall MVDegrain2::GetFrame(int n, IScriptEnvironment* env)
{
	int nWidth_B = nBlkX*(nBlkSizeX - nOverlapX) + nOverlapX;
	int nHeight_B = nBlkY*(nBlkSizeY - nOverlapY) + nOverlapY;

	DebugPrintf("Degrain2: get source frame %d clip %d",n,super);
	PVideoFrame	src	= child->GetFrame(n, env);
	BYTE *pDst[3], *pDstCur[3];
	const BYTE *pSrcCur[3];
	const BYTE *pSrc[3];
	const BYTE *pRefB[3];
	const BYTE *pRefF[3];
	const BYTE *pRefB2[3];
	const BYTE *pRefF2[3];
	int nDstPitches[3], nSrcPitches[3];
	int nRefBPitches[3], nRefFPitches[3];
	int nRefB2Pitches[3], nRefF2Pitches[3];
	unsigned char *pDstYUY2;
	const unsigned char *pSrcYUY2;
	int nDstPitchYUY2;
	int nSrcPitchYUY2;
	bool isUsableB, isUsableF, isUsableB2, isUsableF2;
	int tmpPitch = nBlkSizeX;

	PVideoFrame mvF2 = mvClipF2.GetFrame(n, env);
	mvClipF2.Update(mvF2, env);
	isUsableF2 = mvClipF2.IsUsable();
	mvF2 = 0; // v2.0.9.2 -  it seems, we do not need in vectors clip anymore when we finished copiing them to fakeblockdatas
	PVideoFrame mvF = mvClipF.GetFrame(n, env);
	mvClipF.Update(mvF, env);
	isUsableF = mvClipF.IsUsable();
	mvF = 0; // free
	PVideoFrame mvB = mvClipB.GetFrame(n, env);
	mvClipB.Update(mvB, env);
	isUsableB = mvClipB.IsUsable();
	mvB = 0; // free
	PVideoFrame mvB2 = mvClipB2.GetFrame(n, env);
	mvClipB2.Update(mvB2, env);
	isUsableB2 = mvClipB2.IsUsable();
	mvB2 = 0;

	int				lsb_offset_y = 0;
	int				lsb_offset_u = 0;
	int				lsb_offset_v = 0;

	PVideoFrame dst = env->NewVideoFrame(vi);
	if ( (pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2 )
	{
		if (!planar)
		{
			pDstYUY2 = dst->GetWritePtr();
			nDstPitchYUY2 = dst->GetPitch();
			pDst[0] = DstPlanes->GetPtr();
			pDst[1] = DstPlanes->GetPtrU();
			pDst[2] = DstPlanes->GetPtrV();
			nDstPitches[0]  = DstPlanes->GetPitch();
			nDstPitches[1]  = DstPlanes->GetPitchUV();
			nDstPitches[2]  = DstPlanes->GetPitchUV();
			pSrcYUY2 = src->GetReadPtr();
			nSrcPitchYUY2 = src->GetPitch();
			pSrc[0] = SrcPlanes->GetPtr();
			pSrc[1] = SrcPlanes->GetPtrU();
			pSrc[2] = SrcPlanes->GetPtrV();
			nSrcPitches[0]  = SrcPlanes->GetPitch();
			nSrcPitches[1]  = SrcPlanes->GetPitchUV();
			nSrcPitches[2]  = SrcPlanes->GetPitchUV();
			YUY2ToPlanes(pSrcYUY2, nSrcPitchYUY2, nWidth, nHeight,
			pSrc[0], nSrcPitches[0], pSrc[1], pSrc[2], nSrcPitches[1], isse2);
		}
		else
		{
			pDst[0] = dst->GetWritePtr();
			pDst[1] = pDst[0] + nWidth;
			pDst[2] = pDst[1] + nWidth/2; // YUY2
			nDstPitches[0] = dst->GetPitch();
			nDstPitches[1] = nDstPitches[0];
			nDstPitches[2] = nDstPitches[0];
			pSrc[0] = src->GetReadPtr();
			pSrc[1] = pSrc[0] + nWidth;
			pSrc[2] = pSrc[1] + nWidth/2;
			nSrcPitches[0] = src->GetPitch();
			nSrcPitches[1] = nSrcPitches[0];
			nSrcPitches[2] = nSrcPitches[0];
		}
	}
	else
	{
		pDst[0] = YWPLAN(dst);
		pDst[1] = UWPLAN(dst);
		pDst[2] = VWPLAN(dst);
		nDstPitches[0] = YPITCH(dst);
		nDstPitches[1] = UPITCH(dst);
		nDstPitches[2] = VPITCH(dst);
		pSrc[0] = YRPLAN(src);
		pSrc[1] = URPLAN(src);
		pSrc[2] = VRPLAN(src);
		nSrcPitches[0] = YPITCH(src);
		nSrcPitches[1] = UPITCH(src);
		nSrcPitches[2] = VPITCH(src);
	}

	lsb_offset_y = nDstPitches [0] *  nHeight;
	lsb_offset_u = nDstPitches [1] * (nHeight / yRatioUV);
	lsb_offset_v = nDstPitches [2] * (nHeight / yRatioUV);

	if (lsb_flag)
	{
		memset (pDst [0] + lsb_offset_y, 0, lsb_offset_y);
		if (! planar)
		{
			memset (pDst [1] + lsb_offset_u, 0, lsb_offset_u);
			memset (pDst [2] + lsb_offset_v, 0, lsb_offset_v);
		}
	}

	PVideoFrame refB, refF, refB2, refF2;


	mvClipF2.use_ref_frame (refF2, isUsableF2, super, n, env);
	mvClipF .use_ref_frame (refF,  isUsableF,  super, n, env);
	mvClipB .use_ref_frame (refB,  isUsableB,  super, n, env);
	mvClipB2.use_ref_frame (refB2, isUsableB2, super, n, env);

	if ( (pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2 )
	{
		if (isUsableF2)
		{
			pRefF2[0] = refF2->GetReadPtr();
			pRefF2[1] = pRefF2[0] + refF2->GetRowSize()/2; // YUY2
			pRefF2[2] = pRefF2[1] + refF2->GetRowSize()/4;
			nRefF2Pitches[0]  = refF2->GetPitch();
			nRefF2Pitches[1]  = nRefF2Pitches[0];
			nRefF2Pitches[2]  = nRefF2Pitches[0];
		}
		if (isUsableF)
		{
			pRefF[0] = refF->GetReadPtr();
			pRefF[1] = pRefF[0] + refF->GetRowSize()/2;
			pRefF[2] = pRefF[1] + refF->GetRowSize()/4;
			nRefFPitches[0]  = refF->GetPitch();
			nRefFPitches[1]  = nRefFPitches[0];
			nRefFPitches[2]  = nRefFPitches[0];
		}
		if (isUsableB)
		{
			pRefB[0] = refB->GetReadPtr();
			pRefB[1] = pRefB[0] + refB->GetRowSize()/2;
			pRefB[2] = pRefB[1] + refB->GetRowSize()/4;
			nRefBPitches[0]  = refB->GetPitch();
			nRefBPitches[1]  = nRefBPitches[0];
			nRefBPitches[2]  = nRefBPitches[0];
		}
		if (isUsableB2)
		{
			pRefB2[0] = refB2->GetReadPtr();
			pRefB2[1] = pRefB2[0] + refB2->GetRowSize()/2;
			pRefB2[2] = pRefB2[1] + refB2->GetRowSize()/4;
			nRefB2Pitches[0]  = refB2->GetPitch();
			nRefB2Pitches[1]  = nRefB2Pitches[0];
			nRefB2Pitches[2]  = nRefB2Pitches[0];
		}
	}
	else
	{
		if (isUsableF2)
		{
			pRefF2[0] = YRPLAN(refF2);
			pRefF2[1] = URPLAN(refF2);
			pRefF2[2] = VRPLAN(refF2);
			nRefF2Pitches[0] = YPITCH(refF2);
			nRefF2Pitches[1] = UPITCH(refF2);
			nRefF2Pitches[2] = VPITCH(refF2);
		}
		if (isUsableF)
		{
			pRefF[0] = YRPLAN(refF);
			pRefF[1] = URPLAN(refF);
			pRefF[2] = VRPLAN(refF);
			nRefFPitches[0] = YPITCH(refF);
			nRefFPitches[1] = UPITCH(refF);
			nRefFPitches[2] = VPITCH(refF);
		}
		if (isUsableB)
		{
			pRefB[0] = YRPLAN(refB);
			pRefB[1] = URPLAN(refB);
			pRefB[2] = VRPLAN(refB);
			nRefBPitches[0] = YPITCH(refB);
			nRefBPitches[1] = UPITCH(refB);
			nRefBPitches[2] = VPITCH(refB);
		}
		if (isUsableB2)
		{
			pRefB2[0] = YRPLAN(refB2);
			pRefB2[1] = URPLAN(refB2);
			pRefB2[2] = VRPLAN(refB2);
			nRefB2Pitches[0] = YPITCH(refB2);
			nRefB2Pitches[1] = UPITCH(refB2);
			nRefB2Pitches[2] = VPITCH(refB2);
		}
	}

	MVPlane *pPlanesB[3]  = { 0 };
	MVPlane *pPlanesF[3]  = { 0 };
	MVPlane *pPlanesB2[3] = { 0 };
	MVPlane *pPlanesF2[3] = { 0 };

	if (isUsableF2)
	{
		pRefF2GOF->Update(YUVplanes, (BYTE*)pRefF2[0], nRefF2Pitches[0], (BYTE*)pRefF2[1], nRefF2Pitches[1], (BYTE*)pRefF2[2], nRefF2Pitches[2]);
		if (YUVplanes & YPLANE)
			pPlanesF2[0] = pRefF2GOF->GetFrame(0)->GetPlane(YPLANE);
		if (YUVplanes & UPLANE)
			pPlanesF2[1] = pRefF2GOF->GetFrame(0)->GetPlane(UPLANE);
		if (YUVplanes & VPLANE)
			pPlanesF2[2] = pRefF2GOF->GetFrame(0)->GetPlane(VPLANE);
	}
	if (isUsableF)
	{
		pRefFGOF->Update(YUVplanes, (BYTE*)pRefF[0], nRefFPitches[0], (BYTE*)pRefF[1], nRefFPitches[1], (BYTE*)pRefF[2], nRefFPitches[2]);
		if (YUVplanes & YPLANE)
			pPlanesF[0] = pRefFGOF->GetFrame(0)->GetPlane(YPLANE);
		if (YUVplanes & UPLANE)
			pPlanesF[1] = pRefFGOF->GetFrame(0)->GetPlane(UPLANE);
		if (YUVplanes & VPLANE)
			pPlanesF[2] = pRefFGOF->GetFrame(0)->GetPlane(VPLANE);
	}
	if (isUsableB)
	{
		pRefBGOF->Update(YUVplanes, (BYTE*)pRefB[0], nRefBPitches[0], (BYTE*)pRefB[1], nRefBPitches[1], (BYTE*)pRefB[2], nRefBPitches[2]);// v2.0
		if (YUVplanes & YPLANE)
			pPlanesB[0] = pRefBGOF->GetFrame(0)->GetPlane(YPLANE);
		if (YUVplanes & UPLANE)
			pPlanesB[1] = pRefBGOF->GetFrame(0)->GetPlane(UPLANE);
		if (YUVplanes & VPLANE)
			pPlanesB[2] = pRefBGOF->GetFrame(0)->GetPlane(VPLANE);
	}
	if (isUsableB2)
	{
		pRefB2GOF->Update(YUVplanes, (BYTE*)pRefB2[0], nRefB2Pitches[0], (BYTE*)pRefB2[1], nRefB2Pitches[1], (BYTE*)pRefB2[2], nRefB2Pitches[2]);// v2.0
		if (YUVplanes & YPLANE)
			pPlanesB2[0] = pRefB2GOF->GetFrame(0)->GetPlane(YPLANE);
		if (YUVplanes & UPLANE)
			pPlanesB2[1] = pRefB2GOF->GetFrame(0)->GetPlane(UPLANE);
		if (YUVplanes & VPLANE)
			pPlanesB2[2] = pRefB2GOF->GetFrame(0)->GetPlane(VPLANE);
	}

	PROFILE_START(MOTION_PROFILE_COMPENSATION);
	pDstCur[0] = pDst[0];
	pDstCur[1] = pDst[1];
	pDstCur[2] = pDst[2];
	pSrcCur[0] = pSrc[0];
	pSrcCur[1] = pSrc[1];
	pSrcCur[2] = pSrc[2];

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// LUMA plane Y

	if (!(YUVplanes & YPLANE))
	{
		BitBlt(pDstCur[0], nDstPitches[0], pSrcCur[0], nSrcPitches[0], nWidth, nHeight, isse2);
	}

	else
	{
		if (nOverlapX==0 && nOverlapY==0)
		{
			for (int by=0; by<nBlkY; by++)
			{
				int xx = 0;
				for (int bx=0; bx<nBlkX; bx++)
				{
					int i = by*nBlkX + bx;
					const BYTE * pB, *pF, *pB2, *pF2;
					int npB, npF, npB2, npF2;
					int WSrc, WRefB, WRefF, WRefB2, WRefF2;

					use_block_y (pB , npB , WRefB , isUsableB , mvClipB , i, pPlanesB  [0], pSrcCur [0], xx, nSrcPitches [0]);
					use_block_y (pF , npF , WRefF , isUsableF , mvClipF , i, pPlanesF  [0], pSrcCur [0], xx, nSrcPitches [0]);
					use_block_y (pB2, npB2, WRefB2, isUsableB2, mvClipB2, i, pPlanesB2 [0], pSrcCur [0], xx, nSrcPitches [0]);
					use_block_y (pF2, npF2, WRefF2, isUsableF2, mvClipF2, i, pPlanesF2 [0], pSrcCur [0], xx, nSrcPitches [0]);
					norm_weights (WSrc, WRefB, WRefF, WRefB2, WRefF2);

					// luma
					DEGRAINLUMA(pDstCur[0] + xx, pDstCur[0] + lsb_offset_y + xx,
						lsb_flag, nDstPitches[0], pSrcCur[0]+ xx, nSrcPitches[0],
						pB, npB, pF, npF, pB2, npB2, pF2, npF2,
						WSrc, WRefB, WRefF, WRefB2, WRefF2);

					xx += (nBlkSizeX);

					if (bx == nBlkX-1 && nWidth_B < nWidth) // right non-covered region
					{
						// luma
						BitBlt(pDstCur[0] + nWidth_B, nDstPitches[0],
							pSrcCur[0] + nWidth_B, nSrcPitches[0], nWidth-nWidth_B, nBlkSizeY, isse2);
					}
				}	// for bx

				pDstCur[0] += ( nBlkSizeY ) * (nDstPitches[0]);
				pSrcCur[0] += ( nBlkSizeY ) * (nSrcPitches[0]);

				if (by == nBlkY-1 && nHeight_B < nHeight) // bottom uncovered region
				{
					// luma
					BitBlt(pDstCur[0], nDstPitches[0], pSrcCur[0], nSrcPitches[0], nWidth, nHeight-nHeight_B, isse2);
				}
			}	// for by
		}	// nOverlapX==0 && nOverlapY==0

// -----------------------------------------------------------------

		else // overlap
		{
			unsigned short *pDstShort = DstShort;
			int *pDstInt = DstInt;
			const int tmpPitch = nBlkSizeX;

			if (lsb_flag)
			{
				MemZoneSet(reinterpret_cast<unsigned char*>(pDstInt), 0,
					nWidth_B*4, nHeight_B, 0, 0, dstIntPitch*4);
			}
			else
			{
				MemZoneSet(reinterpret_cast<unsigned char*>(pDstShort), 0,
					nWidth_B*2, nHeight_B, 0, 0, dstShortPitch*2);
			}

			for (int by=0; by<nBlkY; by++)
			{
				int wby = ((by + nBlkY - 3)/(nBlkY - 2))*3;
				int xx = 0;
				for (int bx=0; bx<nBlkX; bx++)
				{
					// select window
					int wbx = (bx + nBlkX - 3)/(nBlkX - 2);
					short *			winOver = OverWins->GetWindow(wby + wbx);

					int i = by*nBlkX + bx;
					const BYTE * pB, *pF, *pB2, *pF2;
					int npB, npF, npB2, npF2;
					int WSrc, WRefB, WRefF, WRefB2, WRefF2;

					use_block_y (pB , npB , WRefB , isUsableB , mvClipB , i, pPlanesB  [0], pSrcCur [0], xx, nSrcPitches [0]);
					use_block_y (pF , npF , WRefF , isUsableF , mvClipF , i, pPlanesF  [0], pSrcCur [0], xx, nSrcPitches [0]);
					use_block_y (pB2, npB2, WRefB2, isUsableB2, mvClipB2, i, pPlanesB2 [0], pSrcCur [0], xx, nSrcPitches [0]);
					use_block_y (pF2, npF2, WRefF2, isUsableF2, mvClipF2, i, pPlanesF2 [0], pSrcCur [0], xx, nSrcPitches [0]);
					norm_weights (WSrc, WRefB, WRefF, WRefB2, WRefF2);

					// luma
					DEGRAINLUMA(tmpBlock, tmpBlockLsb, lsb_flag, tmpPitch, pSrcCur[0]+ xx, nSrcPitches[0],
						pB, npB, pF, npF, pB2, npB2, pF2, npF2,
						WSrc, WRefB, WRefF, WRefB2, WRefF2);
					if (lsb_flag)
					{
						OVERSLUMALSB(pDstInt + xx, dstIntPitch, tmpBlock, tmpBlockLsb, tmpPitch, winOver, nBlkSizeX);
					}
					else
					{
						OVERSLUMA(pDstShort + xx, dstShortPitch, tmpBlock, tmpPitch, winOver, nBlkSizeX);
					}

					xx += (nBlkSizeX - nOverlapX);
				}	// for bx

				pSrcCur[0] += (nBlkSizeY - nOverlapY) * (nSrcPitches[0]);
				pDstShort += (nBlkSizeY - nOverlapY) * dstShortPitch;
				pDstInt += (nBlkSizeY - nOverlapY) * dstIntPitch;
			}	// for by
			if (lsb_flag)
			{
				Short2BytesLsb(pDst[0], pDst[0] + lsb_offset_y, nDstPitches[0], DstInt, dstIntPitch, nWidth_B, nHeight_B);
			}
			else
			{
				Short2Bytes(pDst[0], nDstPitches[0], DstShort, dstShortPitch, nWidth_B, nHeight_B);
			}
			if (nWidth_B < nWidth)
			{
				BitBlt(pDst[0] + nWidth_B, nDstPitches[0],
					pSrc[0] + nWidth_B, nSrcPitches[0],
					nWidth-nWidth_B, nHeight_B, isse2);
			}
			if (nHeight_B < nHeight) // bottom noncovered region
			{
				BitBlt(pDst[0] + nHeight_B*nDstPitches[0], nDstPitches[0],
					pSrc[0] + nHeight_B*nSrcPitches[0], nSrcPitches[0],
					nWidth, nHeight-nHeight_B, isse2);
			}
		}	// overlap - end

		if (nLimit < (pixelsize == 1 ? 255 : 65535))
		{
			if ((pixelsize==1) && isse2)
			{
				LimitChanges_sse2(pDst[0], nDstPitches[0], pSrc[0], nSrcPitches[0], nWidth, nHeight, nLimit);
			}
			else
			{
                if(pixelsize==1)
                    LimitChanges_c<uint8_t>(pDst[0], nDstPitches[0], pSrc[0], nSrcPitches[0], nWidth, nHeight, nLimit);
                else
                    LimitChanges_c<uint16_t>(pDst[0], nDstPitches[0], pSrc[0], nSrcPitches[0], nWidth, nHeight, nLimit);
            }
		}
	}

//----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// CHROMA plane U

	process_chroma (
		UPLANE & nSuperModeYUV,
		pDst [1], pDstCur [1], nDstPitches [1], pSrc [1], pSrcCur [1], nSrcPitches [1],
		isUsableB, isUsableF, isUsableB2, isUsableF2,
		pPlanesB [1], pPlanesF [1], pPlanesB2 [1], pPlanesF2 [1],
		lsb_offset_u, nWidth_B, nHeight_B
	);

//----------------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// CHROMA plane V

	process_chroma (
		VPLANE & nSuperModeYUV,
		pDst [2], pDstCur [2], nDstPitches [2], pSrc [2], pSrcCur [2], nSrcPitches [2],
		isUsableB, isUsableF, isUsableB2, isUsableF2,
		pPlanesB [2], pPlanesF [2], pPlanesB2 [2], pPlanesF2 [2],
		lsb_offset_v, nWidth_B, nHeight_B
	);

//--------------------------------------------------------------------------------

	_mm_empty ();	// (we may use double-float somewhere) Fizick

	PROFILE_STOP(MOTION_PROFILE_COMPENSATION);


	if ( (pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2 && !planar)
	{
		YUY2FromPlanes(pDstYUY2, nDstPitchYUY2, nWidth, nHeight * height_lsb_mul,
					  pDst[0], nDstPitches[0], pDst[1], pDst[2], nDstPitches[1], isse2);
	}
   return dst;
}



void	MVDegrain2::process_chroma (int plane_mask, BYTE *pDst, BYTE *pDstCur, int nDstPitch, const BYTE *pSrc, const BYTE *pSrcCur, int nSrcPitch, bool isUsableB, bool isUsableF, bool isUsableB2, bool isUsableF2, MVPlane *pPlanesB, MVPlane *pPlanesF, MVPlane *pPlanesB2, MVPlane *pPlanesF2, int lsb_offset_uv, int nWidth_B, int nHeight_B)
{
	if (!(YUVplanes & plane_mask))
	{
		BitBlt(pDstCur, nDstPitch, pSrcCur, nSrcPitch, nWidth/xRatioUV*pixelsize, nHeight/yRatioUV, isse2);
	}

	else
	{
		if (nOverlapX==0 && nOverlapY==0)
		{
			for (int by=0; by<nBlkY; by++)
			{
				int xx = 0;
				for (int bx=0; bx<nBlkX; bx++)
				{
					int i = by*nBlkX + bx;
					const BYTE * pBV, *pFV, *pB2V, *pF2V;
					int npBV, npFV, npB2V, npF2V;
					int WSrc, WRefB, WRefF, WRefB2, WRefF2;

					use_block_uv (pBV , npBV , WRefB , isUsableB , mvClipB , i, pPlanesB , pSrcCur, xx, nSrcPitch);
					use_block_uv (pFV , npFV , WRefF , isUsableF , mvClipF , i, pPlanesF , pSrcCur, xx, nSrcPitch);
					use_block_uv (pB2V, npB2V, WRefB2, isUsableB2, mvClipB2, i, pPlanesB2, pSrcCur, xx, nSrcPitch);
					use_block_uv (pF2V, npF2V, WRefF2, isUsableF2, mvClipF2, i, pPlanesF2, pSrcCur, xx, nSrcPitch);
					norm_weights (WSrc, WRefB, WRefF, WRefB2, WRefF2);

					// chroma
					DEGRAINCHROMA(pDstCur + (xx/xRatioUV)*pixelsize, pDstCur + (xx/xRatioUV)*pixelsize + lsb_offset_uv,
						lsb_flag, nDstPitch, pSrcCur + (xx/xRatioUV)*pixelsize, nSrcPitch,
						pBV, npBV, pFV, npFV, pB2V, npB2V, pF2V, npF2V,
						WSrc, WRefB, WRefF, WRefB2, WRefF2);

					xx += (nBlkSizeX)*pixelsize;

					if (bx == nBlkX-1 && nWidth_B < nWidth) // right non-covered region
					{
						// chroma
						BitBlt(pDstCur + (nWidth_B/xRatioUV)*pixelsize, nDstPitch,
							pSrcCur + (nWidth_B/xRatioUV)*pixelsize, nSrcPitch, (nWidth-nWidth_B)/xRatioUV*pixelsize, (nBlkSizeY)/yRatioUV, isse2);
					}
				}	// for bx

				pDstCur += ( nBlkSizeY )/yRatioUV * nDstPitch;
				pSrcCur += ( nBlkSizeY )/yRatioUV * nSrcPitch;

				if (by == nBlkY-1 && nHeight_B < nHeight) // bottom uncovered region
				{
					// chroma
					BitBlt(pDstCur, nDstPitch, pSrcCur, nSrcPitch, nWidth/xRatioUV*pixelsize, (nHeight-nHeight_B)/yRatioUV, isse2);
				}
			}	// for by
		}	// nOverlapX==0 && nOverlapY==0

// -----------------------------------------------------------------

		else // overlap
		{
			unsigned short *pDstShort = DstShort;
			int *pDstInt = DstInt;
            const int tmpPitch = nBlkSizeX; // todo check tmpPitch usage for pixelsize

			if (lsb_flag)
			{
				MemZoneSet(reinterpret_cast<unsigned char*>(pDstInt), 0,
					nWidth_B*4/xRatioUV, nHeight_B/yRatioUV, 0, 0, dstIntPitch*4);
			}
			else
			{
				MemZoneSet(reinterpret_cast<unsigned char*>(pDstShort), 0,
					nWidth_B*2/xRatioUV, nHeight_B/yRatioUV, 0, 0, dstShortPitch*2);
			}

			for (int by=0; by<nBlkY; by++)
			{
				int wby = ((by + nBlkY - 3)/(nBlkY - 2))*3;
				int xx = 0;
				for (int bx=0; bx<nBlkX; bx++)
				{
					// select window
					int wbx = (bx + nBlkX - 3)/(nBlkX - 2);
					short *			winOverUV = OverWinsUV->GetWindow(wby + wbx);

					int i = by*nBlkX + bx;
					const BYTE * pBV, *pFV, *pB2V, *pF2V;
					int npBV, npFV, npB2V, npF2V;
					int WSrc, WRefB, WRefF, WRefB2, WRefF2;

					use_block_uv (pBV , npBV , WRefB , isUsableB , mvClipB , i, pPlanesB , pSrcCur, xx, nSrcPitch);
					use_block_uv (pFV , npFV , WRefF , isUsableF , mvClipF , i, pPlanesF , pSrcCur, xx, nSrcPitch);
					use_block_uv (pB2V, npB2V, WRefB2, isUsableB2, mvClipB2, i, pPlanesB2, pSrcCur, xx, nSrcPitch);
					use_block_uv (pF2V, npF2V, WRefF2, isUsableF2, mvClipF2, i, pPlanesF2, pSrcCur, xx, nSrcPitch);
					norm_weights (WSrc, WRefB, WRefF, WRefB2, WRefF2);

					// chroma
					DEGRAINCHROMA(tmpBlock, tmpBlockLsb, lsb_flag, tmpPitch, pSrcCur + (xx/xRatioUV)*pixelsize, nSrcPitch,
						pBV, npBV, pFV, npFV, pB2V, npB2V, pF2V, npF2V,
						WSrc, WRefB, WRefF, WRefB2, WRefF2);
					if (lsb_flag)
					{ // no pixelsize int and short pointers
						OVERSCHROMALSB(pDstInt + (xx/xRatioUV), dstIntPitch, tmpBlock, tmpBlockLsb, tmpPitch, winOverUV, nBlkSizeX/xRatioUV);
					}
					else
					{
						OVERSCHROMA(pDstShort + (xx/xRatioUV), dstShortPitch, tmpBlock, tmpPitch, winOverUV, nBlkSizeX/xRatioUV);
					}

					xx += (nBlkSizeX - nOverlapX);

				}	// for bx

				pSrcCur += (nBlkSizeY - nOverlapY)/yRatioUV * nSrcPitch ;
				pDstShort += (nBlkSizeY - nOverlapY)/yRatioUV * dstShortPitch;
				pDstInt += (nBlkSizeY - nOverlapY)/yRatioUV * dstIntPitch;
			}	// for by

			if (lsb_flag)
			{
				Short2BytesLsb(pDst, pDst + lsb_offset_uv, nDstPitch, DstInt, dstIntPitch, nWidth_B/xRatioUV, nHeight_B/yRatioUV);
			}
			else
			{
				Short2Bytes(pDst, nDstPitch, DstShort, dstShortPitch, nWidth_B/xRatioUV, nHeight_B/yRatioUV);
			}
			if (nWidth_B < nWidth)
			{
				BitBlt(pDst + (nWidth_B/xRatioUV)*pixelsize, nDstPitch,
					pSrc + (nWidth_B/xRatioUV)*pixelsize, nSrcPitch,
					(nWidth-nWidth_B)/xRatioUV*pixelsize, nHeight_B/yRatioUV, isse2);
			}
			if (nHeight_B < nHeight) // bottom noncovered region
			{
				BitBlt(pDst + nDstPitch*nHeight_B/yRatioUV, nDstPitch,
					pSrc + nSrcPitch*nHeight_B/yRatioUV, nSrcPitch,
					nWidth/xRatioUV*pixelsize, (nHeight-nHeight_B)/yRatioUV, isse2);
			}
		}	// overlap - end

		if (nLimitC < (pixelsize == 1 ? 255 : 65535))
		{
			if ((pixelsize==1) && isse2)
			{
				LimitChanges_sse2(pDst, nDstPitch, pSrc, nSrcPitch, nWidth/xRatioUV, nHeight/yRatioUV, nLimitC);
			}
			else
			{
                if(pixelsize==1)
				    LimitChanges_c<uint8_t>(pDst, nDstPitch, pSrc, nSrcPitch, nWidth/xRatioUV, nHeight/yRatioUV, nLimitC);
                else
                    LimitChanges_c<uint16_t>(pDst, nDstPitch, pSrc, nSrcPitch, nWidth/xRatioUV, nHeight/yRatioUV, nLimitC);
            }
		}
	}
}


void	MVDegrain2::use_block_y (const BYTE * &p, int &np, int &WRef, bool isUsable, const MVClip &mvclip, int i, const MVPlane *pPlane, const BYTE *pSrcCur, int xx, int nSrcPitch)
{
	if (isUsable)
	{
		const FakeBlockData &block = mvclip.GetBlock(0, i);
		int blx = block.GetX() * nPel + block.GetMV().x;
		int bly = block.GetY() * nPel + block.GetMV().y;
		p = pPlane->GetPointer(blx, bly);
		np = pPlane->GetPitch();
		int blockSAD = block.GetSAD();
		WRef = DegrainWeight(thSAD, blockSAD);
	}
	else
	{
		p = pSrcCur + xx; // check: pixelsize or xx increments OK?
		np = nSrcPitch;
		WRef = 0;
	}
}

void	MVDegrain2::use_block_uv (const BYTE * &p, int &np, int &WRef, bool isUsable, const MVClip &mvclip, int i, const MVPlane *pPlane, const BYTE *pSrcCur, int xx, int nSrcPitch)
{
	if (isUsable)
	{
		const FakeBlockData &block = mvclip.GetBlock(0, i);
		int blx = block.GetX() * nPel + block.GetMV().x;
		int bly = block.GetY() * nPel + block.GetMV().y;
		p = pPlane->GetPointer(blx/xRatioUV, bly/yRatioUV);
		np = pPlane->GetPitch();
		int blockSAD = block.GetSAD();
		WRef = DegrainWeight(thSADC, blockSAD);
	}
	else
	{
		p = pSrcCur + (xx/xRatioUV);
		np = nSrcPitch;
		WRef = 0;
	}
}



void	MVDegrain2::norm_weights (int &WSrc, int &WRefB, int &WRefF, int &WRefB2, int &WRefF2)
{
	WSrc = 256;
	int WSum = WRefB + WRefF + WSrc + WRefB2 + WRefF2 + 1;
	WRefB  = WRefB*256/WSum; // normalize weights to 256
	WRefF  = WRefF*256/WSum;
	WRefB2 = WRefB2*256/WSum;
	WRefF2 = WRefF2*256/WSum;
	WSrc = 256 - WRefB - WRefF - WRefB2 - WRefF2;
}
