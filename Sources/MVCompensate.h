#ifndef __MV_COMPENSATE__
#define __MV_COMPENSATE__

#include	"conc/AtomicInt.h"
#include "CopyCode.h"
#include	"MTSlicer.h"
#include "MVClip.h"
#include "MVFilter.h"
#include "overlap.h"
#include "SharedPtr.h"
#include "yuy2planes.h"

#include	<vector>



class MVGroupOfFrames;
class MVPlane;

/*! \brief Filter that compensate the picture
 */
class MVCompensate
:	public GenericVideoFilter
,	public MVFilter
{

public:

	MVCompensate(
		PClip _child, PClip _super, PClip vectors, bool sc, double _recursionPercent,
		int thsad, bool _fields, int nSCD1, int nSCD2, bool isse2, bool _planar,
		bool mt_flag, int trad, bool center_flag, PClip cclip_sptr, int thsad2,
		IScriptEnvironment* env_ptr
	);
	~MVCompensate();
	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);

private:

	typedef	SharedPtr <MVClip>	MVClipSPtr;
	class MvClipInfo
	{
	public:
		MVClipSPtr		_clip_sptr;
		int				_thsad;
	};
	typedef	std::vector <MvClipInfo>	MvClipArray;

	typedef	MTSlicer <MVCompensate>	Slicer;

	void           compensate_slice_normal (Slicer::TaskData &td);
	void           compensate_slice_overlap (Slicer::TaskData &td);
	void           compensate_slice_overlap (int y_beg, int y_end);
	bool           compute_src_frame (int &nsrc, int &nvec, int &vindex, int n) const;

	MvClipArray    _mv_clip_arr;
	bool scBehavior;
	int recursion;
	bool fields;
	PClip super;
	bool isse2;
	bool planar;

	COPYFunction *BLITLUMA;
	COPYFunction *BLITCHROMA;

	YUY2Planes * DstPlanes;

	OverlapWindows *OverWins;
	OverlapWindows *OverWinsUV;

	OverlapsFunction *OVERSLUMA;
	OverlapsFunction *OVERSCHROMA;
	unsigned short * DstShort;
	unsigned short * DstShortU;
	unsigned short * DstShortV;
	int dstShortPitch;
	int dstShortPitchUV;

	int nSuperWidth;
	int nSuperHeight;
	int nSuperHPad;
	int nSuperVPad;
	MVGroupOfFrames *pRefGOF;
	MVGroupOfFrames *pSrcGOF;

	unsigned char *pLoop[3];
	int nLoopPitches[3];

	int            _trad;         // Temporal radius. 0 = single frame
	PClip          _cclip_sptr;   // Frame that will be introduced at the center of the compensated frames.
	bool           _multi_flag;
	bool           _center_flag;  // Indicates if the output frames should be in the order -tr, ..., -1, C, +1, ..., +tr (true) or -1, +1, -2, +2,..., -tr, +tr (false).

	bool           _mt_flag;

	// Processing variables
	MVClip *       _mv_clip_ptr;  // Vector clip used to process this frame
	int            _thsad;
	const int		ySubUV;
	int            fieldShift;
	BYTE *         pDst [3];
	int            nDstPitches [3];
	const BYTE *   pSrc[3];
	int            nSrcPitches[3];
	MVPlane *      pPlanes[3];
	MVPlane *      pSrcPlanes[3];

	// This array has an nBlkY size. It is used in vertical overlap mode
	// to avoid read/write sync problems when processing is multithreaded.
	// Only elements corresponding to the first row of each sub-plane are
	// actually used. They count how many sub-planes (excepted their last
	// row) have been processed on each side of the boundary. When a counter
	// reaches 2, the boundary row (just above the element position) can be
	// processed safely.
	std::vector <conc::AtomicInt <int> >
						_boundary_cnt_arr;

};

#endif
