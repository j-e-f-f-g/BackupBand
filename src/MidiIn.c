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

#include <sys/eventfd.h>
#include <sys/epoll.h>
#include "Options.h"
#include "Main.h"
#include "PickDevice.h"
#include "MidiIn.h"
#include "AccompSeq.h"
#include "StyleData.h"
#include "AudioPlay.h"
#include "Prefs.h"
#include "FileLoad.h"
#include "Setup.h"
#include "SongSheet.h"
typedef uint32_t (*GUICTLDATAFUNC)(register GUICTL *);


// ===================== Actions
// For actions whose value is 0 or 1 (or which step through a range
// 1 unit at a time).
#define ACTION_PEDAL				3		// When pressed, sets param to highest value or "on". When released, sets param to lowest value or "off".
#define ACTION_ONOFFSWITCH		1		// Toggles param between "off" (lowest) and "on" (highest) value, or swaps between most recent 2 values.
#define ACTION_DECBUTTON		0		// When pressed, turns param off, or decrements param by 1 unit.
#define ACTION_INCBUTTON		2		// When pressed, turns param on, or increments param by 1 unit.

#define ACTION_1					0x04	// ACTION_1/2/3 are OR'd with the above 4 values, to yield a wider range of possible values, for
#define ACTION_2					0x08	// example, a category can have many pedal actions such as ACTION_PEDAL, ACTION_PEDAL|ACTION_1,
#define ACTION_3					0x10	// ACTION_PEDAL|ACTION_2 ... to ACTION_PEDAL|ACTION_1|ACTION_2|ACTION_3.
#define ACTION_RANGELIMIT		0x20	// Sets param to the value within a High and Low range. OR'ed only for Pgm Change and Channel Press assigns
#define ACTION_SCALE				0x80	// Sets param to the value 0-based and/or scaled. OR'ed only with ACTION_FADER

// For actions whose value range is > 2. Action value must be set ACTION_FADER or greater
#define ACTION_FADER				0x40

// ACTIONCAT Properties
#define ACTIONPROP_THREADID	0x20
#define ACTIONPROP_COMMON		0x40
#define ACTIONPROP_SCALE_0		0x80
#define ACTIONPROP_ID_MASK		0x1F

#pragma pack (1)

typedef uint32_t (ACTION_FUNC)(register unsigned char, register unsigned char);

// Info about one category of assignable actions
typedef struct {
	ACTION_FUNC *	Func;
	const char *	HelpStrs;
	const char *	Desc;
	unsigned char	Properties;
	unsigned char	RobotNum;
} ACTIONCAT;

typedef struct {
	unsigned char		Status;	// MIDI status. 0xF0 for realtime/common
	union {
	unsigned char		Id;		// CC# for controller, Note#, RealTime status for "off" state, Pgm change/pressure Low range limit
	unsigned char		Low;
	};
	unsigned char		High;		// RealTime status for "on" state, Pgm change/pressure High range limit, note command value for fader
} MIDIACTION;

typedef struct {
	unsigned char		Qualifiers;
	union {
	unsigned short		Key;
	unsigned short		ButtonNum;
	};
} PCACTION;

// An instance of an assignment
typedef struct {
	unsigned char		CatIndex;
	unsigned char		Action;		// Bitmask of ACTION_ values
	unsigned char		LastValue;
	union {
	MIDIACTION			Midi;			// MIDI
	PCACTION				Pc;			// PC key/mouse
	};
} ACTIONINSTANCE;

#pragma pack ()




static char					DescBuf[120];

#ifndef NO_MIDI_IN_SUPPORT
static uint32_t			TapTempo;
static unsigned char		NumTaps;

unsigned char				CmdSwitchNote;
static unsigned char		CmdSwitchMode;
static unsigned char		BrowsedNote;
#endif

// =======================================
// The data handlers for the assignable actions
// =======================================

static const char SingleSubs[] = "Plays a drum fill\0\
 each time you press\0\
 Alternates between the 2 most recent \0\
 when you release\0\
guitar/bass/pad robots\0\
. Use this with the Wind chords model";	// \7

// ***************** Tempo category handler ***************
static const char TempoHelp[] = "Tempo\0\
Slows the \1 by 1\3@1\0\
Raises the \1 by 1\3@1\0\
Gradually slows the \1 by a slight amount\3@1\0\
Gradually speeds up the \1 by a slight amount\3@1\0\
\4tempos\3@1\0\
Sets the \1 after 4 taps of@1\0\
Selects among the entire range of \1s as you move the fader";
static const char TempoStrs[] = {7|(5<<4), ACTION_DECBUTTON, ACTION_INCBUTTON,
	ACTION_DECBUTTON|ACTION_1, ACTION_INCBUTTON|ACTION_1,
	ACTION_ONOFFSWITCH, ACTION_INCBUTTON|ACTION_2, ACTION_FADER,
	'D','e','c','r','e','a','s','e',0,
	'I','n','c','r','e','a','s','e',0,
	'R','i','t','a','r','d',0,
	'A','c','c','e','l','e','r','a','n','d','o',0,
	'S','w','a','p',0,
	'T','a','p',0,
	'B','P','M',0,
};

#ifndef NO_MIDI_IN_SUPPORT

static uint32_t do_taptempo(register unsigned char threadId)
{
	register uint32_t			amt;

	// First Tap?
	if (!NumTaps)
	{
		// Get initial time
		TapTempo = get_current_clock();
	}

	if (++NumTaps >= 4)
	{
		NumTaps = 0;
		amt = (60000 * (4-1)) / (get_current_clock() - TapTempo);
		if (amt > 10 && amt <= 255) set_bpm(amt);
	}

	return setTempoLabel(NumTaps);
}
#endif

static uint32_t tempoHandler(register unsigned char mode, register unsigned char mididata2)
{
	register uint32_t			mask;
	register unsigned char	tempo;
	register unsigned char	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
	mask = CTLMASK_NONE;

	if (lockTempo(threadId) == threadId)
	{
		mididata2 &= 0x7F;

		if (mode & ACTION_2)
		{
#ifndef NO_MIDI_IN_SUPPORT
			mask = do_taptempo(threadId);
#endif
		}

		// INCBUTTON = accelerando
		// DECBUTTON = ritard
		else if (mode & ACTION_1)
			set_ritard_or_accel(mididata2);

		else
		{
			tempo = set_bpm(0);

			// FADER = 40 to 177
			if (mode >= ACTION_FADER)
			{
				if (mididata2 <= 30)
					mididata2 = (mididata2 + 20) * 2;
				else
					mididata2 += 101;
			}
			else
			{
				// ONOFFSWITCH = swap previous tempo
				if (mode == ACTION_ONOFFSWITCH)
					mididata2 = get_prev_bpm();
				// INCBUTTON = increment
				// DECBUTTON = dec
				else
					mididata2 = tempo + mode - 1;
			}

			if (tempo != set_bpm(mididata2)) mask = CTLMASK_TEMPO;
		}
	}

	unlockTempo(threadId);
	return mask;
}

// ***************** Play category handler ***************
static const char PlayHelp[] = "Play\0\
Stops \1\0\
Starts \1\0\
Starts \1 with count-off\0\
Switches \1 on or off\3@1\0\
Switches \1 on or off with count-off\3@1\0\
Play continues while you press@1. Stops\5\0\
Play pauses while you press@1. Resumes\5\0\
Turns auto-start on\0\
Turns auto-start off\0\
Turns auto-start on or off\3@1";
static const char PlayStrs[] = {10|(10<<4), ACTION_DECBUTTON|ACTION_1, ACTION_INCBUTTON|ACTION_1, ACTION_INCBUTTON|ACTION_2,
	ACTION_ONOFFSWITCH|ACTION_1, ACTION_ONOFFSWITCH|ACTION_2, ACTION_PEDAL|ACTION_1, ACTION_PEDAL|ACTION_3,
	ACTION_DECBUTTON, ACTION_INCBUTTON, ACTION_ONOFFSWITCH,
	'O','f','f',0,
	'O','n',0,
	'O','n',' ','+',' ','c','o','u','n','t',0,
	'O','n','/','o','f','f',0,
	'O','n','/','o','f','f',' ','+',' ','c','o','u','n','t',0,
	'O','f','f',' ','w','h','i','l','e',' ','p','u','s','h',0,
	'O','n',' ','w','h','i','l','e',' ','p','u','s','h',0,
	'A','u','t','o','s','t','a','r','t',' ','o','f','f',0,
	'A','u','t','o','s','t','a','r','t',' ','o','n',0,
	'A','u','t','o','s','t','a','r','t',' ','o','n','/','o','f','f',0,
};

static uint32_t playHandler(register unsigned char mode, register unsigned char mididata2)
{
	register unsigned char	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
	mididata2 &= 0x7F;

	if (mode < ACTION_1)
	{
		if (mode == ACTION_ONOFFSWITCH)
			TempFlags ^= TEMPFLAG_AUTOSTART;
		else if (mididata2)
			TempFlags |= TEMPFLAG_AUTOSTART;
		else
			TempFlags &= ~TEMPFLAG_AUTOSTART;
		return CTLMASK_AUTOSTART;
	}

	if (mode == (ACTION_PEDAL|ACTION_1)) mididata2 ^= 0x01;
	if ((BeatInPlay && !mididata2) || (!BeatInPlay && mididata2)) play_beat(((mode & ACTION_2) ? 0 : 1), threadId);
	return CTLMASK_NONE;	// Beat thread signals main to draw play button, so don't do it now
}

// ***************** Misc category handler ***************
static const char MiscHelp[] = "Miscellaneous\0\
Temporarily mutes the guitar, bass, and pad until you play another chord\0\
Same as Break but also mutes the solo instrument. This is equivalent to clicking the Panic button\0\
Switches note command mode on/off.\0\
Raises the overall volume\0\
Lowers the overall volume\0\
Sets the overall volume";
static const char MiscStrs[] = {6|(5<<4),ACTION_DECBUTTON,ACTION_INCBUTTON,ACTION_ONOFFSWITCH,ACTION_INCBUTTON|ACTION_1,ACTION_DECBUTTON|ACTION_1,ACTION_FADER,
	'A','c','c','o','m','p',' ','B','r','e','a','k',0,
	'P','a','n','i','c',0,
	'N','o','t','e',' ','c','o','m','m','a','n','d',0,
	'M','a','s','t','e','r',' ','V','o','l',' ','U','p',0,
	'M','a','s','t','e','r',' ','V','o','l',' ','D','o','w','n',0,
	'M','a','s','t','e','r',' ','V','o','l','u','m','e',0,
};

#ifndef NO_MIDI_IN_SUPPORT
void endCmdMode(void)
{
	NumTaps = CmdSwitchMode = 0;
	BrowsedNote = 128;
}

static uint32_t notifyCmdMode(void)
{
	if (CmdSwitchMode)
	{
		BrowsedNote = 128;
		strcpy(&DescBuf[0], "Command Key mode");
	}
	else
	{
		endCmdMode();
		DescBuf[0] = 0;
	}

	return (isMainScreen() && !(AppFlags4 & APPFLAG4_NO_TIPS)) ? CTLMASK_CMDNOTEBROWSE : CTLMASK_NONE;
}
#endif

static uint32_t miscHandler(register unsigned char mode, register unsigned char mididata2)
{
	register unsigned char	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
	mididata2 &= 0x7F;

	if (mode < ACTION_FADER)
	{
		if (mode & ACTION_1)
		{
			mode = getMasterVol();
			if (mididata2)
			{
				if (mode >= 100) goto none;
				mode++;
			}
			else
			{
				if (!mode)
none:				return CTLMASK_NONE;
				mode--;
			}
			mididata2 = mode;
			goto vol;
		}

#ifndef NO_MIDI_IN_SUPPORT
		if (mode == ACTION_ONOFFSWITCH)
		{
			CmdSwitchMode = (mididata2 << 1);
			return notifyCmdMode();
		}
#endif
		return (mididata2 ? allNotesOff(threadId) : mute_playing_chord(threadId));
	}
	if (mode & ACTION_SCALE) mididata2 = (((uint32_t)mididata2 * (uint32_t)100) / 127);
	if (mididata2 > 100) mididata2 = 100;
vol:
	return setMasterVol(mididata2);
}

// ***************** Songsheet category handler ***************
static const char SongHelp[] = "Songsheet\0\
Turns off \1 mode, and returns to style select mode\0\
Selects the next \1\0\
Selects one of the available \1s. Your midi message must contain an index number where 0 is the first \1 displayed \
in the \1 list, 1 is the second, etc";
static const char SongStrs[] = {3|(2<<4),ACTION_DECBUTTON,ACTION_INCBUTTON,ACTION_INCBUTTON|ACTION_1,
	'S','o','n','g',' ','O','f','f',0,
	'N','e','x','t',' ','S','o','n','g',0,
	'S','e','l','e','c','t',' ','S','o','n','g',0,
};

static uint32_t songHandler(register unsigned char mode, register unsigned char mididata2)
{
#ifndef NO_SONGSHEET_SUPPORT
	register unsigned char	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
	mididata2 &= 0x7F;

	if (!mode)
	{
		if (!isSongsheetActive()) goto out;
		mididata2 = 0xff;
		goto sel;
	}
	if (mode & ACTION_1)
		return selectNextSongSheet();
	mididata2++;
sel:
	return selectSongSheet(mididata2, threadId);
out:
#endif
	return CTLMASK_NONE;
}

// ***************** Transpose category handler ***************
static const char TransposeHelp[] = "Transpose\0\
\1s down 1\3@1\0\
\1s up 1\3@1\0\
Resets \1 to 0\0\
\4transpose\31\0\
\1s within the range -5 to +4 half steps as you move the fader";
static const char TransposeStrs[] = {5|(4<<4), ACTION_DECBUTTON, ACTION_INCBUTTON, ACTION_DECBUTTON|ACTION_1, ACTION_ONOFFSWITCH, ACTION_FADER,
	'D','o','w','n',' ','1','/','2',0,
	'U','p',' ','1','/','2',0,
	'R','e','s','e','t',0,
	'S','w','a','p',0,
	'-','4','/','+','5',' ','R','a','n','g','e',0,
};

static uint32_t transposeHandler(register unsigned char mode, register unsigned char mididata2)
{
	register unsigned char	orig;
	register unsigned char 	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
	mididata2 &= 0x7F;

	orig = setTranspose(0xff, threadId);

	// ACTION_FADER = -5 to +4
	if (mode >= ACTION_FADER)
	{
		if (mode & ACTION_SCALE) mididata2 = (((uint32_t)mididata2 * (uint32_t)9) / 127);
		if (mididata2 > 9) mididata2 = 9;
	}
	// reset to 0
	else if (mode & ACTION_1)
		mididata2 = 5;
	// INC/dec = up/down half step
	else
		mididata2 = orig + mode - 1;

	return (mididata2 != orig && mididata2 == setTranspose(mididata2, threadId)) ? CTLMASK_TRANSPOSE : CTLMASK_NONE;
}

// ***************** Select Style category handler ***************
static const char StylesHelp[] = "Select Style\0\
Selects the next style\0\
Selects the first style in the next category\0\
Cycles through the styles in the current category only\0\
\4selected styles\0\
Selects from the entire range of styles";
static const char StylesStrs[] = {5|(4<<4), ACTION_INCBUTTON|ACTION_1, ACTION_INCBUTTON|ACTION_2, ACTION_INCBUTTON|ACTION_3, ACTION_ONOFFSWITCH, ACTION_FADER,
	'N','e','x','t',0,
	'C','a','t','e','g','o','r','y',0,
	'C','y','c','l','e',0,
	'S','w','a','p',0,
	'R','a','n','g','e',0,
};

static uint32_t styleHandler(register unsigned char mode, register unsigned char mididata2)
{
	register unsigned char 	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
	mididata2 &= 0x7F;

	// INC = first style in next category
	if (mode & ACTION_2)
	{
next:	cycleStyleCategory();
first:
		return selectStyle(0, threadId);
	}

	// 1 = next style
	// 3 = next style in category
	if (mode & (ACTION_1|ACTION_3))
	{
		register void *	ptr;

		if ((ptr = getCurrentStyle()) && (ptr = getStyle(ptr)))
			return selectStyleByPtr(ptr, threadId) | CTLMASK_STYLES;
		if (mode & ACTION_3) goto first;
		goto next;
	}

	// Swap 2 most recent styles
	if (mode == ACTION_ONOFFSWITCH)
	{
		register void *		ptr;
		register uint32_t		mask;

		if ((ptr = getPrevStyle()) && (mask = selectStyleByPtr(ptr, threadId))) return mask;
	}

	// FADER selects from full range
	if (mode	>= ACTION_FADER)
	{
		mode = ((uint32_t)mididata2 * getNumOfStyles()) / 127;
		return change_style(mode, 0, threadId);
	}

	return CTLMASK_NONE;
}

// ***************** Select Variation category handler ***************
static const char VarHelp[] = "Select variation\0\
\2. Doesn't change the variation if \"Variation Cycle\" is off\0\
Switches between Verse/Chorus/Bridge\3@1. There is no drum fill inbetween\0\
Switches between Verse/Chorus/Bridge\3@1. \2 inbetween each switch\0\
Jumps to the Verse variation with no drum fill\0\
\2, then jumps to the Verse variation\0\
Jumps to the Chorus variation with no drum fill\0\
\2, then jumps to the Chorus variation\0\
Jumps to the Bridge variation with no drum fill\0\
\2, then jumps to the Bridge variation";
static const char VarStrs[] = {9|(9<<4), ACTION_ONOFFSWITCH, ACTION_ONOFFSWITCH|ACTION_1, ACTION_ONOFFSWITCH|ACTION_2,
	ACTION_DECBUTTON, ACTION_DECBUTTON|ACTION_3, ACTION_INCBUTTON, ACTION_INCBUTTON|ACTION_3,
	ACTION_DECBUTTON|ACTION_2, ACTION_DECBUTTON|ACTION_3|ACTION_2,
	'F','i','l','l',0,
	'V','-','>','C','-','>','B',0,
	'V','-','>','C','-','>','B',' ','f','i','l','l',0,
	'V','e','r','s','e',0,
	'V','e','r','s','e',' ','f','i','l','l',0,
	'C','h','o','r','u','s',0,
	'C','h','o','r','u','s',' ','f','i','l','l',0,
	'B','r','i','d','g','e',0,
	'B','r','i','d','g','e',' ','f','i','l','l',0,
};

static uint32_t variationHandler(register unsigned char mode, register unsigned char mididata2)
{
	register unsigned char	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
	mididata2 &= 0x7F;

	if ((mode & 0x03) == ACTION_ONOFFSWITCH)
	{
		if (mode & (ACTION_1|ACTION_2))
		{
			if (mode & ACTION_1) PlayFlags |= PLAYFLAG_FILLPLAY;	// skip autofill
			return playFillAndAdvance(threadId);
		}

		PlayFlags |= PLAYFLAG_STYLEJUMP;
	}
	else
	{
		if (lockVariation(threadId) == threadId)
		{
			if (mode & ACTION_3) PlayFlags |= PLAYFLAG_FILLPLAY;

			if (mode & ACTION_2) mididata2 = 2;
			else mididata2 = ((uint32_t)mididata2 * (uint32_t)3) / 127;
			// note selectStyleVariation unlocks
			return selectStyleVariation(mididata2, threadId);
		}
		unlockVariation(threadId);
	}

	return CTLMASK_NONE;
}

// ***************** Sustain category handler ***************
static const char SustainHelp[] = "Sustain\0\
Turns bass \1 off\0\
Turns bass \1 on\0\
Switches bass \1 on or off\3@1\0\
Turns bass \1 on while you press@1. Turns off\5\0\
Same as bass \"+On\", but reverse polarity\0\
Turns pad \1 off\0\
Turns pad \1 on\0\
Switches pad \1 on or off\3@1\0\
Turns pad \1 on while you press@1. Turns off\5\0\
Same as pad \"+On\", but reverse polarity\0\
Turns solo \1 off\0\
Turns solo \1 on\0\
Switches solo \1 on or off\3@1\0\
Turns solo \1 on while you press@1. Turns off\5\0\
Same as solo \"+On\", but reverse polarity";
static const char SustainStrs[] = {15|(15<<4),
	ACTION_DECBUTTON|ACTION_2,ACTION_INCBUTTON|ACTION_2,ACTION_ONOFFSWITCH|ACTION_2,ACTION_PEDAL|ACTION_2,ACTION_PEDAL|ACTION_2|ACTION_1,
	ACTION_DECBUTTON|ACTION_3,ACTION_INCBUTTON|ACTION_3,ACTION_ONOFFSWITCH|ACTION_3,ACTION_PEDAL|ACTION_3,ACTION_PEDAL|ACTION_3|ACTION_1,
	ACTION_DECBUTTON,ACTION_INCBUTTON,ACTION_ONOFFSWITCH,ACTION_PEDAL,ACTION_PEDAL|ACTION_1,
	'B','a','s','s',' ','O','f','f',0,
	'B','a','s','s',' ','O','n',0,
	'B','a','s','s',' ','O','n','/','O','f','f',0,
	'B','a','s','s',' ','P','u','s','h',' ','O','n',' ','+',0,
	'B','a','s','s',' ','P','u','s','h',' ','O','n',' ','-',0,
	'P','a','d',' ','O','f','f',0,
	'P','a','d',' ','O','n',0,
	'P','a','d',' ','O','n','/','O','f','f',0,
	'P','a','d',' ','P','u','s','h',' ','O','n',' ','+',0,
	'P','a','d',' ','P','u','s','h',' ','O','n',' ','-',0,
	'S','o','l','o',' ','O','f','f',0,
	'S','o','l','o',' ','O','n',0,
	'S','o','l','o',' ','O','n','/','O','f','f',0,
	'S','o','l','o',' ','P','u','s','h',' ','O','n',' ','+',0,
	'S','o','l','o',' ','P','u','s','h',' ','O','n',' ','-',0,
};

static uint32_t sustainHandler(register unsigned char mode, register unsigned char mididata2)
{
	register unsigned char	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
	mididata2 &= 0x7F;
	if (ACTION_1 & mode) mididata2 ^= 0x01;
	if (ACTION_2 & mode) mode = PLAYER_BASS;
	else if (ACTION_3 & mode) mode = PLAYER_PAD;
	else mode = PLAYER_SOLO;
	return set_sustain(mode, mididata2, threadId);
}

// ***************** Reverb category handler ***************
static const char RevHelp[] = "Reverb\0\
Turns \1 off\0\
Turns \1 on\0\
Switches \1 on or off\3@1\0\
Turns \1 on while you press@1. Turns off\5\0\
Turns \1 off while you press@1. Turns on\5\0\
Raises the \1 volume by 1\0\
Lowers the \1 volume by 1\0\
Sets the \1 volume";
static const char RevStrs[] = {8|(7<<4),ACTION_DECBUTTON,ACTION_INCBUTTON,ACTION_ONOFFSWITCH,ACTION_PEDAL,ACTION_PEDAL|ACTION_1,
	ACTION_INCBUTTON|ACTION_2,ACTION_DECBUTTON|ACTION_2,ACTION_FADER,
	'O','f','f',0,
	'O','n',0,
	'O','n','/','O','f','f',0,
	'O','n',' ','w','h','i','l','e',' ','p','u','s','h',0,
	'O','f','f',' ','w','h','i','l','e',' ','p','u','s','h',0,
	'V','o','l','u','m','e',' ','U','p',0,
	'V','o','l','u','m','e',' ','D','o','w','n',0,
	'V','o','l','u','m','e',0,
};

static uint32_t reverbHandler(register unsigned char mode, register unsigned char mididata2)
{
#ifndef NO_REVERB_SUPPORT
	if (mode >= ACTION_FADER)
	{
		if (mode & ACTION_SCALE) mididata2 = ((uint32_t)mididata2 * (uint32_t)100) / 127;
		if (mididata2 > 100) mididata2 = 100;
vol:	return setReverbVol(mididata2);
	}

	if (ACTION_2 & mode)
	{
		mode = setReverbVol(0xff);
		if (mididata2)
		{
			if (mode >= 100)
none:			return CTLMASK_NONE;
			mode++;
		}
		else
		{
			if (!mode) goto none;
			mode--;
		}
		mididata2 = mode;
		goto vol;
	}

	if (ACTION_1 & mode) mididata2 ^= 0x01;
	mode = TempFlags;
	if (mode == ACTION_ONOFFSWITCH)
		TempFlags ^= APPFLAG3_NOREVERB;
	else if (!mididata2)
		TempFlags |= APPFLAG3_NOREVERB;
	else
		TempFlags &= ~APPFLAG3_NOREVERB;
	return (mode != TempFlags ? CTLMASK_REVERBMUTE : CTLMASK_NONE);
#else
	return CTLMASK_NONE;
#endif
}

// ***************** Mute category handler ***************
static const char MuteHelp[] = "Mute\0\
Turns \6 off\0\
Turns \6 on\0\
Switches \6 on or off\3@1\0\
Turns \6 off while you press@1. Turns on\5\0\
Turns drum robot off\0\
Turns drum robot on\0\
Switches drum robot on or off\3@1\0\
Turns drum robot off while you press@1. Turns on\5\0\
Turns bass robot off\0\
Turns bass robot on\0\
Switches bass robot on or off\3@1\0\
Turns bass robot off while you press@1. Turns on\5\0\
Turns guitar robot off\0\
Turns guitar robot on\0\
Switches guitar robot on or off\3@1\0\
Turns guitar robot off while you press@1. Turns on\
Turns pad robot off\0\
Turns pad robot on\0\
Switches pad robot on or off\3@1\0\
Turns pad robot off while you press@1. Turns on\5";
static const char MuteStrs[] = {20,
ACTION_DECBUTTON|ACTION_3|ACTION_2|ACTION_1, ACTION_INCBUTTON|ACTION_3|ACTION_2|ACTION_1, ACTION_ONOFFSWITCH|ACTION_3|ACTION_2|ACTION_1, ACTION_PEDAL|ACTION_3|ACTION_2|ACTION_1,
ACTION_DECBUTTON, ACTION_INCBUTTON, ACTION_ONOFFSWITCH, ACTION_PEDAL,
ACTION_DECBUTTON|ACTION_1, ACTION_INCBUTTON|ACTION_1, ACTION_ONOFFSWITCH|ACTION_1, ACTION_PEDAL|ACTION_1,
ACTION_DECBUTTON|ACTION_2, ACTION_INCBUTTON|ACTION_2, ACTION_ONOFFSWITCH|ACTION_2, ACTION_PEDAL|ACTION_2,
ACTION_DECBUTTON|ACTION_3, ACTION_INCBUTTON|ACTION_3, ACTION_ONOFFSWITCH|ACTION_3, ACTION_PEDAL|ACTION_3,
'A','c','c','o','m','p',' ','o','f','f',0,
'A','c','c','o','m','p',' ','o','n',0,
'A','c','c','o','m','p',' ','o','n','/','o','f','f',0,
'A','c','c','o','m','p',' ','p','r','e','s','s',0,
'D','r','u','m','s',' ','o','f','f',0,
'D','r','u','m','s',' ','o','n',0,
'D','r','u','m','s',' ','o','n','/','o','f','f',0,
'D','r','u','m','s',' ','p','r','e','s','s',0,
'B','a','s','s',' ','o','f','f',0,
'B','a','s','s',' ','o','n',0,
'B','a','s','s',' ','o','n','/','o','f','f',0,
'B','a','s','s',' ','p','r','e','s','s',0,
'G','u','i','t','a','r',' ','o','f','f',0,
'G','u','i','t','a','r',' ','o','n',0,
'G','u','i','t','a','r',' ','o','n','/','o','f','f',0,
'G','u','i','t','a','r',' ','p','r','e','s','s',0,
'P','a','d',' ','o','f','f',0,
'P','a','d',' ','o','n',0,
'P','a','d',' ','o','n','/','o','f','f',0,
'P','a','d',' ','p','r','e','s','s',0,
};

static uint32_t muteHandler(register unsigned char mode, register unsigned char mididata2)
{
	register unsigned char	robotMask;
	register unsigned char	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
	mididata2 &= 0x7F;

	robotMask = 0;
	if (mode & ACTION_1) robotMask |= APPFLAG3_NOBASS;
	if (mode & ACTION_2) robotMask |= APPFLAG3_NOGTR;
	if (mode & ACTION_3) robotMask |= APPFLAG3_NOPAD;
	if (!(mode & (ACTION_3|ACTION_2|ACTION_1))) robotMask = APPFLAG3_NODRUMS;

	switch (mode & 0x03)
	{
		// DECBUTTON = Turn mute off
//		case ACTION_DECBUTTON:
//			robotMask |= 0;
//			break;

		// PEDAL = Turn mute on if pedal pressed
		case ACTION_PEDAL:
			if (mididata2) break;

		// INCBUTTON = Turn mute on
		case ACTION_INCBUTTON:
			robotMask |= ACCOMP_ON;
			break;

		// ACTION_ONOFFSWITCH = Toggle mute
		case ACTION_ONOFFSWITCH:
			robotMask |= ACCOMP_TOGGLE;
	}
	return mute_robots(robotMask, threadId);
}

// ***************** Solo instrument category handler ***************
static const char PatchHelp[] = "Human player\0\
Turns the solo volume boost off\0\
Turns the solo volume boost on\0\
Switches the solo boost on or off\3@1\0\
Turns the solo boost on while you press@1. Turns off\5";
static const char PatchStrs[] = {4|(4<<4),
	ACTION_DECBUTTON|ACTION_2, ACTION_INCBUTTON|ACTION_2, ACTION_ONOFFSWITCH|ACTION_2, ACTION_PEDAL|ACTION_2,
	'B','o','o','s','t',' ','O','f','f',0,
	'B','o','o','s','t',' ','O','n',0,
	'B','o','o','s','t',' ','O','n','/','O','f','f',0,
	'B','o','o','s','t',' ','p','u','s','h',0,
};

static const char CommonPatchHelp[] = "Selects the previous \1 instrument\0\
Selects the next \1 instrument\0\
Switches between the two most recently selected \1 instruments\0\
Raises the \1 volume by 1\0\
Lowers the \1 volume by 1\0\
Selects from the full range of available instruments. Your midi message must contain an index number where 0 is the first instrument displayed \
in the \1 instrument list, 1 is the second, etc\0\
Selects from the full range of available \1 instruments. Your midi message must contain a program number that matches the PGM number in the instrument's .txt file\0\
Sets the \1 volume";
static const char CommonPatchStrs[] = {8|(5<<4),
   ACTION_DECBUTTON|ACTION_3, ACTION_INCBUTTON|ACTION_3, ACTION_ONOFFSWITCH|ACTION_3,
   ACTION_DECBUTTON|ACTION_1, ACTION_INCBUTTON|ACTION_1,
	ACTION_FADER, ACTION_FADER+1, ACTION_FADER+2,
	'P','r','e','v','i','o','u','s',0,
	'N','e','x','t',0,
	'S','w','a','p',0,
	'V','o','l','u','m','e',' ','D','o','w','n',0,
	'V','o','l','u','m','e',' ','U','p',0,
	'G','r','i','d',' ','S','e','l','e','c','t',0,
	'P','G','M',' ','S','e','l','e','c','t',0,
	'S','e','t',' ','V','o','l','u','m','e',0,
};

static uint32_t set_instrument(register unsigned char, register unsigned char, register uint32_t);

static const char BassHelp[] = "Bass\0\
Turns bass split off\0\
Turns bass split on\0\
Turns bass split on in legato mode\0\
Switches bass to off, on, or legato\3@1";
static const char BassStrs[] = {4|(4<<4),
	ACTION_DECBUTTON|ACTION_2, ACTION_INCBUTTON|ACTION_2, ACTION_DECBUTTON|ACTION_2|ACTION_1,	ACTION_ONOFFSWITCH|ACTION_2,
	'S','p','l','i','t',' ','O','f','f',0,
	'S','p','l','i','t',' ','O','n',0,
	'L','e','g','a','t','o',' ',0,
	'O','f','f','/','O','n','/','L','e','g','a','t','o',0,
};
static uint32_t bassHandler(register unsigned char mode, register unsigned char mididata2)
{
	if (mode & ACTION_2)
	{
		register unsigned char	flags;

		mididata2 &= 0x7F;
		flags = AppFlags & (APPFLAG_BASS_LEGATO|APPFLAG_BASS_ON);
		AppFlags &= ~(APPFLAG_BASS_LEGATO|APPFLAG_BASS_ON);

		if (mode & ACTION_1) goto leg;

		if (mididata2) mididata2 = APPFLAG_BASS_ON;
		if (mode == (ACTION_ONOFFSWITCH|ACTION_2))
		{
			if (flags & APPFLAG_BASS_ON)
leg:			mididata2 = APPFLAG_BASS_LEGATO;
			else if (flags & APPFLAG_BASS_LEGATO)
				mididata2 = 0;
		}

		AppFlags |= mididata2;
		return 0;
	}

	return set_instrument(mode, mididata2, PLAYER_BASS);
}

static uint32_t gtrHandler(register unsigned char mode, register unsigned char mididata2)
{
	return set_instrument(mode, mididata2, PLAYER_GTR);
}

static uint32_t patchHandler(register unsigned char mode, register unsigned char mididata2)
{
	if (mode & ACTION_2)
	{
		// ONOFFSWITCH toggles on/off
		// other = 0 for off, 1 on
		toggle_solo_vol(mididata2 & 0x01, (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID);
		return CTLMASK_VOLBOOST;
	}

	return set_instrument(mode, mididata2, PLAYER_SOLO);
}

static uint32_t set_instrument(register unsigned char mode, register unsigned char mididata2, register uint32_t roboNum)
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	register unsigned char	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
#endif
	mididata2 &= 0x7F;

	// Set volume
	if ((mode & ~ACTION_SCALE) == ACTION_FADER+2)
	{
		if ((mode & ACTION_SCALE)
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
			&& DevAssigns[roboNum] == &SoundDev[DEVNUM_MIDIOUT1]
#endif
			) mididata2 = (((uint32_t)mididata2 * (uint32_t)100) / 127);
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
vol:	return setInstrumentVol(roboNum, mididata2, threadId);
#else
vol:	return setInstrumentVol(roboNum, mididata2);
#endif
	}

	if (mode & ACTION_1)
	{
		mode = getInstrumentVol(roboNum);
		if (mididata2)
		{
			if (mode >= 100) goto none;
			mode++;
		}
		else
		{
			if (!mode) goto none;
			mode--;
		}
		mididata2 = mode;
		goto vol;
	}

	if (mode & ACTION_3)
	{
		mode &= 0x03;

		// Previously selected instrument
		if (mode == ACTION_ONOFFSWITCH)
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
			return setInstrumentByPtr(0, roboNum, threadId);
#else
			return setInstrumentByPtr(0, roboNum);
#endif

		if (mode != ACTION_PEDAL)
		{
			void *	ptr;

			if (mididata2)
			{
				ptr = getCurrentInstrument(roboNum);
				do
				{
				} while ((ptr = getNextInstrument(ptr, roboNum)) && !(*getInstrumentName(ptr)));
				if (!ptr) ptr = getNextInstrument(0, roboNum);
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
				return setInstrumentByPtr(ptr, roboNum, threadId);
#else
				return setInstrumentByPtr(ptr, roboNum);
#endif
			}

			if (!(ptr = getNextInstrument(0, roboNum)))
none:			return CTLMASK_NONE;
			do
			{
				if (*getInstrumentName(ptr) && ptr == getCurrentInstrument(roboNum) && mididata2) break;
				mididata2++;
			} while ((ptr = getNextInstrument(ptr, roboNum)));
			mididata2--;
			roboNum |= (SETINS_BYINDEX|SETINS_SHOWHIDDEN);
		}
	}
	else
	{
		// Select instrument by Index
		// Select instrument by Bank ctlr and Pgm #
		if (mode & ACTION_SCALE) mididata2 = ((uint32_t)mididata2 * (uint32_t)getNumOfInstruments(roboNum)) / 128;
		roboNum |= ((mode & 0x01) ? ((BankNums[roboNum * 2] << 8) | (BankNums[(roboNum * 2) + 1] << 16))  : SETINS_BYINDEX);
	}

	return setInstrumentByNum(roboNum|threadId, mididata2, 0);
}

static uint32_t kitHandler(register unsigned char mode, register unsigned char mididata2)
{
	return set_instrument(mode, mididata2, PLAYER_DRUMS);
}

static const char PadHelp[] = "Background Pad\0\
Selects the Strings pad\0\
Switches the Strings pad on/off\0\
Selects Strings while you press@1. Selects None\5\0\
Selects the Brass pad\0\
Switches the Brass pad on/off\0\
Selects Brass while you press@1. Selects None\5\0\
Selects the Organ pad\0\
Switches the Organ pad on/off\0\
Selects Organ while you press@1. Selects None\5\0\
Switches between None/Strings/Brass/Organ\3@1\0\
No selection";
static const char PadStrs[] = {11|(11<<4),
	ACTION_INCBUTTON|ACTION_2, ACTION_PEDAL|ACTION_2, ACTION_ONOFFSWITCH|ACTION_2,
	ACTION_INCBUTTON|ACTION_2|ACTION_1, ACTION_PEDAL|ACTION_2|ACTION_1, ACTION_ONOFFSWITCH|ACTION_2|ACTION_1,
	ACTION_INCBUTTON|ACTION_2|ACTION_3, ACTION_PEDAL|ACTION_2|ACTION_3, ACTION_ONOFFSWITCH|ACTION_2|ACTION_3,
	ACTION_INCBUTTON, ACTION_DECBUTTON,
	'S','t','r','i','n','g','s',' ','O','n',0,
	'S','t','r','i','n','g','s',' ','O','n','/','O','f','f',0,
	'S','t','r','i','n','g','s',' ','P','r','e','s','s',0,
	'B','r','a','s','s',' ','O','n',0,
	'B','r','a','s','s',' ','O','n','/','O','f','f',0,
	'B','r','a','s','s',' ','P','r','e','s','s',0,
	'O','r','g','a','n',' ','O','n',0,
	'O','r','g','a','n',' ','O','n','/','O','f','f',0,
	'O','r','g','a','n',' ','P','r','e','s','s',0,
	'C','y','c','l','e',0,
	'O','f','f',0,
};
static uint32_t padHandler(register unsigned char mode, register unsigned char mididata2)
{
		if (mode & ACTION_2 || mode <= ACTION_PEDAL)
		{
			register unsigned char	padsel;

			if (mode == ACTION_INCBUTTON)
			{
				padsel = getPadPgmNum() + 1;
				if (padsel > 3) padsel = 0;
			}
			else if (!(mididata2 & 0x7F))
				padsel = 0;
			else
			{
				padsel = 1;
				if (mode & ACTION_1) padsel = 2;
				if (mode & ACTION_3) padsel = 3;
			}

			return changePadInstrument(padsel | ((mididata2 & 0x80) ? GUITHREADID : MIDITHREADID));
		}

		return set_instrument(mode, mididata2, PLAYER_PAD);
}

// ***************** Chord category handler ***************
static const char ChordsHelp[] = "Chord\0\
Selects a minor chord while you press@1. Selects major\5\7\0\
Switches between major or minor chord\3@1\7\0\
Selects a minor chord\7\0\
Selects a major chord\7\0\
Each MIDI value selects a root note only. Use this in conjunction with the above 4 actions, if you do not have a controller that generates note messages\0\
Each MIDI value selects a chord only. You must play the root note separately with a note message\0\
Each MIDI value selects both a chord and root note";
static const char ChordsStrs[] = {7|(4<<4), ACTION_PEDAL, ACTION_ONOFFSWITCH, ACTION_DECBUTTON, ACTION_INCBUTTON,
	ACTION_ONOFFSWITCH|ACTION_FADER, ACTION_PEDAL|ACTION_FADER, ACTION_FADER,
	'M','i','n','o','r',' ','p','r','e','s','s',0,
	'M','a','j','o','r','/','M','i','n','o','r',0,
	'M','i','n','o','r',0,
	'M','a','j','o','r',0,
	'R','o','o','t',0,
	'C', 'h','o','r','d',0,
	'C', 'h','o','r','d','+','R','o','o','t',0,
};

static uint32_t chordHandler(register unsigned char mode, register unsigned char mididata2)
{
	register unsigned char	threadId;

	threadId = (mididata2 & 0x80) ? GUITHREADID : MIDITHREADID;
	mididata2 &= 0x7F;

	if ((mode & 0x03) == ACTION_ONOFFSWITCH)
	{
		if (mode & ACTION_SCALE)
		{
			mididata2 = (mididata2 % 12) + 24;
			if (mididata2 < 28) mididata2 += 12;
			return eventChordTrigger(mididata2, 254, threadId);
		}

		// ONOFF = toggle major/minor
		return eventChordTrigger(0, 255, threadId);
	}
	// Chord+Root = particular chords via 1 msg
	if (mode >= ACTION_FADER)
	{
		mode = (mididata2 % 12) + 24;
		if (mode < 28) mode += 12;
		return pickChord(mode, mididata2 / 12, threadId);
	}

	// Pedal = major when off, minor when on
	// Chord (PEDAL|SCALE) = 0 major, 1 minor, 2 dom7, etc
	if (mididata2 > 7)
	{
		if (mididata2 == 8) mididata2 = (mididata2 & 0x07) | 0x40;
		else mididata2 = (mididata2 & 0x07) | 0x80;
	}

	return eventChordTrigger(0, mididata2, threadId);
}




// =======================================
// The categories of assignable actions
// =======================================

static ACTIONCAT ActionCategory[] = {
	{chordHandler, ChordsHelp, ChordsStrs,					ACTIONPROP_THREADID | ACTIONPROP_SCALE_0 | MAPID_MIDICHORD, 0},
	{tempoHandler, TempoHelp, TempoStrs,			 		ACTIONPROP_THREADID | MAPID_TEMPO, 0},
	{playHandler, PlayHelp, PlayStrs,						ACTIONPROP_THREADID | MAPID_PLAY, 0},
	{transposeHandler, TransposeHelp, TransposeStrs,	ACTIONPROP_THREADID | ACTIONPROP_SCALE_0 | MAPID_TRANSPOSE, 0},
	{styleHandler, StylesHelp, StylesStrs,					ACTIONPROP_THREADID | ACTIONPROP_SCALE_0 | MAPID_STYLE, 0},
	{variationHandler, VarHelp, VarStrs,					ACTIONPROP_THREADID | ACTIONPROP_SCALE_0 | MAPID_VARIATION, 0},
	{patchHandler, PatchHelp, PatchStrs,					ACTIONPROP_THREADID | ACTIONPROP_SCALE_0 | ACTIONPROP_COMMON | MAPID_SOLOPATCH, 0},
	{sustainHandler, SustainHelp, SustainStrs,			ACTIONPROP_THREADID | MAPID_SUSTAIN, 0},
	{kitHandler, 0, CommonPatchStrs,							ACTIONPROP_THREADID | ACTIONPROP_SCALE_0 | MAPID_DRUMS, PLAYER_DRUMS},
	{bassHandler, BassHelp, BassStrs,						ACTIONPROP_THREADID | ACTIONPROP_SCALE_0 | ACTIONPROP_COMMON | MAPID_BASS, 0},
	{gtrHandler, 0, CommonPatchStrs,							ACTIONPROP_THREADID | ACTIONPROP_SCALE_0 | MAPID_GTR, PLAYER_GTR},
	{padHandler, PadHelp, PadStrs,							ACTIONPROP_THREADID | ACTIONPROP_SCALE_0 | ACTIONPROP_COMMON | MAPID_PAD, 0},
	{muteHandler, MuteHelp, MuteStrs,						ACTIONPROP_THREADID | MAPID_MUTE, 0},
	{reverbHandler, RevHelp, RevStrs,						ACTIONPROP_SCALE_0 | MAPID_REVERB, 0},
	{songHandler, SongHelp, SongStrs,						ACTIONPROP_THREADID | ACTIONPROP_SCALE_0 | MAPID_SONGSHEET, 0},
	{miscHandler, MiscHelp, MiscStrs,	 					ACTIONPROP_THREADID | MAPID_MISC, 0},
};

#define NUM_OF_ACTIONCATS	(sizeof(ActionCategory) / sizeof(ACTIONCAT))

#ifndef NO_MIDI_IN_SUPPORT

static void hide_value_ctl(register GUICTL *);

// Assignment of actions to notes (Command Key mode)
static unsigned char		CommandKeyAssigns[88*3] = {
MAPID_TEMPO,ACTION_DECBUTTON,0,MAPID_TEMPO,ACTION_INCBUTTON,0,MAPID_TEMPO,ACTION_DECBUTTON|ACTION_1,0,MAPID_TEMPO,ACTION_INCBUTTON|ACTION_1,0,MAPID_TEMPO,ACTION_INCBUTTON|ACTION_2,0,

MAPID_PLAY,ACTION_DECBUTTON|ACTION_1,0,MAPID_PLAY,ACTION_INCBUTTON|ACTION_1,0,MAPID_PLAY,ACTION_INCBUTTON|ACTION_2,0,MAPID_PLAY,ACTION_DECBUTTON,0,MAPID_PLAY,ACTION_INCBUTTON,0,

MAPID_TRANSPOSE,ACTION_DECBUTTON,0,MAPID_TRANSPOSE,ACTION_INCBUTTON,0,MAPID_TRANSPOSE,ACTION_DECBUTTON|ACTION_1,0,

MAPID_STYLE,ACTION_INCBUTTON|ACTION_1,0,MAPID_STYLE,ACTION_INCBUTTON|ACTION_2,0,MAPID_STYLE,ACTION_INCBUTTON|ACTION_3,0,

MAPID_SOLOPATCH,ACTION_INCBUTTON|ACTION_2,0,MAPID_SOLOPATCH,ACTION_DECBUTTON|ACTION_2,0,MAPID_SOLOPATCH,ACTION_ONOFFSWITCH|ACTION_2,0,MAPID_SOLOPATCH,ACTION_DECBUTTON|ACTION_3,0,
MAPID_SOLOPATCH,ACTION_INCBUTTON|ACTION_3,0,MAPID_SOLOPATCH,ACTION_ONOFFSWITCH|ACTION_3,0,MAPID_SOLOPATCH,ACTION_DECBUTTON|ACTION_1,0,MAPID_SOLOPATCH,ACTION_INCBUTTON|ACTION_1,0,

MAPID_BASS,ACTION_ONOFFSWITCH|ACTION_2,0,MAPID_BASS,ACTION_DECBUTTON|ACTION_3,0,MAPID_BASS,ACTION_INCBUTTON|ACTION_3,0,MAPID_BASS,ACTION_ONOFFSWITCH|ACTION_3,0,
MAPID_BASS,ACTION_DECBUTTON|ACTION_1,0,MAPID_BASS,ACTION_INCBUTTON|ACTION_1,0,MAPID_MUTE,ACTION_PEDAL|ACTION_1,0,

MAPID_GTR,ACTION_DECBUTTON|ACTION_3,0,MAPID_GTR,ACTION_INCBUTTON|ACTION_3,0,MAPID_GTR,ACTION_ONOFFSWITCH|ACTION_3,0,MAPID_GTR,ACTION_DECBUTTON|ACTION_1,0,
MAPID_GTR,ACTION_INCBUTTON|ACTION_1,0,MAPID_MUTE,ACTION_PEDAL|ACTION_2,0,

MAPID_DRUMS,ACTION_DECBUTTON|ACTION_3,0,MAPID_DRUMS,ACTION_INCBUTTON|ACTION_3,0,MAPID_DRUMS,ACTION_ONOFFSWITCH|ACTION_3,0,MAPID_DRUMS,ACTION_DECBUTTON|ACTION_1,0,
MAPID_DRUMS,ACTION_INCBUTTON|ACTION_1,0,MAPID_MUTE,ACTION_PEDAL,0,

MAPID_PAD,ACTION_DECBUTTON,0,MAPID_PAD,ACTION_INCBUTTON|ACTION_2, 0,MAPID_PAD,ACTION_INCBUTTON|ACTION_2|ACTION_1, 0,MAPID_PAD,ACTION_INCBUTTON|ACTION_2|ACTION_3, 0,MAPID_MUTE,ACTION_DECBUTTON|ACTION_3,0,

MAPID_MUTE,ACTION_DECBUTTON|ACTION_3|ACTION_2|ACTION_1,0,MAPID_MUTE,ACTION_INCBUTTON|ACTION_3|ACTION_2|ACTION_1,0,MAPID_MUTE,ACTION_ONOFFSWITCH|ACTION_3|ACTION_2|ACTION_1,0,
MAPID_MUTE,ACTION_PEDAL|ACTION_3|ACTION_2|ACTION_1,0,

MAPID_MISC,ACTION_INCBUTTON|ACTION_1,0,MAPID_MISC,ACTION_DECBUTTON|ACTION_1,0,

MAPID_VARIATION,ACTION_ONOFFSWITCH,0,MAPID_VARIATION,ACTION_ONOFFSWITCH|ACTION_1,0,MAPID_VARIATION,ACTION_ONOFFSWITCH|ACTION_2,0,MAPID_VARIATION,ACTION_DECBUTTON,0,
MAPID_VARIATION,ACTION_DECBUTTON|ACTION_3,0,MAPID_VARIATION,ACTION_INCBUTTON,0,MAPID_VARIATION,ACTION_INCBUTTON|ACTION_3,0,
MAPID_VARIATION,ACTION_DECBUTTON|ACTION_2,0,MAPID_VARIATION,ACTION_DECBUTTON|ACTION_3|ACTION_2,0,

MAPID_REVERB,ACTION_DECBUTTON,0,MAPID_REVERB,ACTION_INCBUTTON,0,MAPID_REVERB,ACTION_INCBUTTON|ACTION_2,0,MAPID_REVERB,ACTION_DECBUTTON|ACTION_2,0,

MAPID_SONGSHEET,ACTION_DECBUTTON,0,MAPID_SONGSHEET,ACTION_INCBUTTON,0,

0xff,0,0,0xff,0,0,0xff,0,0,0xff,0,0,0xff,0,0,0xff,0,0,
0xff,0,0,0xff,0,0,0xff,0,0,0xff,0,0,0xff,0,0,
0xff,0,0,0xff,0,0,0xff,0,0,0xff,0,0,0xff,0,0,0xff,0,0,0xff,0,0,0xff,0,0};

static unsigned char		CommandKeyChanges[88/8];

// MIDI Controller input
static pthread_t			MidiInThread;
void *						MidiInSigHandle;
static MIDIINSIPHON *	SiphonFunc;
static int					MidiEventHandle, MidiEventQueue;

// To smooth the attack of rolls
#define MIDIKEY_HISTORY	8
static unsigned char		PlayingKeys[MIDIKEY_HISTORY];
static uint32_t			PlayingTimes[MIDIKEY_HISTORY];

// For MIDI input viewer
static unsigned char *	PendingPtr;
static unsigned char *	MidiViewInPtr;
static unsigned char *	MidiViewCurrPtr;
static uint32_t			ViewTopY;
static uint32_t			ViewNumOfLines;
static unsigned char		TestNumTicks;
static unsigned char		TestNumBeats;
static unsigned char		TestDrawBeats;
static unsigned char		MidiMsg[3];

static unsigned short	ChansInUse;

// 0 to 15, or 16 for "Any"
unsigned char				MasterMidiChan = 16;

char							BassOct = 28;

// midi note # where your controller is split into a lower
// half that plays bass/chords, and an upper half
// that plays the currently selected Solo instrument.
// Only in effect if left hand bass not "off", or
// master chan == one of the robot or human chans
unsigned char				SplitPoint = 56;

// Error msgs
static const char			NoMidiInThreadMsg[] = "Can't create MIDI In thread!";
static const char			NoController[] = "Can't open your MIDI controller!";

#endif

// Assignment of actions to mouse buttons
static unsigned char		MouseAssigns[32*3] = {0xff,0,0,
MAPID_PLAY,ACTION_ONOFFSWITCH|ACTION_1,0,
MAPID_SUSTAIN,ACTION_PEDAL,0,
MAPID_TEMPO,ACTION_INCBUTTON,0,	// Mouse wheel up
MAPID_TEMPO,ACTION_DECBUTTON,0,	// Mouse wheel down
MAPID_MISC,ACTION_DECBUTTON,0,
MAPID_TRANSPOSE,ACTION_DECBUTTON,0,
MAPID_TRANSPOSE,ACTION_INCBUTTON,0,
MAPID_STYLE,ACTION_INCBUTTON|ACTION_1,0,
MAPID_STYLE,ACTION_INCBUTTON|ACTION_2,0,
MAPID_STYLE,ACTION_INCBUTTON|ACTION_3,0,
MAPID_SOLOPATCH,ACTION_ONOFFSWITCH|ACTION_2,0,
MAPID_SOLOPATCH,ACTION_DECBUTTON|ACTION_3,0,
MAPID_SOLOPATCH,ACTION_INCBUTTON|ACTION_3,0,
MAPID_SOLOPATCH,ACTION_ONOFFSWITCH|ACTION_3,0,
MAPID_BASS,ACTION_ONOFFSWITCH,0,
MAPID_PAD,ACTION_DECBUTTON,0,
MAPID_PAD,ACTION_INCBUTTON,0,
MAPID_PAD,ACTION_FADER+1,61,
MAPID_PAD,ACTION_FADER+1,16,
MAPID_VARIATION,ACTION_ONOFFSWITCH,0,
MAPID_VARIATION,ACTION_ONOFFSWITCH|ACTION_1,0,
MAPID_VARIATION,ACTION_ONOFFSWITCH|ACTION_2,0,
MAPID_VARIATION,ACTION_DECBUTTON,0,
MAPID_VARIATION,ACTION_DECBUTTON|ACTION_3,0,
MAPID_VARIATION,ACTION_INCBUTTON,0,
MAPID_VARIATION,ACTION_INCBUTTON|ACTION_3,0,
MAPID_VARIATION,ACTION_DECBUTTON|ACTION_2,0,
MAPID_VARIATION,ACTION_DECBUTTON|ACTION_3|ACTION_2,0,
MAPID_REVERB,ACTION_ONOFFSWITCH,0,
MAPID_SONGSHEET,ACTION_DECBUTTON,0,
MAPID_SONGSHEET,ACTION_INCBUTTON,0};

static unsigned char		MouseChanges[32/8];

// Choosing command assignment
#define ACTIONLIST_MOUSE		0
#define ACTIONLIST_PCKEY		1
#define ACTIONLIST_MIDIMSG		2
#define ACTIONLIST_MIDINOTE	3
unsigned char *			BytePtr;

static ACTIONINSTANCE *	SelActionEntry;
static ACTIONCAT *		SelCategory;
static ACTIONINSTANCE *	EntryList[2];
static uint32_t			EntryListSize[2];
static ACTIONINSTANCE	Msg;
static unsigned short	ActionWidth;
static unsigned short	NumOfActions;
static unsigned char		ActionListNum;

// For drum HH pedal
unsigned char				HHvalue;

const char HelpStr[] = "Help";
static const char HelpSubs[] = " the mouse button\0 the computer key\0 some MIDI button\0 a note on your controller";
static const char AsnFilenames[] = "mousemap\0pckeymap\0midimap\0notemap";

#ifndef NO_MIDI_IN_SUPPORT

static void * midiAssignSiphon(register void *, register unsigned char *);
static void midiViewClear(void);
static char * beginInfo(register char *);
static void * midiViewSiphon(register void *, register unsigned char *);
static void drawMidiEvts(GUIAPPHANDLE, GUICTL *, GUIAREA *);
static const char * getClockLabel(GUIAPPHANDLE, GUICTL *, char *);

static uint32_t ctl_set_msgtype(register GUICTL *);
static uint32_t ctl_set_msgid(register GUICTL *);
static uint32_t ctl_set_scale(register GUICTL *);
static uint32_t ctl_set_range(register GUICTL *);
static uint32_t ctl_set_low(register GUICTL *);
static uint32_t ctl_set_high(register GUICTL *);
static uint32_t ctl_asn(register GUICTL *);
static uint32_t ctl_assign_high(register GUICTL *);
static uint32_t ctl_set_midichan(register GUICTL *);
static const char * getMsgIdLabel(GUIAPPHANDLE, GUICTL *, char *);
static const char * getDataLabel(GUIAPPHANDLE, GUICTL *, char *);
static void init_midiview(register unsigned char);
static int handleMidiView(register GUIMSG *);
static void retToMidiMsgAsn(void);

static const char	ChanStr[] = "MIDI Channel";
static const char	ClearScreen[] = "Clear\nscreen";
static const char PgmTypes[] = "style\0songsheet\0instrument\0pad\0bass\0kit";
static const char DefCtlMsgs[] = "Tempo\0Volume\0Autostart\0Sustain\0Legato\0Volume Boost\0Play On/Off\0Next Variation\0Reverb On/Off\0Reverb Volume\0Transpose Up\0Transpose Down\0Panic\0<unused Control>";
static const char IndividualCtlStrs[] = "Bank select\0Volume\0Sustain\0Legato\0";
static const char	AssignStr[] = "Assign";
static const char	NumStr[] = "%3u";

static const char		StatStrs[] = {0,'N','o','t','e',0,
'A','f','t','e','r','t','o','u','c','h',0,
'C','o','n','t','r','o','l','l','e','r',0,
'P','r','o','g','r','a','m',0,
'P','r','e','s','s','u','r','e',0,
'W','h','e','e','l',0,
'S','y','s','t','e','m',0,
0};

#define STATSTR_CONTINUE	6
static const char		SysStrs[] = "Start\0Continue\0Stop\0Reset\0";
#define NUM_OF_SYS_STAT	4
static const unsigned char	SysStatus[] = {0xfa, 0xfb, 0xfc, 0xff, 0x00};

static void midiPlay(register GUICTL * ctl)
{
	play_beat(1, GUITHREADID);
}

static void returnToSetup(register GUICTL * ctl)
{
	midiviewDone();
	doSetupScreen();
}

#define MIDIVIEW_PLAY	0
#define MIDIVIEW_CLOCK	1
#define MIDIVIEW_NAMES	2
#define MIDIVIEW_CLEAR	3
#define MIDIVIEW_OK		4
#define MIDIVIEW_LIST	5

// MIDI Event display
static GUICTL		MidiViewCtls[] = {
 	{.Type=CTLTYPE_PUSH,		.Ptr=midiPlay,			.BtnLabel=play_button_label,	.Y=1, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_GET_LABEL},
 	{.Type=CTLTYPE_STATIC,								.BtnLabel=getClockLabel,.Y=1, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_GET_LABEL, .Flags.Local=CTLFLAG_LEFTTEXT},
  	{.Type=CTLTYPE_CHECK,								.Label="Controller names",	.Y=1, .Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_PUSH,		.Ptr=midiViewClear,	.Label=&ClearScreen[0],	.Y=1, .Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_PUSH,		.Ptr=returnToSetup,	.Label="Return to\nSetup",	.Y=1, .Attrib.NumOfLabels=1, .Flags.Local=CTLFLAG_ESC},
 	{.Type=CTLTYPE_AREA,									.AreaDraw=drawMidiEvts,	.Y=2,	.Flags.Global=CTLGLOBAL_APPUPDATE},
 	{.Type=CTLTYPE_END},
};

#define MAPCTL_STAT			0
#define MAPCTL_TYPE			1
#define MAPCTL_CHAN			2
#define MAPCTL_ASN			3
#define MAPCTL_RANGE			4
#define MAPCTL_SCALE			5
#define MAPCTL_LOW_LABEL	6
#define MAPCTL_LOW			7
#define MAPCTL_HIGH_LABEL	8
#define MAPCTL_HIGH			9
#define MAPCTL_HIGH_ASN		10
#define MAPCTL_MIDIVIEW		11
#define MAPCTL_END			12

static GUICTL		MidiMsgGuiCtls[] = {
 	{.Type=CTLTYPE_ARROWS,					.Ptr=ctl_set_msgtype,	.Label=&StatStrs[6],		.Y=0, .Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_PUSH,						.Ptr=ctl_set_msgid,		.BtnLabel=getMsgIdLabel,.Y=0, .Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_NOPADDING|CTLGLOBAL_GET_LABEL},
  	{.Type=CTLTYPE_ARROWS,					.Ptr=ctl_set_midichan,	.Label=ChanStr,			.Y=0, .Attrib.NumOfLabels=16, .MinValue=1, .Flags.Local=CTLFLAG_NOSTRINGS},
 	{.Type=CTLTYPE_PUSH|CTLTYPE_HIDE,	.Ptr=ctl_asn,				.Label=AssignStr,			.Y=0, .Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_NOPADDING},
  	{.Type=CTLTYPE_CHECK,					.Ptr=ctl_set_range,		.Label="Range limit",	.Y=1, .Attrib.NumOfLabels=1},
  	{.Type=CTLTYPE_CHECK,					.Ptr=ctl_set_scale,		.Label="Full motion slider",	.Y=1, .Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_STATIC,													.Label="Low:",				.Y=2, .Attrib.NumOfLabels=1},
  	{.Type=CTLTYPE_PUSH,						.Ptr=ctl_set_low,			.BtnLabel=getDataLabel,	.Y=2, .Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_NOPADDING|CTLGLOBAL_GET_LABEL},
 	{.Type=CTLTYPE_STATIC,													.Label="High:",			.Y=2, .Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_PUSH,						.Ptr=ctl_set_high,		.BtnLabel=getDataLabel,	.Y=2, .Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_NOPADDING|CTLGLOBAL_GET_LABEL},
 	{.Type=CTLTYPE_PUSH|CTLTYPE_HIDE,	.Ptr=ctl_assign_high,	.Label=AssignStr,			.Y=2, .Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Type=CTLTYPE_AREA,						.AreaDraw=drawMidiEvts,	.Y=2,	.Flags.Global=CTLGLOBAL_APPUPDATE},
 	{.Type=CTLTYPE_END},
};

#endif


static void common_gui(void);
static void select_first_cat(void);
static ACTIONINSTANCE * getExistingAsn(register unsigned char, register unsigned char);
static void format_asn_desc(void);
static unsigned char alreadyAssigned(register ACTIONINSTANCE *);






void * isActionSelected(void)
{
	return SelActionEntry;
}



static void initActionsList(void)
{
	EntryList[0] = 0;
	EntryListSize[0] = 0;
	memset(MouseChanges, 0, sizeof(MouseChanges));

#ifndef NO_MIDI_IN_SUPPORT
	EntryList[1] = 0;
	EntryListSize[1] = 0;
	memset(CommandKeyChanges, 0, sizeof(CommandKeyChanges));
#endif
}



static void midi_reset(void)
{
	clearChord(0 | GUITHREADID);
	HHvalue = 0;
#ifndef NO_MIDI_IN_SUPPORT
	endCmdMode();
	memset(&PlayingKeys[0], 0, sizeof(PlayingKeys));
	memset(&PlayingTimes[0], 0, sizeof(PlayingTimes));
	SiphonFunc = 0;
	SoundDev[DEVNUM_MIDIIN].Lock = 0;
#endif
}


/**************** initMidiInVars() ****************
 * Called at program start to init
 * global vars.
 */

void initMidiInVars(void)
{
	midi_reset();

	initActionsList();

#ifndef NO_MIDI_IN_SUPPORT
	MidiInThread = 0;
	CmdSwitchNote = 0x80|21;
#endif

	select_first_cat();
}





/******************* freeActionAssigns() *****************
 * Frees the 3 ACTIONINSTANCE lists. Called when app
 * is terminating.
 */

void freeActionAssigns(void)
{
	register unsigned int	i;
	register ACTIONINSTANCE *	entry;

	for (i=0; i<2; i++)
	{
		if ((entry = EntryList[i])) free(entry);
	}

	initActionsList();
}











/***************** makeHelpStr() ****************
 * Formats a help description (in TempBuffer) about
 * the currently selected action.
 */

int makeAsnHelpStr(void)
{
	register const char *	from;

	{
	register const char *	src;
	register unsigned char	total, flag;

	if (!(from = SelCategory->HelpStrs)) goto com;
	while (*from++);
	src = SelCategory->Desc;
	flag = 1;

again:
	total = (unsigned char)(*src++);
	if (src != &MuteStrs[1]) total &= 0x0F;
	while (total--)
	{
		if ((unsigned char)(*src++) == Msg.Action) goto found;
		while (*from++);
	}

	if (flag && (SelCategory->Properties & ACTIONPROP_COMMON))
	{
com:	from = &CommonPatchHelp[0];
		src = &CommonPatchStrs[0];
		flag = 0;
		goto again;
	}

	*TempBuffer = 0;
	return 0;
	}

found:
	{
	register char *			to;
	register unsigned char	count;

	to = (char *)TempBuffer;
	while ((count = (unsigned char)*from++))
	{
		register const char *	insert;

		if (count < ' ')
		{
			insert = SingleSubs;
			if (!(--count))
				insert = SelCategory->HelpStrs ? SelCategory->HelpStrs : getMusicianName(SelCategory->RobotNum);
			else while (--count)
			{
				while (*insert++);
			}
			goto copy;
		}
		if (count == '@')
		{
			count = *from++ - '0';
			if (*from >= '0' && *from <= '9')
			{
				count *= 10;
				count += (*from++ - '0');
			}

			insert = &HelpSubs[0];
			while (--count)
			{
				while (*insert++);
				while (*insert++);
				while (*insert++);
			}

			count = ActionListNum;
			while (count--)
			{
				while (*insert++);
			}

copy:		while ((*to++ = *insert++))
			;
			to--;
		}
		else
			*to++ = (char)count;
	}
	*to++ = '.';
	*to = 0;
	}

	return 1;
}




/******************* allocEntry() *****************
 * Allocates room for one more ACTIONINSTANCE in the
 * specified list.
 *
 * listNum = ACTIONLIST_MIDIMSG for MIDI msgs,
 * ACTIONLIST_PCKEY for PC key.
 */

static ACTIONINSTANCE * allocEntry(register unsigned char listNum)
{
	register unsigned char *	entry;
	register uint32_t				size;

	// We allocate blocks of 20 ACTIONINSTANCEs at a time, to avoid having
	// to realloc the array too often. Since the user typically configures
	// the "Setup -> Commands" once only, this potential waste of mem is
	// negligible
	listNum -= ACTIONLIST_PCKEY;
	size = EntryListSize[listNum];
	if (!(entry = (unsigned char *)EntryList[listNum]) || !(size % (sizeof(ACTIONINSTANCE) * 20)))
	{
		if (!(entry = malloc(size + (sizeof(ACTIONINSTANCE) * 20))))
			show_msgbox(&NoMemStr[0]);
		else
		{
			if (EntryList[listNum])
			{
				memcpy(entry, EntryList[listNum], size);
				free(EntryList[listNum]);
			}

			EntryList[listNum] = (ACTIONINSTANCE *)entry;
		}
	}

	if (entry)
	{
		entry += size;
		EntryListSize[listNum] += sizeof(ACTIONINSTANCE);
	}
	return (ACTIONINSTANCE *)entry;
}




/******************* deleteEntry() *****************
 * Deletes the specified ACTIONINSTANCE from the
 * current list.
 */

static void deleteEntry(register ACTIONINSTANCE * entry)
{
	register uint32_t		size, listNum;

	listNum = ActionListNum - ACTIONLIST_PCKEY;

	if ((char *)entry >= (char *)EntryList[listNum] && (char *)entry < (char *)EntryList[listNum] + EntryListSize[listNum])
	{
		EntryListSize[listNum] -= sizeof(ACTIONINSTANCE);
		if ((size = EntryListSize[listNum]))
		{
			memmove(entry, entry + 1, size - ((char *)entry - (char *)EntryList[listNum]));
			if (!(size % (sizeof(ACTIONINSTANCE) * 20)))
			{
				if (!(entry = malloc(size + (sizeof(ACTIONINSTANCE) * 20))))
					show_msgbox(0);
				else
				{
					if (EntryList[listNum])
					{
						memcpy(entry, EntryList[listNum], size);
						free(EntryList[listNum]);
					}

					EntryList[listNum] = (ACTIONINSTANCE *)entry;
				}
			}
		}
	}
}

static void markChanged(ACTIONINSTANCE * entry)
{
	register unsigned char *	start;
	register unsigned char *	bits;
	register uint32_t				index;

	start = &MouseAssigns[0];
	bits = &MouseChanges[0];
#ifndef NO_MIDI_IN_SUPPORT
	if ((unsigned char *)entry < start || (unsigned char *)entry >= &start[32*3])
	{
		start = &CommandKeyAssigns[0];
		bits = &CommandKeyChanges[0];
	}
#endif
	index = ((unsigned char *)entry - start) / 3;
	bits += (index / 8);
	*bits |= (0x01 << (index % 8));
}

void eraseSelAction(void)
{
	register ACTIONINSTANCE *	entry;

	if ((entry = getExistingAsn(Msg.CatIndex, Msg.Action)))
	{
		if (ActionListNum == ACTIONLIST_MOUSE
#ifndef NO_MIDI_IN_SUPPORT
		 || ActionListNum == ACTIONLIST_MIDINOTE
#endif
		 )
		{
			entry->CatIndex = 0xFF;
			markChanged(entry);
		}
		else
			deleteEntry(entry);

		// Indicate this list must be saved to disk, sometime before
		// the app terminates
		SaveConfigFlag |= (0x01 << ActionListNum);
		resetActionsList();
		clearMainWindow();
	}
}




static unsigned char * copy_8(register unsigned char * buffer, register unsigned char * entry, register unsigned char bits)
{
	unsigned char *	start;

	start = &MouseAssigns[0];
#ifndef NO_MIDI_IN_SUPPORT
	if (entry < start || entry >= &start[32*3]) start = &CommandKeyAssigns[0];
#endif

	while (bits)
	{
		if (bits & 0x01)
		{
			*buffer++ = (entry - start) / 3;
			memcpy(buffer, entry, 3);
			buffer[0] = ActionCategory[entry[0]].Properties & ACTIONPROP_ID_MASK;
			buffer += 3;
		}

		bits >>= 1;
		entry += 3;
	}

	return buffer;
}

/******************* saveCmdAssigns() *****************
 * Saves the 3 ACTIONINSTANCE lists to their config
 * files on disk, if the user has changed them.
 */

 void saveCmdAssigns(void)
{
	if (SaveConfigFlag & (SAVECONFIG_KEYMAP|SAVECONFIG_MIDIMAP|SAVECONFIG_MOUSEMAP))
	{
		register const char *		fn;
		register unsigned char		i;

		fn = &AsnFilenames[0];
#ifndef NO_MIDI_IN_SUPPORT
		for (i=0; i < 4; i++)
#else
		for (i=0; i < 2; i++)
#endif
		{
			if ((SaveConfigFlag & (0x01 << i)))
			{
				register int		fh;
				uint32_t				size;
				register unsigned char *	ptr;

				ptr = TempBuffer;

#ifndef NO_MIDI_IN_SUPPORT
				// Command notes use the CommandKeyAssigns[] array
				if (i == ACTIONLIST_MIDINOTE)
				{
					for (size=0; size < 88/8; size++)
						ptr = copy_8(ptr, &CommandKeyAssigns[size * 8 * 3], CommandKeyChanges[size]);
				}
				else
#endif
				// Mouse uses the MouseAssigns[] array
				if (!i) // ACTIONLIST_MOUSE
				{
					for (size=0; size < 32/8; size++)
						ptr = copy_8(ptr, &MouseAssigns[size * 8 * 3], MouseChanges[size]);
				}

				else
				{
					register ACTIONINSTANCE *	entry;

					size = EntryListSize[i - ACTIONLIST_PCKEY];

					// Make a temp copy and store the ACTIONCAT MAPID # in LastValue
					memcpy(TempBuffer, EntryList[i - ACTIONLIST_PCKEY], size);
					entry = (ACTIONINSTANCE *)TempBuffer;
					do
					{
						entry->LastValue = ActionCategory[entry->CatIndex].Properties & ACTIONPROP_ID_MASK;
					} while (++entry < (ACTIONINSTANCE *)(TempBuffer + size));
					ptr = (unsigned char *)entry;
				}

				// If no assigns, del the disk file
//				if (ptr == TempBuffer) goto del;

				// Create the file
				if ((fh = create_config_file(fn)) == -1)
				{
err:				show_msgbox(fn);
//					format_file_err((char *)&GuiBuffer[0], size, 2);
//del:				unlink((char *)&GuiBuffer[0]);
				}
				else
				{
					size = write(fh, TempBuffer, size);
					close(fh);
					if (size != (ptr - TempBuffer)) goto err;

					// Indicate this list is now saved to disk
					SaveConfigFlag &= ~(0x01 << i);
				}
			}

			while (*fn++);
		}
	}
}





static ACTIONCAT * getActionCatById(register unsigned char id)
{
	register ACTIONCAT *	actionCat;

	actionCat = &ActionCategory[0];
	do
	{
		if ((actionCat->Properties & ACTIONPROP_ID_MASK) == id) return actionCat;
	} while (++actionCat < &ActionCategory[NUM_OF_ACTIONCATS]);
	return 0;
}




#ifndef NO_MIDI_IN_SUPPORT

const char * getCmdKeyStr(void)
{
	return &DescBuf[0];
}



static void checkForSwitchNote(register unsigned char * ptr)
{
	if (ptr[1] == CmdSwitchNote)
		GuiWinSignal(GuiApp, 0, SIGNALMAIN_CMDSWITCHERR);
	else
	{
		updateMidiView();
		MidiViewCurrPtr = ptr;
		MidiMsg[0] = ptr[0];
		MidiMsg[1] = ptr[1];
		MidiMsg[2] = ptr[2];
		ctl_set_msgtype(0);
	}
}
#endif




/******************* loadCmdAssigns() *****************
 * Loads the 3 ACTIONINSTANCE lists (MIDI, mouse, PC keys)
 * from their config files on disk, if they exist.
 */

void loadCmdAssigns(char * path, uint32_t len)
{
	register const char *		fn;
	register unsigned char		i;

	fn = &AsnFilenames[0];
	i = 0;

	do
	{
		register uint32_t				size;

		// Open the file if it exists
		if ((size = load_config_file(fn)))
		{
			// Transfer settings to globals. Also check file integrity
			// since it's a binary format
			if (!i || i == ACTIONLIST_MIDINOTE)
			{
				register unsigned char *	from;
				register unsigned char *	to;

				if (size % 4) goto corrupt;
				from = TempBuffer;
				do
				{
					ACTIONCAT *		category;

					if (i)
					{
						if (from[0] >= 88) goto corrupt;
						to = &CommandKeyAssigns[0];
					}
					else
					{
						if (from[0] >= 32) goto corrupt;
						to = &MouseAssigns[0];
					}
					to += (*from++ * 3);
					if ((category = getActionCatById(from[0])))
					{
						from[0] = (category - &ActionCategory[0]);
						if (memcmp(to, from, 3))
						{
							memcpy(to, from, 3);
							markChanged((ACTIONINSTANCE *)to);
						}
					}
					from += 3;
				}
				while ((size -= 4));
			}
			else
			{
				if (size % sizeof(ACTIONINSTANCE))
				{
corrupt:			sprintf((char *)TempBuffer, "%s is corrupt.", fn);
					show_msgbox((char *)TempBuffer);
				}
				else
				{
					// Resolve the "Id" numbers to 0-based "Index". Get rid of any
					// obsolete actions
					{
					register ACTIONINSTANCE *	entry;
					register ACTIONINSTANCE *	to;

					to = entry = (ACTIONINSTANCE *)TempBuffer;
					do
					{
						ACTIONCAT *		category;

						if ((category = getActionCatById(entry->LastValue)))
						{
							entry->LastValue = 0;
							entry->CatIndex = category - &ActionCategory[0];
							memcpy(to++, entry, sizeof(ACTIONINSTANCE));
						}
					} while (++entry < (ACTIONINSTANCE *)(TempBuffer + size));

					size = (char *)to - (char *)TempBuffer;
					}

					// Round up to 20 entries. We keep this extra buffering
					// around to minimize mem allocs for when user adds more
					// assigns
					{
					register uint32_t		blocks, listNum;

					listNum = i - ACTIONLIST_PCKEY;
					EntryListSize[listNum] = size;
					blocks = size / sizeof(ACTIONINSTANCE);
					size = blocks / 20;
					if (blocks % 20) size++;
					if (!(EntryList[listNum] = malloc(size * (sizeof(ACTIONINSTANCE) * 20))))
						show_msgbox(0);
					else
						memcpy(EntryList[listNum], TempBuffer, EntryListSize[listNum]);
					}
				}
			}
		}

		while (*fn++);
#ifndef NO_MIDI_IN_SUPPORT
	} while (++i < 4);
#else
	} while (++i < 2);
#endif
	// Resolve the "Id" numbers to 0-based "Index". Get rid of any
	// obsolete actions
	{
	register unsigned char *	cmdKey;
	register unsigned char *	endptr;

	cmdKey = &MouseAssigns[0];
	endptr = &cmdKey[32*3];
#ifndef NO_MIDI_IN_SUPPORT
repeat:
#endif
	do
	{
		ACTIONCAT *		category;

		if ((category = getActionCatById(cmdKey[0])))
			cmdKey[0] = category - &ActionCategory[0];
		else
			cmdKey[0] = 0xFF;
		cmdKey += 3;
	} while (cmdKey < endptr);

#ifndef NO_MIDI_IN_SUPPORT
	if (endptr == &MouseAssigns[32 * 3])
	{
		cmdKey = &CommandKeyAssigns[0];
		endptr = &cmdKey[88*3];
		goto repeat;
	}
#endif
	}
}




/********************* makeCmdStr() ****************
 * Formats a printable string of the category and
 * action name.
 */

static char * makeCmdStr(ACTIONCAT * category, char * dest, register unsigned char action)
{
	register const char *	src;
	register const char *	labels;
	unsigned char				total;

	src = category->HelpStrs ? category->HelpStrs : getMusicianName(category->RobotNum);
	while ((*dest++ = *src++));
	dest[-1] = ' ';

	action &= ~(ACTION_RANGELIMIT|ACTION_SCALE);
	src = category->Desc;
again:
	total = (unsigned char)(*src++);
	if (src != &MuteStrs[1]) total &= 0x0F;
	labels = src + total;
	while (total--)
	{
		if ((unsigned char)(*src++) == action)
		{
			while ((*dest++ = (char)(*labels++)));
			dest--;
			goto out;
		}

		while (*labels++);
	}

	if (category && (category->Properties & ACTIONPROP_COMMON))
	{
		src = &CommonPatchStrs[0];
		category = 0;
		goto again;
	}

	*dest = 0;
out:
	return dest;
}





/**************** chooseActionsList() ****************
 * Sets which action list is currently displayed
 * and edited in "Setup -> Commands" (ie, actions
 * for midi, mouse, or PC keys).
 *
 * listNum = ACTIONLIST_MOUSE for mouse btn,
 * ACTIONLIST_PCKEY for Pc keyboard, ACTIONLIST_MIDIMSG
 * for a non-note Midi evt. ACTIONLIST_MIDINOTE for a
 * "Command Key Mode" behavior.
 *
 * Sets "NumOfActions", "ActionListNum", and
 * "SelCategory" globals.
 */

void chooseActionsList(register unsigned char listNum)
{
	register ACTIONCAT *		category;
	register unsigned char	total;

	// Save which list we're configuring (Mouse=0, Pc Key=1, Midi Ctrl=2, MIDI note=3)
	ActionListNum = listNum;

	// Count how many actions this list offers (count category headings too)
	SelCategory = category = &ActionCategory[0];
	NumOfActions = NUM_OF_ACTIONCATS;
	do
	{
		total = *((unsigned char *)category->Desc);
		if (category->Desc != &MuteStrs[0])
		{
			if (listNum == ACTIONLIST_PCKEY) total >>= 4;
			total &= 0x0f;
		}

		NumOfActions += total;
		if (category->Properties & ACTIONPROP_COMMON)
		{
			total = CommonPatchStrs[0];
			if (listNum == ACTIONLIST_PCKEY) total >>= 4;
			total &= 0x0f;
			NumOfActions += total;
		}
	} while (++category < &ActionCategory[NUM_OF_ACTIONCATS]);
}






static void storeEntry(void)
{
	memcpy(SelActionEntry, &Msg, sizeof(ACTIONINSTANCE));

	// Mark this list for saving at app terminate
	SaveConfigFlag |= (0x01 << ActionListNum);
}

















// ============================
// User assignment of some BackupBand action to
// a specific Midi message.
// ============================

#ifndef NO_MIDI_IN_SUPPORT

static void handle_sysctl(register GUIMSG * msg)
{
	register GUICTL *		ctl;

	ctl = msg->Mouse.SelectedCtl;
	if (ctl->Flags.Global & CTLGLOBAL_PRESET)
	{
		if (ctl->PresetId == GUIBTN_OK)
		{
ok:		if (List.CurrItemNum >= NUM_OF_SYS_STAT) goto out;
			*BytePtr = SysStatus[List.CurrItemNum];
		}

		retToMidiMsgAsn();
	}
	else if (!msg->Mouse.ListAction) goto ok;
out:
	return;
}

static GUILIST * realtimeDrawFunc(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app)
	{
		register const char *	str;

		str = &SysStrs[0];
		do
		{
			if (GuiListDrawItem(app, ctl, area, str)) break;
			while (*str++);
		} while (str[0]);
	}

	return &List;
}

static GUIFUNCS SysGuiFuncs = {dummy_drawing,
handle_sysctl,
dummy_keypress,
0};

static void doSys(void)
{
	midiviewDone();
	doPickItemDlg(realtimeDrawFunc, &SysGuiFuncs);
	headingCopyTo( "Pick the desired MIDI message", 0);
	headingShow(GUICOLOR_GOLD);
	List.NumOfItems = NUM_OF_SYS_STAT;
	List.ColumnWidth = GuiTextWidth(MainWin, &SysStrs[STATSTR_CONTINUE]);
}





static const char * getSysLabel(register unsigned char status)
{
	register const unsigned char *	i;
	register const char *		ptr;

	i = SysStatus;
	ptr = &SysStrs[0];

	while (*i && *i != status)
	{
		while (*ptr++);
		i++;
	}

	return ptr;
}




static const char * get_status_str(register unsigned char stat)
{
	register const char *	ptr;

	ptr = &StatStrs[0];
	stat = (stat >> 4) - 8;
	while (stat--) { while(*ptr++); };
	return ptr;
}

static void show_or_hide(register GUICTL * ctl, register unsigned char flag)
{
	GuiCtlShow(GuiApp, MainWin, ctl, flag);
}

static void show_midictls(void)
{
	register unsigned char	i;

	// "Range Limit" is shown only if a Pgm Change or Chan Pressure msg
	i = (Msg.Midi.Status >= 0xC0 && Msg.Midi.Status < 0xE0) ? 0 : CTLTYPE_HIDE;
	show_or_hide(&MidiMsgGuiCtls[MAPCTL_RANGE], i);
	if (i) Msg.Action &= ~ACTION_RANGELIMIT;

	// "High" and "Low" are shown only if "Range Limit" is chosen, or if Realtime
	if (Msg.Midi.Status >= 0xF0)
	{
		i = 0;

		// For PEDAL, 2 realtime can be entered for the two
		// states
		if ((Msg.Action & 0x03) == ACTION_PEDAL) goto ped;
	}
	else
	{
		i = (Msg.Action & ACTION_RANGELIMIT) ? 0 : CTLTYPE_HIDE;
ped:	show_or_hide(&MidiMsgGuiCtls[MAPCTL_HIGH], i);
		show_or_hide(&MidiMsgGuiCtls[MAPCTL_HIGH_LABEL], i);
	}
	show_or_hide(&MidiMsgGuiCtls[MAPCTL_LOW], i);
	show_or_hide(&MidiMsgGuiCtls[MAPCTL_LOW_LABEL], i);

	// Don't show the "Assign" buttons until user clicks a msg in midiview
	if (!i && !MidiMsg[0]) i = CTLTYPE_HIDE;
	show_or_hide(&MidiMsgGuiCtls[MAPCTL_HIGH_ASN], i);
	show_or_hide(&MidiMsgGuiCtls[MAPCTL_ASN], (MidiMsg[0] ? 0 : CTLTYPE_HIDE));

	// Realtime don't have a chan
	i = (Msg.Midi.Status >= 0xF0) ? CTLTYPE_HIDE : 0;
	if (!i && (Msg.Midi.Status & 0x0F) != MidiMsgGuiCtls[MAPCTL_CHAN].Attrib.Value)
	{
		GuiCtlArrowsInit(&MidiMsgGuiCtls[MAPCTL_CHAN], Msg.Midi.Status & 0x0F);
		GuiCtlUpdate(GuiApp, MainWin, &MidiMsgGuiCtls[MAPCTL_CHAN], 0, 0);
	}
	show_or_hide(&MidiMsgGuiCtls[MAPCTL_CHAN], i);

	// Only aft/controller use the midi Id
	show_or_hide(&MidiMsgGuiCtls[MAPCTL_TYPE], Msg.Midi.Status >= 0xC0 ? CTLTYPE_HIDE : 0);

	// "Full motion" is for only actions set by a fader
	show_or_hide(&MidiMsgGuiCtls[MAPCTL_SCALE], Msg.Action < ACTION_FADER ? CTLTYPE_HIDE : 0);
}



static void ctl_update_scale(void)
{
	MidiMsgGuiCtls[MAPCTL_SCALE].Attrib.Value = (Msg.Action & ACTION_SCALE) ? 1 : 0;
	GuiCtlUpdate(GuiApp, MainWin, &MidiMsgGuiCtls[MAPCTL_SCALE], 0, 0);
}

static uint32_t ctl_set_scale(register GUICTL * ctl)
{
	Msg.Action ^= ACTION_SCALE;
	ctl_update_scale();
	show_midictls();
	return (uint32_t)-2;
}

/*************** initMidiHighLow() ******************
 * Initializes the Low/High fields of a Program Change
 * or Chan Pressure.
 */

static void initMidiHighLow(register unsigned char status)
{
	Msg.Midi.High = Msg.Midi.Low = 0;
	if (status >= 0xC0 && status <= 0xDF)
	{
//		Msg.Midi.Low = 0;
		Msg.Midi.High = 127;
	}
}

static void ctl_update_range(void)
{
	if ((MidiMsgGuiCtls[MAPCTL_RANGE].Attrib.Value = (Msg.Action & ACTION_RANGELIMIT) ? 1 : 0))
		initMidiHighLow(Msg.Midi.Status);
	GuiCtlUpdate(GuiApp, MainWin, &MidiMsgGuiCtls[MAPCTL_RANGE], 0, 0);
}

static uint32_t ctl_set_range(register GUICTL * ctl)
{
	Msg.Action ^= ACTION_RANGELIMIT;
	ctl_update_range();
	show_midictls();
	return (uint32_t)-2;
}

/******************* ctl_set_msgtype() *****************
 * Called when user changes the "Message type" (ie,
 * Controller, Aftertouch, Program, etc).
 */

static uint32_t ctl_set_msgtype(register GUICTL * ctl)
{
	register unsigned char	status, hi;

	hi = (Msg.Action < ACTION_FADER) ? 0xF0 : 0xE0;

	if (ctl)
	{
		status = Msg.Midi.Status & 0xf0;
		if (ctl->Flags.Local & CTLFLAG_DOWN_SELECT)
		{
			if (status <= 0xB0) goto ignore;
			status -= 16;
		}
		else
		{
			if (status >= hi) goto ignore;
			status += 16;
		}

		initMidiHighLow(status);
		MidiMsg[1] = Msg.Midi.Id;

		// If not realtime, then we have a MIDI chan
		if (status < 0xf0) status |= MidiMsgGuiCtls[MAPCTL_CHAN].Attrib.Value;
	}
	else
	{
		status = MidiMsg[0];
		initMidiHighLow(status);

		// If a realtime msg, then set status to F0, and store
		// the specific realtime in Id
		if (status > 0xF0)
		{
			MidiMsg[1] = status;
			status = 0xf0;
		}

		// Hide the Assign btns until another msg selected by user
		MidiMsg[0] = 0;
	}

//	if (Msg.Midi.Status != status || Msg.Midi.Id != MidiMsg[1])
	{
		Msg.Midi.Status = status;
		Msg.Midi.Id = MidiMsg[1];

		MidiMsgGuiCtls[MAPCTL_STAT].Flags.Local &= ~(CTLFLAG_NO_UP|CTLFLAG_NO_DOWN);
		status &= 0xf0;
		if (status <= 0xB0) MidiMsgGuiCtls[MAPCTL_STAT].Flags.Local |= CTLFLAG_NO_DOWN;
		if (status >= hi) MidiMsgGuiCtls[MAPCTL_STAT].Flags.Local |= CTLFLAG_NO_UP;

		MidiMsgGuiCtls[MAPCTL_STAT].Label = get_status_str(status) - 1;

		show_midictls();
		GuiCtlUpdate(GuiApp, MainWin, &MidiMsgGuiCtls[MAPCTL_STAT], 0, 0);
	}
ignore:
	return (uint32_t)-2;
}

/******************* ctl_asn() *****************
 * Called when user clicks the "Assign" button for
 * "Message type".
 */

static uint32_t ctl_asn(register GUICTL * ctl)
{
	return ctl_set_msgtype(0);
}

/******************* ctl_set_midichan() *****************
 * Called when user changes the "MIDI Channel".
 */

static uint32_t ctl_set_midichan(register GUICTL * ctl)
{
	if (GuiCtlArrowsValue(GuiApp, ctl))
	{
		Msg.Midi.Status = (Msg.Midi.Status & 0xF0) | ctl->Attrib.Value;
		show_midictls();
		return 1;
	}
	return (uint32_t)-2;
}

/******************* ctl_set_low() *****************
 * Called when user changes the "Low" limit. This is
 * applicable only for an "Action" id OR'd with
 * ACTION_RANGELIMIT, or realtime.
 */

static void storeMsgVal(register GUINUMPAD * numpad)
{
	if (numpad && numpad->Value < 128) *BytePtr = (unsigned char)numpad->Value;
	retToMidiMsgAsn();
}

static uint32_t ctl_set_low(register GUICTL * ctl)
{
	midiviewDone();
	BytePtr = &Msg.Midi.Low;
	if (Msg.Midi.Status >= 0xF0)
		doSys();
	else
		doNumeric(storeMsgVal, 0, 0);
	return (uint32_t)-2;
}

static const char * getDataLabel(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	if (app)
	{
		register unsigned char value;

		value = (ctl == &MidiMsgGuiCtls[MAPCTL_LOW] ? Msg.Midi.Low : Msg.Midi.High);
		if (Msg.Midi.Status < 0xF0)
			sprintf(buffer, NumStr, value);
		else
			buffer = (char *)getSysLabel(value);
		return buffer;
	}

	return &SysStrs[STATSTR_CONTINUE];
}

/******************* ctl_set_high() *****************
 * Called when user changes the "High" limit. This is
 * applicable only for an "Action" id OR'd with
 * ACTION_RANGELIMIT.
 */

static uint32_t ctl_set_high(register GUICTL * ctl)
{
	midiviewDone();
	BytePtr = &Msg.Midi.High;
	if (Msg.Midi.Status >= 0xF0)
		doSys();
	else
		doNumeric(storeMsgVal, 0, 0);
	return (uint32_t)-2;
}

/******************* ctl_set_msgid() *****************
 * Called when user changes the "MIDI message id". This is
 * applicable only to note, aftertouch, and controller msgs.
 */

static uint32_t ctl_set_msgid(register GUICTL * ctl)
{
	midiviewDone();
	BytePtr = &Msg.Midi.Id;
//	if (Msg.Midi.Status < 0xB0)
//		getNoteNum();
//	else
		doNumeric(storeMsgVal, 0, 0);
	return (uint32_t)-2;
}

static const char * getMsgIdLabel(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	if (!app)
		numToPitch(buffer, 1);
	else if (Msg.Midi.Status >= 0xB0)
		sprintf(buffer, NumStr, Msg.Midi.Id);
	else
		numToPitch(buffer, Msg.Midi.Id);

	return buffer;
}

static uint32_t ctl_assign_high(register GUICTL * ctl)
{
	register unsigned char	status;

	status = MidiMsg[0];
	if (status >= 0xF0)
	{
		if (Msg.Midi.Status < 0xF0) goto wrong;
		Msg.Midi.High = status;
		goto upd;
	}
	if (Msg.Midi.Status != status)
	{
		char	buffer[120];

		if ((status & 0xf0) != (Msg.Midi.Status & 0xf0))
wrong:	sprintf(GuiBuffer, "Do you want to change Low to a %s message?", get_status_str(Msg.Midi.Status));
		else
			sprintf(GuiBuffer, "Do you want to change Low to channel %u?", (Msg.Midi.Status & 0x0F) + 1);
errs:	strcpy(buffer, GuiBuffer);
		if (GuiErrShow(GuiApp, buffer, GUIBTN_ESC_SHOW|GUIBTN_NO_SHOW|GUIBTN_YES_SHOW|GUIBTN_YES_DEFAULT) == GUIBTN_YES)
		{
			Msg.Midi.Status = status;
			Msg.Midi.Id = MidiMsg[1];
			goto set;
		}
	}
	else if (Msg.Midi.Id != MidiMsg[1])
	{
		sprintf(GuiBuffer, "High must be %s number %u", get_status_str(Msg.Midi.Status), Msg.Midi.Id);
		goto errs;
	}
	else
set:	Msg.Midi.High = MidiMsg[2];

upd:
	MidiMsg[0] = 0;
	show_midictls();
	return (uint32_t)-2;
}




/******************* handle_midimouse() ********************
 * Called by GUI thread when user operates (with the mouse)
 * a ctl in the MIDI Command assignment screen.
 */

static void handle_midimouse(register GUIMSG * msg)
{
	register GUICTL *		ctl;

	ctl = msg->Mouse.SelectedCtl;

	if (!handleMidiView(msg))
	{
		if (ctl->Flags.Global & CTLGLOBAL_PRESET)
		{
			if (ctl->PresetId == GUIBTN_OK)
			{
				if (Msg.Midi.Status <= 0xA0)
				{
					register unsigned char		cmd;

					cmd = CmdSwitchNote & 0x7f;
					if (Msg.Midi.Id > cmd && Msg.Midi.Id < cmd+88)
					{
						SelActionEntry = (ACTIONINSTANCE *)(&CommandKeyAssigns[(Msg.Midi.Id - cmd - 1) * 3]);
						if (memcmp(SelActionEntry, &Msg.CatIndex, 3))
						{
							memcpy(SelActionEntry, &Msg.CatIndex, 3);
							markChanged(SelActionEntry);
							SaveConfigFlag |= (0x01 << ActionListNum);
						}
						goto abort;
					}
				}
				else
				{
					if (!SelActionEntry) goto newasn;

					SelActionEntry->LastValue = 0;

					// Check if the user actually changed anything
					if (memcmp(SelActionEntry, &Msg, sizeof(ACTIONINSTANCE)))
					{
						register ACTIONINSTANCE *	entry;

						// Check if there's another MIDI Msg already assigned to this action
newasn:
						if ((entry = EntryList[1]))
						{
							ACTIONINSTANCE *	end;

							end = (ACTIONINSTANCE *)((unsigned char *)entry + EntryListSize[1]);

							if (Msg.Midi.Status < 0xf0)
							{
								register unsigned short		val;

								val = getShort(&Msg.Midi.Status);

								// If full val range, just match MidiStatus and MidiId
								if (!(SelActionEntry->Action & ACTION_RANGELIMIT))
								{
									while (entry < end)
									{
										if (getShort(&entry->Midi.Status) == val) goto conflict;
										entry++;
									}
								}
								else while (entry < end)
								{
									if (getShort(&entry->Midi.Status) == val &&
										(!(entry->Action & ACTION_RANGELIMIT) ||
										(entry->Midi.Low >= Msg.Midi.Low && entry->Midi.Low <= Msg.Midi.High) ||
										(entry->Midi.High >= Msg.Midi.Low && entry->Midi.High <= Msg.Midi.High))
										)
									{
conflict:							if (!alreadyAssigned(entry)) goto abort;
										goto copy;
									}

									entry++;
								}
							}
						}

						if (SelActionEntry || (SelActionEntry = allocEntry(ActionListNum)))
copy:						storeEntry();
					}
				} // not NTN
			}

abort:	midiviewDone();

			doSetupScreen();
		}

		else if (ctl->Ptr && ((GUICTLDATAFUNC)(ctl->Ptr))(ctl) != (uint32_t)-2)
			GuiCtlUpdate(GuiApp, MainWin, ctl, 0, 0);
	}
}





/*************** doMidiMsgAsn() ****************
 * Shows/operates the screen to change the
 * currently selected action's midi msg assignment.
 *
 * SelCategory =	ACTIONCAT currently selected.
 */

static GUIFUNCS MidiMsgGuiFuncs = {dummy_drawing,
handle_midimouse,
dummy_keypress,
0};

void doMidiMsgAsn(void)
{
	ActionListNum = ACTIONLIST_MIDIMSG;

	// If this action already has a midi msg assigned, copy to temp
	// MIDIACTION global var. Otherwise init to mod wheel
	if (!(SelActionEntry = getExistingAsn(Msg.CatIndex, Msg.Action)))
	{
		Msg.Midi.Status = 0xB0;
		Msg.Midi.Id = 1;
		Msg.Midi.High = 0;
	}
	else
		memcpy(&Msg, SelActionEntry, sizeof(ACTIONINSTANCE));
	Msg.LastValue = 0;

	MidiMsgGuiCtls[MAPCTL_RANGE].Attrib.Value = (Msg.Action & ACTION_RANGELIMIT) ? 1 : 0;

	retToMidiMsgAsn();

	MidiMsg[0] = Msg.Midi.Status;
	MidiMsg[1] = Msg.Midi.Low;
	MidiMsg[2] = Msg.Midi.High;
	Msg.Midi.Status = 0;
	ctl_set_msgtype(0);
	ctl_update_scale();
	ctl_update_range();
}

static void retToMidiMsgAsn(void)
{
	GuiFuncs = &MidiMsgGuiFuncs;
	MainWin->Ctls = &MidiMsgGuiCtls[0];
	common_gui();
	init_midiview(1);
}















static const char * getValueLabel(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	sprintf(buffer, NumStr, Msg.LastValue);
	return buffer;
}

static void retToMidiNoteAsn(void);

static void storeNoteVal(register GUINUMPAD * numpad)
{
	if (numpad && numpad->Value < 128) Msg.LastValue = (unsigned char)numpad->Value;
	retToMidiNoteAsn();
}

static uint32_t ctl_set_action_val(register GUICTL * ctl)
{
	doNumeric(storeNoteVal, 0, 0);
	return 0;
}

static const char * getNoteNumLabel(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	if (Msg.Midi.Id > 127) return "<unassigned>";
	numToPitch(buffer, Msg.Midi.Id + (CmdSwitchNote + 0x7f));
	return buffer;
}

static uint32_t ctl_set_notenum(register GUICTL * ctl)
{
	register char *	str;

	BytePtr = &Msg.Midi.Id;
	ReturnFunc = &retToMidiNoteAsn;
	str = copystr(DescBuf, "Play the note you want assigned to ");
	makeCmdStr(&ActionCategory[Msg.CatIndex], str, Msg.Action);
	doNoteScreen(DescBuf, 0);
	return 0;
}

unsigned char isNoteAssign(void)
{
	return BytePtr == &Msg.Midi.Id;
}
static const char ValueStr[] = "Value:";

static GUICTL		MidiNoteGuiCtls[] = {
 	{.Type=CTLTYPE_PUSH,		.Y=2,	.BtnLabel=getNoteNumLabel,	.Ptr=&ctl_set_notenum,	.Attrib.NumOfLabels=1, .Width=7, .Flags.Global=CTLGLOBAL_NOPADDING|CTLGLOBAL_GET_LABEL},
 	{.Type=CTLTYPE_STATIC,	.Y=2,	.Label=ValueStr,											.Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_PUSH,		.Y=2, .BtnLabel=getValueLabel,	.Width=5, .Ptr=&ctl_set_action_val, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_NOPADDING|CTLGLOBAL_GET_LABEL},
 	{.Type=CTLTYPE_END},
};

/*************** doMidiNoteAsn() ****************
 * Shows/operates the screen to change the
 * currently selected action's midi note assignment.
 *
 * SelCategory =	ACTIONCAT currently selected.
 */

void doMidiNoteAsn(void)
{
	ActionListNum = ACTIONLIST_MIDINOTE;

	if (!(SelActionEntry = getExistingAsn(Msg.CatIndex, Msg.Action)))
	{
		Msg.Midi.Id = 60 - (CmdSwitchNote + 0x7f);
		Msg.LastValue = 127;
	}
	else
	{
		Msg.Midi.Id = (((unsigned char *)SelActionEntry - &CommandKeyAssigns[0]) / 3);
		Msg.LastValue = SelActionEntry->LastValue;
	}
	Msg.Midi.Status = 0x90;

	// Hide the value push btn if not a fader action
	hide_value_ctl(&MidiNoteGuiCtls[0]);

	retToMidiNoteAsn();
}

static void retToMidiNoteAsn(void)
{
	MainWin->Ctls = &MidiNoteGuiCtls[0];
	GuiFuncs = &MidiMsgGuiFuncs;
	common_gui();
}

#endif	// NO_MIDI_IN_SUPPORT



























// ==================================================
// Draw the Actions list in "Setup -> Commands" screen

/******************** drawActionsList() *******************
 * Displays the names of actions that the user can
 * configure to be controlled via MIDI Input, mouse, or
 * PC keyboard.
 */

GUILIST * drawActionsList(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app)
	{
		ACTIONCAT *		category;
		unsigned char	orig, color;

		category = &ActionCategory[0];
		orig = List.CurrItemNum;
		color = 0;
		do
		{
			const char *			label;
			const unsigned char *mode;
			unsigned char			total, flag;

			// User can't select a heading
			if (area->ItemIndex == orig) List.CurrItemNum++;

			if (List.CurrItemNum >= List.TopItemNum) GuiTextSetColor(MainWin, GUICOLOR_DARKGREEN);
			if (!(label = category->HelpStrs)) label = getMusicianName(category->RobotNum);
			if (GuiListDrawItem(app, ctl, area, label)) goto out;
			color = GUICOLOR_DARKGREEN;

			mode = (unsigned char *)category->Desc;
			flag = 1;
again:	total = *mode++;
			if ((char *)mode != &MuteStrs[1])
			{
				label = (char *)mode + (total & 0x0f);
				if (ActionListNum == ACTIONLIST_PCKEY) total >>= 4;
				total &= 0x0f;
			}
			else
				label = (char *)mode + total;

			do
			{
				register char *		ptr;

				ptr = GuiBuffer;
				*ptr++ = ' ';
				*ptr++ = ' ';
				while ((*ptr++ = *label++));

				if (List.CurrItemNum >= List.TopItemNum)
				{
					if (area->ItemIndex == List.CurrItemNum)
					{
						SelCategory = category;
						Msg.Action = *mode;
						Msg.CatIndex = category - &ActionCategory[0];
						color = GUICOLOR_RED;
					}
					else
					{
						// If this action is assigned, draw in gray (instead of black)
						if (getExistingAsn(category - &ActionCategory[0], *mode))
						{
							if (color == GUICOLOR_GRAY) goto skip2;
							color = GUICOLOR_GRAY;
							goto skip_color;
						}
					}

					if (color != GUICOLOR_BLACK)
					{
						color = GUICOLOR_BLACK;
skip_color:			if (List.CurrItemNum >= List.TopItemNum) GuiTextSetColor(MainWin, color);
					}
				}

skip2:		if (GuiListDrawItem(app, ctl, area, GuiBuffer)) goto out;

				// Next action for this category
				mode++;
			} while (--total);

			if (flag && (category->Properties & ACTIONPROP_COMMON))
			{
				mode = (unsigned char *)&CommonPatchStrs[0];
				flag = 0;
				goto again;
			}
		} while (++category < &ActionCategory[NUM_OF_ACTIONCATS]);
out:
		List.CurrItemNum = orig;
	}

	return &List;
}

static void select_first_cat(void)
{
	SelActionEntry = 0;
	SelCategory = &ActionCategory[0];
	Msg.Action = *(ActionCategory[0].Desc + 1);
	Msg.CatIndex = 0;
}

void resetActionsList(void)
{
	showPickItemList(drawActionsList);
	List.ColumnWidth = ActionWidth /* + (GuiApp->GuiFont.CharWidth * 2) */;
	List.NumOfItems = NumOfActions;
	List.CurrItemNum = 1;
	select_first_cat();
}























/******************** getExistingAsn() *********************
 * Finds any ACTIONINSTANCE in the current list (ActionListNum)
 * that matches the specified action category index and
 * action.
 *
 * RETURNS: Ptr to the matching ACTIONINSTANCE, or 0 if none.
 */

static ACTIONINSTANCE * getExistingAsn(register unsigned char catIndex, register unsigned char action)
{
	register unsigned char *	endptr;
	{
	register unsigned char *	ptr;

#ifndef NO_MIDI_IN_SUPPORT
	// Command notes use the CommandKeyAssigns[] array
	if (ActionListNum == ACTIONLIST_MIDINOTE)
	{
		ptr = &CommandKeyAssigns[0];
		endptr = &ptr[88*3];
		goto find_Existing;
	}
#endif

	// Mouse uses the MouseAssigns[] array
	if (!ActionListNum) // ACTIONLIST_MOUSE
	{
		ptr = &MouseAssigns[0];
		endptr = &ptr[32*3];
#ifndef NO_MIDI_IN_SUPPORT
find_Existing:
#endif
		do
		{
			if (((ACTIONINSTANCE *)ptr)->CatIndex == catIndex && ((ACTIONINSTANCE *)ptr)->Action == action)
				return (ACTIONINSTANCE *)ptr;
			ptr += 3;
		} while (ptr < endptr);

		goto zero;
	}
	}

	// ACTIONLIST_PCKEY or ACTIONLIST_MIDIMSG have a linked list of full ACTIONINSTANCEs
	{
	register ACTIONINSTANCE *	entry;

	// See if the chosen action is already assigned to the desired controller source (midi msg, or pc key).
	if ((entry = EntryList[ActionListNum - ACTIONLIST_PCKEY]))
	{
		endptr = ((unsigned char *)entry) + EntryListSize[ActionListNum - ACTIONLIST_PCKEY];

		while ((unsigned char *)entry < endptr)
		{
			if (entry->CatIndex == catIndex && entry->Action == action) return entry;
			entry++;
		}
	}
zero:
	return 0;
	}
}




/********************* alreadyAssigned() ***************
 * Prompts user to overwrite any conflicting ACTIONINSTANCE.
 *
 * RETURNS: 1=yes, 0=no.
 */

static unsigned char alreadyAssigned(register ACTIONINSTANCE * entry)
{
	char	buffer[120];

	strcpy(buffer, "Already assigned to \"");
	strcpy(makeCmdStr(&ActionCategory[entry->CatIndex], &buffer[21], entry->Action), "\". Overwrite?");

	if (GuiErrShow(GuiApp, buffer, GUIBTN_ESC_SHOW|GUIBTN_NO_SHOW|GUIBTN_YES_SHOW|GUIBTN_YES_DEFAULT) != GUIBTN_YES)
		return 0;
	if (!SelActionEntry)
		SelActionEntry = entry;
	else
		deleteEntry(entry);

	return 1;
}




static unsigned char assignKeyToAction(void)
{
	register unsigned short		newAsn;

	// User didn't change the assignment, or no initial assign made?
	newAsn = Msg.Pc.Key;
	if (!SelActionEntry || SelActionEntry->Pc.Key != newAsn || SelActionEntry->Pc.Qualifiers != Msg.Pc.Qualifiers)
	{
		register ACTIONINSTANCE *		entry;

		if ((entry = EntryList[0]))
		{
			while ((char *)entry < (char *)EntryList[0] + EntryListSize[0])
			{
				if (entry->Pc.Key == newAsn && entry->Pc.Qualifiers == Msg.Pc.Qualifiers)
				{
					if (!alreadyAssigned(entry)) goto abort;
					goto copy;
				}

				entry++;
			}
		}

		if (SelActionEntry || (SelActionEntry = allocEntry(0)))
		{
copy:		Msg.LastValue = 0;
			storeEntry();
			return 1;
		}
	}
abort:
	return 0;
}





// ========================
// Mouse button action assignment
// ========================

static void hide_value_ctl(register GUICTL * ctl)
{
	while (ctl->Type)
	{
		if (ctl->Label == &ValueStr[0])
		{
			// Hide the value push btn if not a fader action
			if (Msg.Action >= ACTION_FADER)
			{
				ctl->Type &= ~CTLTYPE_HIDE;
				ctl[1].Type &= ~CTLTYPE_HIDE;
			}
			else
			{
				ctl->Type |= CTLTYPE_HIDE;
				ctl[1].Type |= CTLTYPE_HIDE;
			}

			break;
		}

		ctl++;
	}
}

static const char MouseLabels[] = "Left\0Middle\0Right\0Wheel up\0Wheel down";

static const char * getBtnLabel(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	register const char *	str;
	register unsigned char	btnNum, i;

	if (!app) btnNum = 1;
	else btnNum = Msg.Pc.ButtonNum;

	if (btnNum > 4)
		sprintf(buffer, "Button %u", btnNum + 1);
	else
	{
		str = MouseLabels;
		i = btnNum;
		while (i--)
		{
			while (*str++);
		}
		sprintf(buffer, btnNum > 2 ? "%s" : "%s button", str);
	}

	return buffer;
}

unsigned char setMouseBtn(register char *buffer, register unsigned char btn)
{
	if (buffer) getBtnLabel(btn ? GuiApp : 0, 0, buffer);
	else if (btn < 16) Msg.Pc.ButtonNum = btn;
	return Msg.Pc.ButtonNum;
}

static GUICTL		MouseGuiCtls[] = {
 	{.Type=CTLTYPE_PUSH, .BtnLabel=getBtnLabel, .Y=2, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_GET_LABEL},
 	{.Type=CTLTYPE_STATIC,	.Y=2,	.Label=ValueStr,				.Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_PUSH,		.Y=2, .BtnLabel=getValueLabel,	.Ptr=&ctl_set_action_val, .Width=5, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_NOPADDING|CTLGLOBAL_GET_LABEL},
	{.Type=CTLTYPE_END},
};

static void mhandle_mouse(register GUIMSG * msg)
{
	register GUICTL *		ctl;

	if ((ctl = msg->Mouse.SelectedCtl) && ctl->Flags.Global & CTLGLOBAL_PRESET)
	{
		if (ctl->PresetId == GUIBTN_OK)
		{
			if (ActionListNum == ACTIONLIST_PCKEY)
			{
				if (!Msg.Pc.Key) goto out;
				assignKeyToAction();
			}
			else if (Msg.CatIndex != 0xff)
			{
				register ACTIONINSTANCE *	entry;

				entry = (ACTIONINSTANCE *)(&MouseAssigns[Msg.Pc.ButtonNum * 3]);
				if (memcmp(entry, &Msg.CatIndex, 3))
				{
					memcpy(entry, &Msg.CatIndex, 3);
					markChanged(entry);
					SaveConfigFlag |= (0x01 << ActionListNum);
				}
			}
		}
		doSetupScreen();
	}
	else if (ctl == &MouseGuiCtls[0])
	{
store: if (msg->Mouse.ButtonNum && msg->Mouse.ButtonNum < 33) Msg.Pc.ButtonNum = msg->Mouse.ButtonNum - 1;
		GuiCtlUpdate(GuiApp, MainWin, ctl, 0, 0);
	}
	else if (msg->Mouse.X >= MouseGuiCtls[0].X && msg->Mouse.X < MouseGuiCtls[0].X + MouseGuiCtls[0].Width &&
			msg->Mouse.Y >= MouseGuiCtls[0].Y && msg->Mouse.Y < MouseGuiCtls[0].Y + GuiCtlGetHeight(GuiApp, &MouseGuiCtls[0]))
	{
		goto store;
	}
	else if (ctl->Ptr && ((GUICTLDATAFUNC)(ctl->Ptr))(ctl) != (uint32_t)-2)
		GuiCtlUpdate(GuiApp, MainWin, ctl, 0, 0);

out:
	return;
}

static const char MDirections[] = "Move the mouse pointer over the above graphical box. \
Then operate the desired mouse button for the assignment. If not satisfied with this \
mouse assignment, repeat the process. When done, click Ok to save your assignment, \
or Cancel to abort.";

static void mouse_drawing(void)
{
	GUIBOX	box;

	box.X = GuiWinGetBound(GuiApp, MainWin, GUIBOUND_XBORDER);
	box.Y = MouseGuiCtls[0].Y + GuiCtlGetHeight(GuiApp, &MouseGuiCtls[0]) + GuiWinGetBound(GuiApp, MainWin, GUIBOUND_YSPACE|GUIBOUND_YHEADSTR|GUIBOUND_UPPER);
	box.Height = GuiWinGetBound(GuiApp, MainWin, GUIBOUND_LOWER) - box.Y;
	box.Width = MainWin->WinPos.Width - (2 * box.X);
	box.Y = GuiTextDrawMsg(GuiApp, &MDirections[0], &box, (GUICOLOR_DARKGRAY << 24));
}

static void resize_area(void)
{
	GuiCtlCenterBtns(GuiApp, MainWin, &MouseGuiCtls[0], 3);
}

static GUIFUNCS MouseGuiFuncs = {mouse_drawing,
mhandle_mouse,
dummy_keypress,
resize_area};

static void format_asn_desc(void)
{
	makeCmdStr(SelCategory, DescBuf, Msg.Action);
	headingCopyTo(DescBuf, headingCopyTo("Assign to ", 0));
	headingShow(GUICOLOR_GOLD);
}

/********************* doMouseBtnAsn() ***********************
 * Shows/operates the "mouse button assignment" screen.
 */

static void common_gui(void)
{
	MainWin->Menu = 0;
	MainWin->Flags = GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY|GUIWIN_TAB_KEY;
	MainWin->LowerPresetBtns = GUIBTN_OK_SHOW|GUIBTN_CANCEL_SHOW|GUIBTN_CENTER;
	clearMainWindow();
	if (Msg.CatIndex != 0xff) format_asn_desc();
	GuiCtlSetSelect(GuiApp, 0, 0, (GUICTL *)0 + GUIBTN_CANCEL);
}

/****************** doMouseBtnAsn() *****************
 * Manages the screen where the user enters his
 * choice of mouse button for the currently
 * selected action.
 */

void doMouseBtnAsn(void)
{
	ActionListNum = ACTIONLIST_MOUSE;
	GuiFuncs = &MouseGuiFuncs;
	MainWin->Ctls = &MouseGuiCtls[0];
	if (Msg.CatIndex != 0xff)
	{
		Msg.Pc.ButtonNum = 2;
		Msg.LastValue = 127;
		if ((SelActionEntry = getExistingAsn(Msg.CatIndex, Msg.Action)))
		{
			Msg.Pc.ButtonNum = ((unsigned char *)SelActionEntry - &MouseAssigns[0]) / 3;
			Msg.LastValue = SelActionEntry->LastValue;
		}
	}
	common_gui();
	hide_value_ctl(&MouseGuiCtls[0]);
	resize_area();
	MainWin->Flags = GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY|GUIWIN_TAB_KEY|GUIWIN_ALL_MOUSEBTN;
}

void doMouseChordBtn(void)
{
	Msg.CatIndex = 0xff;
	Msg.Action = 0;
	doMouseBtnAsn();
}













// ======================================
// PC key action assignment

static const char ModifierKeys[] =  {5, 'S','h','i','f','t',
7, 'C','a','p','l','o','c','k',
4, 'C','t','r','l',
3, 'A','l','t',
4, '0','x','1','0',
4, '0','x','2','0',
3, 'W','i','n',
4, 'M','e','n','u',
0};

static const char Group1[] = {5, 'S', 'p', 'a', 'c', 'e',
1,'!',
6,'D','Q','u','o','t','e',
1,'#',
1,'$',
1,'%',
1,'&',
5,'Q','u','o','t','e',
1,'(',
1,')',
1,'*',
1,'+',
5,'C','o','m','m','a',
1,'-',
6,'P','e','r','i','o','d',
5,'S','l','a','s','h',
0};

static const char Group2[] = {4,'H','o','m','e',
10,'L','e','f','t',' ','A','r','r','o','w',
8,'U','p',' ','A','r','r','o','w',
11,'R','i','g','h','t',' ','A','r','r','o','w',
10,'D','o','w','n',' ','A','r','r','o','w',
7,'P','a','g','e',' ','U','p',
9,'P','a','g','e',' ','D','o','w','n',
3,'E','n','d',
5,'B','e','g','i','n',
6,'I','n','s','e','r','t',
3,'D','e','l',
0};

static const char Group3[] = {1,'*',
1,'+',
9,'S','e','p','a','r','a','t','o','r',
1,'-',
7,'D','e','c','i','m','a','l',
1,'/',
0};

static const char XGroup[] = {4,'B','a','c','k',
1,' ',
4,'L','i','n','e',
5,'C','l','e','a','r',
1,' ',
5,'P','a','u','s','e',
10,'S','c','r','o','l','l','L','o','c','k',
6,'S','y','s','R','e','q',
1,' ',
6,'D','e','l','e','t','e',
0};

static const char UnKnown[] = {9,'<','U','n','k','n','o','w','n','>'};

static const char Keypad[] = {7,'K','e','y','p','a','d',' '};
static const char InsertStr[] = {6,'I','n','s','e','r','t'};
static const char MenuStr[] = {4,'M','e','n','u'};

/******************** make_key_str() *******************
 * Translates PCACTION to an ascii string.
 *
 * RETURNS: ascii string in TempBuffer.
 */

static void make_key_str(void)
{
	register char *				ptr;

	ptr = (char *)TempBuffer;

	// Do modifier keys
	{
	register const char *		label;
	register unsigned char		chr;

	chr = Msg.Pc.Qualifiers & (0x01|0x02|0x04|0x08|0x20);
	label = &ModifierKeys[0];
	while (chr && *label)
	{
		if (chr & 0x01)
		{
			if (ptr != (char *)TempBuffer) *(ptr)++ = ' ';
			memcpy(ptr, &label[1], *label);
			ptr += *label;
		}

		chr >>= 1;
		label += *label + 1;
	}
	}

	{
	register const char *		str;
	register uint16_t				code, i;

	code = Msg.Pc.Key;
	if (ptr != (char *)TempBuffer) *(ptr)++ = ' ';

	if (code >= XK_Mode_switch)
	{
		// F1 to F5
		if (code >= XK_F1)
		{
			if (code > XK_F35) goto unknown;
			*ptr++  = 'F';
			sprintf(ptr, "%u", code - (XK_F1 - 1));
			goto done;
		}

		// ======= Keypad
		str = &Keypad[0];
		memcpy(ptr, &str[1], *str);
		ptr += *str;

		// 0 to 9
		if (code >= XK_KP_0)
		{
			if (code > XK_KP_9) goto unknown;
			*ptr++ = (code - XK_KP_0) + 0x30;
			goto out2;
		}

		// KP_Equal to KP_divide
		if (code >= XK_KP_Equal)
		{
			if (code >= XK_KP_Multiply)
			{
				str = Group3;
				i = XK_KP_Multiply;
				goto out3;
			}

			code = '=';
			goto one;
		}

		if (code >= XK_KP_Home)
		{
			i = XK_KP_Home;
			goto home;
		}

		goto unknown;
	}

	if (code >= XK_BackSpace)
	{
		//
		if (code >= XK_Home)
		{
			if (code >= XK_Insert)
			{
				str = &InsertStr[0];
				goto out;
			}
			if (code >= XK_Menu)
			{
				str = &MenuStr[0];
				goto out;
			}
			i = XK_Home;
home:		str = &Group2[0];
			goto out3;
		}

		i = XK_BackSpace;
		str = &XGroup[0];
		goto out3;
	}

	if (code >= '0')
	{
//		if (code <= '9' || ((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z')))
		{
one:		*ptr++ = (char)code;
			goto out2;
		}
	}
	else
	{
		str = &Group1[0];
		i = ' ';
		do
		{
out3:		if (code == i++) goto out;
			str += *str + 1;
		} while (*str);
	}
unknown:
	str = &UnKnown[0];
out:
	memcpy(ptr, &str[1], *str);
	ptr += *str;
out2:
	*ptr = 0;
	}
done:
	return;
}

static void pckeys_keypress(register GUIMSG * msg)
{
	if (!msg->Key.Direction)
	{
		register uint32_t		keycode;

		keycode = msg->Key.Code;
		if (keycode == XK_Return)
		{
			if (Msg.Pc.Key)
			{
				assignKeyToAction();
				goto done;
			}
		}
		if (keycode == XK_Escape)
done:		doSetupScreen();
		else if (keycode != XK_Tab && keycode >= ' ')
		{
			if (keycode >= 'a' && keycode <= 'z') keycode &= 0x5F;
			Msg.Pc.Key = keycode;
			Msg.Pc.Qualifiers = msg->Key.Flags;
			make_key_str();
			clearMainWindow();
		}
	}
}

static const char Directions[] = "Press and hold any combination of the Shift, Ctrl, and Alt \
keys. You can additionally turn Caps Lock on. While still holding any, all, or none \
of those keys, press and release one more key of your choosing. If not satisfied with this \
key assignment, repeat the process. When done, press the Enter key to save your assignment, \
or press Esc to abort. Note you can't use the Enter, Esc, and Tab keys in an \
assignment, as BackupBand uses them for other purposes.";

static void key_drawing(void)
{
	GUIBOX	box;
	register uint32_t	y;

	box.Y = y = GuiWinGetBound(GuiApp, MainWin, GUIBOUND_YHEADSTR);
	box.Height = GuiWinGetBound(GuiApp, MainWin, GUIBOUND_LOWER) - box.Y;
	box.X = GuiWinGetBound(GuiApp, MainWin, GUIBOUND_XBORDER);
	box.Width = MainWin->WinPos.Width - (2 * box.X);
	box.Y = GuiTextDrawMsg(GuiApp, Directions, &box, (GUICOLOR_DARKGRAY << 24));
	if (TempBuffer[0])
	{
		box.Height -= (box.Y - y);
		GuiTextDrawMsg(GuiApp, (char *)TempBuffer, &box, (GUICOLOR_DARKGREEN << 24)|GUIDRAW_Y_CENTER);
	}
}

static GUIFUNCS PcKeyGuiFuncs = {key_drawing,
mhandle_mouse,
pckeys_keypress,
0};

/****************** doPcKeyAsn() *****************
 * Manages the screen where the user enters his
 * choice of pc key combination for the currently
 * selected action.
 */

void doPcKeyAsn(void)
{
	ActionListNum = ACTIONLIST_PCKEY;
	GuiFuncs = &PcKeyGuiFuncs;
	MainWin->Ctls = 0;
	Msg.Pc.Qualifiers = Msg.Pc.Key = 0;
	if ((SelActionEntry = getExistingAsn(Msg.CatIndex, Msg.Action)))
		memcpy(&Msg, SelActionEntry, sizeof(ACTIONINSTANCE));
	common_gui();
	make_key_str();
	MainWin->Flags = GUIWIN_RAW_KEY;
}






/***************** drawActionAssign() ******************
 * Displays the current assignment (mouse button, pc key,
 * or midi msg) for the selected action (in the Command
 * screen's full list of actions).
 */

void drawActionAssign(register uint32_t x, register uint32_t y)
{
	if ((SelActionEntry = getExistingAsn(Msg.CatIndex, Msg.Action)))
	{
		register char *	buffer;

		buffer = GuiBuffer;
		switch (ActionListNum)
		{
			case 3:
			{
				register unsigned char note;

				note = (((unsigned char *)SelActionEntry - &CommandKeyAssigns[0]) / 3) + (CmdSwitchNote & 0x7F);
				if (note < 128)
					numToPitch(GuiBuffer, note);
				else
					strcpy(GuiBuffer, "<Out of range>");
				break;
			}
			case 2:
				memcpy(&Msg, SelActionEntry, sizeof(ACTIONINSTANCE));
				sprintf(buffer, "%s ", get_status_str(Msg.Midi.Status));
				while (*buffer++);
				buffer--;
				getMsgIdLabel(GuiApp, 0, buffer);
				break;
			case 1:
				memcpy(&Msg, SelActionEntry, sizeof(ACTIONINSTANCE));
				make_key_str();
				break;
			case 0:
				Msg.Pc.ButtonNum = ((unsigned char *)SelActionEntry - &MouseAssigns[0]) / 3;
				getBtnLabel(GuiApp, 0, buffer);
		}

		GuiTextDraw(GuiApp, x, y, GuiBuffer);
	}
}











// =======================================
// Real-time execution of action assignments

/********************** doMouseorKeyCmd() **********************
 * Called by main thread to issue a command assigned to
 * one of the mouse buttons, or a computer key.
 *
 * id =			Mouse button number (0 to 31) or computer key.
 * direction =	1 for mouse/key down, 0 for up.
 *
 * RETURN: 0 = no mapped cmd, non-0 = mapped
 */

unsigned char doMouseorKeyCmd(register uint32_t id, register unsigned char direction)
{
	register ACTIONINSTANCE *	entry;

	if (id < 32)
	{
		entry = (ACTIONINSTANCE *)(&MouseAssigns[id * 3]);
		if (entry->CatIndex != 0xff) goto got_it;
	}
	else
	{
		register char *		mapEnd;

		entry = EntryList[0];
		mapEnd = (char *)entry + EntryListSize[0];
		while ((char *)entry < mapEnd)
		{
			if ((entry->Pc.Key | (entry->Pc.Qualifiers << 16)) == id) goto got_it;
			entry++;
		}
	}

unused:
	return 0;

got_it:
	{
	register unsigned char	mode;

	mode = entry->Action;

	// For pedal, value = direction (1 for on, 0 for off)
	if ((mode & 0x03) != ACTION_PEDAL)
	{
		// Other modes ignore release transition
		if (!direction) goto unused;

		if (id <= 32 && mode >= ACTION_FADER)
			direction = entry->LastValue;

		else switch (mode & 0x03)
		{
			case ACTION_ONOFFSWITCH:
				// Flip prev value
				direction = entry->LastValue ^ 1;
				break;

			case ACTION_DECBUTTON:
				// Value = 0
				direction = 0;
		}
	}

	entry->LastValue = direction;
	if (ActionCategory[entry->CatIndex].Properties & ACTIONPROP_THREADID) direction |= 0x80;
	if ((id = ActionCategory[entry->CatIndex].Func(mode, direction))) drawGuiCtl(0, id, 0);
	}

	return 1;
}












//=============================================
// RawMidi and ALSA Seq MIDI input
//=============================================

#ifndef NO_MIDI_IN_SUPPORT

/******************** setMidiInSiphon() *******************
 * Called by GUI thread to set a function that siphons off
 * MIDI Input. operation=0 clears this.
 *
 * Must Not be called by MIDI in thread.
 */

void setMidiInSiphon(register MIDIINSIPHON * func, register unsigned char operation)
{
	// Setting or clearing?
	if (!operation)
	{
		// Do nothing if we're not the installed handler. Otherwise if midi in thread is
		// awake, wait for it to go back to sleep before we clear
		if (SiphonFunc == func)
		{
			while (__atomic_or_fetch(&SoundDev[DEVNUM_MIDIIN].Lock, GUITHREADID, __ATOMIC_RELAXED) != GUITHREADID) usleep(1000);
			SiphonFunc = 0;
			goto set;
		}
	}
	else
	{
		// If the MIDI IN thread is currently processing some incoming data,
		// let it finish with that and go back to sleep. Then we can set
		// the siphon function
		if (SiphonFunc != func)
		{
			while (__atomic_or_fetch(&SoundDev[DEVNUM_MIDIIN].Lock, GUITHREADID, __ATOMIC_RELAXED) != GUITHREADID) usleep(1000);
			SiphonFunc = func;
set:		if (func == midiViewSiphon || func == midiAssignSiphon) PendingPtr = 0;
		}

		endCmdMode();
	}

	CmdSwitchMode = 0;
	__atomic_and_fetch(&SoundDev[DEVNUM_MIDIIN].Lock, ~GUITHREADID, __ATOMIC_RELAXED);
}





int isMidiInSiphon(register MIDIINSIPHON * func)
{
	return func == SiphonFunc;
}





void updChansInUse(void)
{
	register unsigned char	i;

	ChansInUse = 0;
	for (i=0; i <= PLAYER_SOLO; i++)
	{
		if (DevAssigns[i]) ChansInUse |= (0x0001 << MidiChans[i]);
	}
}






/************** addToHistory() *****************
 * Keeps track of the time inbetween notes being
 * played by a (live human) drummer. If it appears
 * that he's playing a roll or flam on a particular
 * drum (ie, a particular note #), then we want to
 * "soften" the attack of some of those hits. We can
 * do that with digitized drum waves by skipping
 * some of the beginning samples. This function only
 * timestamps each note, and decides what times
 * constitute a roll. The caller does the soften.
 */

static unsigned char addToHistory(register unsigned char noteNum)
{
	register uint32_t			evtTime;
	register uint32_t			i, unused, earliest;
	unsigned char				ret;

	// Get current time from the beat thread. It's quicker
	// than asking the OS. Let the beat thread do the latter
	evtTime = get_current_clock();

	// See if this same note # has been played recently,
	// and if so, record the time inbetween that note
	// and this note happening
	i = unused = ret = 0;
	earliest = PlayingTimes[0];
	for (; i<MIDIKEY_HISTORY ; i++)
	{
		// Same note?
		if (PlayingKeys[i] == noteNum)
		{
			// Time diff less than 80 clock ticks = roll. Inform
			// caller of this "special condition" by setting
			// bit 7 of midi velocity (which is not normal. Me
			// doing it here for this purpose is the _only_
			// place it happens)
			if (evtTime - PlayingTimes[i] < 80) ret = 0x80;
			unused = i;
			break;
		}

		if (PlayingTimes[i] < earliest)
		{
			earliest = PlayingTimes[i];
			unused = i;
		}
	}

	// Update our history
	PlayingTimes[unused] = evtTime;
	PlayingKeys[unused] = noteNum;

	return ret;
}

static ACTIONINSTANCE * inform_main_of_cmd(register ACTIONINSTANCE * entry, register unsigned char note)
{
	if (!(AppFlags4 & APPFLAG4_NO_TIPS))
	{
		if (note != BrowsedNote)
		{
			BrowsedNote = note;
			if (entry)
				makeCmdStr(&ActionCategory[entry->CatIndex], &DescBuf[0], entry->Action);
			else
			{
				DescBuf[0] = '<';
				numToPitch(&DescBuf[1], note);
				strcat(DescBuf, " unassigned>");
			}
			if (isMainScreen()) //GuiWinSignal(GuiApp, 0, SIGNALMAIN_CMDMODE_SEL);
				signalMainFromMidiIn(MidiInSigHandle, CTLMASK_CMDNOTEBROWSE);
		}

		// If browsing (command switch note-off not yet received), don't issue
		// the command. We want to let the user simply browse cmds
		if (CmdSwitchMode & 0x01)
		{
			CmdSwitchMode |= 0x04;	// Indicate browsing happened
			entry = 0;
		}
	}

	return entry;
}

/*
#define MIDIVIEW_NOTEPLAY		(1 << 5)
#define MIDIVIEW_PGM				(2 << 5)
#define MIDIVIEW_CTL				(3 << 5)
#define MIDIVIEW_OTHER			(4 << 5)
#define MIDIVIEW_SYS				(5 << 5)
*/
/************** do_master_note() ***************
 *
 * RETURN: Bit #31 set if note consumed, or 0 if not.
 */

static uint32_t do_master_note(register unsigned char * msg, register unsigned char status)
{
	register uint32_t			mask;

	// If no robots/humans sharing this chan, we use the entire range,
	// and don't care about the sharing
	if (!(ChansInUse & (0x0001 << (status & 0x0F)))) goto chord;

#if !defined(NO_FULLKEY_MODEL) || !defined(NO_GTRCHORD_MODEL)
	// Full and Guitar models use the entire range for chords, but also
	// share this midi chan
	if (AppFlags & (APPFLAG_GTRCHORD|APPFLAG_FULLKEY)) goto notake;
#endif

	// If the master chan is set the same as any of the robots/human, then we must
	// apply the split setting to determine which of us gets this event. Whether we
	// then pass this msg to the individual robots/human depends whether Master Chan
	// is Auto. For Auto, all robots are assumed to be on the Solo chan (as far as MIDI
	// IN is concerned -- not MIDI out), will sound only if the robot "Play" is turned off,
	// and must honor the split point. If Master != Auto, and a robot/human is the same as
	// the Master chan, its range is cut off at the split point

	// Song sheet not controlling chord changes?
	if (
#ifndef NO_SONGSHEET_SUPPORT
		!isSongsheetActive() &&
#endif
		msg[0] < ((AppFlags & APPFLAG_2KEY) ? SplitPoint+1 : SplitPoint))
	{
		if (MasterMidiChan == 16)
#if !defined(NO_FULLKEY_MODEL) || !defined(NO_GTRCHORD_MODEL)
notake:
#endif
			status = 0;
chord:
		mask = midiChordTrigger(msg[0] + setTranspose(0xFE, 0 /* MIDITHREADID */), msg[1]);
		if (PendingPtr) mask = 0x90;
		return status ? mask | 0x80000000 : mask;
	}

	return 0;
}





static uint32_t do_master_ctrl(register unsigned char * msg)
{
	register uint32_t			mask;
	register unsigned char	data;

	mask = CTLMASK_NONE;
	data = msg[1];
	switch (msg[0])
	{
		case 1:
		{
			mask = tempoHandler(ACTION_FADER, data);
			data = 0xA6;
			break;
		}
		case 7:
		{
			mask = miscHandler(ACTION_FADER|ACTION_SCALE, data);
			data = 0xA7;
			break;
		}
		case 13:
		{
			mask = playHandler(ACTION_ONOFFSWITCH, data);
			data = 0xA8;
			break;
		}
		case 7+32:
		{
			msg[0] = 7;
			return -1;
		}
		case 64:
		{
			mask = set_sustain(PLAYER_SOLO, data > 63 ? 1 : 0, MIDITHREADID);
			data = 0xA9;
			break;
		}
		case 65:
		{
			mask = (0x01 << PLAYER_SOLO);
			if (data > 63) LegatoPedal |= mask;
			else LegatoPedal &= ~mask;
			data = 0xAA;
			break;
		}
		case 66:
		{
			toggle_solo_vol(2, MIDITHREADID);
			mask = CTLMASK_VOLBOOST;
			data = 0xAB;
			break;
		}
		case 67:
		{
			play_beat(1, MIDITHREADID);
			data = 0xAC;
			break;
		}
		case 69:
		{
			PlayFlags |= PLAYFLAG_STYLEJUMP;
			data = 0xAD;
			break;
		}
		case 80:
		{
			mask = reverbHandler(ACTION_ONOFFSWITCH, data);
			data = 0xAE;
			break;
		}
		case 91:
		{
			mask = reverbHandler(ACTION_FADER|ACTION_SCALE, data);
			data = 0xAF;
			break;
		}
		case 96:
		{
			mask = transposeHandler(ACTION_INCBUTTON, 0);
			data = 0xB0;
			break;
		}
		case 97:
		{
			mask = transposeHandler(ACTION_DECBUTTON, 0);
			data = 0xB1;
			break;
		}
		case 121:
		{
			allNotesOff(MIDITHREADID);
			data = 0xB2;
			break;
		}
		default:
			data = 0xB3;
	}

	if (PendingPtr) mask = data;
	return mask;
}





static uint32_t do_master_pgm(register unsigned char pgm)
{
	register uint32_t			mask;
	register unsigned char	data;

	// For program change on the Master chan, select among styles,
	// songsheets, patches, pads, basses, and drum kits in that order

	mask = CTLMASK_NONE;

	// Styles
	data = 0xA0;
	if (pgm >= getNumOfStyles())
	{
		pgm -= getNumOfStyles();
		data++;

		// Songsheets
		if (pgm >= SongsheetBox.NumOfItems)
		{
			pgm -= SongsheetBox.NumOfItems;
			data++;

			// Solo patches
			if (pgm >= getNumOfInstruments(PLAYER_SOLO))
			{
				pgm -= getNumOfInstruments(PLAYER_SOLO);
				data++;

				// Pads
				if (pgm >= 4)
				{
					pgm -= 4;
					data++;

					// Basses
					if (pgm >= getNumOfInstruments(PLAYER_BASS))
					{
						pgm -= getNumOfInstruments(PLAYER_BASS);
						data++;

						// Kits
						mask = setInstrumentByNum(PLAYER_DRUMS | SETINS_BYINDEX | SETINS_SHOWHIDDEN | MIDITHREADID, pgm, 0);
					}
					else
						mask = setInstrumentByNum(PLAYER_BASS | SETINS_BYINDEX | SETINS_SHOWHIDDEN | MIDITHREADID, pgm, 0);
				}
				else
					mask = changePadInstrument(pgm | MIDITHREADID);
			}
			else
				mask = setInstrumentByNum(PLAYER_SOLO | SETINS_BYINDEX | SETINS_SHOWHIDDEN | MIDITHREADID, pgm, 0);
		}
		else
			mask = selectSongSheet(pgm + 1, MIDITHREADID);
	}
	else
		mask = change_style(pgm, 0, MIDITHREADID);

	if (PendingPtr)
	{
		PendingPtr[2] = pgm;
		mask = data;
	}

	return mask;
}



// Cancel any tap tempo in progress
uint32_t cancel_midi_taptempo(void)
{
	if (NumTaps)
	{
		NumTaps = 0;
		setTempoLabel(0);
		if (isMainScreen()) return CTLMASK_TEMPO;
	}

	return 0;
}


static uint32_t do_drum_note(register unsigned char *, register unsigned char);
static uint32_t do_bass_note(register unsigned char, register unsigned char);
static uint32_t individualCtlr(register unsigned char *, register unsigned char);
static uint32_t findMidiSysCmd(register unsigned char, unsigned char *);
static ACTIONINSTANCE * findMidiCmd(unsigned char *, register unsigned char);

#ifndef NO_SEQ_IN_SUPPORT
static const unsigned char	SeqStatus[] = {0x90, 0x80, 0xA0, 0x00, 0xB0, 0xC0, 0xD0, 0xE0};
#endif

#pragma pack(1)
struct MIDIINDATA {
	unsigned char			RunningStatus;
	unsigned char			Data1;
	union {
	unsigned char			InputBuffer[32];
	struct epoll_event 	Event;
	};
};
#pragma pack()

/******************** midiInThread() **********************
 * A secondary thread that handles MIDI controller input.
 * Supports both ALSA Seq API, and RawMidi API. Only one of
 * those 2 is used at any given time.
 */

static void * midiInThread(void * arg)
{
	struct MIDIINDATA	data;
	unsigned char		runningCount;

	data.RunningStatus = 0x90;
	runningCount = 2;

	// Set the priority to slightly less than audio thread (AudioPlay.c), but same
	// as Beat Play thread (AccompSeq.c), and higher than gui thread
	set_thread_priority();

//	arg = GuiWinSignal(GuiApp, 0, 0);
	MidiInSigHandle = arg = (void *)3;

	for (;;)
	{
		unsigned char *	inBufEnd;

		// Wait for some midi input from controller
		epoll_wait(MidiEventHandle, &data.Event, 1, -1);

		// Main thread wants us to terminate?
		if (data.Event.data.fd == MidiEventQueue || !SoundDev[DEVNUM_MIDIIN].Handle) break;

#ifndef NO_SEQ_IN_SUPPORT
		if ((SoundDev[DEVNUM_MIDIIN].DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_SEQ)
		{
			snd_seq_event_t *ev;

			inBufEnd = &data.InputBuffer[0];
			do
			{
				if (snd_seq_event_input((snd_seq_t *)SoundDev[DEVNUM_MIDIIN].Handle, &ev) < 0) break;

				if (ev->type >= SND_SEQ_EVENT_START && ev->type <= SND_SEQ_EVENT_STOP)
					*inBufEnd++ = SysStatus[ev->type - SND_SEQ_EVENT_START];
				if (ev->type >= SND_SEQ_EVENT_NOTEON && ev->type <= SND_SEQ_EVENT_PITCHBEND)
				{
					*inBufEnd++ = SeqStatus[ev->type - SND_SEQ_EVENT_NOTEON] | ev->data.control.channel;
					if (ev->type <= SND_SEQ_EVENT_KEYPRESS)
					{
						*inBufEnd++ = ev->data.note.note;
						*inBufEnd++ = ev->data.note.velocity;
					}
					else if (ev->type <= SND_SEQ_EVENT_CHANPRESS)
					{
						if (ev->type < SND_SEQ_EVENT_PGMCHANGE)
							*inBufEnd++ = (unsigned char)ev->data.control.param;
						*inBufEnd++ = (unsigned char)ev->data.control.value;
					}
					else
					{
						register unsigned short		val;

						val = ev->data.control.value + 8192;
						*inBufEnd++ = (unsigned char)(val >> 7);
						*inBufEnd++ = (unsigned char)(val & 0x7F);
					}
				}

				snd_seq_free_event(ev);
			} while (inBufEnd < &data.InputBuffer[32 - 3]);
		}
		else
#endif
		{
			register int				count;

			if ((count = snd_rawmidi_read((snd_rawmidi_t *)SoundDev[DEVNUM_MIDIIN].Handle, &data.InputBuffer[0], sizeof(data.InputBuffer))) < 0) count=0;
			inBufEnd = &data.InputBuffer[count];
		}

		{
		unsigned char *				inBuf;
		register unsigned char		status;

		inBuf = &data.InputBuffer[0];
next_in:
		while (inBuf < inBufEnd)
		{
			// Move the byte to the head of the buffer, so when we have a complete msg,
			// the 2 or 3 bytes are RunningStatus/Data1/InputBuffer[0]
			data.InputBuffer[0] = status = *inBuf++;

			// Status byte?
			if (status & 0x80)
			{
				// Is it MIDI System realtime/common/exclusive msg?
				if (status >= 0xF0)
				{
					// Ignore active sense
					if (status == 0xFE) goto next_in;

					if (status != 0xF0 && status != 0xF7)
					{
						register uint32_t		mask;

						// Let other threads know that I'm awake and processing midi input
						if (__atomic_or_fetch(&SoundDev[DEVNUM_MIDIIN].Lock, MIDITHREADID, __ATOMIC_RELAXED) != MIDITHREADID) goto next_byte;

						// Does some thread want to siphon this msg?
						if (SiphonFunc) goto siphon;
resumeSys:
#ifndef NO_MIDICLOCK_IN
						if (is_midiclock())
						{
							mask = CLOCKTHREADID;
							switch (status)
							{
								case 0xf8:
								{
									advance_midiclock();
									goto next_byte;
								}
								case 0xfa:
								case 0xfb:
									goto start;
								case 0xfc:
									goto stop;
							}
						}
#endif
						// Ignore MIDI Clock
						if (status == 0xF8) goto next_byte;

						// Has the user mapped it to some action?
						if ((mask = findMidiSysCmd(status, 0)))
							signalMainFromMidiIn(arg, mask);

						// If MIDI Start,Stop,Continue, then implement it when
						// using internal clock, unless user assigned it otherwise
						// above
						else
#ifndef NO_MIDICLOCK_IN
						if (!is_midiclock())
#endif
						{
							mask = MIDITHREADID;
							switch (status)
							{
								case 0xfa:
								case 0xfb:
								{
start:							start_play(0, mask);
									goto next_byte;
								}
								case 0xfc:
								{
stop:								stop_play(0, mask);
									goto next_byte;
								}
							}
						}
					}

					// We have no further use for realtime/common/exclusive msgs,
					// so skip any following data bytes (except wrt realtime msgs)
					if (data.InputBuffer[0] < 0xF8) data.RunningStatus = 0;
next_byte:
					// Let other threads know I'm done processing midi, and going to sleep
					__atomic_and_fetch(&SoundDev[DEVNUM_MIDIIN].Lock, ~MIDITHREADID, __ATOMIC_RELAXED);
				}
				else
				{
					// We have the start of a midi msg.

					// Update running status, and the count of data bytes we expect will follow
					data.RunningStatus = status;
					runningCount = (status >= 0xC0 && status <= 0xDF ? 1 : 2);
				}

				goto next_in;
			}

			// Get any Midi data byte(s)

			// Are we ignoring this msg? (status == 0)
			if ((status = data.RunningStatus))
			{
				// Check if we are now starting a new msg via running status
				if (!runningCount) runningCount = (status >= 0xC0 && status <= 0xDF ? 1 : 2);

				// Do we have a complete MIDI msg?
				if (--runningCount)
				{
					// Not yet finished fetching this midi msg. Store first data byte
					// and then proceed to retrieve the next byte
					data.Data1 = data.InputBuffer[0];
				}
				else
				{
					// We got a complete MIDI msg now. Proceed to process it.
					if (status >= 0xC0 && status <= 0xDF) data.Data1 = data.InputBuffer[0];

					// Change note-off to note-on 0 vel, retaining midi chan
					if (status < 0x90)
					{
						status |= 0x90;
						data.InputBuffer[0] = 0;
					}

					if (__atomic_or_fetch(&SoundDev[DEVNUM_MIDIIN].Lock, MIDITHREADID, __ATOMIC_RELAXED) != MIDITHREADID) goto next_byte;

					// ====================== MIDI callback ======================
					// Does another thread want to siphon this msg? If so, call that siphon function. It
					// will do something with this midi msg, then report back what it wants us to do
					if (SiphonFunc)
					{
						register MIDIINSIPHON *	func;

siphon:				if ((func = SiphonFunc(arg, status >= 0xF0 ? &data.InputBuffer[0] : &data.RunningStatus)) != (MIDIINSIPHON *)-1)
						// RETURN: -1 means siphon this message, 1 means process the message as usual, 0 or
						// a ptr to some MIDIINSIPHON means to change the state of siphon (where 0 is remove
						// siphon)
						{
							if (func == (MIDIINSIPHON *)1)
							{
								// Other thread wants us to proceed with processing the msg
								// as we normally would
								if (status > 0xF0) goto resumeSys;
								goto resume;
							}

							// Other thread wants us to remove/change its siphon handler
							SiphonFunc = func;
						}

						// Other thread wants us to skip processing this msg. (ie The thread stole
						// this msg from us)
						goto next_byte;
					}

					// =======================================
					// Master chan command key mode, and note, handling
					// =======================================
					{
					register uint32_t			mask;
					register unsigned char	chan;

resume:			chan = status & 0x0f;
					mask = 0;

#if !defined(NO_DRUMPAD_MODEL) || !defined(NO_WINDCTL_MODEL)
					if ((AppFlags & (APPFLAG_DRUMPAD|APPFLAG_WINDCTL)) && (MasterMidiChan > 15 || MasterMidiChan == chan))
					{
						if (ChordTrigger == (status & 0xf0)) goto ctl;
						if ((status & 0xf0) == 0xB0 && ChordTrigger == data.Data1)
						{
							data.Data1 = data.InputBuffer[0];
ctl:						chan = (data.Data1 % 12) + 24;
							if (chan < 28) chan += 12;
							data.Data1 /= 12;
							mask = pickChord(chan, data.Data1, MIDITHREADID);
							if (PendingPtr) mask = 0x90;
							goto saveview;
						}
					}
#endif
					if (status < 0xA0)
					{
						// Apply velocity curve
						if (data.InputBuffer[0] && VelCurve) data.InputBuffer[0] = Comp1VelCurve[data.InputBuffer[0] - 1];

						// Is this a note msg on the Master channel? "Auto" always matches
						if (MasterMidiChan > 15 || MasterMidiChan == chan)
						{
							// User sets up "Setup -> Commands -> Switch Note" to designate a
							// note that switches the Master chan between invoking assigned actions, or playing chords
							if (data.Data1 == CmdSwitchNote)
							{
								// Note-off
								if (!data.InputBuffer[0])
								{
									// Has browsing occurred?
									if (CmdSwitchMode & 0x04)
									{
										// Yes, so we simply end browsing mode, but don't end
										// command key mode
										CmdSwitchMode = 0x01;
									}

									// If not CMD_ALWAYS_ON, we switch in/out of command key mode. Otherwise, we just switch browsing
									CmdSwitchMode ^= 0x03;
									if (CmdSwitchMode) goto next_byte;
									if (AppFlags4 & APPFLAG4_CMD_ALWAYS_ON)
									{
										CmdSwitchMode = 0x02;
										status = 0xc0;
										mask = CTLMASK_CMDNOTEBROWSE;
										goto saveview;
									}
								}

								else
								{
									CmdSwitchMode |= 0x01;
								}
								status = ((mask = notifyCmdMode()) ? 0xc1 : 0xc0);
								goto saveview;
							}

							// If not "auto", we use CmdSwitchNote as the split point between chords and command keys
							if (CmdSwitchMode && (MasterMidiChan > 15 || data.Data1 > (CmdSwitchNote & 0x7f))) goto cmd;

							// Cancel any tap tempo in progress
							mask = cancel_midi_taptempo();

							// If the Master chan is set to "Auto" (16), then all received note
							// events are automatically routed to the Human Solo chan, and all
							// other evts handled globally (as if received on the Master chan)
							if (MasterMidiChan > 15)
							{
								chan = MidiChans[PLAYER_SOLO];
								status = (status & 0xf0) | chan;

								// For AUTO, if user has selected bass or drums for the lower split,
								// don't play chords if play is stopped
								if (MasterMidiChan > 16 || (!BeatInPlay && (AppFlags & (APPFLAG_BASS_ON|APPFLAG_BASS_LEGATO)))) goto robotnote;
							}

							if ((mask |= do_master_note(&data.Data1, status)) & 0x80000000)
							{
								mask &= 0x7fffffff;
								if ((MidiViewInPtr = PendingPtr)) goto sigview;
								goto sigview2;
							}
						}
					}

					// Not a note
					else
					{
						// ================================================
						// See if this midi msg is mapped to an action. If so, execute it
						// ================================================
						{
						register ACTIONINSTANCE *	entry;

cmd:					if ((entry = findMidiCmd(&data.Data1, status)))
						{
							if (entry != (ACTIONINSTANCE *)-1)
							{
								register ACTION_FUNC *	func;

								func = ActionCategory[entry->CatIndex].Func;
								mask = func(entry->Action & (~ACTION_RANGELIMIT), entry->LastValue);

								// If MIDI Test screen is being shown, then there are no gui ctls displayed that
								// need updating. Instead, we need to store info on the ACTIONINSTANCE we just
								// executed, as well as the midi msg that triggered it
								if (status < 0xA0)
									status = (unsigned char)(((unsigned char *)entry - &CommandKeyAssigns[0]) / 3);
								else
									status = (unsigned char)(entry - EntryList[1]) + 1;

								if (func != &tempoHandler || entry->Action != (ACTION_INCBUTTON|ACTION_2))
									mask |= cancel_midi_taptempo();

saveview:					if ((MidiViewInPtr = PendingPtr))
								{
									MidiViewInPtr[3] = status;
									//	GuiWinSignal(GuiApp, arg, SIGNALMAIN_MIDIVIEW);
sigview:							GuiWinSignal(GuiApp, 0, SIGNALMAIN_MIDIVIEW);
								}
								else
sigview2:						if (mask) signalMainFromMidiIn(arg, mask);
							}
							goto next_byte;
						}
						}

						// ================================================
						// Master chan handling of non-note event
						// ================================================
						if (MasterMidiChan > 15 || MasterMidiChan == chan)
						{
							switch (status >> 4)
							{
								case 0x0B:
									mask = do_master_ctrl(&data.Data1);
									break;

								// For program change on the Master chan, select among styles,
								// songsheets, patches, pads, basses, and drum kits in that order
								case 0x0C:
									mask = do_master_pgm(data.Data1);

								// Note: no default handling for AFT, PRESS, and PWL on Master channel
							}

							mask |= cancel_midi_taptempo();

							// -1 means "I didn't handle it. Check the individual robot handlers
							if (mask != (uint32_t)-1)
							{
final:						status = (unsigned char)mask;
								goto saveview;
							}
							mask = 0;
						}

						// ===========================
						// Non-note for Drums/Bass/Guitar/Pad/Solo
						// ===========================
						{
						register unsigned char	musicianNum;

						musicianNum = 0;
						do
						{
							if (MidiChans[musicianNum] == chan && DevAssigns[musicianNum])
							{
								switch (status >> 4)
								{
									case 0x0B:
									{
										mask = individualCtlr(&data.RunningStatus, musicianNum);
										break;
									}

									// Pgm Change
									case 0x0C:
									{
										mask = setInstrumentByNum(musicianNum | ((BankNums[musicianNum * 2] << 8) | (BankNums[(musicianNum * 2) + 1] << 16)) | MIDITHREADID, data.Data1, 0);
										if (PendingPtr) mask = 0xBF;
	//									break;
									}
								}
							}
						} while (++musicianNum <= PLAYER_SOLO);
						}

						if (mask) goto final;
					}

					// ===========================
					// Note handling for Drums/Bass/Guitar/Pad/Solo
					// ===========================
					{
					register unsigned char	musicianNum;

robotnote:		musicianNum = 0;

//					data.Data1 += setTranspose(0xFE, 0 /* MIDITHREADID */);

					// Determine who it's for: Drummer, Bass, Guitar, Pad, and/or Human
					do
					{
						// For drums, we allow controller, and poly aftertouch, msgs to trigger drum notes, since
						// lots of electronic pads offer this option. The exception is MIDI bank select controllers
						// (#0 and #32), which we use in conjunction with MIDI program change to select kits
						if (status < (musicianNum ? 0xA0 : 0xC0) &&

							// If a musician is disabled ("Playback Device = Off"), his instrument doesn't play at all
							DevAssigns[musicianNum])
						{
							if (musicianNum == PLAYER_SOLO)
							{
								// If Master chan is AUTO, then we must honor the SplitPoint, unless FULL/GTR modes
								if (MidiChans[PLAYER_SOLO] == chan && ((AppFlags & (APPFLAG_GTRCHORD|APPFLAG_FULLKEY)) ||
									data.Data1 >= ((AppFlags & APPFLAG_2KEY) ? SplitPoint+1 : SplitPoint)))
								{
									// Output to MIDI module or internal synth. Note: Querying transpose value doesn't require threadId
									if (!data.InputBuffer[0])
										stopSoloNote(data.Data1 + setTranspose(0xFE, 0 /* MIDITHREADID */), PLAYER_SOLO|MIDITHREADID);
									else
										startSoloNote(data.Data1 + setTranspose(0xFE, 0 /* MIDITHREADID */), data.InputBuffer[0], PLAYER_SOLO|MIDITHREADID);

									mask |= 0x88;
								}
							}

							else if ((MasterMidiChan > 15 || MidiChans[musicianNum] == chan) &&

									// A robot's instrument can be played live only if that robot is muted, or play has stopped
									(!BeatInPlay || (TempFlags & (APPFLAG3_NODRUMS << musicianNum))))
							{
								// If same channel as solo (or Master=AUTO), then we must honor the SplitPoint
								if (chan == MidiChans[PLAYER_SOLO])
								{
									if (musicianNum == PLAYER_DRUMS)
									{
										// Note # within the lower split range (Setup -> Human -> Controller -> Split)?
										if (data.Data1 < SplitPoint &&

											// ..."Setup -> Human -> Solo Instrument -> Lower Split" == "Drums"
											(AppFlags & (APPFLAG_BASS_ON|APPFLAG_BASS_LEGATO)) == (APPFLAG_BASS_ON|APPFLAG_BASS_LEGATO))
										{
											goto play_dr;
										}
									}
									else if (musicianNum == PLAYER_BASS)
									{
										// Bass Split is where the lower note range of the user's controller plays one of the bass
										// patches, while the upper range simultaneously plays other patches. Typically this is useful to a
										// keyboardist who wants to play "left-hand bass" (instead of using BackupBand's robot bassist).

										// Note # within the split range?
										if (data.Data1 < SplitPoint)
										{
											// "Lower Split" is "Bass", or "Legato Bass" (not "Off" or "Drums")...
											switch (AppFlags & (APPFLAG_BASS_ON|APPFLAG_BASS_LEGATO))
											{
												case APPFLAG_BASS_ON:
												case APPFLAG_BASS_LEGATO:
													goto play_ba;
											}
										}
									}
									else if (data.Data1 >= ((AppFlags & APPFLAG_2KEY) ? SplitPoint+1 : SplitPoint))
									{
										if ((AppFlags4 & APPFLAG4_UPPER_PAD) && musicianNum == PLAYER_PAD) goto play_pad;
									}
								}

								else switch (musicianNum)
								{
									case PLAYER_DRUMS:
play_dr:								mask |= do_drum_note(&data.Data1, status);
										break;
									case PLAYER_BASS:
play_ba:								mask |= do_bass_note(data.Data1, data.InputBuffer[0]);
										break;
									case PLAYER_PAD:
									{
										// The background pad can be played by either a robot musician, or the user. When played by the
										// robot, he simply holds sustained chords. When played by the user, the pad patch (ie, strings,
										// brass, or organ) plays the same notes as (doubles) whatever other patch the user is playing
										// on the upper range of his controller. Typically this is used to "layer" sounds, such as strings
										// beneath piano. The user chooses who plays the background pad by setting "Setup -> Robots ->
										// Background pad" to either "Play" on or off.
										//
										// Regardless of who plays it, the background pad itself is turned on/off via the four buttons
										// labeled "None", "Strings", "Brass", and "Organ" on the main screen (under "Background pad").
play_pad:							if (!data.InputBuffer[0])
											stopSoloNote(data.Data1, PLAYER_PAD|MIDITHREADID);
										else
											startSoloNote(data.Data1, data.InputBuffer[0], PLAYER_PAD|MIDITHREADID);

										mask |= 0x84;
									}
								}
							}

						}
					} while (++musicianNum <= PLAYER_SOLO);
					} // Drums/Bass/Guitar/Pad/Solo

					if (!PendingPtr) mask = 0;
					goto final;
					} // resume:
				}	// Completed midi msg
			}	// Ignoring msg
		} // while (inBuf < inBufEnd)
		}
	} // for (;;)

//	GuiWinSignal(GuiApp, arg, 0);

	__atomic_and_fetch(&SoundDev[DEVNUM_MIDIIN].Lock, ~MIDITHREADID, __ATOMIC_RELAXED);
	MidiInThread = 0;
	return 0;
}








static uint32_t do_drum_note(register unsigned char * msg, register unsigned char status)
{
	if (status < 0xB0)
	{
		if (status >= 0xA0)
		{
			status = PLAYZONEFLAG_TOUCH_TRIGGER;
			goto drum;
		}

		// For drums, the "Internal Synth" ignores Note-off, and note #0 has a reserved use in startDrumNote()
		if (msg[1] && msg[0])
		{
			register unsigned char	noteNum;

			msg[1] |= addToHistory(msg[0]);
			status = 0;
drum:		noteNum = msg[0];
			if ((AppFlags & (APPFLAG_BASS_ON|APPFLAG_BASS_LEGATO)) == (APPFLAG_BASS_ON|APPFLAG_BASS_LEGATO))
				noteNum += (34 - BassOct);
			if (!startDrumNote(status, noteNum, msg[1], MIDITHREADID)) goto good;
		}
	}

	else switch ((status = msg[0]))
	{
		// Bank select doesn't trigger notes
//		case 0:
//		case 7:
//		case 32:
//			break;

		// Pedal CC? Tell startDrumNote to do some intelligent modeling
		// of hihat behavior
		case 4:
			status = 0;

		// All other controllers trigger notes
		default:
			if (!startDrumNote(PLAYZONEFLAG_CC_TRIGGER, status, msg[1], MIDITHREADID))
good:			return 0x81;
	}

	return 0;
}






static uint32_t do_bass_note(register unsigned char noteNum, register unsigned char velocity)
{
	// Apply user's "key transpose" setting
	noteNum += (setTranspose(0xFE, 0 /* MIDITHREADID */) + (28 - BassOct));

	if (velocity)
		startBassNote(noteNum, velocity, MIDITHREADID);

	// Ignore ntf if legato bass
	else if (!(AppFlags & APPFLAG_BASS_LEGATO))
		stopBassNote(noteNum, MIDITHREADID);

	// Just siphoned this note for bass
	return 0x82;
}





static uint32_t individualCtlr(register unsigned char * msg, register unsigned char musicianNum)
{
	register uint32_t	mask;

	mask = 0;

	// Output to external MIDI software/hardware if chosen by user
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[musicianNum] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		sendMidiOut(DevAssigns[musicianNum], msg, (msg[0] >= 0xC0 && msg[0] <= 0xDF) ? 2|MIDITHREADID : 3|MIDITHREADID);
		musicianNum = 0xFF;
	}
#endif

	switch (msg[1])
	{
		case 0:
		case 32:
		{
			// Save bank select controller for the above musician
			if (musicianNum != 0xFF) BankNums[(musicianNum*2) + (msg[1] ? 1 : 2)] = msg[2];
			musicianNum = 0xB5;
			break;
		}

		case 0x07:
		{
			// Volume
			if (musicianNum != 0xFF) mask = set_instrument((ACTION_FADER+2)|ACTION_SCALE, msg[2], musicianNum);
			musicianNum = 0xB6;
			break;
		}

		case 64:
		{
			if (musicianNum != 0xFF) mask = set_sustain(musicianNum, msg[2] > 63 ? 1 : 0, MIDITHREADID);
			musicianNum = 0xB7;
			break;
		}

		case 65:
		{
			if (musicianNum != 0xFF)
			{
				musicianNum = (0x01 << musicianNum);
				if (msg[2] > 63) LegatoPedal |= musicianNum;
				else LegatoPedal &= ~musicianNum;
			}
			musicianNum = 0xAA;
			break;
		}

		default:
			musicianNum = 0;
	}

	return PendingPtr ? musicianNum : mask;
}




/****************** findMidiSysCmd() ****************
 * Looks through the list of assignable actions for
 * one that is assigned to the specified MIDI realtime
 * message.
 *
 * RETURN: A bitmask of CTLID_xxx values for gui ctls
 * whose value has changed.
 *
 * Note: A realtime message can be mapped to more than
 * 1 action.
 */

static uint32_t findMidiSysCmd(register unsigned char status, unsigned char * op)
{
	register uint32_t		mask;

	if (op) *op = 0xFF;
	mask = 0;

	// Realtime/common support only Pedal, OnOffSwitch, On, or Off modes, and status matches
	// either Msg.Midi.Low (On) or Msg.Midi.High (Off)
	if (status > 0xF8)
	{
		register ACTIONINSTANCE *	entry;

		entry = EntryList[1];
		while ((char *)entry < (char *)EntryList[1] + EntryListSize[1])
		{
			register unsigned char val;

			if (entry->Midi.High == status)
			{
				if ((entry->Action & 0x03) == ACTION_PEDAL)
				{
					val = 0;
					goto found;
				}
			}
			if (status == entry->Midi.Low)
			{
				val = 1;
				switch (entry->Action & 0x03)
				{
					case ACTION_ONOFFSWITCH:
					{
 						val = entry->LastValue ^ 1;
						break;
					}

					case ACTION_DECBUTTON:
						val = 0;
    			}

found:		if (op)
				{
					*op = (unsigned char)(entry - EntryList[1]);
					break;
				}

				entry->LastValue = val;
				mask |= ActionCategory[entry->CatIndex].Func(entry->Action, val);
			}

			entry++;
		}
	}

	return mask;
}





/****************** findMidiCmd() ****************
 * Looks through the list of assignable actions for
 * one that is assigned to the received MIDI message.
 *
 * RETURN: Ptr to the ACTIONINSTANCE for the
 * matching action, or 0 if no match.
 */

static ACTIONINSTANCE * findMidiCmd(unsigned char * msg, register unsigned char status)
{
	register ACTIONINSTANCE *	entry;
	register unsigned char		cmd, id;

	id = msg[0];

	// Looking for a note command?
	if (status < 0xA0)
	{
		cmd = CmdSwitchNote & 0x7f;
		if (id > cmd && id < cmd+88)
		{
			entry = (ACTIONINSTANCE *)(&CommandKeyAssigns[(id - cmd - 1) * 3]);
			if (entry->CatIndex == 0xFF) entry = 0;

			// If tooltips on for command key mode, signal main to display the description in menubar.
			// If user is holding down the command switch note, then "browsing"
			if (inform_main_of_cmd(entry, id))
			{
				if (entry->Action >= ACTION_FADER)
				{
					cmd = entry->LastValue;
					goto used;
				}

				// For note-on, set the value to 127. Note-offs are always 0
				cmd = msg[1] ? 127 : 0;
				goto to_1_or_0;
			}
		}

ignore:
		return (ACTIONINSTANCE *)-1;
	}
	else if ((entry = EntryList[1]))
	{
		register char *	end;

		end = ((char *)EntryList[1] + EntryListSize[1]);

		// Pgm/ChanPress match only the status and data1 value. MIDI cmd ID = 0.
		// PitchWhl matches only status and MSB value. MIDI cmd ID = 0.
		cmd = msg[1];
		if (status >= 0xC0)
		{
			id = 0;
			cmd = msg[0];
		}

		while ((char *)entry < end)
		{
			if (entry->Midi.Status == status && entry->Midi.Id == id)
			{
				// ========================
				// An action that uses a range of values (fader)
				if (entry->Action >= ACTION_FADER)
				{
					// Pgm Change and Channel Pressure can optionally have a low/high range
					if (!(entry->Action & ACTION_RANGELIMIT)) goto used;

					if	(cmd >= entry->Midi.Low && cmd <= entry->Midi.High)
					{
						if (ActionCategory[entry->CatIndex].Properties & ACTIONPROP_SCALE_0) cmd -= entry->Midi.Low;
						goto used;
					}

					// If we didn't match the range, then keep checking the assigns in
					// case there's some other one that falls within our desired range
				}

				// =======================
				// Assigning to On/Off btn/pedal, or toggle switch. (ie, value must
				// be translated to 0 or 1)
				else
				{
					// If user chose a range limit (Pgm and Chan Press only), then
					// ignore any data value outside the range. Then scale the range
					// around 64 (64 is center of range)
					if (entry->Action & ACTION_RANGELIMIT)
					{
						if	(cmd > entry->Midi.High || cmd < entry->Midi.Low) goto next;
						cmd = (64 - ((entry->Midi.High - entry->Midi.Low) / 2)) + (cmd - entry->Midi.Low);
					}

					// Values < 64 = Off (0), above 63 = On (1)
to_1_or_0:		if (cmd < 64)
					{
						// Only ACTION_PEDAL actions use off value. Other actions react only to on
						if ((entry->Action & 0x03) != ACTION_PEDAL) goto ignore;
						cmd = 0;
						goto used;
					}
					cmd = 1;
					switch (entry->Action & 0x03)
					{
						case ACTION_ONOFFSWITCH:
						{
							// An ONOFFSWITCH action flips the previously received value
							cmd = entry->LastValue ^ 1;
							break;
						}

						case ACTION_DECBUTTON:
						{
							// A DECBUTTON reacts only to an on value, but considers it as off
							cmd = 0;
						}
					}

					// Save the value so caller can pass it to the action's handler. Also
					// ONOFFSWITCH will need it
used:				entry->LastValue = cmd;

					MidiViewInPtr = PendingPtr;

					return entry;
				}
			}

next:		++entry;
		} // while
	}	// Not NTN

	// No assignment
	return 0;
}









// ===========================================
// MIDI Input Viewer
// ===========================================


static void miditest_mouse(register GUIMSG * msg)
{
	handleMidiView(msg);
}

static GUIFUNCS MidiTestFuncs = {dummy_drawing,
miditest_mouse,
dummy_keypress,
0};

void startMidiTest(void)
{
	MainWin->Ctls = &MidiViewCtls[0];
	GuiFuncs = &MidiTestFuncs;
	init_midiview(0);
	MainWin->Menu = 0;
	MainWin->Flags = GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY|GUIWIN_TAB_KEY;
	MainWin->LowerPresetBtns = 0;
	GuiCtlSetSelect(GuiApp, 0, 0, &MidiViewCtls[MIDIVIEW_OK]);
}

static const char * getClockLabel(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	if (!app) TestNumBeats = 4;
	memcpy(buffer, "Clock ****", TestNumBeats + 6);
	buffer[TestNumBeats + 6] = 0;
	return buffer;
}

void updateMidiClock(void)
{
	if (SiphonFunc == midiViewSiphon)
		GuiCtlUpdate(GuiApp, MainWin, &MidiViewCtls[MIDIVIEW_CLOCK], MainWin->Ctls, 0);
}

void updateMidiView(void)
{
	if (SiphonFunc == midiViewSiphon || SiphonFunc == midiAssignSiphon)
		GuiCtlUpdate(GuiApp, MainWin, (SiphonFunc == midiAssignSiphon ? &MidiMsgGuiCtls[MAPCTL_MIDIVIEW] : &MidiViewCtls[MIDIVIEW_LIST]), 0, 0);
}

void updateMidiViewStop(void)
{
	if (SiphonFunc == midiViewSiphon)
		GuiCtlUpdate(GuiApp, MainWin, &MidiViewCtls[MIDIVIEW_PLAY], 0, 0);
}

/******************* isValidAssign() *********************
 * Called by Midi in thread to check if the received
 * midi msg can be assigned to the currently selected
 * action (Msg.Action).
 */

static int isValidAssign(register unsigned char * msg)
{
	register unsigned char	status;

	status = msg[0];

	// If a note msg, check if the user has set CmdSwitchNote
//	if (status >= 0xA0 || CmdSwitchNote <= 127)
	{
		// A system realtime msg?
		if (status >= 0xF0)
		{
			// Realtime is allowed only if the action isn't via a FADER.
			// Plus we support only some realtime
			if (Msg.Action < ACTION_FADER)
			{
				register const unsigned char *	ptr;

				ptr = &SysStatus[0];
				while (*ptr)
				{
					if (*ptr++ == status) goto good;
				}
			}

			goto bad;
		}

good:
		return 1;
	}
bad:
	return 0;
}

void storeMsgForView(register unsigned char * msg)
{
	register unsigned char *	ptr;
	register unsigned char 		status;

	// Clear the buffer?
	if (!(ptr = MidiViewInPtr))
	{
		// No selection
		MidiViewCurrPtr = 0;

		// Store a 0 uint32_t to mark the circular buffer's "start" position
		ptr = TempBuffer;
		storeLong(0, ptr + 1020);
	}
	else
	{
		// Roll over circular buffer?
		ptr += 4;
		if (ptr >= TempBuffer + 1024) ptr = TempBuffer;
	}

	storeLong(0, ptr);		// Not yet determined if we react to this msg

	// Change note-off to note-on 0 vel, retaining chan
	status = *msg++;
	if (status < 0x90) status |= 0x90;
	ptr[0] = status;

	if (status < 0xf0)
	{
		ptr[1] = *msg++;
		if (status < 0xc0) ptr[2] = *msg;
	}

	PendingPtr = ptr;
}

/************** midiAssignSiphon() ****************
 * Called by MIDI In thread for each incoming MIDI
 * msg, when entering a MIDI command assignment.
 */

static void * midiAssignSiphon(register void * signalHandle, register unsigned char * msg)
{
	if (*msg != 0xf8 && isValidAssign(msg))
	{
		storeMsgForView(msg);
		MidiViewInPtr = PendingPtr;

		// Signal GUI thread to display the received msg
//		GuiWinSignal(GuiApp, signalHandle, SIGNALMAIN_MIDIVIEW);
		GuiWinSignal(GuiApp, 0, SIGNALMAIN_MIDIVIEW);
	}

	return (void *)-1;
}

/************** midiViewSiphon() ****************
 * Called by MIDI In thread for each incoming MIDI
 * msg, when testing/viewing MIDI input.
 */

static void * midiViewSiphon(register void * signalHandle, register unsigned char * msg)
{
	// If MIDI clock, just advance the clock countoff
	if (*msg == 0xf8)
	{
		if (++TestNumTicks >= 24)
		{
			TestNumTicks = 0;
			if (++TestNumBeats >= 5) TestNumBeats = 1;
//			GuiWinSignal(GuiApp, signalHandle, SIGNALMAIN_MIDICLOCK);
			GuiWinSignal(GuiApp, 0, SIGNALMAIN_MIDICLOCK);
		}
	}
	else
		storeMsgForView(msg);

	return (MIDIINSIPHON *)1;
}

static char * copyRobotInfo(register char *, register unsigned char);

static void drawMidiEvts(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	register unsigned char *	ptr;
	unsigned char					viewFlags;

	ViewNumOfLines = viewFlags = 0;

	if (app && (ptr = MidiViewInPtr) && area && (area->Bottom - area->Top) > app->GuiFont.Height)
	{
		register unsigned short		y;

		y = area->Bottom - app->GuiFont.Height;

		while (getLong(ptr) && y >= area->Top)
		{
			register unsigned char 		data;
			register char *				str;
			unsigned short					x;

			ViewTopY = y;
			x = area->Left;

			// Highlight the currently selected event
			if (ptr == MidiViewCurrPtr)
			{
				GUIBOX	box;

				viewFlags = 0x80;
				box.X = x;
				box.Y = y;
				box.Width = ActionWidth - 1;
				box.Height = app->GuiFont.Height;
				GuiWinRect(GuiApp, &box, GUICOLOR_GREEN);
			}

			// Display the next event

			// If test view, and the event does nothing, draw in faint color
			viewFlags &= 0x80;
			if (!ptr[3] && SiphonFunc == &midiViewSiphon) viewFlags |= GUICOLOR_GRAY;

			str = &GuiBuffer[0];
			data = ptr[0];
			if (data < 0xf0)
			{
				if (data < 0xB0)
				{
					if (data < 0xA0)
					{
						if (!(viewFlags & 0x7f)) viewFlags |= (ptr[2] ? GUICOLOR_RED : GUICOLOR_DARKGRAY);
						str = copystr(str, ptr[2] ? "On  " : "Off ");
					}
					else
					{
						if (!(viewFlags & 0x7f)) viewFlags |= GUICOLOR_ORANGE;
						str = copystr(str, "Aft ");
					}
					numToPitch(str, ptr[1]);
				}
				else
				{
					if (!(viewFlags & 0x7f)) viewFlags |= (GUICOLOR_DARKGREEN + ((data >> 4) - 0x0B));
					if (data < 0xc0 && MidiViewCtls[MIDIVIEW_NAMES].Attrib.Value && (str = (char *)getGmCtl(ptr[1]))) goto name;
					str = (char *)get_status_str(data);
					if ((data >= 0xC0 && data <= 0xDF) || data >= 0xE0)
					{
						viewFlags |= 0x40;
name:					strcpy(&GuiBuffer[0], str);
					}
					else
						sprintf(&GuiBuffer[0], "%s %u", str, ptr[1]);
				}

				str = &GuiBuffer[0];
			}
			else
			{
				viewFlags |= GUICOLOR_GOLD;
				str = (char *)getSysLabel(data);
			}

			GuiTextSetColor(MainWin, viewFlags & 0x1F);
			GuiTextDraw(GuiApp, x, y, str);

			str = &GuiBuffer[0];
			x += ActionWidth;
			if (data < 0xF0)
			{
				sprintf(str, "Chan %2u Value %3u", (data & 0x0F) + 1, ptr[(viewFlags & 0x40) ? 1 : 2]);
				GuiTextDraw(GuiApp, x, y, str);
			}

			// Print info on what BackupBand does with this msg
			if (SiphonFunc == &midiViewSiphon)
			{
				register unsigned char	flag;

				x += ActionWidth + (app->GuiFont.CharWidth << 2);
				flag = ptr[3];

				if (!flag)
					GuiTextDraw(GuiApp, x, y, (data >= 0xB0 || CmdSwitchNote > 127) ? "<ignored>" : "<Unassigned note command>");

				else if (flag < 0x80)
				{
					ACTIONINSTANCE *	entry;

					if (ptr[0] < 0xA0)
						entry = (ACTIONINSTANCE *)(&CommandKeyAssigns[(flag - 1) * 3]);
					else
						entry = EntryList[1] + flag - 1;
					makeCmdStr(&ActionCategory[entry->CatIndex], str, entry->Action);
					GuiTextDraw(GuiApp, x, y, str);
				}

				if (flag >= 0xA6 && flag <= 0xB3)
				{
					register const char *	temp;

					if (flag != 0xB3) str = copystr(str, "Sets ");
					temp = &DefCtlMsgs[0];
					flag -= 0xA6;
					while (flag--)
					{
						while (*temp++);
					}
					strcpy(str, temp);

					goto show;
				}

				if (flag == 0xBF)
				{
					register const char *	temp;

					str = copystr(str, "Selects ");

					data &= 0x0f;
					for (flag=0; flag <= PLAYER_SOLO; flag++)
					{
						if (MidiChans[flag] == data)
						{
							void *	pgmPtr;

							if (str[-1] != ' ')
							{
								*str++ = ',';
								*str++ = ' ';
							}
							if (setInstrumentByNum((uint32_t)flag | ((BankNums[flag * 2] << 8)) | SETINS_NO_LSB | GUITHREADID, ptr[1], &pgmPtr))
								temp = getInstrumentName(pgmPtr);
							else
								temp = "<no instrument>";
							{
							register char	chr;

							while ((chr = *temp++))
							{
								if (chr == '_') chr = ' ';
								*str++ = chr;
							}
							}

							str = copystr(copystr(str, " for "), getMusicianName(flag));
						}
					}

					goto show;
				}

				if (flag >= 0xB5 && flag <= 0xBE)
				{
					register const char *	temp;

					str = copystr(str, "Sets ");

					temp = &IndividualCtlStrs[0];
					flag -= 0xB5;
					while (*temp && flag--)
					{
						while (*temp++);
					}
					while ((*str++ = *temp++));

					data &= 0x0f;
					temp = 0;
					for (flag=0; flag <= PLAYER_SOLO; flag++)
					{
						if (MidiChans[flag] == data)
						{
							void * pgmPtr;

							while ((pgmPtr = getNextInstrument(pgmPtr, flag)) && data--);
							strcpy(str, pgmPtr ? getInstrumentName(pgmPtr) : "<none>");
							if (!temp)
							{
								strcpy(str - 1, " for");
								str += 3;
							}
							else
								*str++ = ',';
							*str++ = ' ';
							temp = getMusicianName(flag);
							while ((*str++ = *temp++));
							--str;
						}
					}

					goto show;
				}

				if (flag >= 0x81 && flag <= 0x9F)
				{
					if (flag & 0x10)
						str = copystr(str, "Plays chords");

					if (flag & 0x01)
						str = copyRobotInfo(str, PLAYER_DRUMS);

					if (flag & 0x02)
						str = copyRobotInfo(str, PLAYER_BASS);

					if (flag & 0x04)
						str = copyRobotInfo(str, PLAYER_PAD);

					if (flag & 0x08)
						str = copyRobotInfo(str, PLAYER_SOLO);
				}

				// For program change, if control chan matches this chan, then select among styles,
				// songsheets, patches, pads, basses, and drum kits in that order
				if (flag >= 0xA0 && flag <= 0xA5)
				{
					register unsigned char	musician;
					data = ptr[2];
					str = copystr(str, "Selects ");

					flag -= 0xA0;
					switch (flag)
					{
						case 5:
						{
							musician = PLAYER_DRUMS;
							goto fpgm;
						}
						case 4:
						{
							register void * pgmPtr;

							musician = PLAYER_BASS;
fpgm:						pgmPtr = 0;
							while ((pgmPtr = getNextInstrument(pgmPtr, musician)) && data--);
							strcpy(str, pgmPtr ? getInstrumentName(pgmPtr) : "<none>");
							break;
						}

						case 3:
						{
							strcpy(str, (const char *)getPadCached(data));
							break;
						}

						case 2:
						{
							musician = PLAYER_SOLO;
							goto fpgm;
						}

						case 1:
						{
							register void * song;

							song = 0;
							while ((song = getNextSongSheet(song)) && data--);
							strcpy(str, (song ? getSongSheetName(song) : "<none>"));
							break;
						}

						case 0:
						{
							change_style(data, str, MIDITHREADID);
//							break;
						}
					}

					// Replace underscores with spaces
					while (*str)
					{
						if (*str == '_') *str = ' ';
						str++;
					}
					*str++ = ' ';

					{
					register const char *	temp;

					temp = &PgmTypes[0];
					while (flag--)
					{
						while (*temp++);
					}
					strcpy(str, temp);
					}
					goto show;
				}

				if (flag >= 0xc0 && flag <= 0xc1)
				{
					GuiTextSetColor(MainWin, GUICOLOR_LIGHTBLUE);
					str = copystr(copystr(str, " ============= Command notes o"), flag == 0xc0 ? "ff" : "n");
				}

show:			if (str > GuiBuffer) GuiTextDraw(GuiApp, x, y, GuiBuffer);
			}

			ViewNumOfLines++;
			ptr -= 4;
			if (ptr < TempBuffer) ptr = TempBuffer + 1020;
			y -= app->GuiFont.Height;
		}
	}

	if (!(viewFlags & 0x80)) MidiViewCurrPtr = 0;
}

char * copystr(register char * to, register const char * from)
{
	while ((*to++ = *from++));
	return --to;
}

static char * beginInfo(register char * str)
{
	return copystr(str, (str == GuiBuffer ? "Plays " : ", "));
}

static char * copyRobotInfo(register char * str, register unsigned char robotNum)
{
	str = beginInfo(str);
	str = copystr(str, getMusicianName(robotNum));
	str = copystr(str, " on ");
	if (DevAssigns[robotNum])
	{
		if (!(robotNum = (unsigned char)(DevAssigns[robotNum] - &SoundDev[DEVNUM_AUDIOOUT])))
			str = copystr(str, "Synth");
		else
		{
			str = copystr(str, "MIDI ");
			*str++ = robotNum + '1';
			*str = 0;
		}
	}
	else
		str = copystr(str, "<nothing>");
	return str;
}

static void init_midiview(register unsigned char assign)
{
	PlayFlags |= PLAYFLAG_STOP;

	if (TempBuffer)
	{
		TestNumTicks = TestNumBeats = TestDrawBeats = 0;

		midiViewClear();

		setMidiInSiphon(assign ? &midiAssignSiphon : &midiViewSiphon, 1);
	}
}

/****************** midiViewClear() ****************
 * Clears the incoming midi msgs.
 */

static void midiViewClear(void)
{
	MidiViewInPtr = 0;
	clearMainWindow();

	// No midi msg selected by user
	MidiMsg[0] = 0;
}

/************** handleMidiView() ******************
 * Called by GUI thread when user operates one of the
 * ctls associated with the midi input view.
 */

static int handleMidiView(register GUIMSG * msg)
{
	register GUICTL * ctl;

	ctl = msg->Mouse.SelectedCtl;
	if (ctl)
	{
		if (ctl->AreaDraw == drawMidiEvts)
		{
			register unsigned char *	ptr;

			if (SiphonFunc == &midiAssignSiphon && (ptr = MidiViewInPtr) && msg->Mouse.AbsY >= ViewTopY)
			{
				register uint32_t				numLines;

				numLines = (msg->Mouse.AbsY - ViewTopY) / GuiApp->GuiFont.Height;
				if (numLines < ViewNumOfLines)
				{
					ptr -= ((ViewNumOfLines - 1 - numLines) * 4);
					if (ptr < TempBuffer) ptr = (TempBuffer + 1024) - (TempBuffer - ptr);
					checkForSwitchNote(ptr);
				}
			}
handled:
			return 1;
		}

		if (ctl >= &MidiViewCtls[0] && ctl < &MidiViewCtls[sizeof(MidiViewCtls)/sizeof(GUICTL)])
		{
			if (ctl->Ptr) ((VOIDRETFUNC)ctl->Ptr)();
			goto handled;
		}
	}

	return 0;
}

void midiviewDone(void)
{
	if (SiphonFunc == midiViewSiphon || SiphonFunc == midiAssignSiphon)
		setMidiInSiphon(SiphonFunc, 0);
}











// ====================================
// MIDI IN device open/close
// ====================================

/******************** closeMidiIn() *******************
 * Stops MIDI In thread, and closes MIDI in.
 */

void closeMidiIn(void)
{
	register snd_rawmidi_t *	handle;

	// End any midi test display
	midiviewDone();

	handle = SoundDev[DEVNUM_MIDIIN].Handle;

	// Signal MIDI in thread to terminate
	SoundDev[DEVNUM_MIDIIN].Handle = 0;
	if (MidiInThread)
	{
		uint64_t		data;

		data = 0;
		while (MidiInThread && ++data < 10)
		{
			write(MidiEventQueue, &data, sizeof(data));
			usleep(1000);
		}

		close(MidiEventHandle);
		close(MidiEventQueue);
	}

	// Close ALSA handle
	SoundDev[DEVNUM_MIDIIN].Handle = handle;
	close_midi_port(&SoundDev[DEVNUM_MIDIIN]);

	// Reset globals
	midi_reset();
}





/******************** openMidiIn() *******************
 * Opens MIDI input device, and starts MIDI In thread.
 * Supports both ALSA Seq API, and RawMidi API. Only one of
 * those 2 is used at any given time.
 *
 * RETURN: 0 if success.
 *
 * NOTE: Displays an error msg.
 *
 * Caller must closeMidiIn() first.
 */

int openMidiIn(void)
{
	register int			err;
	const char *			message;

	message = &NoController[0];

	// Is a MIDI Input dev chosen?
	if (SoundDev[DEVNUM_MIDIIN].Card >= 0 && (SoundDev[DEVNUM_MIDIIN].DevFlags & DEVFLAG_DEVTYPE_MASK))
	{
		// User wants Alsa seq API? If so, create a "BackupBand MIDI In" port
#ifndef NO_SEQ_IN_SUPPORT
		if ((SoundDev[DEVNUM_MIDIIN].DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_SEQ)
		{
			if ((SoundDev[DEVNUM_MIDIIN].Handle = openAlsaSeq(&WindowTitle[0])))
			{
				if ((SoundDev[DEVNUM_MIDIIN].Card = snd_seq_create_simple_port((snd_seq_t *)SoundDev[DEVNUM_MIDIIN].Handle, "MIDI In", SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_SOFTWARE|SND_SEQ_PORT_TYPE_SYNTHESIZER|SND_SEQ_PORT_TYPE_APPLICATION)) < 0)
					message = "Can't create software MIDI IN";
				else
				{
					// If a client app was specified, connect to it
					if (!SoundDev[DEVNUM_MIDIIN].DevHash || snd_seq_connect_from(SoundDev[DEVNUM_MIDIIN].Handle, SoundDev[DEVNUM_MIDIIN].Card, SoundDev[DEVNUM_MIDIIN].Dev, SoundDev[DEVNUM_MIDIIN].SubDev) >= 0)
						goto handles;
 					message = "Can't connect to software MIDI IN";
				}
			}

			goto bad;
		}

		// Otherwise, connect to some hardware midi in
		else
#endif
		{
			snd_rawmidi_params_t *		params;

			sprintf(GuiBuffer, &CardSpec[0], SoundDev[DEVNUM_MIDIIN].Card, SoundDev[DEVNUM_MIDIIN].Dev, SoundDev[DEVNUM_MIDIIN].SubDev);
			if ((err = snd_rawmidi_open((snd_rawmidi_t **)&SoundDev[DEVNUM_MIDIIN].Handle, 0, GuiBuffer, SND_RAWMIDI_NONBLOCK)) < 0)
			{
				if (err == -EBUSY) message = "Another program is using your MIDI input.";
				goto bad;
			}

			if (!snd_rawmidi_params_malloc(&params))
			{
				snd_rawmidi_params_current((snd_rawmidi_t *)SoundDev[DEVNUM_MIDIIN].Handle, params);
				snd_rawmidi_params_set_avail_min((snd_rawmidi_t *)SoundDev[DEVNUM_MIDIIN].Handle, params, 1);
				snd_rawmidi_params_set_no_active_sensing((snd_rawmidi_t *)SoundDev[DEVNUM_MIDIIN].Handle, params, 1);
				snd_rawmidi_params((snd_rawmidi_t *)SoundDev[DEVNUM_MIDIIN].Handle, params);
				snd_rawmidi_params_free(params);
			}
		}

#ifndef NO_SEQ_IN_SUPPORT
handles:
#endif
		{
		register int				numFds;
		struct pollfd *			fdArray;

		message =  "Error setting up MIDI In";

		fdArray = (struct pollfd *)GuiBuffer;
#ifndef NO_SEQ_IN_SUPPORT
		if ((SoundDev[DEVNUM_MIDIIN].DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_SEQ)
		{
			numFds = snd_seq_poll_descriptors_count((snd_seq_t *)SoundDev[DEVNUM_MIDIIN].Handle, POLLIN);
			snd_seq_poll_descriptors((snd_seq_t *)SoundDev[DEVNUM_MIDIIN].Handle, fdArray, numFds, POLLIN);
		}
		else
#endif
		{
			numFds = snd_rawmidi_poll_descriptors_count((snd_rawmidi_t *)SoundDev[DEVNUM_MIDIIN].Handle);
			snd_rawmidi_poll_descriptors((snd_rawmidi_t *)SoundDev[DEVNUM_MIDIIN].Handle, fdArray, numFds);
		}

		if ((MidiEventQueue = eventfd(0, EFD_NONBLOCK)) >= 0)
		{
			if ((MidiEventHandle = epoll_create(1 + numFds)) >= 0)
			{
				struct epoll_event	eventMsg;

				eventMsg.events = EPOLLIN;
				eventMsg.data.fd = MidiEventQueue;

				if (epoll_ctl(MidiEventHandle, EPOLL_CTL_ADD, eventMsg.data.fd, &eventMsg) >= 0)
				{
					while (numFds--)
					{
						eventMsg.events = fdArray[numFds].events;
						eventMsg.data.fd = fdArray[numFds].fd;
						if (epoll_ctl(MidiEventHandle, EPOLL_CTL_ADD, eventMsg.data.fd, &eventMsg) < 0) goto bad2;
					}

					// Start up a background thread to handle midi input
					if (!pthread_create(&MidiInThread, 0, midiInThread, 0))
					{
						pthread_detach(MidiInThread);
						return 0;
					}

					message = &NoMidiInThreadMsg[0];
				}

bad2:			close(MidiEventHandle);
			}

			close(MidiEventQueue);
		}
		}

bad:	MidiInThread = 0;
		closeMidiIn();
		show_msgbox(message);
		return -1;
	}

	return 0;
}

#endif // NO_MIDI_IN_SUPPORT



/*************** positionCmdAsnGui() ****************
 * Sets initial position/scaling of GUI ctls based
 * upon font size.
 */

void positionCmdAsnGui(void)
{
#ifndef NO_MIDI_IN_SUPPORT
	GuiCtlScale(GuiApp, MainWin, &MidiMsgGuiCtls[0], -1);
	GuiCtlScale(GuiApp, MainWin, &MidiNoteGuiCtls[0], -1);
	GuiCtlScale(GuiApp, MainWin, &MidiViewCtls[0], -1);
#endif
	chooseActionsList(0);
	ActionWidth = GuiTextWidth(MainWin, VarHelp);
	GuiCtlScale(GuiApp, MainWin, &MouseGuiCtls[0], -1);
}
