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
#include "AudioPlay.h"
#include "AccompSeq.h"
#include "StyleData.h"
#include "FileLoad.h"
#include "SongSheet.h"

/*
Each event in a song sheet:

	unsigned char		Measure;
	unsigned char		Beat;
	unsigned short		Flags;

Measure is the measure offset from the previous event.

Beat is the PPQN tick upon which the event occurs, where 0 is the measure start.

Flags is as follows:

If CHORD flag set, next byte is the chord. Top 4 bits is root note where 1 is E, and
12 is D#. Low 3 bits is Scale index 0 to 7. NOTEADD_SEVENTH and NOTEADD_NINETH
gotten from flags. A 0 indicates no playing chord.

If STYLE flag set, next void * is the Style. On disk, it's the nul-terminated
category name, followed by the style name.

If VARIATION flag set, next byte is the variation number.

If GUITAR flag set, next byte is the guitar patch number. If bit #7 set, then the
bank number.

If BASS flag set, next byte is the bass patch number. If bit #7 set, then the
bank number.

If DRUMS flag set, next byte is the kit program number.

If TEMPO flag set, next byte is the bpm.

If PAD flag set, next byte is the pad number, where 0 = none.

If REPEAT set, the remaining flag bits are as so:

  13 12  11 10  9  8    | 7  6  5  4    |  3  2  1  0
									Total # of		Current loop
									loops, where		count
									0=infinite

After the byte offset is a table of unsigned short offsets, referenced from
the table head. The first short is an offset to the repeat start. The second
short is an offset to the repeat end. The next short is an offset to the
_last_ repeat ending. Each ending should conclude with a REPEATEND event where
remaining flag bits 0 to 13 are a byte offset back to the REPEAT table.

A SONGEND event marks where the song's ending begins.

A SONGDONE event marks the end of data.
 */

#ifndef NO_SONGSHEET_SUPPORT

#pragma pack(1)
typedef struct _SONGSHEET {
	struct _SONGSHEET *		Next;
	unsigned char				Data[1];
} SONGSHEET;
#pragma pack()




// A linked list of all songsheets
static SONGSHEET *		SongSheetList = 0;

// Song sheet being played, or 0 if none
static SONGSHEET *		CurrentSongSheet = 0;

// Current event being played
static unsigned char *	SongEventPtr;

// Last event in song
static unsigned char *	SongEndEventPtr;

// For playback
static unsigned char		CurrentMeas;





// =============================================
// Play a Song Sheet
// =============================================

/***************** freeSongSheets() ******************
 * Frees all SONGSHEETs.
 */

void freeSongSheets(void)
{
	register SONGSHEET *	songPtr;

	while ((songPtr = SongSheetList))
	{
		SongSheetList = songPtr->Next;
		free(songPtr);
	}
}





/*************** songQueueEndEvent() ****************
 * Finds the ending measures, and set "ActionFlags" for
 * the current playing song (a locking mechanism to prevent
 * race conditions between threads).
 */

static unsigned char * songQueueEndEvent(unsigned char * variationFlag)
{
	register unsigned char *	ptr;

	ptr = SongEventPtr;
	SongEndEventPtr = 0;
	*variationFlag = 0;
	for (;;)
	{
		register unsigned short	flags;

		ptr += 2;
		flags = ((unsigned short)ptr[0] << 8) | ptr[1];
		ptr += 2;

		if (flags & SONGFLAG_REPEATEND) continue;

		if (flags & SONGFLAG_REPEAT)
		{
			// Reinit repeat count
			flags &= 0x00F0;
			ptr[-1] = (unsigned char)flags;

			// Skip over the table
			if (flags)
				flags = ((flags >> 4) + 2) * 2;
			else
				flags = 2;

			ptr += flags;
			continue;
		}

		if (flags & SONGFLAG_SONGEND)
			SongEndEventPtr = ptr - 4;

		if (flags & SONGFLAG_SONGDONE)
		{
			if (!SongEndEventPtr) SongEndEventPtr = ptr - 4;
			return ptr;
		}

		if (flags & SONGFLAG_VARIATION)
		{
			*variationFlag = 1;
			ptr++;
		}
		if (flags & SONGFLAG_STYLE) ptr += *ptr;
		if (flags & SONGFLAG_PAD) ptr += 2;
		if (flags & SONGFLAG_CHORD) ptr++;
		if (flags & SONGFLAG_TEMPO) ptr++;
		if (flags & SONGFLAG_TIMESIG) ptr += 2;
	}
}





/****************** songStart() ***************
 * Called by Beat Play thread (or main/midi thread when
 * user selecting a song while stopped) to reset the song
 * sheet to the beginning.
 */

uint32_t songStart(void)
{
	register uint32_t	refresh;

	SongEventPtr = 0;
	refresh = 0;
	if (CurrentSongSheet)
	{
		unsigned char		variationFlag;

		// Skip the name[] and other bookkeeping to get to the song data
		SongEventPtr = &CurrentSongSheet->Data[1] + strlen((char *)&CurrentSongSheet->Data[0]);

		// Locate the ending seq so we have it ready to play at any given moment
		songQueueEndEvent(&variationFlag);

		// "Play" the non-note events at time 0
		CurrentMeas = 1;
		refresh = nextSongBeat(0);

		// If the song doesn't set the variation, default to Verse
		if (!variationFlag)
		{
			refresh |= selectStyleVariation(0, BEATTHREADID);
			PlayFlags |= PLAYFLAG_FILLPLAY;	// skip autofill
		}
	}

	return refresh;
}




void * isSongsheetActive(void)
{
	return CurrentSongSheet;
}




/****************** nextSongBeat() ***************
 * Called by Beat Play thread to play the song
 * sheet events at the current time.
 *
 * RETURN: Mask of GUICTLs to refresh. CTLMASK_NONE
 * if none.
 */


uint32_t nextSongBeat(register unsigned char ppqn)
{
	register uint32_t	refresh;

	refresh = CTLMASK_NONE;

	if (SongEventPtr)
	{
		register unsigned char	measure;

again:
		measure = CurrentMeas;

		// Just hit the downbeat of the next measure?
		if (ppqn == 0xff)
		{
 			CurrentMeas++;

			// If user wants to stop, queue the end variation
			if ((PlayFlags & PLAYFLAG_STOP) && getCurrVariationNum(VARIATION_INPLAY) != 4)
			{
				SongEventPtr = SongEndEventPtr;
				ppqn = 0;
				measure = CurrentMeas = 1;
			}
		}

		while (SongEventPtr[0] <= measure && SongEventPtr[1] <= ppqn)
		{
			register unsigned short	flags;

			CurrentMeas = measure = 0;
			SongEventPtr += 2;
			flags = ((unsigned short)SongEventPtr[0] << 8) | SongEventPtr[1];
			SongEventPtr += 2;

			// Finished a repeat ending?
			if (flags & SONGFLAG_REPEATEND)
			{
				register unsigned char *	repeatEvt;

				// Get the REPEAT event's table head, and dec its loop count (low byte of Flags)
				SongEventPtr -= (flags & ~SONGFLAG_REPEATEND);
				repeatEvt = SongEventPtr - 1;

				// Dec the loop count
				--repeatEvt[0];

				// If 0, jump to repeat end
				if (!(repeatEvt[0] & 0x000F))
				{
					flags = 2;
					goto reset;
				}

				// Jump to repeat start
				flags = SONGFLAG_REPEAT;
			}

			// Start the next repeat ending?
			if (flags & SONGFLAG_REPEAT)
			{
				register int				offset;
				register unsigned char	count;

				// Assume infinite loop, which immediately jumps back to repeat start
				offset = (int)(*((unsigned short *)SongEventPtr));

				// Not infinite loop?
				if ((count = ((flags >> 4) & 0x000F)))
				{
					// Are we just starting this repeat? If so, init count
					if (!(flags & 0x000F))
					{
						flags |= count;
						SongEventPtr[-1] = (unsigned char)flags;
					}

					// Jump to the next repeat ending
					flags = ((flags & 0x000F) + 1) * sizeof(short);
reset:			offset = -((int)(*((unsigned short *)(SongEventPtr+flags))));
				}

				SongEventPtr -= offset;
				ppqn = 0;
				CurrentMeas = measure = 1;
				continue;
			}

			if (flags & SONGFLAG_SONGDONE)
			{
				SongEventPtr -= 4;
				PlayFlags |= PLAYFLAG_STOP;
				break;
			}

			if (flags & SONGFLAG_SONGEND)
				PlayFlags |= PLAYFLAG_STOP;

			if (flags & SONGFLAG_VARIATION)
			{
				register unsigned char	varnum;

				varnum = *SongEventPtr++;
				if (varnum != getCurrVariationNum(VARIATION_USER) && selectStyleVariation(varnum, BEATTHREADID))
				{
					PlayFlags |= PLAYFLAG_FILLPLAY;	// skip autofill
					refresh |= CTLMASK_VARIATION;
				}
			}

			// Set the style after the variation above so it picks up the latter
			if (flags & SONGFLAG_STYLE)
			{
				refresh |= set_song_style(*((void **)(SongEventPtr+1)));
				SongEventPtr += *SongEventPtr;
			}

			if (flags & SONGFLAG_PAD)
			{
				register unsigned char	data;

				if ((data = *SongEventPtr++)) ChordVel = data;
				if ((data = *SongEventPtr++) != getPadPgmNum())
				{
					refresh |= changePadInstrument(data | BEATTHREADID);
					if (data) ++ChordPlayCnt;
				}
			}

			if (flags & SONGFLAG_CHORD)
			{
				if (!SongEventPtr[0])
					BeatInPlay |= (INPLAY_USERMUTEBASS|INPLAY_USERMUTEGTR);
				else
					songChordChange(SongEventPtr[0], (unsigned char)(flags & (SONGFLAG_ADDNINE|SONGFLAG_ADDSEVEN)));
				++SongEventPtr;
			}

			if (flags & SONGFLAG_TEMPO)
			{
				register unsigned char	bpm;

				if (lockTempo(BEATTHREADID))
				{
					bpm = *SongEventPtr++;
					if (bpm <= 10) bpm = getStyleDefTempo();
					if (bpm != set_bpm(0))
					{
						set_bpm(bpm);
						refresh |= CTLMASK_TEMPO;
					}
				}

				unlockTempo(BEATTHREADID);
			}

			if (flags & SONGFLAG_TIMESIG) SongEventPtr += 2;

			if (flags & SONGFLAG_FILL) PlayFlags |= PLAYFLAG_FILLPLAY;
		}

		if (ppqn == 0xff)
		{
			ppqn = 0;
			goto again;
		}
	}

	return refresh;
}








// =============================================
// Load/save a Song Sheet from disk
// =============================================

/****************** loadSongSheet() ***************
 * A secondary thread that loads the specified song sheet.
 *
 * fn =			File's nul-terminated full pathname.
 * NamePtr =	Ptr to the filename.
 *
 * If an error, copies a msg to TempBuffer[].
 */

void loadSongSheet(const char * fn)
{
	register SONGSHEET *		mem;
	register uint32_t			len;
	uint32_t						nameLen;

	CurrentSongSheet = mem = 0;

	nameLen = strlen(NamePtr);
	if ((len = load_text_file(fn, 3|FILEFLAG_NO_NUL)))
	{
		// Check for min size
		if (len < 4)
corrupt:	format_text_err("songsheet is invalid", 0xFFFFFFFF, 0);
		else
		{
			// Allocate a SONGSHEET to store the whole (binary) file
			if (!(mem = (SONGSHEET *)malloc(sizeof(SONGSHEET) + len + nameLen)))
memerr:		setMemErrorStr();
			else
			{
				memcpy(&mem->Data[nameLen + 1], TempBuffer, len);
				memcpy((char *)&mem->Data[0], NamePtr, nameLen + 1);

				// Check integrity and resolve style references
				{
				register unsigned char *		ptr;
				register unsigned char *		end;
				register unsigned short			flags;

				nameLen += 1;
				ptr = &mem->Data[nameLen + len - 2];
				if (ptr[0] != 0x10 || ptr[1] || !mem->Data[nameLen]) goto corrupt;
				ptr = &mem->Data[nameLen];
				end = ptr + len;
				for (;;)
				{
					ptr += 2;
					if (ptr + 2 > end) goto corrupt;
					flags = ((unsigned short)ptr[0] << 8) | ptr[1];
					ptr += 2;

					if (flags & SONGFLAG_REPEATEND) continue;

					if (flags & SONGFLAG_REPEAT)
					{
						register unsigned char	count;

						count = (unsigned char)(flags >> 4);
						if (ptr + (count ? ((count + 2) * 2) : 2) >= end) goto corrupt;
						len = (((unsigned int)ptr[0] << 8) | ptr[1]);
						if (ptr - len < &mem->Data[nameLen]) goto corrupt;
						*((unsigned short *)ptr) = (unsigned short)len;
						if (count)
						{
							flags = (count + 1) * 2;
							do
							{
								len = (((unsigned int)ptr[flags] << 8) | ptr[flags+1]);
								if (ptr + len >= end) goto corrupt;
								*((unsigned short *)(ptr+flags)) = (unsigned short)len;
								flags -= 2;
							} while (flags);

							len = (count + 2) * 2;
						}
						else
							len = 2;
					}
					else
					{
						if (flags & SONGFLAG_SONGDONE) break;

						if (flags & SONGFLAG_VARIATION) ptr++;

						if (flags & SONGFLAG_STYLE)
						{
							len = *ptr++ - 1;
							if  (!(*((void **)ptr) = get_song_style((char *)ptr)))
							{
								len = strlen((char *)ptr) + 1;
								if (!alloc_temp_buffer(len + 27 + strlen((char *)ptr + len) + strlen(NamePtr))) goto memerr;
								sprintf((char *)TempBuffer, "Can't find %s/%s for %s songsheet", ptr, ptr + len, NamePtr);
								setErrorStr((char *)TempBuffer);
								goto out;
							}
							ptr += len;
						}

						len = 0;
						if (flags & SONGFLAG_PAD) len = 2;
						if (flags & SONGFLAG_CHORD) len++;
						if (flags & SONGFLAG_TEMPO) len++;
						if (flags & SONGFLAG_TIMESIG) len += 2;
					}

					ptr += len;
				}
				}

				// Link into list
				mem->Next = SongSheetList;
				SongSheetList = mem;
				mem = 0;

				SongsheetBox.NumOfItems++;

				// Find widest name
				GuiTextSetLarge(MainWin);
				len = GuiTextWidth(MainWin, NamePtr);
				if (SongsheetBox.ColumnWidth < len) SongsheetBox.ColumnWidth = len;
			}
		}
	}
out:
	if (mem) free(mem);
}










#if 0
static void doEditSongSheet(register SONGSHEET * sheet)
{
	register unsigned char *	ptr;
	unsigned char		actionFlags;

	alloc_temp_buffer(2048);

	CurrentSongSheet = (SONGSHEET *)TempBuffer;
	CurrentSongSheet->Data[0] = 0;
	SongEventPtr = &sheet->Data[1] + strlen((char *)&sheet->Data[0]);
	ptr = songQueueEndEvent(&actionFlags);
	memcpy(&CurrentSongSheet->Data[1], SongEventPtr, ptr - (unsigned char *)SongEventPtr);
	SongEventPtr = &CurrentSongSheet->Data[1];
	songQueueEndEvent(&actionFlags);
}
#endif

void positionSongGui(void)
{
	GuiListItemWidth(GuiApp, MainWin, &SongsheetBox, 0);
}








/******************* selectSongSheet() ********************
 * Selects a SongSheet for play/edit.
 *
 * sheetnum = Index of songsheet where 1 is the first. 0xff
 *				if toggling song mode on/off. 0 turns song mode
 * 			off.
 */

uint32_t selectSongSheet(register unsigned char sheetnum, register unsigned char threadId)
{
	register uint32_t		guimask;

	guimask = CTLMASK_NONE;

	// Can't select during play
	if (!BeatInPlay)
	{
		register SONGSHEET *		sheet;

		if (CurrentSongSheet)
		{
			CurrentSongSheet = 0;
			guimask = CTLMASK_SONGSHEET|CTLMASK_SONGSHEETLIST;

			// If toggled off, don't select below
			if (sheetnum == 0xff) goto out;
		}

		// Selecting a sheet, or toggling on
		guimask = clearChord(2|threadId);

		// If toggling on, present a list of sheets and let user pick
		if (sheetnum == 0xff)
			showSongList();
		else
		{
			sheet = SongSheetList;
			while (--sheetnum && sheet) sheet = sheet->Next;
			if ((CurrentSongSheet = sheet))
			{
				guimask |= changePadInstrument(threadId) | songStart() | CTLMASK_SONGSHEET|CTLMASK_SONGSHEETLIST;

				// If "Auto" set, turn Autostart on whenever selecting a song
				if (AppFlags3 & APPFLAG3_AUTOSTARTARM)
				{
					TempFlags |= TEMPFLAG_AUTOSTART;
					guimask |= CTLMASK_AUTOSTART;
				}
			}
		}
	}
out:
	return guimask;
}

GUILIST		SongsheetBox;

static void song_mouse(register GUIMSG * msg)
{
	register GUICTL *	ctl;

	ctl = msg->Mouse.SelectedCtl;
	if (ctl->Flags.Global & CTLGLOBAL_PRESET)
	{
		if (ctl->PresetId == GUIBTN_OK) goto ok;
		goto cancel;
	}
	switch (msg->Mouse.ListAction)
	{
		case GUILIST_SELECTION:
ok:		if (SongsheetBox.CurrItemNum != -1)
				selectSongSheet(SongsheetBox.CurrItemNum + 1, GUITHREADID);
		case GUILIST_ABORT:
cancel:	showMainScreen();
	}
}

static GUIFUNCS	SongsheetFuncs = {dummy_drawing, song_mouse, dummy_keypress};

void showSongList(void)
{
	doPickItemDlg(draw_songsheets, &SongsheetFuncs);
}

uint32_t selectNextSongSheet(void)
{
	if (!BeatInPlay && SongSheetList)
	{
		if (!CurrentSongSheet || !(CurrentSongSheet = CurrentSongSheet->Next))
			CurrentSongSheet = SongSheetList;

		return changePadInstrument(GUITHREADID) | songStart() | CTLMASK_SONGSHEET;
	}

	return CTLMASK_NONE;
}

void * getNextSongSheet(register void * ptr)
{
	if (!ptr) ptr = SongSheetList;
	else ptr = ((SONGSHEET *)ptr)->Next;
	return ptr;
}

const char * getSongSheetName(register void * ptr)
{
	return (const char *)(&((SONGSHEET *)ptr)->Data[0]);
}

#endif
