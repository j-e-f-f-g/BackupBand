// Backup Band for Linux
// Copyright 2013 Jeff Glatt

// Backup Band is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Backup Band is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Backup Band. If not, see <http://www.gnu.org/licenses/>.

#include "Options.h"
#include "Main.h"
#include "StyleData.h"
#include "AccompSeq.h"
#include "AudioPlay.h"
#include "SongSheet.h"
#include "FileLoad.h"
#ifndef O_NOATIME
#define O_NOATIME        01000000
#endif

#define VARFLAG_DEFAULT_END		0x80
#define VARFLAG_HAS_HALFMEAS		0x40



#pragma pack(1)
// Prepended to STYLE_VARIATION to maintain a linked list
struct VARIATION_HEAD {
	struct VARIATION_HEAD *	Next;
	uint32_t						Name;			// Name hash formed from filename
};

// Holds the data for a drum, bass, or guitar variation
typedef struct {
	unsigned char				NumPtns;		// How many total 1 measure patterns. Max=127.
	union {
	unsigned char				NumFills;	// Drums: How many total 1 measure fill patterns. or'ed with VARFLAG_DEFAULT_END if using DefaultDrumEnding pattern
	unsigned char				Flags;		// Bass/Gtr: Flag bits
	} u;
	unsigned char				NumChains;	// How many total chains
	unsigned char				Chains[1];	// Variable-sized array holding the chains/patterns
} STYLE_VARIATION;

#pragma pack()

/* =============================== Guitar Variation ================================

 "Flags" are GTRFLAG bit flags that describe the variation.

 The next bytes are the ptn chain. The first byte is a count of how many measures
 are in the chain (including the count), followed by a ptn # for each measure. For example:
 3+1, 9, 1, 7
 means there are 3 measures. Ptn #9 plays for the first meas, then ptn #1, then #3.
 This repeats indefinitely until the user changes to another style, or ends play
 (at which time an end variation is played). If a byte is negative, then it is a Repeat
 Marker. A Repeat Marker indicates how many Chains backward to jump in order to repeat
 the section (ie, it's a negative offset to the Chain that begins the section.)

 After the ptn chain are the ptns. Each ptn is one full measure's worth of
 events (except for the first ptn (#0) which may be the latter half of a measure).
 All of the ptns for a guitar variation are stored in one array.

 For each ptn:
 The first byte is a count of how many more bytes are in the ptn (including this
 count byte). The remaining bytes are the events. Each event starts with a Timing
 byte referenced in 24 PPQN.

 The next byte is either the scale step #, or string #, or special (non-note) event.
 I call this the "note specification" byte. If top nibble has only bit 4 set
 (NON_NOTE), then this is a special event. If bit #7 set (SCALESTEP), then low nibble
 is an index into Scale[], and an octave #. If bit #7 clear (STRINGNUM), then low
 nibble is a string # of 1 (highest) to 6 (lowest), or 7 for all strings downstroke,
 8 for all strings upstroke, 9 for highest strings downstroke, 10 for highest strings
 upstroke, 13 for root+fifth Power chord, or 15 for root+sixth Power chord.

 For a note-on event, there follows a velocity byte.

 The last event in the ptn is the measure end event. It has only a Timing byte =
 PPQN in 1 measure.

 Bit assignment:

 NOTE ON
 7         |  6     |   5    |   4  3        |   2 1 0        |  7         | 6 5 4 3 2 1
 flag =     Bend +   OFF flag=     Octave         Step #      | Hammer-On    Velocity
 SCALESTEP   flag       0      0 (low) to 3    0 (root) to 6  |  flag

 7         |  6     |   5    |   4        |   3 2 1 0         |   7       | 6 5 4 3 2 1
 flag =     Bend +   Bend -   Non-Note        String #        | Hammer-On    Velocity
 STRINGNUM   flag     flag     flag=0         1 to 6          |  flag
                                              7 down chord
                                              8 up chord
                                              9 down chord 2
                                              10 up chord 2
															 13 Power 5th
															 15 Power 6th

 NOTE OFF
 7         |  6     |   5    |   4  3        |   2 1 0
 flag =     Bend +   OFF flag=    Octave         Step #
 SCALESTEP  flag			1	    0 (low) to 3    0 (root) to 6

 7         |  6     |   5    |   4        |   3 2 1 0
 flag =     flag=1   flag=1    Unused        String #
 STRINGNUM        			    					1 to 6
															 0 all notes off


 ARTICULATION CHANGE EVENT
  7  6  5  4        |   3 2 1 0         |   7 6 5 4 3 2 1
 Special ID = 1         Cmd = 0			0 = normal, 1=muted, 2=harmonic, 3=chord chunk

 EFFECTS ON-OFF EVENT

*/

/* =============================== Bass Variation ================================
 All of the ptns for a bass Variation are stored in one array, like with the guitar,
 but with the following differences:

 A ptn event byte is the scale step # and velocity. If 0, a Note Off event. The
 high nibble is velocity offset from 90. Low 3 bits is an index into Scale[].
 If bit 3 set, subtract an octave.

 NOTE ON
 7         |  6     |   5    |   4      |  3      |   2 1 0        |  7 6 5 4 3 2 1
 flag = 0   Bend +   Bend -     Oct +      Oct -		Step #       |   Velocity
			    flag     flag     flag			flag		0 (root) to 6   |

 NOTE OFF
  7         |  6 5 4    | 3 2 1 0
 flag = 1      Cmd = 0    Data = 0

 ARTICULATION CHANGE EVENT
  7          | 6 5 4    | 3 2 1 0
  Flag = 1		Cmd = 1		0=normal
									1=muted
									2=slap
									3=pop

 */

// =============================== Drum Variation ================================
//
// Chains[] is an array of bytes that represent which ptns to play in ascending order.
// Each byte, if a positive number, is the ptn # to be played. I refer to such a byte as a
// "Pattern Chain". If a byte is OR'ed with 0x80, then it indicates a fill measure should
// be substituted.
//
// NOTES: A variation is limited to 128 ptns (ie, ptn # 0 to 127).
//
// A loop is limited to encompassing 127 chains.
//
// All of the ptns for a variation are stored in one array. The first byte indicates
// how many more bytes are in the ptn (including this count byte). The remaining
// bytes are the events. Each event starts with a Timing byte referenced in 24
// PPQN. The next byte is the note #. The next byte is the note velocity. Only Note
// On events are stored for drums. The last event in the ptn is the EOT event. It
// has only a Timing byte. For any event, if the high bit of velocity is set, this
// means to lessen the initial attack on the note (by skipping beginning samples).
// Any fill ptns must be at the end of the array, and any end ptn must be last in
// the array.


/* =============================== Styles ================================
 A style is loaded into a STYLE struct where:

DrumPtns[] points to the "Intro", "Verse", "Chorus", "Bridge", and "End" style variations for drums.

BassPtns[] points to the "Intro", "Verse", "Chorus", "Bridge", and "End" style variations for bass.

GtrPtns[] points to the "Intro", "Verse", "Chorus", "Bridge", and "End" style variations for guitar.

DrumPgm is the Program Change number to select this style's kit.

Tempo is default BPM.

Name[] is the style's nul-terminated name.

On disk, a style is stored in a text file. Each line starts with a keyword, followed by its properties.

A style's name is taken from its filename. An underscore is replaced with a line break when displayed.

The CAT line must be included. CAT is followed by the name of the category under which the style is displayed.

The DRUM line is optional. It is followed by the program number of the drumkit, and then the names of the drum Verse, Chorus, Bridge, Intro, and End variations, in that order. Each name is separated by a comma. If a name is "/", then a default name is used consisting of the style name with a "V", "C", "B", "I", or "E" appended for the 5 possible variations. If the program number is omitted, defaults to standard (0).

The BASS line is optional. It is followed by the program number of the bass patch, and then the names of the bass Verse, Chorus, Bridge, Intro, and End variations, as per the DRUM line. If the program number is omitted, defaults to picked bass (0).

The GTR line is optional. It is followed by the program number of the guitar patch, and then the names of the bass Verse, Chorus, Bridge, Intro, and End variations, as per the DRUM line. If the program number is omitted, defaults to clean electric (0).

The BPM line is optional. It is followed by the default tempo. If omitted, tempo = 120.

The TIME line is optional. It is followed by the time sig, for example 7/8. If omitted, time = 4/4.

The PROP line is optional. It is followed by any of RITARD.
*/

#pragma pack(1)

typedef struct {
	STYLE_VARIATION *		BassPtns[5];
	STYLE_VARIATION *		GtrPtns[5];
	STYLE_VARIATION *		DrumPtns[5];
	unsigned char			StyleFlags;
	unsigned char			BassPgm;
	unsigned char			GtrPgm;
	unsigned char			DrumPgm;
	unsigned char			Tempo;
	unsigned char			MeasureLen;
	char						Name[1];
} STYLE;

struct STYLEHEAD {
	struct STYLEHEAD *	Next;
	STYLE						Style;
};

typedef struct _STYLE_CAT {
	struct _STYLE_CAT *	Next;
	struct STYLEHEAD *	Styles;
	char						Name[1];
} STYLE_CAT;

#pragma pack()

// STYLE->StyleFlags
#define STYLEFLAG_RITARD			0x01

// Linked lists of allocated STYLE_VARIATIONs for bass, gtr, and drums
static struct VARIATION_HEAD *	VariationLists[3] = {0, 0, 0};

// Currently selected style category
static STYLE_CAT *					CurrentCategory = 0;
static STYLE_CAT *					PrevCategory = 0;
static STYLE_CAT *					Categories = 0;

// Currently selected style
static STYLE *							CurrentStyle = 0;
static STYLE *							PrevStyle = 0;
static STYLE *							PlayCurrentStyle;

// Playing drum evt
const unsigned char *				DrumEvtPtr;

// Current style's drum chain byte to play
static const unsigned char *		PlayDrumChainPtr;
static STYLE_VARIATION *			PlayDrumVariation;

// Currently playing Bass/Gtr variations
static STYLE_VARIATION *			PlayGtrVariation;
static STYLE_VARIATION *			PlayBassVariation;
static char	*							PlayGtrChainPtr;
static char *							PlayBassChainPtr;
static const unsigned char	*		GtrSameEvtPtr;
static const unsigned char *		BassSameEvtPtr;
static char								PlayBassPtnNum[2] = {0, -1};
static char								PlayGtrPtnNum[2] = {0, -1};

static unsigned char		SilentVariation[] = {1,	// NumPtns
0,			// NumFills
1+16, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // chain
1+1, PPQN_VALUE*4};
static unsigned char		DefaultDrumEnding[] = {1,	// NumPtns
0,			// NumFills
1+1, 0, // chain
10+1,
0, 38, 115,
0, 36, 108,
0, 49, 100,
4*PPQN_VALUE};
static unsigned char		DefaultBassEnding[] = {1,	// NumPtns
0,			// Flags
1+1, 0, // chain
4+1,
0, 0, 106,
4*PPQN_VALUE};
static unsigned char		DefaultGtrEnding[] = {1,	// NumPtns
0,			// Flags
1+1, 0, // chain
4+1,
0, 7, 72,
4*PPQN_VALUE};

// For rotating fills
static unsigned char		PlayFillPtnNum[5];

// Current variation. 0=Verse,1=Chorus,2=Bridge,3=Intro,4=End
static unsigned char		VariationNum[2];

// 1 or -1
static char					CycleVariation = 1;





/************* getMeasureTime() *******************
 * Called by beat play thread to get the time length
 * of the current measure.
 */

unsigned char getMeasureTime(void)
{
	return PlayCurrentStyle->MeasureLen;
}



/************* isStyleQueued() *******************
 * Called by beat play thread to check whether the
 * style it has queued is the same as the currently
 * selected style.
 */

int isStyleQueued(void)
{
	return (PlayCurrentStyle == CurrentStyle);
}





/********************* queueAccompEnd() *******************
 * Queues the bass/gtr end variations.
 *
 * Called by Beat Play thread.
 */

static void queueAccompEnd(void)
{
	register STYLE_VARIATION *		ptr;

	// Is there an end variation?
	ptr = PlayCurrentStyle->BassPtns[4];
	if (!ptr)
	{
		// No. Use the last ptn in current variation

		if (!PlayBassVariation || (PlayBassVariation->u.Flags & VARFLAG_DEFAULT_END))
		{
			ptr = (STYLE_VARIATION *)&DefaultBassEnding[0];
			goto defbass;
		}

		PlayBassChainPtr = (char *)&PlayBassPtnNum[0];
		PlayBassPtnNum[0] = PlayBassVariation->NumPtns - 1;
	}
	else
	{
defbass:
		PlayBassVariation = ptr;
		PlayBassChainPtr = (char *)&ptr->Chains[0];
	}

	ptr = PlayCurrentStyle->GtrPtns[4];
	if (!ptr)
	{
		// No. Use the last ptn in current variation
		if (!PlayGtrVariation || (PlayGtrVariation->u.Flags & VARFLAG_DEFAULT_END))
		{
			ptr = (STYLE_VARIATION *)&DefaultGtrEnding[0];
			goto defgtr;
		}
		PlayGtrChainPtr = (char *)&PlayGtrPtnNum[0];
		PlayGtrPtnNum[0] = PlayGtrVariation->NumPtns - 1;
	}
	else
	{
defgtr:
		PlayGtrVariation = ptr;
		PlayGtrChainPtr = (char *)&ptr->Chains[0];
	}
}





/********************* queueDrumEnd() *******************
 * Queues the drum end variation.
 *
 * Called by Beat Play thread.
 */

static void queueDrumEnd(void)
{
	{
	register STYLE_VARIATION *		ptr;

	// Is there an end variation?
	ptr = PlayCurrentStyle->DrumPtns[4];
	if (!ptr)
	{
		// No. Use the last ptn in current variation if marked as an ending.
		// Otherwise use DefaultDrumEnding

		// Stop play immediately after the final drum stroke of this 1 meas
		PlayFlags |= PLAYFLAG_FINAL_PTN;

		ptr = PlayDrumVariation;
		if (ptr->u.NumFills & VARFLAG_DEFAULT_END)
		{
			ptr = (STYLE_VARIATION *)&DefaultDrumEnding[0];
			goto def;
		}

		PlayDrumChainPtr = &PlayFillPtnNum[4];
		PlayFillPtnNum[4] = ptr->NumPtns - 1;
	}
	else
	{
def:	PlayDrumVariation = ptr;
		PlayDrumChainPtr = &ptr->Chains[0];
		if (ptr != (STYLE_VARIATION *)&DefaultDrumEnding[0])
		{
			register unsigned char *	ptn;

			// Get MeasureLen from first meas in drum variation
			ptn = (unsigned char *)&PlayDrumChainPtr[ptr->NumChains - 1];
			PlayCurrentStyle->MeasureLen = *(ptn + *ptn - 1);
		}
	}
	}

	// Set the length of the default meas
	DefaultBassEnding[sizeof(DefaultBassEnding)-1] = DefaultGtrEnding[sizeof(DefaultGtrEnding)-1] = DefaultDrumEnding[sizeof(DefaultDrumEnding)-1] =
		PlayCurrentStyle->MeasureLen;

	VariationNum[VARIATION_INPLAY] = VariationNum[VARIATION_USER] = 4;
}





/********************* setDrumEvtPtr() *******************
 * Sets DrumEvtPtr to point to the first drum event in the
 * specified ptn.
 *
 * Called by Beat Play thread.
 */

static void setDrumEvtPtr(register unsigned char ptnnum)
{
	register const STYLE_VARIATION *		ptr;

	ptr = PlayDrumVariation;

#ifndef NDEBUG
	if (ptnnum >= ptr->NumPtns)
	{
		printf("The drumbeat is improperly formatted: %d\r\n", (char)ptnnum);
		ptnnum = 0;
	}
#endif

	// Start with ptn chain
	DrumEvtPtr = &ptr->NumChains;

	do
	{
		// Skip to next ptn. First time, skip over chain
		DrumEvtPtr += *DrumEvtPtr;

	// Is this the desired ptn?
	} while (ptnnum--);

	// Skip size of Drum Pattern
	DrumEvtPtr++;
}





/***************** queueDrumFill() ****************
 * Queues the next drum fill.
 *
 * Called by Beat Play thread.
 *
 * RETURNS: 1 if there's a fill. 0 if not.
 */

static unsigned char queueDrumFill(void)
{
	register const STYLE_VARIATION *	ptr;
	register char 							ptnNum, totalPtns;
	register unsigned char 				numFills;

	ptr = PlayDrumVariation;
	if ((numFills = (ptr->u.NumFills & ~VARFLAG_DEFAULT_END)))
	{
		register char *					fillPtr;

		fillPtr = (char *)&PlayFillPtnNum[(ptr == PlayCurrentStyle->DrumPtns[0] ? 0 : VariationNum[VARIATION_INPLAY])];

		// Rotate to next fill
		ptnNum = *fillPtr - 1;

		totalPtns = (char)(ptr->NumPtns - ((ptr->u.NumFills & VARFLAG_DEFAULT_END) ? 0 : 1));
		if (ptnNum < totalPtns - numFills) ptnNum = totalPtns - 1;
		*fillPtr = ptnNum;

		// Indicate that we're playing the fill ptn now.  Note: A songsheet explicitly issues
		// a fill, so we don't add an implicit one here if a songsheet is playing
#ifndef NO_SONGSHEET_SUPPORT
		if (!isSongsheetActive())
#endif
			PlayFlags |= PLAYFLAG_FILLPLAY;

		// Queue first fill evt
		setDrumEvtPtr(ptnNum);

		return 1;
	}

	// If no fill ptns, tell caller to queue next meas/variation
	return 0;
}





/***************** queueNextDrumMeas() ****************
 * Queues the next drum measure.
 *
 * Called by Beat Play thread at the end of each measure.
 *
 * RETURNS: CTLMASK_NONE if no window redraw needed, or a
 * mask of which ctls need redrawing.
 *
 * Caller must ensure that main window is redrawn if needed.
 */

static uint32_t queueVariation(register unsigned char);

uint32_t queueNextDrumMeas(void)
{
	register STYLE_VARIATION *	ptr;
	register uint32_t				refresh;

	ptr = PlayDrumVariation;
	refresh = CTLMASK_NONE;

	// User wants to jump to a variation or another style?
	if (PlayFlags & PLAYFLAG_STYLEJUMP)
	{
		// Yes. Did we just play a fill? If not, does user want a fill?
		// Usually we need to play a fill first before playing the next variation
		if ((PlayFlags & PLAYFLAG_FILLPLAY) || !queueDrumFill())
		{
			// Yes we already did the fill. Queue the next variation or style

			// Has user changed the style?
			if (PlayCurrentStyle != CurrentStyle)
			{
				// Reset everything
				refresh = update_play_style(BEATTHREADID);
				goto out;
			}

			// User changed only the variation. Force queueVariation() to set play
			// variation to user's choice, with a fill inbetween
			VariationNum[VARIATION_INPLAY] |= 0x80;
			goto next;
		}

		// We queued a fill, so PLAYFLAG_FILLPLAY set. The next time here (ie, after
		// the fill), we queue the selected variation
	}
	else
	{
		// User wants to stop?
		if ((PlayFlags & PLAYFLAG_STOP) &&

			// Are we playing an End variation?
			VariationNum[VARIATION_INPLAY] != 4)
		{
			// No. We can't terminate. First we have to queue bass/gtr/drums end variations and then
			// play them so the bass/gtr/drums have a musically asthetic ending
			queueDrumEnd();
			queueAccompEnd();
			refresh = CTLMASK_VARIATION;
		}

		// If at the chain end, queue next variation, except if playing End variation (which
		// plays once, then stops)
		else if (PlayDrumChainPtr >= &ptr->Chains[ptr->NumChains - 1])
		{
			if (VariationNum[VARIATION_INPLAY] == 4)
			{
				// We just hit the very end. Signal caller to stop
				PlayFlags |= PLAYFLAG_FINAL_PTN;
				goto out;
			}

next:		refresh = queueVariation(BEATTHREADID);
		}

		{
		register unsigned char		ptnNum;

		// Is next chain a fill marker? If so, queue a fill. Otherwise queue the meas. Note: A
		// songsheet explicitly issues a fill, so we don't add an implicit one here if a songsheet
		// is playing
		ptnNum = *PlayDrumChainPtr++;
#ifndef NO_SONGSHEET_SUPPORT
		if (isSongsheetActive())
#endif
		{
			if (PlayFlags & PLAYFLAG_FILLPLAY)
			{
				PlayFlags &= ~PLAYFLAG_FILLPLAY;
				ptnNum |= 0x80;
			}
			else
				ptnNum &= ~0x80;
		}

		if (!(ptnNum & 0x80) || !queueDrumFill())
		{
			PlayFlags &= ~PLAYFLAG_FILLPLAY;
			setDrumEvtPtr(ptnNum & ~0x80);
		}
		}
	}
out:
	return refresh;
}





/***************** queueVariation() ****************
 * Queues the next drum/bass/gtr variation.
 *
 * Called by Beat Play thread.
 * Also called by main/midi threads when play is stopped.
 *
 * Not used for End variation. Use queueDrumEnd() and
 * queueAccompEnd().
 *
 * RETURNS: CTLMASK_NONE if no variation change.
 *
 * Caller must ensure that main window is redrawn if
 * needed.
 */

static uint32_t queueVariation(register unsigned char threadId)
{
	register uint32_t	refresh;

	refresh = CTLMASK_NONE;

//printf("PlayVariationNum=%02X VariationNum=%02X\r\n", VariationNum[VARIATION_INPLAY], VariationNum[VARIATION_USER]);

	// Did user (or songsheet) change the variation? If so, get what variation #,
	// and queue its events
	if (VariationNum[VARIATION_INPLAY] != VariationNum[VARIATION_USER])
	{
		CycleVariation = 1;

		VariationNum[VARIATION_INPLAY] &= 0x7F;
		if (VariationNum[VARIATION_INPLAY] != VariationNum[VARIATION_USER])
		{
			VariationNum[VARIATION_INPLAY] = VariationNum[VARIATION_USER];
			goto redraw;
		}

		goto queue;
	}

	// SongSheet explicitly changes the variation, so no automatic cycle/advance
#ifndef NO_SONGSHEET_SUPPORT
	if (!isSongsheetActive())
#endif
	{
		// If we just finished the Intro, automatically cycle to the Verse
		if (VariationNum[VARIATION_INPLAY] >= 3) goto verse;

		// Cycle to next variation, or repeat current variation if CycleVariation = 0
		if (!(TempFlags & APPFLAG3_NOVARCYCLE))
		{
			if (CycleVariation)
			{
				VariationNum[VARIATION_INPLAY] += CycleVariation;

				// If we just cycled past the Verse, cycle forwards to chorus
				if (VariationNum[VARIATION_INPLAY] == -1)
				{
					CycleVariation = 1;
					goto chorus;
				}

				// If we just cycled past the Bridge, cycle backwards to chorus
				if (VariationNum[VARIATION_INPLAY] >= 3)
				{
					CycleVariation = -1;
chorus:			VariationNum[VARIATION_INPLAY] = 1;
				}
			}

			if (!isVariation(VariationNum[VARIATION_INPLAY]))
verse:		VariationNum[VARIATION_INPLAY] = 0;
redraw:	refresh = CTLMASK_VARIATION;
		}
	}
queue:
	// Get the bass/gtr variations. Use Verse if requested variation not supplied,
	// or silent variation if no verse
	{
	register STYLE_VARIATION *	ptr;
	register unsigned char		pgm;

	ptr = PlayCurrentStyle->BassPtns[VariationNum[VARIATION_INPLAY]];
	if (!ptr) ptr = PlayCurrentStyle->BassPtns[0];
	if (!ptr) ptr = (STYLE_VARIATION *)&SilentVariation[0];
	if (ptr != PlayBassVariation)
	{
		PlayBassChainPtr = (char *)(&ptr->Chains[0]);
		PlayBassVariation = ptr;
	}

	ptr = PlayCurrentStyle->GtrPtns[VariationNum[VARIATION_INPLAY]];
	if (!ptr) ptr = PlayCurrentStyle->GtrPtns[0];
	if (!ptr) ptr = (STYLE_VARIATION *)&SilentVariation[0];
	if (ptr != PlayGtrVariation)
	{
		PlayGtrChainPtr = (char *)(&ptr->Chains[0]);
		PlayGtrVariation = ptr;
	}

	// Set the gtr patch, only if Setup->Robots->Instruments->Change = "Always" or "Articulations"
	pgm = (AppFlags2 & (APPFLAG2_PATCHCHG_MANUAL|APPFLAG2_PATCHCHG_STOPPED));
	if (!pgm ||	pgm == (APPFLAG2_PATCHCHG_MANUAL|APPFLAG2_PATCHCHG_STOPPED))
		refresh |= setInstrumentByNum(PLAYER_GTR | SETINS_NO_LSB | (uint32_t)threadId | ((PlayGtrVariation->u.Flags & 0x0f) << 8),
		!pgm ? PlayCurrentStyle->GtrPgm : 0xFF, 0);
	}

	// Get drum variation
	{
	register STYLE_VARIATION *	ptr;

	ptr = PlayCurrentStyle->DrumPtns[VariationNum[VARIATION_INPLAY]];

	// If this variation doesn't exist, use the verse
	if (!ptr)
	{
		ptr = PlayCurrentStyle->DrumPtns[0];

		// If no defined intro, do last 4 meas of verse
		if (VariationNum[VARIATION_INPLAY] >= 3)
		{
			register char	tempnum;

			tempnum = (char)ptr->NumChains - 1;
			if (tempnum >= 4) tempnum -= 4;
			else tempnum = 0;
			PlayDrumChainPtr = &ptr->Chains[(unsigned char)tempnum];
			goto useVerse;
		}

		VariationNum[VARIATION_INPLAY] = 0;
	}

	// Reset beat to play from start (ie, set PlayDrumChainPtr to point to first Chain byte)
	PlayDrumChainPtr = &ptr->Chains[0];
useVerse:
	PlayDrumVariation = ptr;
	}

	{
	register unsigned char *	ptn;
	register unsigned char *	chainPtr;

	chainPtr = (unsigned char *)&PlayGtrVariation->NumChains;
	if (PlayBassVariation != (STYLE_VARIATION *)&SilentVariation[0]) chainPtr = (unsigned char *)&PlayBassVariation->NumChains;
	if (PlayDrumVariation != (STYLE_VARIATION *)&SilentVariation[0]) chainPtr = (unsigned char *)&PlayDrumVariation->NumChains;

	// Get MeasureLen from first meas in non-silent variation
	ptn = (unsigned char *)&chainPtr[*chainPtr];
	PlayCurrentStyle->MeasureLen = *(ptn + *ptn - 1);
//printf("%u\r\n",PlayCurrentStyle->MeasureLen);
	// Set the length of the silent meas
	SilentVariation[sizeof(SilentVariation)-1] = PlayCurrentStyle->MeasureLen;
	}

	// "Jump to variation" finished
	// Fill must have finished playing
	// Ignore any pending stop/ritard cuz user selected a variation
	PlayFlags &= ~(PLAYFLAG_STYLEJUMP|PLAYFLAG_FILLPLAY|PLAYFLAG_STOP|PLAYFLAG_FINAL_PTN|PLAYFLAG_ACCEL|PLAYFLAG_RITARD);

	// Change done. Update VariationNum
	VariationNum[VARIATION_USER] = VariationNum[VARIATION_INPLAY];

	return refresh;
}





/********************* update_play_style() *******************
 * Called by Beat Play thread when user has changed the style.
 * Also called by main/midi threads when play is stopped.
 */

uint32_t update_play_style(register unsigned char threadId)
{
	register uint32_t	refresh;

	// This new style is now playing
	PlayCurrentStyle = CurrentStyle;

	// Reset beat to play from start of currently selected variation
	PlayGtrVariation = PlayBassVariation = 0;
	VariationNum[VARIATION_INPLAY] |= 0x80;	//reset, but don't change, variation
	refresh = queueVariation(threadId);

	// Reset fills
	PlayFillPtnNum[0] = PlayFillPtnNum[1] = PlayFillPtnNum[2] = PlayFillPtnNum[3] = PlayFillPtnNum[4] = 0;

	// Queue the first drum evt to play
	setDrumEvtPtr(*PlayDrumChainPtr++);

	// Queue the first bass/gtr ptn in the chain
	setAccompEvtPtr(0xFF);

	if (!(AppFlags2 & (APPFLAG2_PATCHCHG_MANUAL|APPFLAG2_PATCHCHG_STOPPED)))
	{
		// Set current drum kit to this style's kit
		refresh |= (setInstrumentByNum(PLAYER_DRUMS | SETINS_NO_MSB | SETINS_NO_LSB | (uint32_t)threadId, PlayCurrentStyle->DrumPgm, 0) |

		// Set bass patch. (Guitar is set above in queueVariation)
		setInstrumentByNum(PLAYER_BASS | SETINS_NO_MSB | SETINS_NO_LSB | (uint32_t)threadId, PlayCurrentStyle->BassPgm, 0));
	}

	// Restore PPQN clock in case of previous ritard. Don't do this if a SongSheet is playing -- it controls tempo
#ifndef NO_SONGSHEET_SUPPORT
	if (isSongsheetActive())
#endif
		set_PPQN(set_bpm(0));

	return refresh;
}





/********************* setAccompEvtPtr() *******************
 * Sets BassEvtPtr to point to the first event in the next
 * bass measure.
 *
 * Sets GtrEvtPtr to point to the first event in the next
 * gtr measure.
 *
 * Called by Beat Play thread.
 *
 * clock =		0 to STYLE->MeasureLen - 1 queues the current
 *					Song Chain to that PPQN clock, 0xFF queues
 *					next Chain's measure.
 */

void setAccompEvtPtr(unsigned char clock)
{
	register STYLE_VARIATION *	ptr;
	register unsigned char		tempnum;

	// Bass/gtr accomp on?
	ptr = PlayBassVariation;

	// Caller wants the next full meas?
	if (clock >= PlayCurrentStyle->MeasureLen)
	{
		// If at the chain end, reset to start
		if (PlayBassChainPtr >= (char *)&ptr->Chains[ptr->NumChains - 1]) PlayBassChainPtr = (char *)&ptr->Chains[0];

		// Is next chain a Repeat marker? (ie, negative number)
		// If so, loop back to repeat start
		if (*PlayBassChainPtr < 0) PlayBassChainPtr += *PlayBassChainPtr;

		tempnum = *PlayBassChainPtr++;
		BassEvtPtr = &ptr->NumChains;

		do
		{
			// Skip to next ptn. First time, skip over chain
			BassEvtPtr += *BassEvtPtr;

		// Is this the desired ptn?
		} while (tempnum--);

		// Skip size of Pattern
		BassEvtPtr++;

		BassSameEvtPtr = BassEvtPtr;
	}

	else
	{
		if (clock && (ptr->u.Flags & VARFLAG_HAS_HALFMEAS) && (VariationNum[VARIATION_INPLAY] < 4 || !(ptr->u.Flags & VARFLAG_DEFAULT_END)))
		{
			tempnum = 0;
			BassEvtPtr = &ptr->NumChains;
			do
			{
				BassEvtPtr += *BassEvtPtr;
			} while (tempnum--);

			BassEvtPtr++;
		}
		else
			BassEvtPtr = BassSameEvtPtr;
		if (clock)
		{
			while (clock > *BassEvtPtr) BassEvtPtr += ((BassEvtPtr[1] & BASSEVTFLAG_NOT_ON) ? 2 : 3);
		}
	}

	// Do the same for the gtr
	ptr = PlayGtrVariation;
	if (clock >= PlayCurrentStyle->MeasureLen)
	{
		if (PlayGtrChainPtr >= (char *)&ptr->Chains[ptr->NumChains - 1]) PlayGtrChainPtr = (char *)&ptr->Chains[0];
		if (*PlayGtrChainPtr < 0) PlayGtrChainPtr += *PlayGtrChainPtr;
		tempnum = *PlayGtrChainPtr++;
		GtrEvtPtr = &ptr->NumChains;
		do
		{
			GtrEvtPtr += *GtrEvtPtr;
		} while (tempnum--);
		GtrEvtPtr++;
		GtrSameEvtPtr = GtrEvtPtr;
	}
	else
	{
		if (clock && (ptr->u.Flags & VARFLAG_HAS_HALFMEAS) && (VariationNum[VARIATION_INPLAY] < 4 || !(ptr->u.Flags & VARFLAG_DEFAULT_END)))
		{
			tempnum = 0;
			GtrEvtPtr = &ptr->NumChains;
			do
			{
				GtrEvtPtr += *GtrEvtPtr;
			} while (tempnum--);
			GtrEvtPtr++;
		}
		else
			GtrEvtPtr = GtrSameEvtPtr;
		if (clock)
		{
			while (clock > *GtrEvtPtr)
			{
				GtrEvtPtr++;
				GtrEvtPtr += ((GtrEvtPtr[0] & GTREVTFLAG_STEPOFFMASK) == GTREVTFLAG_STEPOFF || (GtrEvtPtr[0] & GTREVTFLAG_STRINGOFFMASK) == GTREVTFLAG_STRINGOFF) ? 1 : 2;
			}
		}
	}
}





/******************** lockPlay() ********************
 * Arbitrates thread access to starting/stopping play.
 */

static unsigned char PlayLock = 0;

unsigned char lockPlay(register unsigned char threadId)
{
	return __atomic_or_fetch(&PlayLock, threadId, __ATOMIC_RELAXED);
}

void unlockPlay(register unsigned char threadId)
{
	__atomic_and_fetch(&PlayLock, ~threadId, __ATOMIC_RELAXED);
}





void start_play(register unsigned char countOff, register unsigned char threadId)
{
	// Not already in Play?
	if (!BeatInPlay) play_beat(countOff, threadId);
}

void stop_play(register unsigned char countOff, register unsigned char threadId)
{
	// Not already in Play?
	if (BeatInPlay) play_beat(countOff, threadId);
}

/********************* play_beat() *********************
 * Toggles Start/Stop bass/drums/chords play.
 *
 * threadId OR'd with 0x01 if no countoff.
 *
 * Calls lockPlay/unlockPlay.
 */

void play_beat(register unsigned char countOff, register unsigned char threadId)
{
	// When "Clock" is set to MIDI Sync, then the ID "CLOCKTHREADID" locks play
	// until BackupBand's Clock is set back to internal. So this call fails
	if (lockPlay(threadId) == threadId &&

		// Make sure a style is loaded/selected
		CurrentStyle)
	{
		// Not already in Play?
		if (!BeatInPlay)
		{
			// Tell beat play thread to init/reset vars related to user's style selection
			PlayCurrentStyle = 0;

			// Tell Beat play thread whether to do countoff
			setCountOff(0, (countOff ? 1 : (CurrentStyle->MeasureLen == 3*PPQN_VALUE ? 4 : 5)));

			// Tell beat play thread to start
			signalBeatThread(threadId);
		}

		// Stop play
		else
		{
			// If a ritard is programmed for this style, initiate it
			if (
#ifndef NO_SONGSHEET_SUPPORT
				!isSongsheetActive() &&
#endif
				PlayCurrentStyle && (PlayCurrentStyle->StyleFlags & STYLEFLAG_RITARD)) PlayFlags |= PLAYFLAG_RITARD;

			// Tell Beat play thread to jump to end ptn, and
			// then send gui thread a signal
			PlayFlags |= PLAYFLAG_STOP;
		}
	}

	unlockPlay(threadId);
}




unsigned char getStyleDefTempo(void)
{
	return CurrentStyle->Tempo;
}
unsigned char getStyleDefBass(void)
{
	return CurrentStyle->BassPgm;
}
unsigned char getStyleDefKit(void)
{
	return CurrentStyle->DrumPgm;
}
unsigned char getStyleDefGtr(void)
{
	return CurrentStyle->GtrPgm;
}



void * getPrevStyle(void)
{
	return PrevStyle;
}



/******************** lockStyle() ********************
 * Arbitrates thread access to changing style.
 */

static unsigned char StyleLock = 0;

unsigned char lockStyle(register unsigned char threadId)
{
	return __atomic_or_fetch(&StyleLock, threadId, __ATOMIC_RELAXED);
}

void unlockStyle(register unsigned char threadId)
{
	__atomic_and_fetch(&StyleLock, ~threadId, __ATOMIC_RELAXED);
}



/******************** set_style() ******************
 * Sets the current style.
 *
 * NOTE: Calls lockStyle()/unlockStyle().
 */

static uint32_t set_style(register STYLE * stylePtr, register unsigned char threadId)
{
	register uint32_t		status;

	status = CTLMASK_NONE;
	if (lockStyle(threadId) == threadId)
	{
		// Is the desired style already queued to play?
		if (CurrentStyle != stylePtr)
		{
			// No. So make it the current style. Save the ptr to what will
			// now be the previous so user can utilize the command to switch
			// between prev and current
			PrevCategory = CurrentCategory;
			PrevStyle = CurrentStyle;
			CurrentStyle = stylePtr;

			// Select the Intro variation (but don't queue its note events yet)
			status = CTLMASK_STYLES | CTLMASK_VARIATION;
			selectStyleVariation(3, threadId);

			// If beat thread isn't running, let's select the style's
			// assigned instruments, and queue the intro
			if (!BeatInPlay)
			{
				if (!(AppFlags2 & APPFLAG2_PATCHCHG_MANUAL))
				{
					status |= setInstrumentByNum(PLAYER_DRUMS | SETINS_NO_MSB | SETINS_NO_LSB | (uint32_t)threadId, stylePtr->DrumPgm, 0);
					status |= setInstrumentByNum(PLAYER_BASS | SETINS_NO_MSB | SETINS_NO_LSB | (uint32_t)threadId, stylePtr->BassPgm, 0);
					// Bass/Drum don't have articulation's via bank select, but gtr does. Select "normal"
					// articulation (MSB=0) here. Variation may change it later
					status |= setInstrumentByNum(PLAYER_GTR | SETINS_NO_LSB | (uint32_t)threadId, stylePtr->GtrPgm, 0);
				}

				VariationNum[VARIATION_INPLAY] = 3;
				status |= update_play_style(threadId);
			}
			// If in play, tell the beat thread to jump to this new style at
			// the next measure. It will then issue the patch changes
			else
#ifndef NO_SONGSHEET_SUPPORT
				PlayFlags |= (threadId != BEATTHREADID ? PLAYFLAG_STYLEJUMP : PLAYFLAG_STYLEJUMP|PLAYFLAG_FILLPLAY);
#else
				PlayFlags |= PLAYFLAG_STYLEJUMP;
#endif
		}

		// If selecting the same style, set default tempo. Don't do this if a SongSheet (BEATTHREADID)
		else if (set_bpm(0) != stylePtr->Tempo && threadId != BEATTHREADID)
		{
			if (lockTempo(threadId) == threadId)
			{
				set_bpm(stylePtr->Tempo);
				status = CTLMASK_TEMPO;
			}
			unlockTempo(threadId);
		}

		if (!BeatInPlay)
		{
			// If not in play, mute any bass/gtr from the previous play
			if (PlayFlags & PLAYFLAG_CHORDSOUND)
				status |= clearChord(2|threadId);

			// If "Auto" set, turn Autostart on whenever selecting a style during stop
			if (AppFlags3 & APPFLAG3_AUTOSTARTARM)
			{
				TempFlags |= TEMPFLAG_AUTOSTART;
				status |= CTLMASK_AUTOSTART;
			}
		}

		unlockStyle(threadId);
	}

	return status;
}





/******************** change_style() *********************
 * Changes the style to the specified #. Called by
 * MIDI In or GUI threads.
 */

uint32_t change_style(register unsigned char styleNum, register char * str, register unsigned char threadId)
{
	register STYLE_CAT *	category;

	category = Categories;
	while (category)
	{
		register struct STYLEHEAD *	style;

		if ((style = category->Styles))
		{
			while (styleNum-- && (style = style->Next));
			if (style)
			{
				if (!str)
				{
					CurrentCategory = category;
					return set_style(&style->Style, threadId);
				}

				strcpy(str, &style->Style.Name[0]);
				goto out;
			}
		}
		category = category->Next;
	}
out:
	return CTLMASK_NONE;
}







/******************** selectStyle() *********************
 * Changes the style to the specified # within the
 * current category. Called by Midi or GUI threads.
 */

uint32_t selectStyle(register unsigned char styleNum, register unsigned char threadId)
{
	register struct STYLEHEAD *	style;

	if (CurrentCategory && (style = CurrentCategory->Styles))
	{
		while (styleNum-- && (style = style->Next));
		if (style)
			return set_style(&style->Style, threadId);
	}

	return CTLMASK_NONE;
}

uint32_t selectStyleByPtr(register void * style, register unsigned char threadId)
{
	return set_style(style, threadId);
}





/***************** selectStyleCategory() ******************
 * Changes the current style Category to the specified #.
 * Called by GUI or MIDI threads.
 */

uint32_t selectStyleCategory(register unsigned char categoryNum)
{
	register STYLE_CAT *	category;

	category = Categories;
	while (categoryNum-- && category) category = category->Next;
	if (category && category != CurrentCategory)
	{
		CurrentCategory = category;
		return CTLMASK_STYLES;
	}

	return CTLMASK_NONE;
}


void cycleStyleCategory(void)
{
	register STYLE_CAT *	category;

	if (!(category = CurrentCategory) || !(category = category->Next)) category = Categories;
	CurrentCategory = category;
}





void updateStyleCategory(void)
{
	CurrentCategory = PrevCategory;
}





void * get_song_style(register const char * name)
{
	register STYLE_CAT *	category;
	register int			result;

	category = Categories;
	while (category)
	{
		if (!(result = strcasecmp(category->Name, name)))
		{
			register struct STYLEHEAD * stylePtr;

			name += strlen(name) + 1;
			stylePtr = category->Styles;
			while (stylePtr)
			{
				if (!(result = strcasecmp(stylePtr->Style.Name, name))) return &stylePtr->Style;
				if (result > 0) goto out;
				stylePtr = stylePtr->Next;
			}

			goto out;
		}

		if (result > 0) break;
		category = category->Next;
	}
out:
	return 0;
}

uint32_t set_song_style(register void * stylePtr)
{
	if (stylePtr && (STYLE *)stylePtr != PlayCurrentStyle)
	{
		register STYLE_CAT *	category;

		category = Categories;
		while (category)
		{
			register struct STYLEHEAD *	style;

			if ((style = category->Styles))
			{
				do
				{
					if (&style->Style == stylePtr)
					{
						if (lockStyle(BEATTHREADID) == BEATTHREADID)
						{
							CurrentCategory = PrevCategory = category;
							PlayCurrentStyle = 0;
							CurrentStyle = (STYLE *)stylePtr;
							PlayFlags |= (PLAYFLAG_STYLEJUMP|PLAYFLAG_FILLPLAY);
						}
						unlockStyle(BEATTHREADID);

						return CTLMASK_STYLES;
					}

				} while ((style = style->Next));
			}
			category = category->Next;
		}
	}

	return CTLMASK_NONE;
}






// ===========================================
// Enumerating the style Categorys, and the current
// Category's styles.

void * getStyleCategory(register void * categoryPtr)
{
	// if passed 0, start with the first category
	if (!categoryPtr) categoryPtr = Categories;
	else categoryPtr = ((STYLE_CAT *)categoryPtr)->Next;
	return categoryPtr;
}

const char * getStyleCategoryName(register void * categoryPtr)
{
	return ((STYLE_CAT *)categoryPtr)->Name;
}

void * getCurrentStyleCategory(void)
{
	return CurrentCategory;
}

void * getStyle(register void * stylePtr)
{
	if (!stylePtr)
	{
		if (CurrentCategory) stylePtr = CurrentCategory->Styles;
	}
	else
 		stylePtr = ((struct STYLEHEAD *)((char *)stylePtr - sizeof(void *)))->Next;
	return stylePtr ? (char *)stylePtr + sizeof(void *) : 0;
}

void * getFirstStyle(register void * categoryPtr)
{
	register void * stylePtr;

	stylePtr = ((STYLE_CAT *)categoryPtr)->Styles;
	return stylePtr ? (char *)stylePtr + sizeof(void *) : 0;
}

char * getStyleName(register void * stylePtr)
{
	return ((STYLE *)stylePtr)->Name;
}

void * getCurrentStyle(void)
{
	return CurrentStyle;
}

// VARIATION_INPLAY or VARIATION_USER
unsigned char getCurrVariationNum(register unsigned int which)
{
	return VariationNum[which];
}

unsigned int isVariation(register unsigned int which)
{
	if (which < 3)
	{
		register STYLE * stylePtr;

		if (!(stylePtr = CurrentStyle) || (!stylePtr->BassPtns[which] && !stylePtr->GtrPtns[which] &&
			(!stylePtr->DrumPtns[which] || stylePtr->DrumPtns[which] == (STYLE_VARIATION *)&SilentVariation[0])))
			return 0;

	}
	return 1;
}

/******************** lockVariation() ********************
 * Arbitrates thread access to changing variation.
 */

static unsigned char VariationLock = 0;

unsigned char lockVariation(register unsigned char threadId)
{
	return __atomic_or_fetch(&VariationLock, threadId, __ATOMIC_RELAXED);
}

void unlockVariation(register unsigned char threadId)
{
	__atomic_and_fetch(&VariationLock, ~threadId, __ATOMIC_RELAXED);
}



/***************** selectStyleVariation() ******************
 * Changes the current style variation to the specified #.
 * Called by GUI, Midi In, and Beat Play threads.
 */

uint32_t selectStyleVariation(register unsigned char variNum, register unsigned char threadId)
{
	if (lockVariation(threadId) == threadId)
	{
		if (variNum < 4 && isVariation(variNum))
		{
			VariationNum[VARIATION_USER] = variNum;
			PlayFlags |= PLAYFLAG_STYLEJUMP;
			unlockVariation(threadId);
			return CTLMASK_VARIATION;
		}
	}
	unlockVariation(threadId);
	return CTLMASK_NONE;
}

void resetPlayVariation(void)
{
	VariationNum[VARIATION_INPLAY] = 0x80 | 3;
	VariationNum[VARIATION_USER] = 3;
}


uint32_t playFillAndAdvance(register unsigned char threadId)
{
	if (lockVariation(threadId) == threadId)
	{
		register unsigned char	variNum, orig;

		orig = variNum = getCurrVariationNum(VARIATION_USER);
		do
		{
			if (++variNum > 2) variNum = 0;
			if (orig == variNum) variNum = 3;
		} while (!isVariation(variNum));
		return selectStyleVariation(variNum, threadId);	// unlocks
	}
	unlockVariation(threadId);
	return CTLMASK_NONE;
}






//=====================================================
// Load styles/variations.
//=====================================================


/********************* loadVariation() ********************
 * Loads a STYLE_VARIATION.
 *
 * fn =			Variation file's nul-terminated full pathname.
 * namePtr =	Ptr to the filename.
 * which =		Intro, verse, chorus, bridge, or ending.
 *
 * RETURNS: 0 = success, non-zero = fail.
 */

static const char * loadVariation(char * fn, const char * namePtr, unsigned int which)
{
	register struct VARIATION_HEAD *		mem;
	register STYLE_VARIATION *				variation;
	register unsigned long					len;
	{
	register int					hFile;
	struct stat						buf;

	mem = 0;

	if ((hFile = open(&fn[0], O_RDONLY|O_NOATIME)) == -1)
	{
		namePtr = "Can't open %s %s style used by %s";
bad:	if (mem) free(mem);
		return namePtr;
	}

	// Check for min size
	fstat(hFile, &buf);
	if (!(len = buf.st_size) || len < 4)
	{
		close(hFile);
corrupt:
		namePtr = "%s %s style is invalid";
		goto bad;
	}

	// Allocate a STYLE_VARIATION to read in the whole (binary) file
	if (!(mem = (struct VARIATION_HEAD *)malloc(sizeof(struct VARIATION_HEAD) + len)))
	{
		namePtr = NoMemStr;
		close(hFile);
		goto bad;
	}

	// Read in the file, and close it
	variation = (STYLE_VARIATION *)(&mem[1]);
	buf.st_size = read(hFile, &variation->NumPtns, len);
	close(hFile);
	if (buf.st_size != len)
	{
		namePtr = "Can't read %s %s style";
		goto bad;
	}
	}

	// Check integrity
	{
	register unsigned char *		ptr;
	register unsigned char 			cnt;

	ptr = &variation->NumChains;
	cnt = variation->NumPtns + 1;
	len -= ((ptr - (unsigned char *)variation) + 1);
	do
	{
		if (ptr > &variation->Chains[len]) goto corrupt;
		ptr += *ptr;
	} while (--cnt);
	if (ptr != &variation->Chains[len]) goto corrupt;
	}

	mem->Name = hash_string((unsigned char *)namePtr);

	// Link into list
	mem->Next = VariationLists[which];
	VariationLists[which] = mem;

	return 0;
}





/********************* freeAccompStyles() ********************
 * Frees all STYLE_CAT/STYLE/STYLE_VARIATION/SONGSHEET.
 */

void freeAccompStyles(void)
{
	{
	register struct VARIATION_HEAD *	variation;
	register unsigned int				i;

	for (i=0; i<3; i++)
	{
		while ((variation = VariationLists[i]))
		{
			VariationLists[i] = variation->Next;
			free(variation);
		}
	}
	}

	{
	register STYLE_CAT *				categoryPtr;
	register struct STYLEHEAD *	stylePtr;

	while ((categoryPtr = Categories))
	{
		while ((stylePtr = categoryPtr->Styles))
		{
			categoryPtr->Styles = stylePtr->Next;
			free(stylePtr);
		}

		Categories = categoryPtr->Next;
		free(categoryPtr);
	}
	}

#ifndef NO_SONGSHEET_SUPPORT
	freeSongSheets();
#endif
}





static const char			PtnDirNames[] = "Bass\0Gtr\0Drum";
static const char			VariationExts[] = {'V', 'C', 'B', 'I', 'E'};

/********************* resolveVariations() ********************
 * Resolves a STYLE's reference to a STYLE_VARIATION.
 */

static int resolveVariations(STYLE * stylePtr)
{
	{
	register uint32_t			hash;
	register unsigned int	i, n;
	register char **			ptr;
	const char *				dirName;

	ptr = (char **)&stylePtr->BassPtns[0];
	dirName = PtnDirNames;

	for (n=0; n<3; n++)
	{
		for (i=0; i<5; i++)
		{
			register char *	name;

			if ((name = *ptr))
			{
				hash = hash_string((unsigned char *)name);

				{
				register struct VARIATION_HEAD *	ptnPtr;

				if ((ptnPtr = VariationLists[n]))
				{
					do
					{
						if (ptnPtr->Name == hash)
						{
							*ptr = (char *)ptnPtr + sizeof(struct VARIATION_HEAD);
							goto next;
						}

					} while ((ptnPtr = ptnPtr->Next));
				}
				}

				{
				register const char *	errPtr;

				// Variation not yet loaded
				hash = get_style_datapath((char *)TempBuffer);
				strcpy((char *)&TempBuffer[hash], dirName);
				hash += 3;
				if (TempBuffer[hash]) hash++;
				TempBuffer[hash++] = '/';
				strcpy((char *)&TempBuffer[hash], name);
				if ((errPtr = loadVariation((char *)TempBuffer, name, n)))
				{
					sprintf((char *)TempBuffer, errPtr, name, dirName, stylePtr->Name);
					setErrorStr((char *)TempBuffer);
		 			return -1;
				}

				*ptr = (char *)VariationLists[n] + sizeof(struct VARIATION_HEAD);
				}
			}
next:
			++ptr;
		}

		dirName += strlen(dirName) + 1;
	}
	}

	{
	register unsigned char *	ptn;

	// Must have a drum verse variation. Use "silent" measure if none supplied
	if (!stylePtr->DrumPtns[0]) stylePtr->DrumPtns[0] = (STYLE_VARIATION *)&SilentVariation[0];

	// Get MeasureLen from first meas in drum verse variation
	ptn = &stylePtr->DrumPtns[0]->Chains[stylePtr->DrumPtns[0]->NumChains - 1];
	stylePtr->MeasureLen = *(ptn + *ptn - 1);
	}

	return 0;
}





/**************** create_style_category() *************
 * Creates a STYLE_CAT for the specified category name.
 */

void * create_style_category(const char * name)
{
	register STYLE_CAT *		styleCat;
	register STYLE_CAT *		categoryPtr;
	register STYLE_CAT *		parent;
	register unsigned char	namelen;

	// Add a STYLE_CAT to the list. Sort alphabetically so the
	// cat names appear in the same order every time this app runs,
	// and therefore style selection via MIDI is consistent
	styleCat = Categories;
	parent = (STYLE_CAT *)&Categories;
	while (styleCat)
	{
		if (strcasecmp(styleCat->Name, name) > 0) break;
		parent = styleCat;
		styleCat = styleCat->Next;
	}

	categoryPtr = styleCat;
	namelen = strlen(name);
	if ((styleCat = (STYLE_CAT *)malloc(sizeof(STYLE_CAT) + namelen)))
	{
		memcpy(styleCat->Name, name, namelen + 1);
		parent->Next = styleCat;
		styleCat->Next = categoryPtr;
		styleCat->Styles = 0;
	}

	return styleCat;
}





/**************** linkIntoCategory() *************
 * Links the STYLE into the specified category.
 */

static void linkIntoCategory(STYLE_CAT * styleCat, struct STYLEHEAD * style)
{
	register struct STYLEHEAD *		stylePtr;
	register struct STYLEHEAD *		parent;

	// Sort the individual styles alphabetically under this cat
	stylePtr = styleCat->Styles;
	parent = (struct STYLEHEAD *)&styleCat->Styles;
	while (stylePtr)
	{
		if (strcasecmp(stylePtr->Style.Name, style->Style.Name) >= 0) break;
		parent = stylePtr;
		stylePtr = stylePtr->Next;
	}
	parent->Next = style;
	style->Next = stylePtr;
}



#define NUM_OF_STYLE_IDS	5
#define STYLE_ID_BASS 0
#define STYLE_ID_GTR 	1
#define STYLE_ID_DRUM	2
#define STYLE_ID_TEMPO	3
#define STYLE_ID_PROP	4

static const uint32_t		StyleIds[] = {0x77D9AA4A,	// BASS
0x02622245,	// GTR
0x3341BB43,	// DRUM
0x654A4C60,	// TEMPO
0xD3537926};	// PROP


#define NUM_OF_STYLE_PROPS	1

static const uint32_t	PropIds[] = {0x28FEEFF7};	// RITARD


/********************* loadStyle() ********************
 * Loads a STYLE.
 *
 * fn =			Style file's nul-terminated full pathname.
 * NamePtr =	Ptr to the filename.
 *
 * If an error, copies a msg to TempBuffer[].
 */

void loadStyle(void * cat, const char * fn)
{
	// Load the style file
	if (!load_text_file(fn, 3))
out:	return;

	// Parse the style text file and put the info into a temporary
	// STYLEHEAD
	{
	struct STYLEHEAD				style;
	unsigned char *				copy;
	unsigned char					buffer[3*5*30];
	{
	register unsigned char *	field;
	unsigned char *				ptr;
	register uint32_t				lineNum, temp;

	ptr = TempBuffer;
	copy = buffer;
	lineNum = 1;
	memset(&style, 0, sizeof(struct STYLEHEAD));
	style.Style.BassPgm = 33;
	style.Style.GtrPgm = 26;
	style.Style.Tempo = 120;
	style.Style.MeasureLen = 4*PPQN_VALUE;

	for (;;)
	{
		unsigned char		id;

		lineNum += skip_lines(&ptr);
		if (!*ptr) break;

		field = ptr;
		id = 0x80 | NUM_OF_STYLE_IDS;
		ptr = get_field_id(ptr, &StyleIds[0], &id, 0);
		if (id >= NUM_OF_STYLE_IDS)
		{
			format_text_err("unknown id %s", lineNum, field);
			goto out;
		}

		if (!*ptr || *ptr == '\n')
		{
			format_text_err("missing %s value", lineNum, field);
			goto out;
		}

		switch (id)
		{
			case STYLE_ID_PROP:
			{
				do
				{
					field = ptr;
					id = 0x80 | NUM_OF_STYLE_PROPS;
					ptr = get_field_id(ptr, &PropIds[0], &id, 0);
					if (id >= NUM_OF_STYLE_PROPS)
					{
						format_text_err("unknown prop %s", lineNum, field);
						goto out;
					}

					style.Style.StyleFlags |= (0x01 << id);

				} while (*ptr && *ptr != '\n');

				break;
			}

			case STYLE_ID_TEMPO:
			{
				temp = asciiToNum(&ptr);
				if (!temp || temp > 255 || !ptr)
				{
badval:			format_text_err("bad %s value", lineNum, field);
					goto out;
				}
				style.Style.Tempo = (unsigned char)temp;
				break;
			}

			// BASS,GTR, DRUM line
			default:
			{
				register unsigned char *		pgmNum;
				register STYLE_VARIATION **	vari;

				pgmNum = &style.Style.BassPgm + id;
				switch (id)
				{
					case 0:
						vari = &style.Style.BassPtns[0];
						break;
					case 1:
						vari = &style.Style.GtrPtns[0];
						break;
					default:
						vari = &style.Style.DrumPtns[0];
				}
				if (*ptr != ',')
				{
					*pgmNum = asciiToNum(&ptr);
					if (*pgmNum > 127 || !ptr) goto badval;
					ptr = skip_spaces(ptr);
				}
				if (*ptr == ',') ptr++;

				temp = 0;
				goto loop;

				do
				{
					if (*ptr != ',')
					{
						field = copy;
						while (*ptr >= ' ' && *ptr != ',') *copy++ = *ptr++;	// need bounds checking!!!
						if (*ptr == ',') *ptr = ' ';

						// If a variation name supplied, look for that. If '/', look for a name comprised of
						// the style name with V, C, B, I, or E appended
						if (*field == '/')
						{
							copy = field;
							strcpy((char *)copy, NamePtr);
							copy += strlen((char *)copy);
							*copy++ = VariationExts[temp];
						}
						else
							while (copy > field && *(copy - 1) <= ' ') --copy;
						*copy++ = 0;
						vari[temp] = (STYLE_VARIATION *)field;
					}
					else
						ptr++;

					++temp;
loop:				ptr = skip_spaces(ptr);
				} while (*ptr && *ptr != '\n' && temp < 5);
			}
		}
	}
	}

	// Allocate a STYLEHEAD and copy over the temp struct. For now, use the temporary
	// strings for the variation names
	{
	register struct STYLEHEAD *	stylePtr;
	register unsigned char			namelen;

	// Allocate a STYLE
	namelen = strlen(NamePtr);
	if (!(stylePtr = (struct STYLEHEAD *)malloc(sizeof(struct STYLEHEAD) + namelen)))
		setMemErrorStr();
	else
	{
		memcpy(stylePtr, &style, sizeof(struct STYLEHEAD));
		memcpy(stylePtr->Style.Name, NamePtr, namelen + 1);

		// Load this style's variations and replace the temp names with ptrs to STYLE_VARIATION's
		if (resolveVariations(&stylePtr->Style))
			free(stylePtr);
		else
			// Link this style into its category's list
			linkIntoCategory(cat, stylePtr);
	}
	}
	}
}
