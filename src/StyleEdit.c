	// X Windows and cairo for GUI
#include "GuiCtls.h"
#include "StyleData.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/param.h>
#ifndef O_NOATIME
#define O_NOATIME        01000000
#endif

static void clearMainWindow(void);
static void show_msgbox(register const char *);
static void doPickItemDlg2(register GUILISTFUNC *);
static void positionPickItemGui(void);
typedef void MOUSEFUNC(register GUIMSG *);

//============== DATA ===============
// X Windows structs
static GUIAPPHANDLE		GuiApp;
static GUIWIN *			MainWin;
static MOUSEFUNC *		MouseFunc = 0;

// Flags when to abort Gui loop
static unsigned char		GuiLoop;

static const char			NoMemStr[] = "Out of RAM!";
static const char			WindowTitle[] = "Style Editor";

//static const char	HomeStr[] = "HOME";








// ==========================================

static const char		DidntOpenStr[] = " didn't open";
static const char		CorruptStr[] = " is corrupt";
static const char		HasStr[] = {' ', 'h', 'a', 's', ' ','a', ' '};
static const char		LoopStr[] = {'l', 'o', 'o', 'p'};
static const char		DoneStr[] = {'d', 'o', 'n', 'e'};
static const char		EndStr[] = {'e', 'n', 'd'};
static const char		BadMidiStr[] = " is a bad MIDI file";
static const char		NotMidiStr[] = " isn't a MIDI file";
static const char		Limit128Str[] = " must be limited to 128 measures";
static const char		NoSupTimeStr[] = " has an unsupported time signature";
static const char		BadTimeStr[] = " has a misplaced time signature";
static const char		BadStampStr[] = " has a bad timestamp";
static const char		OneMeasStr[] = " must have at least 1 measure";
static const char		DidntSaveStr[] = " didn't save";
static const char		BadRepeatStr[] = " has more than 1 repeat marker";
static const char		TooManyLoops[] = " has more than 16 nested loops";
static const char		TooManyEndings[] = " has more than 16 loop endings";
static const char		BadEnding[] = " has a bad loop number";
static const char		BadMarker[] = " has a bad marker";
static const char		BadStyle[] = " has a bad style";
static const char		OpenLoop[] = " has a missing loop marker";
static const char		OnMeas[] = " has a marker not on beat 1";
static const char		NoStart[] = " has a missing loop start marker";
static const char		DupeEnding[] = " has a duplicate loop number";

static const unsigned char	Denoms[] = {4*24,2*24,24,12,6,3};

//static const unsigned char	BassName[] = {4, 'b', 'a', 's', 's'};
//static const unsigned char	GtrName[] = {6, 'g', 'u', 'i', 't','a','r'};

static const unsigned char	MthdId[] = { 'M', 'T', 'h', 'd',   0,0,0,6,   0,1,  0,2,  0,24,  'M', 'T', 'r', 'k' ,  0,0,0,0};

static const unsigned char	MtrkPort[] = {0, 0xFF, 9, 12, 'B','a','c','k','u','p','B','a','n','d'};

static const unsigned char	MtrkBass[] = {'B','a','s','s'};
static const unsigned char	MtrkGtr[] = {'E','G','t','r'};
static const unsigned char	MtrkDrum[] = {'P','o','p'};

static const unsigned char	TimeSig[] = {0, 0xFF, 0x58, 4, 4, 2, 24, 8};

static const unsigned char	MtrkEOT[] = {0, 0xFF, 0x2F, 0};

static const unsigned char ScaleSteps[] = {0x00, 0x40, 0x01, 0x41, 0x02, 0x03, 0x43, 0x04, 0x44, 0x05, 0x45, 0x06};

static const unsigned char Maj[] = {0, 2, 4, 5, 7, 9, 11};

static const unsigned char Chords[] = {58, 48, 50, 52, 53, 55, 57, 58, 59, 54, 56, 0,0,49,0,51};

static const unsigned char StrumsOn[] = {0x06, 0x0d, 0x05, 0x0e, 0x04, 0x03, 0x09, 0x02, 0x0a, 0x01, 0x07, 0x08};
static const unsigned char StrumsOff[] = {GTREVTFLAG_STRINGOFF|0x06, GTREVTFLAG_STRINGOFF|0, GTREVTFLAG_STRINGOFF|0x05, GTREVTFLAG_STRINGOFF|0,
											GTREVTFLAG_STRINGOFF|0x04, GTREVTFLAG_STRINGOFF|0x03, GTREVTFLAG_STRINGOFF|0, GTREVTFLAG_STRINGOFF|0x02,
											GTREVTFLAG_STRINGOFF|0, GTREVTFLAG_STRINGOFF|0x01, GTREVTFLAG_STRINGOFF|0, GTREVTFLAG_STRINGOFF|0};

static const unsigned char Pitch[] = {0x20,0x40,0};

static const unsigned char	ChordSheet[] = {0,1,0xFF,4,0xFF,2,0xFF,3,5,0xFF,6};
static const unsigned char	SongScales[] = {64,65,69,71,67,72,74};
static const unsigned char Variations[] = {'V','C','B','I'};

static char *				FilenamePtr;

#define MIDIKEY_HISTORY	24

#pragma pack(1)
struct PLAYHISTORY {
	uint32_t			Times[MIDIKEY_HISTORY];
	unsigned char	Keys[MIDIKEY_HISTORY];
	unsigned char	Ignores[MIDIKEY_HISTORY];
	unsigned char	Counts[MIDIKEY_HISTORY];
};

struct PLAYREPEAT {
	unsigned char *	Start[16];
	unsigned char *	Ending[16];
	unsigned char *	Stylename;
	unsigned char		EndingNum[16];
	unsigned int		Num;
	unsigned char		Flags1, Scale, Endings, VarNum, Tempo, TimeSig1, TimeSig2, Pad, PadVel;
};

struct PLAYLOOP {
	unsigned char *	Start[24];
	unsigned short		CurrentTable[16+1];
	uint32_t				NoteTime;
	unsigned char		NoteSpec, NoteFlags,NumEndings, Pad;
};

struct PLAYVARS {
	union {
	struct PLAYHISTORY	Playing;
	struct PLAYREPEAT		Repeat;
	struct PLAYLOOP		Loop;
	} Vars;
};

#pragma pack()

static struct PLAYVARS	Play;

static unsigned char		StringNums[7];

static unsigned char		StyleType;

static char					fn[PATH_MAX];










// =================================================
// List enumeration/display
// =================================================

static GUILIST		List;

static GUICTL		ListGuiCtls[] = {
 	{.Type=CTLTYPE_AREA, .Flags.Local=CTLFLAG_AREA_LIST|CTLFLAG_AREA_FULL_SIZE|CTLFLAG_AREA_EVENHEIGHT, .Flags.Global=CTLGLOBAL_APPUPDATE|CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_ARROWS, .Attrib.NumOfLabels=1, .Flags.Local=CTLFLAG_AREA_HELPER, .Flags.Global=CTLGLOBAL_AUTO_VAL},
	{.Type=CTLTYPE_END},
};









#define VARFLAG_DEFAULT_END		0x80
#define VARFLAG_HAS_HALFMEAS		0x40

#define MAX_NUM_OF_CHAINS	128

#pragma pack(1)

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






/********************** get_style_path() *********************
 * Copies the path of BackupBand's styles dir into the passed buffer.
 *
 * RETURNS: Size of path in CHARs.
 */

static const char		ConfigDirName[] = {'/','B','a','c','k','u','p','B','a','n','d','/','S','t','y','l','e','s','/'};
static const char		VarDirName[] = "Drum/\0Bass/\0Gtr/";
static const char		SongsDirName[] = "Songs/";

static unsigned int get_style_path(register char * buffer)
{
	register const char *	ptr;
	register unsigned int	len;

	if ((ptr = getenv("HOME")))
	{
		strcpy(buffer, ptr);
		len = strlen(buffer);
	}
	else
	{
		buffer[0] = '.';
		len = 1;
	}

	memcpy(&buffer[len], &ConfigDirName[0], sizeof(ConfigDirName));
	len += sizeof(ConfigDirName);

	strcpy(&buffer[len], (StyleType > 2 ? SongsDirName : &VarDirName[StyleType * 6]));
	return len + strlen(&buffer[len]);
}





static uint32_t addToHistory(register uint32_t evtTime, register unsigned char noteNum)
{
	register uint32_t			unused, i, retTime;

	retTime = 0xFFFFFFFF;
	unused = MIDIKEY_HISTORY;
	for (i = 0; i < MIDIKEY_HISTORY ; i++)
	{
		if (Play.Vars.Playing.Keys[i] == noteNum)
		{
			retTime = Play.Vars.Playing.Times[i];
			Play.Vars.Playing.Ignores[i]++;
			goto got_it;
		}

		if (!Play.Vars.Playing.Keys[i]) unused = i;
	}

	if (unused >= MIDIKEY_HISTORY) unused = 0;
	i = unused;
	Play.Vars.Playing.Ignores[i] = Play.Vars.Playing.Counts[i] = 0;

got_it:
	Play.Vars.Playing.Times[i] = evtTime;
	Play.Vars.Playing.Keys[i] = noteNum;
	Play.Vars.Playing.Counts[i]++;

	return retTime;
}





static unsigned char remFromHistory(register unsigned char noteNum)
{
	register uint32_t			i;

	for (i = 0; i < MIDIKEY_HISTORY ; i++)
	{
		if (Play.Vars.Playing.Keys[i] == noteNum)
		{
			if (!(--Play.Vars.Playing.Counts[i])) Play.Vars.Playing.Keys[i] = 0;
			if (!Play.Vars.Playing.Ignores[i]) return 1;
			Play.Vars.Playing.Ignores[i]--;
			break;
		}
	}

	return 0;
}





/******************** numToPitch() *********************
 * Converts a MIDI note number to a note name (ie,
 * nul-terminated string). For example, MIDI note number 60
 * returns "C 3". Note number 61 returns "C#3", etc. (Note
 * number 0 returns "A -2", so the first octave is -2). The
 * resulting string is stored in the passed buffer (which should
 * be large enough to hold at least 4 characters).
 */

static unsigned char	NtnNams[] = {'C',' ','C','#','D',' ','D','#','E',' ','F',' ','F','#','G',' ','G','#','A',' ','A','#','B',' ','C'};

static unsigned char * numToPitch(register unsigned char * ptr, register unsigned char noteNum)
{
	register unsigned char *	name;
	register unsigned char		oct;

	// Get note name
	name = &NtnNams[(noteNum % 12) << 1];

	// Get sharp or space char after name
	oct = *(name+1);

	// Store note name (and incidental)
	*(ptr)++ = *name;
	if (oct == '#') *(ptr)++ = oct;

	// Get octave
	if (!(oct = noteNum/12))
	{
		*(ptr)++ = '-';
		*(ptr)++ = '2';
	}
	else if (oct < 2)
	{
		*(ptr)++ = '-';
		*(ptr)++ = '1';
	}
	else
		*(ptr)++ = 46 + oct;

	// Nul-terminate
//	*ptr = 0;

	return ptr;
}





/********************* compareID() **********************
 * Compares the passed ID strs (ie, a ptr to 4 Ascii bytes).
 *
 * RETURNS: chunk size if a match, 0 if not.
 */

static uint32_t compareID(register const unsigned char * id1, register const unsigned char * id2)
{
	register unsigned char		i;

	i = 4;
	while (i--)
	{
		if ( *(id1)++ != *(id2)++ ) return 0;
	}
	return ((uint32_t)id1[0] << 24) | ((uint32_t)id1[1] << 16) | ((uint32_t)id1[2] << 8) | ((uint32_t)id1[3]);
}





/********************* vlqToLong() *******************
 * Converts a variable length quantity, and returns the value as a uint32_t.
 * Passed a ptr to the first byte of the variable length quantity, and
 * subsequent bytes follow. Also returns the updated ptr.
 */

static unsigned char * vlqToLong(register unsigned char * ptr, register unsigned char * endptr, uint32_t * len)
{
	register uint32_t val;

	if (ptr < endptr)
	{
		// Get the first byte
		val = (uint32_t)(*(ptr)++);

		// If bit #7 of this first byte is set, then there are more bytes to the quantity
		if ( val & 0x80 )
		{
			val &= 0x7F;
			do
			{
				if (ptr >= endptr) goto bad;

				// Combine the next variable length byte with the accumulated value so far
				val = (val << 7) | (uint32_t)(*(ptr) & 0x7F);

				// Was this the last byte of the variable length quantity?
			} while (*(ptr)++ & 0x80);
		}
	}
	else
bad:	val = 0xFFFFFFFF;

	*len = val;

	return ptr;
}





static void format_err(register const char * message)
{
	register char *pstr;
	register char *copy;

	pstr = FilenamePtr + strlen(FilenamePtr);
	while (pstr > FilenamePtr)
	{
		if (*(--pstr) == '/')
		{
			++pstr;
			copy = FilenamePtr;
			while ((*(copy)++ = *(pstr)++));
			break;
		}
	}

	strcat(FilenamePtr, message);
}




static unsigned char * loadFile(unsigned char ** endptr)
{
	register unsigned char *mem;
	const char *				message;
	register int				inHandle;

	message = DidntOpenStr;
	mem = 0;
//printf("%s\r\n", &fn[0]);
	if ((inHandle = open(&fn[0], O_RDONLY|O_NOATIME)) != -1)
	{
		struct stat			buf;

		message = CorruptStr;

		// Allocate a buffer to load in the midi data, and load it. Also alloc room for max song chain
		fstat(inHandle, &buf);
		if (buf.st_size >= **endptr)
		{
			if (!(mem = (unsigned char *)malloc(buf.st_size + MAX_NUM_OF_CHAINS + sizeof(STYLE_VARIATION) - 1)))
			{
				strcpy(FilenamePtr, NoMemStr);
				goto zero;
			}

			*endptr = mem + buf.st_size + MAX_NUM_OF_CHAINS + sizeof(STYLE_VARIATION) - 1;
			if (read(inHandle, &((STYLE_VARIATION *)mem)->Chains[MAX_NUM_OF_CHAINS], buf.st_size) == buf.st_size)
zero:			message = 0;
		}
		close(inHandle);
	}

	if (message)
	{
		format_err(message);

		if (mem) free(mem);
		mem = 0;
	}

	return mem;
}





/******************* midiToVariation() ******************
 * Reads in a midi format 1 file, and writes a pattern
 * file formatted per BackupBand's style variation.
 *
 * StyleType =		0 for drum, 1 for bass, 2 for guitar.
 *
 * If an error, displays a message.
 */

static unsigned char isCutTime(void);
static unsigned char isUseTempo(void);

static void midiToVariation(void)
{
	register unsigned char *	mem;
	unsigned char *				endptr;

	// Load the midi file into RAM. We'll convert it in-place
	{
	unsigned char					size;

	endptr = &size;
	size = 8 + 6 + 8 + 4;
	if (!(mem = loadFile(&endptr))) goto errout;
	}

	{
	register unsigned char *	pstr;
	register unsigned char *	copy;
	unsigned char *				songChainPtr;
	register uint32_t				evTime;
	uint32_t							measTimeLen;
	unsigned char					divisor, trackNum;

	songChainPtr = &((STYLE_VARIATION *)mem)->Chains[0];
	copy = pstr = songChainPtr + MAX_NUM_OF_CHAINS;

	// Search for the 4 character "MThd" ID
	while (pstr + 14 <= endptr)
	{
		if ((evTime = compareID(pstr, &MthdId[0])))
		{
			if (evTime < 6)
			{
badmidi:		endptr = (unsigned char *)&BadMidiStr[0];
				goto err;
			}

			pstr += 8;

			((STYLE_VARIATION *)mem)->NumPtns = ((STYLE_VARIATION *)mem)->u.NumFills = trackNum = 0;

			// Calculate divisor to convert to 24 PPQN resolution
			measTimeLen = ((uint32_t)pstr[4]<<8) | (uint32_t)pstr[5];
			if (!(divisor = (unsigned char)(measTimeLen / 24)) || measTimeLen % 24)
			{
				endptr = mem;
				sprintf((char *)mem, " has a %u ppqn. Use %u instead", measTimeLen, divisor ? (measTimeLen/24)*24 : 24);
				goto err;
			}
			if (isCutTime()) divisor <<= 1;

			// If no fill/ending track, use default ending
			if (pstr[3] < 2) ((STYLE_VARIATION *)mem)->u.NumFills = VARFLAG_DEFAULT_END;

			// Assume 4/4
			measTimeLen = 4 * 24;

			// Skip over the Mthd
			pstr += evTime;

			goto nexttrk;
		}

		pstr++;
	}

	// Can't find mthd
	endptr = (unsigned char *)&NotMidiStr[0];

err:
	format_err((char *)endptr);
	free(mem);
errout:
	show_msgbox(FilenamePtr);
	return;

	// =======================================
nexttrk:

	if (StyleType > 2) copy = mem;

	// Search for the next "MTrk"
	while (pstr + 8 <= endptr)
	{
		if ((evTime = compareID(pstr, &MthdId[14])))
		{
			unsigned char *		cntPtr;
			uint32_t					measStartTime, time24ppqn;
			unsigned char			runningstatus, flag, putchain, lastNoteOn;

			// Check chunk size is legal
			pstr += 8;
			if (pstr + evTime > endptr) goto badmidi;

			// Reset vars
			measStartTime = evTime = flag = runningstatus = lastNoteOn = 0;
			memset(&Play, 0, sizeof(Play));
			putchain = 1;
			if (StyleType > 2) lastNoteOn = 1;

			// Save first measure start
			cntPtr = copy;

			for (;;)
			{
				// Get timing referenced from start of track (time=0)
nextevt:		pstr = vlqToLong(pstr, endptr, &time24ppqn);
				if (time24ppqn == 0xFFFFFFFF) goto badmidi;

				// For songsheet, if delta time not 0, see if we have some event to insert
				if (StyleType > 2 && time24ppqn && trackNum && (Play.Vars.Repeat.Flags1|flag))
				{
//printf("event %04u, %u:%u\r\n", (uint32_t)(copy - mem), lastNoteOn, copy[1]);
					*copy = lastNoteOn;
					copy += 2;
					*copy++ = Play.Vars.Repeat.Flags1;
					*copy++ = flag;
					if (flag & 0x10) *copy++ = Play.Vars.Repeat.VarNum;
					if (flag & 0x08)
					{
						memcpy(copy, Play.Vars.Repeat.Stylename, Play.Vars.Repeat.Stylename[0]);
						if (Play.Vars.Repeat.Stylename[0] < 8) Play.Vars.Repeat.Stylename[0] = 8;
						copy += Play.Vars.Repeat.Stylename[0];
					}
					if (flag & 0x40)
					{
						*copy++ = Play.Vars.Repeat.PadVel;
						*copy++ = Play.Vars.Repeat.Pad;
					}
					if (flag & 0x04) *copy++ = Play.Vars.Repeat.Scale | (putchain << 4);
					if (flag & 0x20) *copy++ = Play.Vars.Repeat.Tempo;
					if (Play.Vars.Repeat.Flags1 & 0x08)
					{
						*copy++ = Play.Vars.Repeat.TimeSig1;
						*copy++ = Play.Vars.Repeat.TimeSig2;
					}
					Play.Vars.Repeat.Flags1 = Play.Vars.Repeat.Scale = flag = lastNoteOn = 0;
				}

				evTime += time24ppqn;

				// Adjust time for 24 PPQN
				time24ppqn = (evTime / divisor);
				if ((evTime % divisor) > 11) time24ppqn++;

				if (StyleType > 2 || (putchain != 0xff && pstr[0] != 0xFF))
				{
					// Does the evt have a Time >= next ptn boundary?
					while (time24ppqn >= measStartTime + measTimeLen)
					{
						//if (trackNum) printf("EvtTime=%u MeasStart=%u MeasEnd=%u\r\n", evTime, measStartTime * 24, (measStartTime + measTimeLen) * 24);

						// Inc measure start time by 1 measure
						measStartTime += measTimeLen;
						if (StyleType > 2) lastNoteOn++;

						// Don't store any meas until the first note event
						else if (trackNum)
						{
//							if (StyleType > 2)
//								lastNoteOn++;
//							else
							{
								// Skip over the measure length byte
								if (cntPtr == copy) copy++;

								// Del any unneeded EVTs
close:						if (StyleType == 1)
								{
									register unsigned char *		ptn;
									register unsigned char *		to;

									ptn = to = cntPtr + 1;
									while (ptn < copy)
									{
										if (ptn[1] & BASSEVTFLAG_NOT_ON)
										{
											// We can get rid of this ntf if the next
											// event is a ntn <= 1 MIDI clock away
											if (ptn[1] == BASSEVTFLAG_NOTEOFF && copy - ptn >= 5 && (ptn[2] - ptn[0]) < 2 && !(ptn[3] & BASSEVTFLAG_NOT_ON))
											{
												ptn += 2;
												continue;
											}
										}
										else
											*to++ = *ptn++;

										*to++ = *ptn++;
										*to++ = *ptn++;
									}

									copy = to;
								}

								// Insert the meas end timing mark
								*copy++ = measTimeLen;

								// Set the meas length
								if (copy - cntPtr > 255)
								{
									sprintf((char *)mem, " has too many notes in measure %u", ((STYLE_VARIATION *)mem)->NumPtns+1);
									endptr = mem;
									goto err;
								}
								*cntPtr = (unsigned char)(copy - cntPtr);

								{
								register unsigned char		ptnNum;

								ptnNum = 0;

								// Don't add a song chain if parsing fills/ending mtrk
								if (trackNum > 1)
								{
									// If fill flag set in the fill mtrk, then this is the end meas
									if (flag)
									{
										// If the end meas is empty, this indicates to use default ending
										if (cntPtr + 1 >= copy)
										{
											flag = VARFLAG_DEFAULT_END;
											goto def_end;
										}
									}
									else
									{
										// Ignore empty meas in fill mtrk
										if (cntPtr + 1 >= copy) goto ignore;

										// If bass/gtr, then this is the end meas (even without end flag set).
										// For drum, it's another fill
										if (!StyleType) ((STYLE_VARIATION *)mem)->u.NumFills++;
										else flag = VARFLAG_DEFAULT_END;
									}
								}
								else
								{
									register unsigned char *	tempptr;

									// Compare to previous measures that were formatted. If the same as a prev, then don't write it out
									tempptr = &((STYLE_VARIATION *)mem)->Chains[MAX_NUM_OF_CHAINS];
									while (tempptr < cntPtr)
									{
										if (cntPtr[0] == tempptr[0] && !memcmp(cntPtr, tempptr, cntPtr[0]))
										{
											// Discard this copy
ignore:									copy = cntPtr;

											// Set the song chain byte to that prev ptn #
											goto skipit;
										}

										++ptnNum;
										tempptr += tempptr[0];
									}
								}

								// A unique meas, so save it
								cntPtr = copy;

								// inc NumPtns
								if (++((STYLE_VARIATION *)mem)->NumPtns >= 127) goto limit;

								// For chain mtrk, store next Song chain byte
skipit:						if (trackNum < 2)
								{
									if (putchain == 1)
									{
										// For Drum variation, also save state of fill flag
										*songChainPtr++ = ptnNum | (StyleType ? 0 : flag);
										if (songChainPtr >= &((STYLE_VARIATION *)mem)->Chains[MAX_NUM_OF_CHAINS])
										{
limit:									endptr = (unsigned char *)&Limit128Str[0];
											goto err;
										}
									}
									putchain = 1;
								}

								// End measure of fill/ending track?
								else if (flag) goto done;

								// For drum, each meas has its own fill flag, so reinit it
								if (!StyleType) flag = 0;
								}
							}
						}
					}
				}

				if (putchain != 0xff)
				{
					// Adjust time to meas start
					time24ppqn -= measStartTime;
					if (time24ppqn >= measTimeLen)
					{
						endptr = (unsigned char *)&BadStampStr[0];
						goto err;
					}

					if (StyleType > 2) copy[1] = (unsigned char)time24ppqn;
				}

				{
 				register unsigned char	status;

				// Is it a "Meta event" -- that is, a non-MIDI event?
				status = *pstr++;
				if (status == 0xFF)
				{
					uint32_t		delta;

					if (pstr + 2 > endptr) goto badmidi;

					status = *pstr++;
					switch (status)
					{
						// ------- End of Track ------
						case 0x2f:
						{
							// SongSheet
							if (StyleType > 2)
							{
								// Check that all repeats are closed
songdone:					if (Play.Vars.Repeat.Num)
								{
									endptr = (unsigned char *)&OpenLoop[0];
									goto err;
								}

								if (copy == mem) goto empty;

								// Insert DONE event
								*copy++ = lastNoteOn;
								*copy++ = (unsigned char)time24ppqn;
								*copy++ = 0x10;
								*copy++ = 0;

								songChainPtr = copy;
								goto doneSong;
							}

							// If there are note events to be stored, first store them
							// in another new meas, before we process the end of track
							if (cntPtr != copy)
							{
								pstr -= 2;
								goto close;
							}

							pstr++;

							// Process the fill/ending mtrk
							if (++trackNum <= 2)
							{
								// At end of chain mtrk for bass/gtr, if we have a repeat marker,
								// add a song chain to jump back
								if (trackNum == 2 && StyleType && flag)
								{
									*songChainPtr = -((char)(songChainPtr - &((STYLE_VARIATION *)mem)->Chains[flag]));
									if (++songChainPtr >= &((STYLE_VARIATION *)mem)->Chains[MAX_NUM_OF_CHAINS]) goto limit;
								}

								goto nexttrk;
							}

							// At the fill/ending mtrk end, store VARFLAG_DEFAULT_END state
def_end:					((STYLE_VARIATION *)mem)->u.Flags |= flag;

							goto done;
						}

						// ---- Text -----
						case 0x01:
						{
							if (StyleType > 2)
							{
								register unsigned char * name;
								register unsigned char * stylename;
								register unsigned char  chrflags;

								stylename = name = pstr - 1;
								pstr = vlqToLong(pstr, endptr, &delta);
								if (delta == 0xFFFFFFFF || !delta || pstr + delta > endptr) goto badmidi;
								chrflags = 0;
								while (delta--)
								{
									status = *pstr++;
									if ((chrflags & 0x01) || status != ' ')
									{
										chrflags |= 0x01;
										if (status == '/')
										{
											if ((chrflags & 0x02) || stylename == name)
											{
badstyle:									endptr = (unsigned char *)&BadStyle[0];
												goto err;
											}
											chrflags = 0x02;
											status = 0;
											while (name[-1] == ' ') name--;
										}
										*name++ = status;
									}
								}
								if (chrflags != 0x03) goto badstyle;
								while (name[-1] == ' ') name--;
//								if (!name[0]) goto badstyle;
								*name = 0;
								--stylename;
								*stylename = (unsigned char)(name - stylename) + 1;
								Play.Vars.Repeat.Stylename = stylename;
								flag |= 0x08;
								goto nextevt;
							}

							break;
						}

						// ---- Marker -----
						case 0x06:
						{
							if (StyleType > 2)
							{
								if (*pstr == 4 && !memcmp(&DoneStr[0], &pstr[1], 4)) goto songdone;

								if (*pstr == 1)
								{
									status = pstr[1];
									if (status >= 'a' && status <= 'v') status &= 0x5F;
									switch (status)
									{
										case 'B':
											status = 2;
											goto vari;
										case 'C':
											status = 1;
											goto vari;
										case 'I':
											status = 3;
											goto vari;
										case 'V':
											status = 0;
vari:										flag |= 0x10;
											Play.Vars.Repeat.VarNum = status;
											goto varlen;
									}
								}

								// Marker must be on a measure start
								if (time24ppqn)
								{
									endptr = (unsigned char *)&OnMeas[0];
									goto err;
								}

								// Repeat start?
								if (!*pstr)
								{
									if (Play.Vars.Repeat.Num >= 15)
									{
										endptr = (unsigned char *)&TooManyLoops[0];
										goto err;
									}

									// Temporarily save start offset
									Play.Vars.Repeat.Start[Play.Vars.Repeat.Num++] = copy;
//printf("start %04u\r\n", (uint32_t)(copy - mem));
									// Clear loop arrays
resetloop:						memset(&Play.Vars.Repeat.Ending[0], 0, sizeof(Play.Vars.Repeat.Ending));
									memset(&Play.Vars.Repeat.EndingNum[0], 0, sizeof(Play.Vars.Repeat.EndingNum));
									Play.Vars.Repeat.Endings = 0;
								}

								else if (*pstr == 3 && !memcmp(&EndStr[0], &pstr[1], 3))
								{
									Play.Vars.Repeat.Flags1 |= 0x20;
								}

								// Repeat finish?
								else if (*pstr == 4 && !memcmp(&LoopStr[0], &pstr[1], 4))
								{
									unsigned int					adjust;
									register unsigned char *	table;
									register unsigned short		offset;

									if (!Play.Vars.Repeat.Num)
									{
										endptr = (unsigned char *)&NoStart[0];
										goto err;
									}

									adjust = (2 * Play.Vars.Repeat.Endings);

									// If no endings, then insert the REPEAT event here
									if (!Play.Vars.Repeat.Endings)
									{
										*copy++ = lastNoteOn;
										cntPtr = copy;
									}
									else
										cntPtr++;

//printf("loop  %04u insert %u bytes at %04u\r\n", (uint32_t)(copy - mem), adjust, (uint32_t)(cntPtr - mem));

									// Insert the REPEAT event
									*cntPtr++ = 0;
									*cntPtr++ = 0x40;
									*cntPtr++ = (Play.Vars.Repeat.Endings << 4);

									// Set the offset to loop start
									table = cntPtr;
									offset = (unsigned short)(table - Play.Vars.Repeat.Start[--Play.Vars.Repeat.Num]);
									*cntPtr++ = (unsigned char)(offset >> 8);
									*cntPtr++ = (unsigned char)(offset);

									if (!Play.Vars.Repeat.Endings)
										copy = cntPtr;
									else
									{
										// Make room to insert the ending offsets
										cntPtr += 2;
										memmove(cntPtr + adjust, cntPtr, copy - cntPtr);

										// Insert a REPEATEND event for the last ending, unless it's the same as
										// preceding ending
										copy += adjust;
										if (Play.Vars.Repeat.Ending[Play.Vars.Repeat.Endings - 1] != copy+adjust)
										{
											*copy++ = lastNoteOn;
											*copy++ = 0;
											offset = (unsigned short)(copy + 2 - table) | 0x8000;
											*copy++ = (unsigned char)(offset >> 8);
											*copy++ = (unsigned char)(offset);
										}

										// Set the loop end offset
										offset = (unsigned short)(copy - table);
										cntPtr[-2] = (unsigned char)(offset >> 8);
										cntPtr[-1] = (unsigned char)(offset);

										for (;;)
										{
											register unsigned char *	ptr;

											ptr = Play.Vars.Repeat.Ending[--Play.Vars.Repeat.Endings] + adjust;
											Play.Vars.Repeat.Ending[Play.Vars.Repeat.Endings] = ptr;

											if (!Play.Vars.Repeat.Endings) break;

											// Insert a REPEATEND event for the ending
											offset = (unsigned short)(ptr - table) | 0x8000;
											ptr--;
											*ptr-- = (unsigned char)(offset);
											*ptr-- = (unsigned char)(offset >> 8);
											*ptr-- = 0;
										}

										// Fill in the table
										{
										register unsigned char *	ptr;

										ptr = &Play.Vars.Repeat.EndingNum[15];
										do
										{
											if ((status = ptr[0]))
											{
												offset = (unsigned short)(Play.Vars.Repeat.Ending[--status] - table);
												*cntPtr++ = (unsigned char)(offset >> 8);
												*cntPtr++ = (unsigned char)(offset);
											}
										} while (--ptr >= &Play.Vars.Repeat.EndingNum[0]);
										}
									}

									lastNoteOn = 1;
									goto resetloop;
								}

								// Must be an ending
								else
								{
									*copy = lastNoteOn;
									lastNoteOn = 1;

									// If the first ending, this is where we insert the REPEAT event,
									// so save the ptr
									if (!Play.Vars.Repeat.Endings)
									{
										cntPtr = copy;
										copy += 8;
									}

									// If not first, we must leave room for a REPEATEND event, unless
									// this ending is the same spot as preceding ending
									else if (Play.Vars.Repeat.Ending[Play.Vars.Repeat.Endings - 1] != copy)
									{
										copy += 4;
									}

									if (Play.Vars.Repeat.Endings >= 15)
									{
										endptr = (unsigned char *)&TooManyEndings[0];
										goto err;
									}

									// Store the next ending ptr
									if (*pstr > 2 || pstr[1] < '1' || pstr[1] > '9')
									{
										endptr = (unsigned char *)&BadMarker[0];
										goto err;
									}

									status = pstr[1] - '1';
									if (*pstr > 1)
									{
										if (status || pstr[2] < '0' || pstr[2] > '6')
										{
											endptr = (unsigned char *)&BadEnding[0];
											goto err;
										}

										status = pstr[2] - '0' + 10;
									}

									if (Play.Vars.Repeat.EndingNum[status])
									{
										endptr = (unsigned char *)&DupeEnding[0];
										goto err;
									}

									Play.Vars.Repeat.Ending[Play.Vars.Repeat.Endings++] = copy;
									Play.Vars.Repeat.EndingNum[status] = Play.Vars.Repeat.Endings;
//printf("end %u %04u\r\n", status, (uint32_t)(copy - mem));
								}
							}

							break;
						}

						// ---- Tempo -----
						case 0x51:
						{
							if (/*trackNum && */isUseTempo() && StyleType > 2)
							{
								register uint32_t	bpm;

								// Also store BPM
								if ((bpm = ((uint32_t)pstr[1] << 16) | ((uint32_t)pstr[2] << 8) | ((uint32_t)pstr[3]))) bpm = 60000000/bpm;
								Play.Vars.Repeat.Tempo = (unsigned char)bpm;
								flag |= 0x20;
							}

							break;
						}

						// ---- Time Signature -----
						case 0x58:
						{
							if (trackNum < 2)
							{
								if (((STYLE_VARIATION *)mem)->NumPtns && evTime)
								{
									endptr = (unsigned char *)&BadTimeStr[0];
									goto err;
								}

								if (pstr[2] > 5)
								{
unsup:							endptr = (unsigned char *)&NoSupTimeStr[0];
									goto err;
								}

								if (StyleType > 2 && pstr[1] != 4 && pstr[2] != 2)
								{
									Play.Vars.Repeat.TimeSig1 = pstr[1];
									Play.Vars.Repeat.TimeSig2 = pstr[2];
									Play.Vars.Repeat.Flags1 |= 0x08;
								}

								measTimeLen = (uint32_t)pstr[1] * (uint32_t)Denoms[pstr[2]];
								if (measTimeLen > 255) goto unsup;
							}

							break;
						}
					}

varlen:			pstr = vlqToLong(pstr, endptr, &delta);
					pstr += delta;
					if (delta == 0xFFFFFFFF || pstr > endptr) goto badmidi;
				}
				else
				{
					// Resolve running status
					if (status & 0x80) runningstatus = status;
					else pstr--;

					//printf("%02x %02x %02x\r\n",runningstatus,pstr[0],pstr[1]);

					// --------------------- note -------------------
					// Is it a Note?
					if (runningstatus >= 0x80 && runningstatus <= 0x9F)
					{
						if (pstr + 2 > endptr) goto badmidi;

						// Change note-off to on/0-velocity
						if (runningstatus < 0x90) pstr[1] = 0;

						// SongSheet
						if (StyleType > 2)
						{
							if (pstr[1])
							{
								status = pstr[0];
								if (status < 60)
								{
									if (status < 60 - 12)
									{
										if (status == 47) flag |= 0x01;			// SONGFLAG_ADDNINE
										else if (status == 43) flag |= 0x02;	// SONGFLAG_ADDSEVEN
										else goto range;
									}
									else
									{
										if (status < 52) status += 12;
										status -= 51;
										putchain = status;
										flag |= 0x04;
									}
								}
								else
								{
									// high E = FILL, E = minor, F = dominent, G = sus, A = aug, B = dim, C = 2m/R, D = 2M/R
									if (status == 76)
										flag |= 0x80;
									else if (status == 77)
										Play.Vars.Repeat.Scale = putchain = 0;
									else if (status < 64)
									{
										flag |= 0x40;
										Play.Vars.Repeat.Pad = status - 60;
										Play.Vars.Repeat.PadVel = pstr[1];
									}
									else if (status > 60+14 || (Play.Vars.Repeat.Scale = ChordSheet[status - 64]) == 0xFF) goto range;
								}
							}
						}
						else
						{
							// Fill/end/repeat flag (ie Note #0 or 1)?
							if ((status = pstr[0]) < 2)
							{
								// Note-on?
								if (pstr[1])
								{
									// If the chain mtrk of bass/gtr, then the flag indicates a repeat marker,
									// except for the first meas which indicates VARFLAG_HAS_HALFMEAS
									if (StyleType && trackNum < 2)
									{
										if (!((STYLE_VARIATION *)mem)->NumPtns)
										{
											((STYLE_VARIATION *)mem)->u.Flags |= VARFLAG_HAS_HALFMEAS;

											// Note 0 doesn't include the half measure in the Chain[]. Note 1 does
											putchain = status;
										}
										else
										{
											if (status == 1) goto endtrk;

											if (flag)
											{
												endptr = (unsigned char *)&BadRepeatStr[0];
												goto err;
											}

											// Save offset of repeat meas
											flag = (unsigned char)(songChainPtr - &((STYLE_VARIATION *)mem)->Chains[0]);
										}
									}

									// If the chain mtrk of drums, then the flag indicates a fill marker.
									// If the fill/ending mtrk of drum/bass/gtr, then the flag indicates the end meas
									else
									{
	 									flag = 0x80;

										// If note #1, this marks the end of the track. Ignore all subsequent events in this mtrk
										if (status == 1)
endtrk:									putchain = 0xff;
									}
								}
							}
							else
							{
								if (putchain != 0xff)
								{
									unsigned char	addNtf;

									// Note-on?
									addNtf = 0;
									if (pstr[1])
									{
										register uint32_t			prevTime;

										// If a duplicate note, skip it
										if ((prevTime = addToHistory(time24ppqn, status)) != 0xFFFFFFFF)
										{
											if (prevTime == time24ppqn) goto skipnote;

											if (StyleType)
											{
												// We need to insert a ntf before this new note-on
												addNtf = status;
											}
										}
									}
									else if (!remFromHistory(status))
										goto skipnote;

									switch (StyleType)
									{
										case 1:
										{
											if (status < 26+12 || status >= 60+12)
											{
range:										memcpy(mem, &HasStr[0], sizeof(HasStr));
												strcpy((char *)numToPitch(mem + sizeof(HasStr), status), " note out of range");
												endptr = mem;
												goto err;
											}

											// Note-on?
											if (pstr[1])
											{
												lastNoteOn = status;
												status -= 36;
												if (status < 12)
													status = ScaleSteps[status] | BASSEVTFLAG_OCTDOWN;
												else if (status < 24)
													status = ScaleSteps[status - 12];
												else
													status = ScaleSteps[status - 24] | BASSEVTFLAG_OCTUP;

												goto save_evt;
											}

											// Since our bass is monophonic, if this ntf doesn't
											// match the preceding ntn, then it's an extraneous
											// ntf
											if (lastNoteOn == status)
											{
												if (cntPtr == copy) copy++;
												*copy++ = (unsigned char)time24ppqn;
												*copy++ = BASSEVTFLAG_NOTEOFF;
											}

											lastNoteOn = 0;
											break;
										}

										case 2:
										{
											if (status < 60 - (3*12) || status >= 60+(4*12)) goto range;

											if (status >= 60)
												status =  ScaleSteps[(status % 12)] | (((status - 60) / 12) << 3) | (pstr[1] ? 0x80 : GTREVTFLAG_STEPOFF);
											else
											{
												if (pstr[1])
													status =  StrumsOn[status % 12];
												else
													status =  StrumsOff[status % 12];
												status |= Pitch[(pstr[0] - (60 - (3*12))) / 12];
											}

											if (cntPtr == copy) copy++;
											if (addNtf)
											{
	//if (copy >= pstr) printf("over =================================\r\n");

												*copy++ = (unsigned char)time24ppqn;
												*copy++ = status | ((status & 0x80) ? GTREVTFLAG_STEPOFF : GTREVTFLAG_STRINGOFF);
											}

											if (pstr[1]) goto save_evt;
											*copy++ = (unsigned char)time24ppqn;
											*copy++ = status;

											break;
										}

										default:
										{
											// Drum ptn omits note-offs
											if (pstr[1])
											{
												// Implement "roll blending"
												if (lastNoteOn == status) pstr[1] |= 0x80;

												// Skip over measure's length byte. We'll set it later
save_evt:									if (cntPtr == copy) copy++;

//if (copy >= pstr) printf("over =================================\r\n");


												// Copy over the Time referenced from meas start, note #, and vel
												*copy++ = (unsigned char)time24ppqn;
												*copy++ = status;
												*copy++ = pstr[1];
											}
										}
									}
								}
							}
						}

skipnote:			pstr += 2;

						// Since we got a note event, pretend we're no longer parsing the
						// tempo track 0
						if (!trackNum) trackNum++;
					}

					// --------------------- non-note -------------------

					else if (StyleType && StyleType <= 2 && runningstatus >= 0xC0 && runningstatus < 0xD0)
					{
						status = *pstr++;
						if (status && status < 16)
						{
							if (copy == &((STYLE_VARIATION *)mem)->Chains[MAX_NUM_OF_CHAINS])
								((STYLE_VARIATION *)mem)->u.Flags = (((STYLE_VARIATION *)mem)->u.Flags & 0xf0) | status;
/*
							else switch (StyleType)
							{
								case 1:
								{
								}
							}
*/
						}
					}
					else
					{
						// Sysex specifies its length
						if (runningstatus == 0xF0) goto varlen;

						// Save aft note #
						if (runningstatus < 0xB0)
						{
							if (!StyleType) lastNoteOn = pstr[1] ? *pstr : 0;
						}

						// We'll assume the MIDI app doesn't save other
						// System common or realtime

						pstr += (runningstatus >= 0xC0 && runningstatus < 0xE0 ? 1 : 2);
					}
				}
				}
			}
		}

		// Unknown chunk. Skip it
		else
			pstr += 8 + compareID(pstr, pstr);
	}

	// No more chunks?
	if (StyleType > 2 || pstr != endptr) goto badmidi;

done:
	// Set NumChains
	if (!(((STYLE_VARIATION *)mem)->NumChains = (unsigned char)(songChainPtr - &((STYLE_VARIATION *)mem)->Chains[0])))
	{
empty:
		endptr = (unsigned char *)&OneMeasStr[0];
		goto err;
	}
	((STYLE_VARIATION *)mem)->NumChains++;

	// Move the meas ptns to right after the song chain
	pstr = &((STYLE_VARIATION *)mem)->Chains[MAX_NUM_OF_CHAINS];
	memmove(songChainPtr, pstr, copy - pstr);
	songChainPtr += (copy - pstr);

doneSong:

	// Remove .mid from FilenamePtr
	pstr = (unsigned char *)(FilenamePtr + strlen(FilenamePtr));
	while ((char *)pstr > FilenamePtr && pstr[-1] != '/')
	{
		if (*pstr == '.')
		{
			*pstr = 0;
			break;
		}
		--pstr;
	}
	if (*pstr) strcat((char *)pstr, "V");

	// Save the style variation file
	{
	register int				fh;
	register unsigned long 	len;

	if ((fh = open(&fn[0], O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1)
	{
		endptr = (unsigned char *)&DidntOpenStr[0];
		goto err;
	}

	len = write(fh, mem, songChainPtr - mem);
	close(fh);

	if (len != songChainPtr - mem)
	{
		unlink(&fn[0]);
		endptr = (unsigned char *)&DidntSaveStr[0];
		goto err;
	}
	}
	}

	free(mem);
}





static const char MidExtension[] = {'.','m','i','d',0};

static GUILIST * drawStyleItems(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app)
	{
		register DIR *				dirp;
		register struct dirent *dirEntryPtr;
		register unsigned int	len;

		// Open the dir for searching
		len = get_style_path(FilenamePtr);
		FilenamePtr[len] = '.';
		FilenamePtr[len+1] = 0;
		if ((dirp = opendir(&fn[0])))
		{
			// Get next file/dir in the dir
			while ((dirEntryPtr = readdir(dirp)))
			{
				register const char *	str;
				uint32_t						len2;

				// Check that name ends in .mid
				str = dirEntryPtr->d_name;
				len2 = strlen(str);
				if (len2 > sizeof(MidExtension)-1 && !memcmp(&str[len2 - (sizeof(MidExtension)-1)], &MidExtension[0], sizeof(MidExtension)-1))
				{
					if (area)
					{
						if (GuiListDrawItem(app, ctl, area, str))
							break;
					}
					else if (!List.CurrItemNum--)
					{
						strcpy(&FilenamePtr[len], str);
						break;
					}
				}
			}

			closedir(dirp);
		}
	}

	return &List;
}

static void setStyleItemWidth(void)
{
	register DIR *				dirp;
	register struct dirent *dirEntryPtr;
	register unsigned int	len;

	// Open the dir for searching
	len = get_style_path(&fn[0]);
	fn[len] = '.';
	fn[len+1] = 0;
	if ((dirp = opendir(&fn[0])))
	{
		// Get next file/dir in the dir
		while ((dirEntryPtr = readdir(dirp)))
		{
			register const char *	str;

			// Check that name ends in .mid
			str = dirEntryPtr->d_name;
			len = strlen(str);
			if (len > sizeof(MidExtension)-1 && !memcmp(&str[len - (sizeof(MidExtension)-1)], &MidExtension[0], sizeof(MidExtension)-1))
				GuiListItemWidth(GuiApp, MainWin, &List, str);
		}

		closedir(dirp);
	}
}

static void doConvertScreen(void);

static void style_mouse(register GUIMSG * msg)
{
	register GUICTL *		ctl;

	if ((ctl = msg->Mouse.SelectedCtl))
	{
		if (ctl->Flags.Global & CTLGLOBAL_PRESET)
		{
			if (ctl->PresetId == GUIBTN_OK) goto good;
			goto cancel;
		}

		if (ctl->Type == CTLTYPE_AREA)
		{
			switch (msg->Mouse.ListAction)
			{
				case GUILIST_SELECTION:
				{
good:				drawStyleItems(GuiApp, &ListGuiCtls[0], 0);

					// Convert MIDI file to BackupBand variation
					midiToVariation();
				}
				// Fall through

				case GUILIST_ABORT:
cancel:			doConvertScreen();
			}
		}
	}
}

/*********************** makeStyle() ***********************
 * Called when user clicks on the "Make style" button.
 */

static void makeStyle(void)
{
	MouseFunc = style_mouse;
	FilenamePtr = &fn[0];
	doPickItemDlg2(drawStyleItems);
	setStyleItemWidth();
}















static const char	BadChordStr[] = " has a bad chord";
static const char	BadStepStr[] = " has a bad step";
static const char	BadNoteStr[] = " has a bad note number";
static const char	BadVelStr[] = " has a bad velocity";

#define SONGFLAG_ADDNINE	0x0001
#define SONGFLAG_ADDSEVEN	0x0002
#define SONGFLAG_CHORD		0x0004
#define SONGFLAG_STYLE		0x0008
#define SONGFLAG_VARIATION	0x0010
#define SONGFLAG_TEMPO		0x0020
#define SONGFLAG_PAD			0x0040
#define SONGFLAG_FILL		0x0080
#define SONGFLAG_BASS		0x0100
#define SONGFLAG_GUITAR		0x0200
#define SONGFLAG_DRUMS		0x0400
#define SONGFLAG_TIMESIG	0x0800
#define SONGFLAG_SONGDONE	0x1000
#define SONGFLAG_SONGEND	0x2000
#define SONGFLAG_REPEAT		0x4000
#define SONGFLAG_REPEATEND	0x8000

static unsigned char * insert_ntf(register unsigned char * ptr, register unsigned char status)
{
	register unsigned char first;

	first = 1;
	if (status != 0x90) *ptr++ = 0x90;

	status = Play.Vars.Loop.NoteFlags;

	if (status & SONGFLAG_FILL)
	{
		*ptr++ = 76;
		*ptr++ = first = 0;
	}

	if (status & SONGFLAG_PAD)
	{
		*ptr++ = Play.Vars.Loop.Pad;
		*ptr++ = first = 0;
	}

	if (status & SONGFLAG_CHORD)
	{
		register unsigned char 		note;

		if (!(*ptr = first)) ptr++;
		if ((first = Play.Vars.Loop.NoteSpec))
		{
			note = (first >> 4) + 51;
			if (note >= 60) note -= 12;
			*ptr++ = note;
			*ptr++ = 0;

			if (first & 0x07)
			{
				*ptr++ = 0;
				*ptr++ = SongScales[(first & 0x07)];
				*ptr++ = 0;
			}

			if (status & SONGFLAG_ADDNINE)
			{
				*ptr++ = 0;
				*ptr++ = 47;
				*ptr++ = 0;
			}

			if (status & SONGFLAG_ADDSEVEN)
			{
				*ptr++ = 0;
				*ptr++ = 43;
				*ptr++ = 0;
			}
		}
		else
		{
			*ptr++ = 77;
			*ptr++ = 0;
		}
	}

	Play.Vars.Loop.NoteTime = -1;

	return ptr;
}


/********************* longToVlq() *********************
 * Converts a uint32_t (ie, 32-bit) value (upto 0x0FFFFFFF) to
 * a variable length quantity (ie, series of bytes). Passed a
 * ptr to the where to store the (upto 4) bytes. Returns the
 * updated ptr.
 */

static unsigned char * longToVlq(unsigned char * ptr, uint32_t val)
{
	register unsigned char		 *	temp;
	register unsigned char			count;
	unsigned char						buffer[8];

	// Since the max range for val is 0x0FFFFFFF, which fits into 4 variable length bytes,
	// we'll pack up those bytes into a 4 byte buffer
	temp = &buffer[7];

	count = 1;
	*(temp) = (unsigned char)val & 0x7F;
	while ( (val >>= 7) )
	{
		*(--temp) = (unsigned char)val | 0x80;
		++count;
	}

	// Copy those bytes to the app buffer
	do
	{
		*(ptr)++ = *(temp)++;

	} while (--count);

	return ptr;
}

static void variationToMidi(void)
{
		register unsigned char *mem;
		register unsigned char *data;
		unsigned int				len;

		// Load style file
		{
		unsigned char				size;
		unsigned char *			endptr;

		endptr = &size;
		size = StyleType > 2 ? 6 : sizeof(STYLE_VARIATION);
		mem = loadFile(&endptr);
		data = endptr;
		}

		if (!mem)
err:		show_msgbox(FilenamePtr);
		else
		{
			register unsigned char *	ptr;
			int								fh;

			strcat(FilenamePtr, MidExtension);
			if ((fh = open(FilenamePtr, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1)
			{
				format_err(DidntOpenStr);
msg:			free(mem);
				goto err;
			}

			ptr = mem;
			memcpy(ptr, MthdId, sizeof(MthdId));
			ptr += sizeof(MthdId);
			memcpy(ptr, TimeSig, sizeof(TimeSig));
			ptr += sizeof(TimeSig);
			ptr[-4] = 4;
			ptr[-3] = 2;

			if (StyleType > 2)
			{
				register unsigned char *	src;
				uint32_t				measTime, evtTime, prevEvtTime;
				unsigned char		timeAdj, measLen, prevStatus;

				data -= 4;
				if (data[2] != 0x10 || data[3] || !mem[0])
				{
					ptr = (unsigned char *)CorruptStr;
					goto err2;
				}
				data -= 16;

				mem[11] = 1;

				memset(&Play, 0, sizeof(Play));

				// Look for all REPEAT events, and note where we have to
				// add LOOP START markers by saving ptrs to the first
				// entry in the loop table
				src = (&((STYLE_VARIATION *)mem)->Chains[MAX_NUM_OF_CHAINS]);
				len = 0;
				for (;;)
				{
					register unsigned short	flags;

					src += 2;
					flags = ((unsigned short)src[0] << 8) | src[1];
					src += 2;

					if (flags & SONGFLAG_REPEATEND) continue;

					if (flags & SONGFLAG_REPEAT)
					{
						unsigned short *	currentTable;

						if (len >= 24) break;
						Play.Vars.Loop.Start[len++] = src - (((unsigned int)src[0] << 8) | src[1]);

						// Change the table to little endian
						currentTable = (unsigned short *)src;
						if ((prevStatus = (unsigned char)flags)) prevStatus = ((prevStatus >> 4) + 1);
						prevStatus++;

						do
						{
							flags = (((unsigned int)src[0] << 8) | src[1]);
							*currentTable++ = flags;
							src += 2;
						} while (--prevStatus);

						continue;
					}

					if (flags & SONGFLAG_SONGEND) break;

					if (flags & SONGFLAG_VARIATION) src++;
					if (flags & SONGFLAG_STYLE) src += *src;
					if (flags & SONGFLAG_CHORD) src++;
					if (flags & SONGFLAG_TEMPO) src++;
					if (flags & SONGFLAG_PAD) src += 2;
					if (flags & SONGFLAG_TIMESIG) src += 2;
				}

				len = prevEvtTime = measTime = prevStatus = 0;
				timeAdj = 1;
				measLen = 24 * 4;
				Play.Vars.Loop.NoteTime = -1;

				src = (&((STYLE_VARIATION *)mem)->Chains[MAX_NUM_OF_CHAINS]);

				for (;;)
				{
					register unsigned short			flags;
					unsigned char *					currentTable;
					unsigned char						first;

					measTime += (*src++ - timeAdj);
					timeAdj = 0;
					evtTime = (measTime * measLen) + *src++;

					flags = ((unsigned short)src[0] << 8) | src[1];
					src += 2;

					if (Play.Vars.Loop.NoteTime <= evtTime)
					{
						ptr = longToVlq(ptr, Play.Vars.Loop.NoteTime - prevEvtTime);
						prevEvtTime = Play.Vars.Loop.NoteTime;
						ptr = insert_ntf(ptr, prevStatus);
						prevStatus = 0x90;
					}

					ptr = longToVlq(ptr, evtTime - prevEvtTime);
					first = 1;

					for (prevEvtTime=0; prevEvtTime<24; prevEvtTime++)
					{
						if (Play.Vars.Loop.Start[prevEvtTime] + 4 == src)
						{
							if (!(*ptr = first)) ptr++;
							*ptr++ = 0xFF;
							*ptr++ = 0x06;
							*ptr++ = prevStatus = first = 0;
//							Play.Vars.Loop.Start[prevEvtTime] = 0;

							if (ptr > data)
							{
								write(fh, mem, ptr - mem);
								len += (ptr - mem);
								ptr = mem;
							}
						}
					}

					if (flags & SONGFLAG_REPEATEND)
					{
						// We need to insert Ending markers. Go through the loop
						// table, and add a marker for every ending that jumps to
						// current song position
						register unsigned short		offset;
						register unsigned int		endingNum, lastEnding;

addendings:			offset = flags & ~SONGFLAG_REPEATEND;
						endingNum = 0;
						lastEnding = Play.Vars.Loop.NumEndings;
						while (endingNum++ < lastEnding)
						{
							if (Play.Vars.Loop.CurrentTable[endingNum] == offset)
							{
								if (!(*ptr = first)) ptr++;
								first = 0;
								*ptr++ = 0xFF;
								*ptr++ = 0x06;
								if (endingNum > 9)
								{
									*ptr++ = 2;
									*ptr++ = '1';
									*ptr++ = (endingNum - 10) + '0';
								}
								else
								{
									*ptr++ = 1;
									*ptr++ = endingNum + '0';
								}

								if (ptr > data)
								{
									write(fh, mem, ptr - mem);
									len += (ptr - mem);
									ptr = mem;
								}
							}
						}

						// If this is the last ending, then insert the LOOP marker
						if (Play.Vars.Loop.CurrentTable[0] == (unsigned short)(src - currentTable))
						{
							if (!(*ptr = first)) ptr++;
addloop:					*ptr++ = 0xFF;
							*ptr++ = 0x06;
							*ptr++ = 4;
							memcpy(ptr, &LoopStr[0], 4);
							ptr += 4;
						}

						prevStatus = 0;
						timeAdj = 1;
						goto nextsongevt;
					}

					if (flags & SONGFLAG_REPEAT)
					{
						// Skip over the table
						flags &= 0x00F0;
						if (flags)
						{
							currentTable = src;
							Play.Vars.Loop.NumEndings = (flags >> 4);
							flags = ((flags >> 4) + 2) * 2;
							memcpy(Play.Vars.Loop.CurrentTable, src+2, flags-2);
						}
						else
							flags = 2;
						src += flags;

						// Need to insert end markers for first ending
						if (flags > 2) goto addendings;
						if (!(*ptr = first)) ptr++;
						goto addloop;
					}

					if (flags == SONGFLAG_SONGDONE)
					{
						if (Play.Vars.Loop.NoteTime != -1)
						{
							ptr = insert_ntf(ptr, prevStatus);
							*ptr++ = 0;
						}

						*ptr++ = 0xFF;
						*ptr++ = 0x06;
						*ptr++ = 4;
						memcpy(ptr, &DoneStr[0], 4);
						ptr += 4;
						break;
					}

					if (flags & SONGFLAG_SONGEND)
					{
						if (!(*ptr = first)) ptr++;
						*ptr++ = 0xFF;
						*ptr++ = 0x06;
						*ptr++ = 3;
						memcpy(ptr, &EndStr[0], 3);
						ptr += 3;
						first = prevStatus = 0;
					}

					if (flags & SONGFLAG_VARIATION)
					{
						if (!(*ptr = first)) ptr++;
						*ptr++ = 0xFF;
						*ptr++ = 0x06;
						*ptr++ = 1;
						*ptr++ = Variations[*src++];
						first = prevStatus = 0;
					}

					if (flags & SONGFLAG_STYLE)
					{
						if (!(*ptr = first)) ptr++;
						*ptr++ = 0xFF;
						*ptr++ = 0x01;
						*ptr++ = first = *src++ - 2;
						if (ptr + first > data)
						{
							write(fh, mem, ptr - mem);
							len += (ptr - mem);
							ptr = mem;
						}

						do
						{
							if (!(*ptr++ = *src++)) ptr[-1] = '/';
						} while (--first);
						src++;

						first = prevStatus = 0;
					}

					if (flags & (SONGFLAG_FILL|SONGFLAG_CHORD|SONGFLAG_PAD))
					{
						if (Play.Vars.Loop.NoteTime != -1)
						{
							if (!(*ptr = first)) ptr++;
							ptr = insert_ntf(ptr, prevStatus);
							prevStatus = 0x90;
							first = 0;
						}

						Play.Vars.Loop.NoteTime = evtTime + 24;
						Play.Vars.Loop.NoteFlags = (unsigned char)flags;

						if (flags & SONGFLAG_FILL)
						{
							if (!(*ptr = first)) ptr++;
							if (prevStatus != 0x90) *ptr++ = prevStatus = 0x90;
							*ptr++ = 76;
							*ptr++ = 100;
							first = 0;
						}

						if (flags & SONGFLAG_PAD)
						{
							if (!(*ptr = first)) ptr++;
							if (prevStatus != 0x90) *ptr++ = prevStatus = 0x90;
							*ptr++ = Play.Vars.Loop.Pad = src[1] + 60;
							*ptr++ = src[0];
							src += 2;
							first = 0;
						}

						if (flags & SONGFLAG_CHORD)
						{
							register unsigned char 		note;

							if (!(*ptr = first)) ptr++;
							if (prevStatus != 0x90) *ptr++ = prevStatus = 0x90;
							Play.Vars.Loop.NoteSpec = first = *src++;
							if (first)
							{
								note = (first >> 4) + 51;
								if (note >= 60) note -= 12;
								*ptr++ = note;
								*ptr++ = 100;

								if (first & 0x07)
								{
									*ptr++ = 0;
									*ptr++ = SongScales[(first & 0x07)];
									*ptr++ = 100;
								}

								if (flags & SONGFLAG_ADDNINE)
								{
									*ptr++ = 0;
									*ptr++ = 47;
									*ptr++ = 100;
								}

								if (flags & SONGFLAG_ADDSEVEN)
								{
									*ptr++ = 0;
									*ptr++ = 43;
									*ptr++ = 100;
								}
							}
							else
							{
								*ptr++ = 77;
								*ptr++ = 100;
							}
						}

						first = 0;
					}

					if (flags & SONGFLAG_TEMPO)
					{
						if (!(*ptr = first)) ptr++;
						*ptr++ = 0xFF;
						*ptr++ = 0x51;
						*ptr++ = 3;
						prevEvtTime = *src++;
						prevEvtTime = 60000000/prevEvtTime;
						*ptr++ = (unsigned char)(prevEvtTime >> 16);
						*ptr++ = (unsigned char)(prevEvtTime >> 8);
						*ptr++ = (unsigned char)prevEvtTime;
						first = prevStatus = 0;
					}

					if (flags & SONGFLAG_TIMESIG)
					{
						if (!evtTime)
						{
							measLen = ((24*4)/(0x0001 << src[1])) * (uint32_t)src[0];
							mem[sizeof(MthdId) + sizeof(TimeSig) +- 4] = *src++;
							mem[sizeof(MthdId) + sizeof(TimeSig) + - 3] = *src++;
						}
					}

nextsongevt:	if (ptr >= data)
					{
						write(fh, mem, ptr - mem);
						len += (ptr - mem);
						ptr = mem;
					}

					prevEvtTime = evtTime;
				}

				len -= (8+6+8);
				goto donesong;
			}
//==============================================
			{
			register STYLE_VARIATION *	ptn;

			{
			unsigned char	sig;

			ptn = (STYLE_VARIATION *)(&((STYLE_VARIATION *)mem)->Chains[MAX_NUM_OF_CHAINS]);
			data = &ptn->Chains[ptn->NumChains - 1];
			data += *data - 1;

			sig = *data;

			if (!(sig % 24))
				ptr[-4] = sig / 24;
			else if (!(sig % 12))
			{
				ptr[-4] = sig / 12;
				ptr[-3] = 3;
			}
			else if (!(sig % 6))
			{
				ptr[-4] = sig / 6;
				ptr[-3] = 4;
			}
			else
			{
				ptr[-4] = sig / 3;
				ptr[-3] = 5;
			}
//printf("%u %u/%u\r\n", sig, ptr[-4], ptr[-3]);
			}

			{
			uint32_t			prevTime, measTime;
			unsigned char	trackNum, loopCnt, repeatmark, lastNoteNum, marker, evtStatus;

			marker = trackNum = 0;
			data = &ptn->NumChains;

			repeatmark = (data[*data] & 0x80) ? *data + data[*data] : 0xff;
			loopCnt = *data++ - 1;

			// Check whether half measure flag evt needed
			if (StyleType && (ptn->u.Flags & VARFLAG_HAS_HALFMEAS))
			{
				marker++;

				// If half measure isn't included in the chain, we need to
				// write it out before the chain measures
				if (!(*data)) marker++;
				else loopCnt++;
			}

			// Insert gtr/bass articulation pgm change event, if any
			if (StyleType && (ptn->u.Flags & 0x0f))
			{
				*ptr++ = 0;
				*ptr++ = 0xc0 | (StyleType == 2 ? 1 : 0);
				*ptr++ = ptn->u.Flags & 0x0f;
			}

again:
			// Insert the Muse port events
			{
			register const unsigned char *	portname;

			switch (StyleType)
			{
				case 2:
					portname = &MtrkGtr[0];
					measTime = sizeof(MtrkGtr);
					evtStatus = 0x91;
					break;
				case 1:
					portname = &MtrkBass[0];
					measTime = sizeof(MtrkBass);
					evtStatus = 0x90;
					break;
				default:
					portname = &MtrkDrum[0];
					measTime = sizeof(MtrkDrum);
					evtStatus = 0x99;
			}

			memcpy(ptr, MtrkPort, sizeof(MtrkPort));
			ptr[3] += measTime;
			memcpy(ptr + sizeof(MtrkPort), portname, measTime);
			portname = ptr;
			ptr += sizeof(MtrkPort) + measTime;
			*ptr++ = '-';
			*ptr++ = '0';
			measTime = (ptr - portname);
			memcpy(ptr, portname, measTime);
			ptr[2] = 4;
			ptr += measTime;
			}

			// Flush mtrk header
			len = ptr - (mem + 8 + (!trackNum ? 6 + 8 : 0));
			write(fh, mem, ptr - mem);
			ptr = mem;

			// =============================
			prevTime = measTime = lastNoteNum = 0;
			memset(&Play, 0, sizeof(Play));

			while (loopCnt--)
			{
				register unsigned char *	evt;
				unsigned char					cnt, fill;

				// Flush cache if necessary
				if (ptr >= (unsigned char *)ptn - 10)
				{
					write(fh, mem, ptr - mem);
					len += (ptr - mem);
					ptr = mem;
				}

				if (!trackNum)
				{
					// Get next chain's measure ptn
					evt = &ptn->Chains[ptn->NumChains - 1];
					if (!(marker & 0x01))
					{
						fill = *data++;
						cnt = fill & 0x7F;
						while (cnt--) evt += *evt;
					}
					if (StyleType) fill = (!repeatmark-- || marker) ? 0x80 : 0;
				}
				else
				{
					fill = (StyleType || (!loopCnt && !(ptn->u.Flags & VARFLAG_DEFAULT_END))) ? 0x80 : 0;
					evt = data;
				}

				// Insert a note-on for a flag event? (ie, fill/end marker, repeat marker, half meas marker)
				if (fill & 0x80)
				{
					if (marker) marker--;
					ptr = longToVlq(ptr, measTime - prevTime);
					prevTime = measTime;
					if (evtStatus)
					{
						*ptr++ = evtStatus;
						evtStatus = 0;
					}
					*ptr++ = marker;
					*ptr++ = 64;
				}

				// Insert the note on/off for all events in the measure
				cnt = *evt++ - 1;

next:			while (cnt > 1)
				{
					unsigned char		evtTime;

					evtTime = *evt++;

					switch (StyleType)
					{
						case 2:
						{
							unsigned char		status;

							status = *evt++;

							// Scale Step?
							if (status & GTREVTFLAG_SCALESTEP)
							{
								if ((status & GTREVTFLAG_SCALESTEPMASK) > 6)
								{
badstep:							ptr = (unsigned char *)BadStepStr;
									goto err2;
								}

								lastNoteNum = 60 + Maj[(status & GTREVTFLAG_SCALESTEPMASK)] + (((status >> 3) & 0x03) * 12);
								if (status & GTREVTFLAG_PITCHUP) lastNoteNum++;

								// ntf ?
								if (status & GTREVTFLAG_STEPOFFFLAG)
								{
noteoff:							if (remFromHistory(lastNoteNum))
									{
										ptr = longToVlq(ptr, (measTime + evtTime) - prevTime);
										prevTime = measTime + evtTime;
										if (evtStatus)
										{
											*ptr++ = evtStatus;
											evtStatus = 0;
										}
										*ptr++ = lastNoteNum;
										*ptr++ = 0;
									}
									goto dec2;
								}
							}

							// String #
							else
							{
								// Turn off all playing strings?
								if ((status & GTREVTFLAG_STRINGOFFMASK) == GTREVTFLAG_STRINGOFF && !(status & GTREVTFLAG_STRINGNUMMASK))
								{
									register unsigned char *	history;
									register unsigned char 		first;

									history = &Play.Vars.Playing.Keys[0];
									first = 1;
									do
									{
										if (*history)
										{
											if (!first)
												*ptr++ = 0;
											else
											{
												ptr = longToVlq(ptr, (measTime + evtTime) - prevTime);
												prevTime = measTime + evtTime;
												if (evtStatus)
												{
													*ptr++ = evtStatus;
													evtStatus = 0;
												}
											}

											*ptr++ = *history;
											*history = *ptr++ = first = 0;

											if (ptr >= (unsigned char *)ptn - 10)
											{
												write(fh, mem, ptr - mem);
												len += (ptr - mem);
												ptr = mem;
											}
										}
									} while (++history < &Play.Vars.Playing.Keys[MIDIKEY_HISTORY]);

									goto dec2;
								}

								{
								register unsigned char	stringNum;

								stringNum = status & GTREVTFLAG_STRINGNUMMASK;
								if (!(lastNoteNum = Chords[stringNum]))
								{
									ptr = (unsigned char *)BadChordStr;
err2:								close(fh);
									unlink(FilenamePtr);
									format_err((char *)ptr);
									goto msg;
								}

								if (stringNum > 6) stringNum = 0;
								if ((status & GTREVTFLAG_STRINGOFFMASK) == GTREVTFLAG_STRINGOFF)
								{
									lastNoteNum = StringNums[stringNum];
									goto noteoff;
								}

								if (status & GTREVTFLAG_PITCHDOWN) lastNoteNum -= 12;
								if (status & GTREVTFLAG_PITCHUP) lastNoteNum -= 24;
								StringNums[stringNum] = lastNoteNum;
								}
							}

							ptr = longToVlq(ptr, (measTime + evtTime) - prevTime);
							prevTime = measTime + evtTime;
							if (evtStatus)
							{
								*ptr++ = evtStatus;
								evtStatus = 0;
							}

							// Insert a note-off if this note # is still on
							if (addToHistory(prevTime, lastNoteNum) != 0xFFFFFFFF)
							{
								remFromHistory(lastNoteNum);
								*ptr++ = lastNoteNum;
								*ptr++ = 0;
								*ptr++ = 0;
							}

store:					*ptr++ = lastNoteNum;
							if ((*ptr++ = *evt) > 127)
							{
								ptr = (unsigned char *)BadVelStr;
								goto err2;
							}

							evt++;

							// One less evt to insert
							cnt -= 3;
							break;
						}

						case 1:
						{
							unsigned char		status;

							status = *evt++;
							if (status & BASSEVTFLAG_NOT_ON)
							{
								if (status == BASSEVTFLAG_NOTEOFF && lastNoteNum)
								{
									ptr = longToVlq(ptr, (measTime + evtTime) - prevTime);
									prevTime = measTime + evtTime;
									if (evtStatus)
									{
										*ptr++ = evtStatus;
										evtStatus = 0;
									}
									*ptr++ = lastNoteNum;
									*ptr++ = lastNoteNum = 0;
								}

dec2:							cnt -= 2;
								break;
							}

							// ============== Note-on =================

							ptr = longToVlq(ptr, (measTime + evtTime) - prevTime);
							prevTime = measTime + evtTime;
							if (evtStatus)
							{
								*ptr++ = evtStatus;
								evtStatus = 0;
							}

							// Insert a note-off for the previous note-on, but only if
							// we didn't already do so
							if (lastNoteNum)
							{
								*ptr++ = lastNoteNum;
								*ptr++ = 0;
								*ptr++ = 0;
							}

							if ((status & BASSEVTFLAG_SCALESTEPMASK) > 6) goto badstep;
							lastNoteNum = 48 + Maj[(status & BASSEVTFLAG_SCALESTEPMASK)];
							if (status & BASSEVTFLAG_OCTDOWN) lastNoteNum -= 12;
							if (status & BASSEVTFLAG_OCTUP) lastNoteNum += 12;
							if (status & GTREVTFLAG_PITCHDOWN) lastNoteNum--;
							if (status & GTREVTFLAG_PITCHUP) lastNoteNum++;
							goto store;
						}

						default:
						{
							unsigned char *	evt2;

							// Insert timing bytes
							ptr = longToVlq(ptr, (measTime + evtTime) - prevTime);
							prevTime = measTime + evtTime;

							lastNoteNum = 0;
							if (evtStatus)
							{
								lastNoteNum = 1;
								*ptr++ = evtStatus;
								evtStatus = 0;
							}

							evt2 = evt;

							for (;;)
							{
								cnt -= 3;

								// Insert note #
								if ((*ptr++ = *evt++) > 127)
								{
									ptr = (unsigned char *)BadNoteStr;
									goto err2;
								}

								// if the "roll flag" is set, insert an AFT before the note-on
								if (*evt > 127)
								{
									if (lastNoteNum)
									{
										lastNoteNum = 0;
										ptr[-2] = 0xA9;
									}
									else
									{
										ptr[-1] = 0xA9;
										*ptr++ = evt[-1];
									}
									*ptr++ = 0x01;
									*ptr++ = 0x00;
									*ptr++ = 0x99;
									*ptr++ = evt[-1];
								}
								*ptr++ = *evt++ & 0x7f;

								// flush cache if necessary
								if (ptr >= (unsigned char *)ptn - 10)
								{
									write(fh, mem, ptr - mem);
									len += (ptr - mem);
									ptr = mem;
								}

								// If the next event is on this same time, keep looping
								if (evtTime != evt[0])
								{
									// Insert note-offs for the above note-ons. Put them at a time
									// one less than the next event
									evtTime = evt[0] - 1;
									ptr = longToVlq(ptr, (measTime + evtTime) - prevTime);
									prevTime = measTime + evtTime;

									for (;;)
									{
										*ptr++ = *evt2++;
										*ptr++ = 0;
										if (++evt2 >= evt) break;

										if (ptr >= (unsigned char *)ptn - 10)
										{
											write(fh, mem, ptr - mem);
											len += (ptr - mem);
											ptr = mem;
										}

										*ptr++ = 0;
										evt2++;
									}

									// Proceed to the next beat
									goto next;
								}

								// Insert the next note-on on the same time
								*ptr++ = 0;
								evt++;
							}
						}
					}

					if (ptr >= (unsigned char *)ptn - 10)
					{
						write(fh, mem, ptr - mem);
						len += (ptr - mem);
						ptr = mem;
					}
				}
				if (trackNum) data = evt + 1;

				// Insert the note-off for the flag event
				if (fill & 0x80)
				{
					unsigned char		evtTime;

					evtTime = evt[0] - 1;
					ptr = longToVlq(ptr, (measTime + evtTime) - prevTime);
					prevTime = measTime + evtTime;
					if (evtStatus)
					{
						*ptr++ = evtStatus;
						evtStatus = 0;
					}
					*ptr++ = marker;
					*ptr++ = marker = 0;
				}

				measTime += evt[0];
			}

			if (ptr >= (unsigned char *)ptn - 16)
			{
				write(fh, mem, ptr - mem);
				len += (ptr - mem);
				ptr = mem;
			}

			if (trackNum && (ptn->u.Flags & VARFLAG_DEFAULT_END))
			{
				ptr = longToVlq(ptr, measTime - prevTime);
				if (evtStatus) *ptr++ = evtStatus;
				*ptr++ = 0;
				*ptr++ = 64;
				*ptr++ = 3;
				*ptr++ = 0;
				*ptr++ = 0;
			}

			// =============================
			// Write EOT
donesong:
			memcpy(ptr, MtrkEOT, sizeof(MtrkEOT));
			ptr += sizeof(MtrkEOT);
			len += (ptr - mem);
			write(fh, mem, ptr - mem);

			// Adjust chunk size
			mem[0] = (unsigned char)((len >> 24) & 0xFF);
			mem[1] = (unsigned char)((len >> 16) & 0xFF);
			mem[2] = (unsigned char)((len >> 8) & 0xFF);
			mem[3] = (unsigned char)(len & 0xFF);
			lseek(fh, -((off_t)len + 4), SEEK_CUR);
			write(fh, mem, 4);

			// Do fill/ending mtrk
			if (StyleType <= 2 && ++trackNum < 2)
			{
				unsigned char	cnt;

				lseek(fh, 0, SEEK_END);
				memcpy(mem, &MthdId[14], 8);
				ptr = mem + 8;

				data = &ptn->Chains[ptn->NumChains - 1];
				loopCnt = ((ptn->u.Flags & VARFLAG_DEFAULT_END) ? 0 : 1);
				if (!StyleType)
				{
					cnt = ptn->NumPtns - (ptn->u.NumFills & 0x7F) - loopCnt;
					loopCnt += (ptn->u.NumFills & 0x7F);
					goto queue;
				}
				if (loopCnt)
				{
					cnt = ptn->NumPtns - 1;
queue:			while (cnt--) data += *data;
				}

				repeatmark = 0xff;
				goto again;
			}
			}
			}

			close(fh);
			free(mem);
		}
}


static GUILIST * drawMidiItems(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app)
	{
		register DIR *				dirp;
		register struct dirent *dirEntryPtr;
		register unsigned int	len;
		uint32_t						offset;

		// Open the dir for searching
		len = get_style_path(&fn[0]);
		fn[len] = '.';
		fn[len+1] = 0;
		if ((dirp = opendir(&fn[0])))
		{
			register const char *	str;

			offset = List.CurrItemNum;

			// Get next file/dir in the dir
			while ((dirEntryPtr = readdir(dirp)))
			{
				uint32_t		len2;

				// Check that name doesn't end in .mid
				str = dirEntryPtr->d_name;
				len2 = strlen(str);
				if (str[0] != '.' && (len2 < sizeof(MidExtension)-1 || memcmp(&str[len2 - (sizeof(MidExtension)-1)], &MidExtension[0], sizeof(MidExtension)-1)))
				{
					if (area)
					{
						if (GuiListDrawItem(app, ctl, area, str))
							break;
					}
					else if (!offset--)
					{
						strcpy(&FilenamePtr[len], str);
						break;
					}
				}
			}

			closedir(dirp);
		}
	}

	return &List;
}

static void setMidiItemWidth(void)
{
	register DIR *				dirp;
	register struct dirent *dirEntryPtr;
	register unsigned int	len;

	// Open the dir for searching
	len = get_style_path(&fn[0]);
	fn[len] = '.';
	fn[len+1] = 0;
	if ((dirp = opendir(&fn[0])))
	{
		// Get next file/dir in the dir
		while ((dirEntryPtr = readdir(dirp)))
		{
			register const char *	str;

			// Check that name doesn't end in .mid
			str = dirEntryPtr->d_name;
			len = strlen(str);
			if (str[0] != '.' && (len < sizeof(MidExtension)-1 || memcmp(&str[len - (sizeof(MidExtension)-1)], &MidExtension[0], sizeof(MidExtension)-1)))
				GuiListItemWidth(GuiApp, MainWin, &List, str);
		}

		closedir(dirp);
	}
}

static void midi_mouse(register GUIMSG * msg)
{
	register GUICTL *		ctl;

	if ((ctl = msg->Mouse.SelectedCtl))
	{
		if (ctl->Flags.Global & CTLGLOBAL_PRESET)
		{
			if (ctl->PresetId == GUIBTN_OK) goto good;
			goto cancel;
		}

		if (ctl->Type == CTLTYPE_AREA)
		{
			switch (msg->Mouse.ListAction)
			{
				case GUILIST_SELECTION:
				{
good:				drawMidiItems(GuiApp, &ListGuiCtls[0], 0);

					// Convert BackupBand variation to MIDI
					variationToMidi();
				}
				// Fall through

				case GUILIST_ABORT:
cancel:			doConvertScreen();
			}
		}
	}
}

/*********************** makeMidi() ***********************
 * Called when user clicks one of the "Make MIDI" buttons.
 *
 * Converts a style variation to a standard MIDI file.
 */

static void makeMidi(void)
{
	MouseFunc = midi_mouse;
	FilenamePtr = &fn[0];
	doPickItemDlg2(drawMidiItems);
	setMidiItemWidth();
}









//======================================================
// CONVERT SCREEN
//======================================================

// Convert screen ctls
#define CTLID_DRUMSTYLE		0
#define CTLID_BASSSTYLE		1
#define CTLID_GTRSTYLE		2
#define CTLID_SONGSTYLE		3
#define CTLID_CUTTIME		4
#define CTLID_DRUMMIDI		6
#define CTLID_BASSMIDI		7
#define CTLID_GTRMIDI		8
#define CTLID_SONGMIDI		9

static const char		Drum2Str[] = "Drum";
static const char		Bass2Str[] = "Bass";
static const char		Gtr2Str[] = "Guitar";
static const char		SongStr[] = "Song";

static GUICTL		ConvertCtls[] = {
 	{.Label=Drum2Str,			.Y=1, .Width=10, .Attrib.NumOfLabels=1, .Type=CTLTYPE_PUSH,		 .Flags.Global=CTLGLOBAL_GROUPSTART},
 	{.Label=Bass2Str,			.Y=1, .Width=10, .Attrib.NumOfLabels=1, .Type=CTLTYPE_PUSH,		 .Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Label=Gtr2Str,			.Y=1, .Width=10, .Attrib.NumOfLabels=1, .Type=CTLTYPE_PUSH,		 .Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Label=SongStr,			.Y=1, .Width=10, .Attrib.NumOfLabels=1, .Type=CTLTYPE_PUSH,		 .Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Label="Cut time\0Use tempo",	.Y=2, .Attrib.NumOfLabels=2, .Attrib.Value=0x02, .Type=CTLTYPE_CHECK,		 .Flags.Global=CTLGLOBAL_AUTO_VAL},
 	{.Label="Make pattern",			.Type=CTLTYPE_GROUPBOX},

 	{.Label=Drum2Str,			.Y=4, .Width=10, .Attrib.NumOfLabels=1, .Type=CTLTYPE_PUSH,		 .Flags.Global=CTLGLOBAL_GROUPSTART},
 	{.Label=Bass2Str,			.Y=4, .Width=10, .Attrib.NumOfLabels=1, .Type=CTLTYPE_PUSH,		 .Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Label=Gtr2Str,			.Y=4, .Width=10, .Attrib.NumOfLabels=1, .Type=CTLTYPE_PUSH,		 .Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Label=SongStr,			.Y=4, .Width=10, .Attrib.NumOfLabels=1, .Type=CTLTYPE_PUSH,		 .Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Label="Make MIDI",	.Type=CTLTYPE_GROUPBOX},
	{.Type=CTLTYPE_END},
};






static unsigned char isCutTime(void)
{
	return ConvertCtls[CTLID_CUTTIME].Attrib.Value & 0x01;
}
static unsigned char isUseTempo(void)
{
	return ConvertCtls[CTLID_CUTTIME].Attrib.Value & 0x02;
}





/*
static void set_color_black(void)
{
	GuiTextSetColor(MainWin, GUICOLOR_BLACK);

}

static void set_color_red(void)
{
	GuiTextSetColor(MainWin, GUICOLOR_RED);
}
*/



/********************* clearMainWindow() ********************
 * Invalidates area of main window, thereby causing a Expose
 * message to be sent to it.
 */

static void clearMainWindow(void)
{
	GuiWinUpdate(GuiApp, MainWin);
}




static void show_msgbox(register const char * msg)
{
	if (MouseFunc)
		GuiLoop = (unsigned char)GuiErrShow(GuiApp, msg, GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
	else
		printf("%s\r\n", msg);
}





/********************* handle_mouse() **********************
 * Called by GUI thread when user operates a ctl in the Convert
 * screen.
 */

static void handle_mouse(register GUIMSG * msg)
{
	register GUICTL *	ctl;

	if ((ctl = msg->Mouse.SelectedCtl))
	{
		register unsigned int	id;

		id = (unsigned int)(((char *)ctl - (char *)&ConvertCtls[0]) / sizeof(GUICTL));

		if (id == CTLID_CUTTIME)
		{
		}
		else if (id <= CTLID_SONGSTYLE)
		{
			StyleType = (unsigned char)id;
			makeStyle();
		}
		else if (id <= CTLID_SONGMIDI)
		{
			StyleType = (unsigned char)(id - CTLID_DRUMMIDI);
			makeMidi();
		}
	}
}





/********************* doConvertScreen() ***********************
 * Shows/operates the "Convert" screen.
 */

static void doConvertScreen(void)
{
	MouseFunc = handle_mouse;
	MainWin->Flags = GUIWIN_TAB_KEY|GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY;
	MainWin->LowerPresetBtns = 0;
	MainWin->Ctls = &ConvertCtls[0];
	clearMainWindow();
}





/*************** positionConvertGui() ****************
 * Sets initial position/scaling of GUI ctls based
 * upon font size.
 */

static void positionConvertGui(void)
{
	GuiCtlScale(GuiApp, MainWin, &ConvertCtls[0], -1);
}




/**************** do_msg_loop() ****************
 * Does an XWindows GUI loop.
 */

static void do_msg_loop(void)
{
	register GUIMSG *		msg;

	GuiLoop = GUIBTN_RETRY;

	msg = GuiWinGetMsg(GuiApp);

	switch (msg->Type)
	{
		case GUI_MOUSE_CLICK:
		{
			if (!msg->Mouse.Direction && msg->Mouse.ButtonNum == 1)
				MouseFunc(msg);
			break;
		}

		case GUI_WINDOW_CLOSE:
		{
			GuiLoop = GUIBTN_QUIT;
			break;
		}
	}
}





/********************* main() *********************
 * Program Entry point
 */

int main(int argc, char *argv[])
{
	if (!(GuiApp = (GUIAPPHANDLE)GuiInit(0)))
		show_msgbox("Can't start XWindows");
	else
	{
		register GUIINIT *	init;

		MainWin = &GuiApp->MainWin;

		// Create the app window
		init = GuiAppState(GuiApp, GUISTATE_GET_INIT, 0);
//		init->Parent = 0;
		if (GuiWinState(GuiApp, MainWin, GUISTATE_CREATE|GUISTATE_LINK|GUISTATE_OPEN))
		{
			// Scale GUI ctls
			positionConvertGui();
			positionPickItemGui();

			init->Height = MainWin->MinHeight;
			init->Width = MainWin->MinWidth;

			init->Title = &WindowTitle[0];
			GuiWinState(GuiApp, 0, GUISTATE_SHOW|GUISTATE_TITLE|GUISTATE_SIZE);

			doConvertScreen();

			do
			{
				do_msg_loop();
			} while (GuiLoop != GUIBTN_QUIT);
		}

		GuiDone(GuiApp, 0);
	}

	return 0;
}





// ========================= For list enumeration/display =======================

/******************** doPickItemDlg2() ******************
 * Shows/operates the "Pick Item..." screen. Allows
 * user to pick an Item from a list.
 *
 * Caller must set List.ColumnWidth and List.NumOfItems,
 * usually by calling GuiListItemWidth().
 */

static void doPickItemDlg2(register GUILISTFUNC * drawfunc)
{
	// Initially no Item selected
	GuiListItemWidth(GuiApp, MainWin, &List, 0);

	ListGuiCtls[0].ListDraw = drawfunc;

	MainWin->Ctls = &ListGuiCtls[0];
	MainWin->Menu = 0;
	MainWin->LowerPresetBtns = GUIBTN_OK_SHOW|GUIBTN_CANCEL_SHOW|GUIBTN_CENTER;
	MainWin->Flags = GUIWIN_LIST_KEY|GUIWIN_TAB_KEY|GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY;

	clearMainWindow();
	GuiCtlSetSelect(GuiApp, 0, 0, (GUICTL *)GUIBTN_OK);
}

/*************** positionPickItemGui() ****************
 * Sets initial position/scaling of GUI ctls based
 * upon font size.
 */

static void positionPickItemGui(void)
{
	GuiCtlScale(GuiApp, MainWin, &ListGuiCtls[0], -1);
}
