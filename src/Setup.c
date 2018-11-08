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
#include "MVerb.h"
#include "PickDevice.h"
#include "Prefs.h"
#include "MidiIn.h"
#include "AccompSeq.h"
#include "AudioPlay.h"
#include "FileLoad.h"
#include "Editor.h"
#include "Setup.h"
#include "SongSheet.h"
#include "StyleData.h"
#include "Version.h"

//======================================================
// SETUP SCREEN
//======================================================

VOIDRETFUNC 			ReturnFunc;
static unsigned char	MusicianAssignChange;

static void handle_mouse(register GUIMSG * msg);

/********************** updateDevice() *********************
 * Called by doPickSoundDevDlg() when user selects an audio/midi
 * device.
 */

static void updateDevice(register struct SOUNDDEVINFO * sounddev, register unsigned char op)
{
	switch (op)
	{
		// Close currently open dev
		case 2:
		{
			if (sounddev->Handle)
			{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
				if ((sounddev->DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_AUDIO)
					freeAudio(0);
#endif
#ifndef NO_MIDI_IN_SUPPORT
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
				else
#endif
				if (sounddev->DevFlags & DEVFLAG_INPUTDEV)
					closeMidiIn();
#endif
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
#ifndef NO_MIDI_IN_SUPPORT
				else
#endif
					closeMidiOut(sounddev);
#endif
			}

			break;
		}

		// Open new dev
		case 1:
		{
			if ((sounddev->DevFlags & DEVFLAG_DEVTYPE_MASK) && sounddev->Card != -1)
			{
				doLoadDevScreen();

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
				if ((sounddev->DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_AUDIO)
				{
#if !defined(NO_JACK_SUPPORT)
					if (!SoundDev[DEVNUM_AUDIOOUT].DevHash)
					{
						register const char *	err;

						if ((err = open_libjack()))
						{
							show_msgbox(err);
							goto done;
						}
					}
					else
#endif
					{
						// Get sample rate in case user changed it
						if (userSampleRate(0xff) != setSampleRateFactor(0xff) || !setFrameSize(1))
							SaveConfigFlag |= SAVECONFIG_OTHER;
						setSampleRateFactor(userSampleRate(0xff));
					}

					loadDataSets(LOADFLAG_INSTRUMENTS);
					MusicianAssignChange = 0;
					allocAudio();
				}
#endif
#ifndef NO_MIDI_IN_SUPPORT
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
				else
#endif
				if (sounddev->DevFlags & DEVFLAG_INPUTDEV)
					openMidiIn();
#endif
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
#ifndef NO_MIDI_IN_SUPPORT
				else
#endif
					openMidiOut(sounddev);
#endif
			}

			SaveConfigFlag |= SAVECONFIG_DEVICES;
			// Fall through
		}

		// Selection cancel
		default:
		{
done:		headingShow(0);
			ReturnFunc();
		}
	}
}

void doPickOutDev(register struct SOUNDDEVINFO * sounddev, register VOIDRETFUNC func)
{
	headingCopyTo(getPlayDestDevName(sounddev - &SoundDev[0]), 0);
	headingShow(GUICOLOR_GOLD);
	ReturnFunc = func;
	// Update sample rate ctl
	userSampleRate(setSampleRateFactor(0xff));
	doPickSoundDevDlg(sounddev, updateDevice);
}

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
void doPickIntSynthDev(void)
{
	headingCopyTo(getPlayDestDevName(DEVNUM_AUDIOOUT), 0);
	headingShow(GUICOLOR_GOLD);
	userSampleRate(setSampleRateFactor(0xff));
	doPickSoundDevDlg(&SoundDev[DEVNUM_AUDIOOUT], updateDevice);
	update_shown_mask();
}

#endif






#ifndef NO_MIDI_IN_SUPPORT

static const char		ControllerStr[] = "MIDI Controller";

static uint32_t ctl_set_midi_in(register GUICTL * ctl)
{
	headingCopyTo(ControllerStr, 0);
	headingShow(GUICOLOR_GOLD);
	doPickSoundDevDlg(&SoundDev[DEVNUM_MIDIIN], updateDevice);
	update_shown_mask();

	// Not yet done setting this parameter. user must select dev..
	return 0;
}

#endif

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

static uint32_t ctl_set_intsynth_dev(register GUICTL * ctl)
{
	doPickIntSynthDev();
	return 0;
}
#endif

#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)

static uint32_t ctl_set_midiout_dev(register GUICTL * ctl)
{
	register const char *	str;
	register uint32_t	i;

	str = ctl->Label;
	str += strlen(str);
	i = str[-1] - '1';
	headingCopyTo(getPlayDestDevName(DEVNUM_MIDIOUT1 + i), 0);
	headingShow(GUICOLOR_GOLD);
	doPickSoundDevDlg(&SoundDev[DEVNUM_MIDIOUT1 + i], updateDevice);
	update_shown_mask();
	return 0;
}

#endif



static uint32_t update_robot_midichan(register GUICTL * ctl, register unsigned char robotNum)
{
	register unsigned char chan;

	chan = MidiChans[robotNum];
	ctl->Flags.Local &= ~(CTLFLAG_NO_DOWN|CTLFLAG_NO_UP);
	if (!chan) ctl->Flags.Local |= CTLFLAG_NO_DOWN;
	if (chan >= 15) ctl->Flags.Local |= CTLFLAG_NO_UP;
	return 1;
}

static uint32_t set_robot_midichan(register GUICTL * ctl, register unsigned char robotNum)
{
	register unsigned char chan;

	chan = MidiChans[robotNum];
	if (ctl->Flags.Local & CTLFLAG_DOWN_SELECT)
	{
		if (chan)
		{
			--chan;
			goto redraw;
		}
	}
	else if ((ctl->Flags.Local & CTLFLAG_UP_SELECT) && chan < 15)
	{
		chan++;
redraw:
		MidiChans[robotNum] = chan;
		updChansInUse();
		return update_robot_midichan(ctl, robotNum);
	}

	return 0;
}

static char * format_MidiChan(register char * buffer, register unsigned char chan)
{
	if (chan >= 9)
	{
		*buffer++ = '1';
		chan -= 9;
	}
	else
		chan++;
	*buffer++ = chan + '0';
	*buffer = 0;
	return buffer;
}

static void formatMidiChanLabel(register unsigned char robotNum, register char * buffer)
{
	format_MidiChan(copystr(buffer, "Channel") + 1, MidiChans[robotNum]);
}

static const char * getSoloChanLabel(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	formatMidiChanLabel(PLAYER_SOLO, buffer);
	return buffer;
}



static const char CtlNames[] = {0,'B','a','n','k','S','w',' ','H',0,
1,'M','o','d',' ','H',0,
2,'B','r','e','a','t','h',' ','H',0,
4,'F','o','o','t','P','d',' ','H',0,
5,'P',' ','T','i','m','e',' ','H',0,
6,'D','a','t','a',' ','H',0,
7,'V','o','l','u','m','e',' ','H',0,
8,'B','a','l','a','n','c',' ','H',0,
10,'P','a','n',' ','H',0,
11,'E','x','p','r','e','s',' ','H',0,
12,'E','f','f','c',' ','1',' ','H',0,
13,'E','f','f','c',' ','2',' ','H',0,
16,'G','e','n','e','r','l',' ','1',0,
17,'G','e','n','e','r','l',' ','2',0,
18,'G','e','n','e','r','l',' ','3',0,
19,'G','e','n','e','r','l',' ','4',0,
32,'B','a','n','k','S','w',' ','L',0,
33,'M','o','d',' ','L',0,
34,'B','r','e','a','t','h',' ','L',0,
36,'F','o','o','t','P','d',' ','L',0,
37,'P',' ','T','i','m','e',' ','L',0,
38,'D','a','t','a',' ','L',0,
39,'V','o','l','u','m','e',' ','L',0,
40,'B','a','l','a','n','c',' ','L',0,
42,'P','a','n',' ','L',0,
43,'E','x','p','r','e','s',' ','L',0,
44,'E','f','f','c',' ','1',' ','L',0,
45,'E','f','f','c',' ','2',' ','L',0,
64,'H','o','l','d',' ','P','e','d',0,
65,'P','o','r','t','a',' ','O','n',0,
66,'S','u','s','t','e','n','u','t',0,
67,'S','o','f','t',' ','P','e','d',0,
68,'L','e','g','a','t','o','P','d',0,
69,'H','o','l','d','2','P','e','d',0,
70,'S','n','d',' ','V','a','r','i',0,
71,'T','i','m','b','r','e',0,
72,'R','e','l',' ','T','i','m','e',0,
73,'A','t','k',' ','T','i','m','e',0,
74,'B','r','i','g','h','t','n','s',0,
75,'S','n','d','C','t','l',' ','6',0,
76,'S','n','d','C','t','l',' ','7',0,
77,'S','n','d','C','t','l',' ','8',0,
78,'S','n','d','C','t','l',' ','9',0,
79,'S','n','d','C','t','l','1','0',
80,'G','e','n','e','r','l',' ','5',0,
81,'G','e','n','e','r','l',' ','6',0,
82,'G','e','n','e','r','l',' ','7',0,
83,'G','e','n','e','r','l',' ','8',0,
91,'E','f','f','e','c','t','s',0,
92,'T','r','e','m','u','l','o',0,
93,'C','h','o','r','u','s',0,
94,'C','e','l','e','s','t','e',0,
95,'P','h','a','s','e','r',0,
96,'D','a','t','a',' ','+',0,
97,'D','a','t','a',' ','-',0,
98,'N','R','P','N',' ','L',0,
99,'N','R','P','N',' ','H',0,
100,'R','P','N',' ','L',0,
101,'R','P','N',' ','H',0,
120,'S','o','u','n','d','O','f','f',0,
121,'C','o','n','t','l','O','f','f',0,
122,'L','o','c','a','l','K','e','y',0,
123,'N','o','t','e','s','O','f','f',0,
124,'O','m','n','i',' ','O','f','f',0,
125,'O','m','n','i',' ','O','n',0,
126,'M','o','n','o',' ','O','n',0,
127,'P','o','l','y',' ','O','n',0,
-1};

/********************* getGmCtl() *********************
 * Retrieves the General MIDI Controller Name for a
 * controller number.
 */

const char * getGmCtl(unsigned char ctlNum)
{
	register const char *	ptr;

	ptr = &CtlNames[0];
	while (ctlNum > (unsigned char)ptr[0]) ptr += (strlen(ptr + 1) + 2);
	return (ctlNum == (unsigned char)ptr[0] ? ptr + 1 : 0);
}





















#ifndef NO_MIDI_IN_SUPPORT

// =========================================================
// Set split, command note, bass octave
// =========================================================

static const char	PlayNoteStr[] = "Play the desired key for the split point on your MIDI controller. (Typically the center key.) I'm listening for it...";

static void * setNotePoint(register void * signalHandle, register unsigned char * msg)
{
	if (msg[0] >= 0x90 && msg[0] <= 0x9F && msg[2])
	{
		register unsigned char	note;

		note = msg[1];
		if (isNoteAssign())
		{
		   if (note <= (CmdSwitchNote & 0x7f) || note >= (CmdSwitchNote & 0x7f) + 88)
			{
				GuiWinSignal(GuiApp, 0, SIGNALMAIN_CMDSWITCHERR);
				goto ignore;
			}

			note -= (CmdSwitchNote & 0x7f);
		}

		// Settings must be saved to disk
		if (ReturnFunc == &doSetupScreen) SaveConfigFlag |= SAVECONFIG_OTHER;

		// Set the note
		*BytePtr = note;

		// Wake up main thread
//		GuiWinSignal(GuiApp, signalHandle, SIGNALMAIN_MIDIIN);	// lv2 plugin needs sig
		GuiWinSignal(GuiApp, 0, SIGNALMAIN_MIDIIN);

		// Don't siphon any more
		return 0;
	}
ignore:
	// Continue siphoning midi input
	return (void *)-1;
}

void endNotePoint(void)
{
	setMidiInSiphon(&setNotePoint, 0);
	headingShow(0);
	ReturnFunc();
}

static void note_mouse(register GUIMSG * msg)
{
	if (msg->Mouse.SelectedCtl->PresetId == GUIBTN_DEL) *BytePtr = 128;
	endNotePoint();
}

static GUIFUNCS NoteFuncs = {dummy_drawing,
note_mouse,
dummy_keypress,
0};

void doNoteScreen(register const char * title, register unsigned char delFlag)
{
	headingCopyTo(title, 0);
	headingShow(GUICOLOR_PURPLE);
	MainWin->Ctls = 0;
	MainWin->Menu = 0;
	MainWin->Flags = GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY;
	MainWin->LowerPresetBtns = (delFlag) ? GUIBTN_DEL_SHOW|GUIBTN_CANCEL_SHOW|GUIBTN_CENTER : GUIBTN_CANCEL_SHOW|GUIBTN_CENTER;
	GuiFuncs = &NoteFuncs;
	clearMainWindow();
	GuiCtlSetSelect(GuiApp, 0, 0, (GUICTL *)0 + GUIBTN_CANCEL);

	// Tell Midi In thread to pass each midi msg to setNotePoint()
	setMidiInSiphon(&setNotePoint, 1);
}

#endif	// NO_MIDI_IN_SUPPORT








static const char HumanHelpStr[] = "The \7Chords Model \1determines how you wish to play chords upon your controller. \2Split Piano \1means that you intend to divide your controller's note \
range into two halves. The bottom half is where you will play chords for BackupBand to follow, and the top half is where you can play anything you'd like, such as a solo. \2One finger \1 model \
allows you to play minor or major chords with 1 or 2 fingers (on your controller's bottom half). For a major chord, just play the root note. (Play a C note for a C major chord). For a minor \
chord, add the note a half or whole step up. (Play a C note, along with either a Db or D note, for a C minor chord). \2Full piano \1means you intend to play a two-handed freeform part, across \
the entire note range, and BackupBand should deduce chords from that. \2Sensitivity \1sets how many notes you must simultaneously play before BackupBand will recognize a chord. For \
example, if Sensitivity is 4, then you must hold at least4 notes simultaneously in order to change the chord.\n\2Foot (or Bass) pedals \1indicates you will play minor or major chords \
on at least a full octave (C to high C) of pedals. For a major chord, just play the root pedal. For a minor chord, play the root pedal, simultaneously with the high C pedal. You must \
set the high C pedal as BackupBand's Split note.\n\2Wind model \1uses 2 devices. One device is a monophonic controller where you play the root note. The other device is a switch or \
pedal that you press for a minor chord, or release for major. If you use a USB pedal that emulates a mouse, then set \2Minor by \1 to \"Mouse\", and click the adjacent button to configure \
the pedal. If you instead use a pedal that sends MIDI controller, then set \2Minor by \1 to \"Controller\", and click the adjacent button to enter the controller number.\n\2Guitar \1model \
is for a MIDI guitar. BackupBand's robot musicians change chord only when you strum a chord on at least 4 strings.\n\2Drum Pads \1model assigns a different chord to each midi note \
number. Alternately you can set \2Trigger by \1to instead use Aftertouch or Program Change numbers. Or you can use one MIDI Controller. See the manual for a chart.\n\2Change \
on \1determines whether the robots can change the chord on the downbeat of every measure, or only the first and third beats.\nWhen \2Chord Hold \1 is on, then the robots continue \
playing the chord even after you release the notes on your controller. When off, the bass, guitar, and pad robots play only \
while you're sustaining \(holding) the notes of the chord. The drummer continues regardless.\nThe \7MIDI Controller \1section \
is where you set what MIDI device you wish BackupBand's robot musicians to \"follow\" for changing chords, and receiving \
remote commands. Click on the \2Device \1button to choose one device from a list of external MIDI hardware, or other software programs, which BackupBand can follow. Click the Split button, and \
play the note on your controller where you want to split it into 2 note ranges. Note that \2Full Piano \1and \2Guitar \1chord models do not need the split note set.\nThe \
\2Test \1button opens a screen that displays information about each MIDI message received from your controller. Also indicated is what BackupBand does with that message.\nThe \
\2Master Chan \1indicates what MIDI channel you will set your controller to play chords upon. This channel is also used to control Master settings that affect all robots, such as transpose, tempo, \
etc. If set it to \"Auto\", then all notes from your controller (on any channel) will play chords below the split point. Also, all non-note events will control Master settings.\nThe \
\7Solo Instrument \1section are settings for the upper half of your controller (above the split note). You can play any of BackupBand's sampled instruments on its Internal Synth. Or \
you can set the playback to one of the 4 MIDI choices of software synths or external MIDI hardware synths. \2Channel \1determines which MIDI channel you will set your controller to play the \
\"Solo\" instrument. If the Master channel is set to \"Auto\", then the solo channel is ignored, and all notes above the split point play the Solo patch.\n\2Bass Split \1 is set only if you \
want to play the Bass patch below the split note. You must turn off the Bass robot if you don't want him to steal your bass when a style starts to play.";






// =========================================================
// Human Settings
// =========================================================

/******************** numToPitch() *********************
 * Converts a MIDI note number to a note name (ie,
 * nul-terminated string). For example, MIDI note number 60
 * returns "C 3". Note number 61 returns "C#3", etc. (Note
 * "A -2", so the first octave is -2). The
 * resulting string is stored in the passed buffer (which should
 * be large enough to hold at least 5 characters including nul).
 */

static unsigned char	NtnNams[] = {'C',' ','C','#','D',' ','D','#','E',' ','F',' ','F','#','G',' ','G','#','A',' ','A','#','B',' ','C'};

void numToPitch(void * buffer, unsigned char noteNum)
{
	register unsigned char *	ptr;
	register unsigned char *	name;
	register unsigned char		oct;

	// save ptr
	ptr = (unsigned char *)buffer;

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
	*ptr = 0;
}

#ifndef NO_MIDI_IN_SUPPORT
static uint32_t ctl_set_split(register GUICTL * ctl)
{
	BytePtr = &SplitPoint;
	ReturnFunc = &doSetupScreen;
	doNoteScreen(&PlayNoteStr[0], 0);

	// Not yet done setting this parameter. user must enter data..
	return CTLMASK_NONE;
}

static const char * getSplitStr(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	numToPitch(buffer, SplitPoint);
	return buffer;
}

static const char * getMasterChanLabel(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	register char *	ptr;
	register unsigned char val;

	ptr = copystr(buffer, "Master Chan") + 1;
	val = MasterMidiChan;
	if (val > 15)
		strcpy(ptr, val > 16 ? "Basic" : "Auto");
	else
		format_MidiChan(ptr, val);
	return buffer;
}

static uint32_t ctl_update_masterchan(register GUICTL * ctl)
{
	ctl->Flags.Local &= ~(CTLFLAG_NO_DOWN|CTLFLAG_NO_UP);
	if (!MasterMidiChan) ctl->Flags.Local |= CTLFLAG_NO_DOWN;
	if (MasterMidiChan >= 17) ctl->Flags.Local |= CTLFLAG_NO_UP;
	return 1;
}

static uint32_t ctl_set_masterchan(register GUICTL * ctl)
{
	if (ctl->Flags.Local & CTLFLAG_DOWN_SELECT)
	{
		if (MasterMidiChan)
		{
			--MasterMidiChan;
			goto redraw;
		}
	}
	else if ((ctl->Flags.Local & CTLFLAG_UP_SELECT) && MasterMidiChan < 17)
	{
		MasterMidiChan++;
redraw:
		return ctl_update_masterchan(ctl);
	}

	return CTLMASK_NONE;
}

static uint32_t ctl_set_midi_test(register GUICTL * ctl)
{
	startMidiTest();
	return CTLMASK_NONE;
}

static GUICTLDATA	MidiInTestFunc = {ctl_update_nothing, ctl_set_midi_test};
static GUICTLDATA	MasterChanFunc = {ctl_update_masterchan, ctl_set_masterchan};
static GUICTLDATA	SplitFunc = {ctl_update_nothing, ctl_set_split};
static GUICTLDATA	MidiInDevFunc = {ctl_update_nothing, ctl_set_midi_in};

#endif
















// ===================================================
// Human
// ===================================================
static GUICTL * get_trigger_by(void);

static const char	ChordSensStr[] = "Sensitivity";
static const char	CModelsStr[] = {'M','o','d','e','l',':',0,'S','p','l','i','t',' ','P','i','a','n','o',0,'F','u','l','l',' ','P','i','a','n','o',0,
	'1',' ','F','i','n','g','e','r',0,	'F','o','o','t',' ','P','e','d','a','l','s',0,
	'B','r','e','a','t','h','/','W','i','n','d',0,'G','u','i','t','a','r',0,'D','r','u','m','/','P','a','d','s',0};
static const char	ChordBoundStr[] = "Change on\0Every Beat\0Beats 1+3";
static const char	DrumChordStrs[] = "Trigger by\0Note\0Aftertouch\0Controller\0Pgm";
static const char	WindChordStrs[] = "Minor by\0Mouse\0Controller";
static unsigned char TempChordTrigger;
static void updateChordParams();

static void storeIdVal(register GUINUMPAD * numpad)
{
	if (numpad && numpad->Value < 128)
	{
		TempChordTrigger = (unsigned char)numpad->Value;
		SaveConfigFlag |= SAVECONFIG_OTHER;
	}
	doSetupScreen();
}

static uint32_t ctl_set_chordmsgc(register GUICTL * ctl)
{
	if ((AppFlags & APPFLAG_DRUMPAD) || ctl[-1].Attrib.Value)
		doNumeric(storeIdVal, 0, 0);
	else
	{
		doMouseChordBtn();
		SaveConfigFlag |= SAVECONFIG_OTHER;
	}
	return 0;
}

static const char * get_chord_vlabel(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	--ctl;
	if (((AppFlags & APPFLAG_WINDCTL) && !ctl->Attrib.Value) || !app) setMouseBtn(buffer, app ? 1 : 0);
	else
	{
		register const char *	str;
		register unsigned char	ctlNum;

		ctlNum = TempChordTrigger < 127 ? TempChordTrigger : 64;
		str = getGmCtl(ctlNum);
		sprintf(buffer, "%u", ctlNum);
		if (str) sprintf(buffer + strlen(buffer), " (%s)", str);
	}
	return buffer;
}

static const char * get_chord_label(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	if (!app) return &WindChordStrs[15];
	return (AppFlags & APPFLAG_WINDCTL) ? WindChordStrs : DrumChordStrs;
}

static uint32_t ctl_set_chordmsgs(register GUICTL * ctl)
{
	if (AppFlags & APPFLAG_DRUMPAD) GuiCtlShow(GuiApp, MainWin, ctl + 1, (ctl->Attrib.Value == 2) ? 0 : CTLTYPE_HIDE);
	else GuiCtlUpdate(GuiApp, MainWin, ctl + 1, 0, 0);
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_set_chordsens(register GUICTL * ctl)
{
	setChordSensitivity(ctl->Attrib.Value + 1);
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_chordsens(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, setChordSensitivity(0xff) - 1);
	return 1;
}

static uint32_t ctl_set_ctype(register GUICTL * ctl)
{
	register unsigned char val;

	AppFlags &= ~(APPFLAG_2KEY|APPFLAG_1FINGER|APPFLAG_WINDCTL|APPFLAG_GTRCHORD|APPFLAG_FULLKEY|APPFLAG_DRUMPAD);
	clearChord(0|GUITHREADID);

	if ((val = ctl->Attrib.Value))
		AppFlags |= (APPFLAG_FULLKEY << (val - 1));

	val = (AppFlags & APPFLAG_GTRCHORD) ? 3 : 1;
	if (AppFlags & APPFLAG_FULLKEY) val = 2;
	setChordSensitivity(val);
	do
	{
		ctl++;
	} while (ctl->Label != &ChordSensStr[0]);
	ctl_update_chordsens(ctl);
	GuiCtlUpdate(GuiApp, 0, ctl, 0, 0);

	// Done setting this parameter. Let caller redraw the ctl
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_ctype(register GUICTL * ctl)
{
	register unsigned char	val, flags;

	val = 0;
	if ((flags = (AppFlags & (APPFLAG_2KEY|APPFLAG_1FINGER|APPFLAG_WINDCTL|APPFLAG_GTRCHORD|APPFLAG_FULLKEY|APPFLAG_DRUMPAD))))
	{
		flags >>= 1;
		do
		{
			val++;
			flags >>= 1;
		} while (!(flags & 0x01));
	}
	GuiCtlArrowsInit(ctl, val);

	updateChordParams();

	return 1;
}

static uint32_t ctl_set_bound(register GUICTL * ctl)
{
	ChordBoundary = (ChordBoundary == 24 ? 24*2 : 24);

	// Done setting this parameter. Let caller redraw the ctl
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_bound(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, ChordBoundary != 24);
	return 1;
}

#ifndef NO_MIDI_IN_SUPPORT

static uint32_t ctl_set_curve(register GUICTL * ctl)
{
	VelCurve = ctl->Attrib.Value;
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_curve(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, VelCurve);
	return 1;
}


static uint32_t ctl_set_lower_split(register GUICTL * ctl)
{
	AppFlags &= ~(APPFLAG_BASS_LEGATO|APPFLAG_BASS_ON);
	AppFlags |= (ctl->Attrib.Value - 1);
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_lower_split(register GUICTL * ctl)
{
	ctl->Attrib.Value = (AppFlags & (APPFLAG_BASS_LEGATO|APPFLAG_BASS_ON)) + 1;
	return 1;
}

static uint32_t ctl_set_upper_split(register GUICTL * ctl)
{
	AppFlags4 &= ~APPFLAG4_UPPER_PAD;
	if (ctl->Attrib.Value) AppFlags4 |= APPFLAG4_UPPER_PAD;
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_upper_split(register GUICTL * ctl)
{
	ctl->Attrib.Value = (AppFlags4 & APPFLAG4_UPPER_PAD) ? 2 : 1;
	return 1;
}

static const char * getBassOctLabel(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	sprintf(buffer, "%d", (BassOct - 28) / 12);
	return buffer;
}

static uint32_t ctl_set_low_oct(register GUICTL * ctl)
{
	BytePtr = (unsigned char *)&BassOct;
	ReturnFunc = &doSetupScreen;
	doNoteScreen("Play the note on your controller where you want the bass low E to sound.", 0);
	return CTLMASK_NONE;
}

static GUICTLDATA	LowerFunc = {ctl_update_lower_split, ctl_set_lower_split};
static GUICTLDATA	LowerOctFunc = {ctl_update_nothing, ctl_set_low_oct};
static GUICTLDATA	UpperFunc = {ctl_update_upper_split, ctl_set_upper_split};
static GUICTLDATA	CurveFunc = {ctl_update_curve, ctl_set_curve};

#endif

static uint32_t ctl_set_solo_midichan(register GUICTL * ctl)
{
	return set_robot_midichan(ctl, PLAYER_SOLO);
}

static uint32_t ctl_update_solo_midichan(register GUICTL * ctl)
{
	return update_robot_midichan(ctl, PLAYER_SOLO);
}

#if defined(NO_ALSA_AUDIO_SUPPORT) && defined(NO_JACK_SUPPORT)
#define BUSSCNT 5
#elif !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
#define BUSSCNT 6
#else
#define BUSSCNT 2
#endif

static uint32_t set_playdev_assign(register GUICTL * ctl, register uint32_t num)
{
	if (ctl->Attrib.Value >= BUSSCNT - 1)
		ctl = (GUICTL *)0;
	else
		ctl = (GUICTL *)&SoundDev[DEVNUM_AUDIOOUT
#if defined(NO_ALSA_AUDIO_SUPPORT) && defined(NO_JACK_SUPPORT)
		 + 1
#endif
		 + ctl->Attrib.Value];
	DevAssigns[num] = (struct SOUNDDEVINFO *)ctl;
	MusicianAssignChange |= (0x01 << num);
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t get_playdev_assign(register GUICTL * ctl, register uint32_t roboNum)
{
	register struct SOUNDDEVINFO *	dev;

	if (!(dev = DevAssigns[roboNum]))
		roboNum = BUSSCNT - 1;
	else
	{
		roboNum = (dev - &SoundDev[DEVNUM_AUDIOOUT]);
#if defined(NO_ALSA_AUDIO_SUPPORT) && defined(NO_JACK_SUPPORT)
		roboNum--;
#endif
		if (roboNum >= BUSSCNT) roboNum = BUSSCNT - 1;
	}

	GuiCtlArrowsInit(ctl, roboNum);
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_set_solo_playdev(register GUICTL * ctl)
{
	return set_playdev_assign(ctl, PLAYER_SOLO);
}

static uint32_t ctl_update_solo_playdev(register GUICTL * ctl)
{
	return get_playdev_assign(ctl, PLAYER_SOLO);
}

static uint32_t ctl_set_chordhold(register GUICTL * ctl)
{
	AppFlags4 ^= APPFLAG4_NOCHORDHOLD;
	return clearChord(2|GUITHREADID) | CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_chordhold(register GUICTL * ctl)
{
	ctl->Attrib.Value = (AppFlags4 & APPFLAG4_NOCHORDHOLD) ? 0 : 1;

	return 1;
}

static const char MusicianNameStrs[] = "Drums\0Bass\0Guitar\0Pad\0Human";
static const char PlayDestinationNames[] = "Internal Synth\0Ext Synth 1\0Ext Synth 2\0Ext Synth 3\0Ext Synth 4";
#ifndef NO_MIDI_IN_SUPPORT
static const char	LowerStr[] = "Off\0Bass\0Legato Bass\0Drums";
static const char	UpperStr[] = "Off\0Pad\0Upper Layer";
static const char	CurveStr[] = "Vel Curve\0Normal\0Compress";
#endif

#ifndef NO_QWERTY_PIANO
static uint32_t ctl_set_qwerty(register GUICTL * ctl)
{
	AppFlags2 ^= APPFLAG2_QWERTYKEYS;
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_qwerty(register GUICTL * ctl)
{
	ctl->Attrib.Value = ((AppFlags2 & APPFLAG2_QWERTYKEYS) ? 2 : 1);
	return 1;
}

static GUICTLDATA	QwertyFunc = {ctl_update_qwerty, ctl_set_qwerty};
static const char	QwertyStrs[] = "Off\0On\0Qwerty Piano";
#endif

#if defined(NO_ALSA_AUDIO_SUPPORT) && defined(NO_JACK_SUPPORT)
static const char PlayDevStr[] = "Playback on\0Ext Synth 1\0Ext Synth 2\0Ext Synth 3\0Ext Synth 4\0Off";
#elif !defined(NO_MIDI_OUT_SUPPORT)
static const char PlayDevStr[] = "Playback on\0Internal Synth\0Ext Synth 1\0Ext Synth 2\0Ext Synth 3\0Ext Synth 4\0Off";
#else
static const char PlayDevStr[] = "Playback\0On\0Off";
#endif

static GUICTLDATA	ChordTypeFunc = {ctl_update_ctype, ctl_set_ctype};
static GUICTLDATA	ChordBoundFunc = {ctl_update_bound, ctl_set_bound};
static GUICTLDATA	SoloPlayDevFunc = {ctl_update_solo_playdev, ctl_set_solo_playdev};
static GUICTLDATA	SoloMidiChanFunc = {ctl_update_solo_midichan, ctl_set_solo_midichan};
static GUICTLDATA	ChordHoldFunc = {ctl_update_chordhold, ctl_set_chordhold};
static GUICTLDATA	ChordSensFunc = {ctl_update_chordsens, ctl_set_chordsens};
static GUICTLDATA	ChordMsgStatus = {ctl_update_nothing, ctl_set_chordmsgs};
static GUICTLDATA	ChordMsgCtl = {ctl_update_nothing, ctl_set_chordmsgc};

static GUICTL		HumanCtls[] = {
	// ==================== Chords
 	{.Type=CTLTYPE_ARROWS, .Label=CModelsStr,			.Ptr=&ChordTypeFunc,						.Attrib.NumOfLabels=7,	.Flags.Global=CTLGLOBAL_GROUPSTART|CTLGLOBAL_AUTO_VAL},
	{.Type=CTLTYPE_ARROWS, .BtnLabel=&get_chord_label,	.Ptr=&ChordMsgStatus,				.Attrib.NumOfLabels=4,	.Flags.Global=CTLGLOBAL_AUTO_VAL|CTLGLOBAL_GET_LABEL},
 	{.Type=CTLTYPE_PUSH,	  .BtnLabel=&get_chord_vlabel,	.Ptr=&ChordMsgCtl,				.Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_NOPADDING|CTLGLOBAL_GET_LABEL},
 	{.Type=CTLTYPE_ARROWS, .Label=ChordBoundStr,		.Ptr=&ChordBoundFunc,			.Y=1,	.Attrib.NumOfLabels=2},
	{.Type=CTLTYPE_CHECK,  .Label="Chord Hold",		.Ptr=&ChordHoldFunc,				.Y=1,	.Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_AUTO_VAL},
  	{.Type=CTLTYPE_GROUPBOX, .Y=2, .Label="Chords"},
 	{.Type=CTLTYPE_ARROWS, .Label=ChordSensStr,		.Ptr=&ChordSensFunc,						.Attrib.NumOfLabels=5, 	.MinValue=2, .Flags.Local=CTLFLAG_NO_DOWN|CTLFLAG_NOSTRINGS, .Flags.Global=CTLGLOBAL_AUTO_VAL},

#ifndef NO_MIDI_IN_SUPPORT
	// ==================== Solo instrument
  	{.Type=CTLTYPE_ARROWS,	.Y=2,	.BtnLabel=getSoloChanLabel,.Ptr=&SoloMidiChanFunc,			.Attrib.NumOfLabels=1,		 		.Flags.Global=CTLGLOBAL_GROUPSTART|CTLGLOBAL_GET_LABEL},
 	{.Type=CTLTYPE_ARROWS,	.Y=2,	.Label=VolStr,					.Ptr=(void *)CTLSTR_PATCHVOL, .Attrib.NumOfLabels=100+1, .Flags.Local=CTLFLAG_NOSTRINGS|CTLFLAG_NO_UP,	.Flags.Global=CTLGLOBAL_PRESET},
 	{.Type=CTLTYPE_ARROWS,	.Y=2,	.Label=PlayDevStr,			.Ptr=&SoloPlayDevFunc,			.Attrib.NumOfLabels=BUSSCNT, .Flags.Local=CTLFLAG_NO_DOWN,	.Flags.Global=CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_RADIO,	.Y=3, .X=1,	.Label=&LowerStr[0],	.Ptr=&LowerFunc,					.Attrib.NumOfLabels=4, 				.Flags.Global=CTLGLOBAL_GROUPSTART|CTLGLOBAL_AUTO_VAL},
	{.Type=CTLTYPE_STATIC,	.Y=3, .Label="Octave:",														.Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_PUSH, 	.Y=3, .BtnLabel=getBassOctLabel,	.Ptr=&LowerOctFunc, 				.Attrib.NumOfLabels=1, .Width=3,	.Flags.Global=CTLGLOBAL_NOPADDING|CTLGLOBAL_GET_LABEL},
  	{.Type=CTLTYPE_GROUPBOX, .Label="Lower split"},
 	{.Type=CTLTYPE_RADIO,	.Y=4, .X=1,	.Label=&UpperStr[0],.Ptr=&UpperFunc,					.Attrib.NumOfLabels=2, .Flags.Local=CTLFLAG_LABELBOX, .Flags.Global=CTLGLOBAL_AUTO_VAL},
	{.Type=CTLTYPE_ARROWS, 	.Y=4,	.Label=&CurveStr[0],			.Ptr=&CurveFunc,					.Attrib.NumOfLabels=2,				.Flags.Global=CTLGLOBAL_AUTO_VAL},
  	{.Type=CTLTYPE_GROUPBOX, .Label="Solo instrument"},

  	// =================== Controller
 	{.Type=CTLTYPE_PUSH,		.Label="Device",					.Y=5,	.Attrib.NumOfLabels=1, .Ptr=&MidiInDevFunc, .Flags.Global=CTLGLOBAL_GROUPSTART},
 	{.Type=CTLTYPE_ARROWS,	.BtnLabel=getMasterChanLabel,.Y=5,	.Attrib.NumOfLabels=1, .Ptr=&MasterChanFunc, .Flags.Global=CTLGLOBAL_GET_LABEL},
#ifndef NO_QWERTY_PIANO
 	{.Type=CTLTYPE_RADIO,	.Y=5,	.X=1, .Label=&QwertyStrs[0],.Ptr=&QwertyFunc,				.Attrib.NumOfLabels=2,		 .Flags.Local=CTLFLAG_LABELBOX},
#endif
 	{.Type=CTLTYPE_PUSH,		.Label="Test",						.Y=6,	.Attrib.NumOfLabels=1, .Ptr=&MidiInTestFunc},
 	{.Type=CTLTYPE_STATIC,	.Label="Split\nNote:",			.Y=6,	.Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_PUSH, 	.BtnLabel=getSplitStr,			.Y=6,	.Attrib.NumOfLabels=1, .Ptr=&SplitFunc,  .Width=7,		.Flags.Global=CTLGLOBAL_NOPADDING|CTLGLOBAL_GET_LABEL},
 	{.Type=CTLTYPE_GROUPBOX, .Label=ControllerStr},
#elif !defined(NO_QWERTY_PIANO)
 	{.Type=CTLTYPE_RADIO,	.Y=2,	.X=1, .Label=&QwertyStrs[0],.Ptr=&QwertyFunc,				.Attrib.NumOfLabels=2,		 .Flags.Local=CTLFLAG_LABELBOX},
#endif
	{.Type=CTLTYPE_END},
};

static GUICTL * get_trigger_by(void)
{
	register GUICTL *			ctl;

	ctl = &HumanCtls[0];
	do
	{
		ctl++;
	} while (ctl->BtnLabel != &get_chord_label);

	return ctl;
}

static void updateChordParams(void)
{
	register GUICTL *			ctl;
	register unsigned char	flag;

	ctl = get_trigger_by();

	if (AppFlags & (APPFLAG_DRUMPAD|APPFLAG_WINDCTL))
	{
		flag = 0;
		ctl->Attrib.NumOfLabels = (AppFlags & APPFLAG_WINDCTL) ? 2 : 4;
	}
	else
		flag = CTLTYPE_HIDE;
	GuiCtlShow(GuiApp, MainWin, ctl, flag);
	GuiCtlShow(GuiApp, MainWin, ctl + 1, (!flag && (ctl->Attrib.Value == 2 || (AppFlags & APPFLAG_WINDCTL))) ? 0 : CTLTYPE_HIDE);
	do
	{
		ctl++;
	} while (ctl->Label != &ChordSensStr[0]);

	if (AppFlags & (APPFLAG_2KEY|APPFLAG_1FINGER|APPFLAG_WINDCTL)) flag = 0;
	GuiCtlShow(GuiApp, MainWin, ctl, flag ? 0 : CTLTYPE_HIDE);
}

void setMidiCmdChordModel(register unsigned char onFlag)
{
	register unsigned char val;

	val = AppFlags;
	if (onFlag)
	{
		AppFlags &= ~(APPFLAG_2KEY|APPFLAG_1FINGER|APPFLAG_WINDCTL|APPFLAG_GTRCHORD|APPFLAG_FULLKEY|APPFLAG_DRUMPAD);
		AppFlags |= APPFLAG_WINDCTL;
	}
	else if (val & APPFLAG_WINDCTL)
		AppFlags &= ~APPFLAG_WINDCTL;

	if (val != AppFlags)
	{
		register GUICTL *	ctl;

		ctl = &HumanCtls[0];
		while (ctl->Label != CModelsStr) ctl++;
		ctl_update_ctype(ctl);
	}
}

const char * getPlayDestDevName(register unsigned char devnum)
{
	register const char *	strs;

	strs = &PlayDestinationNames[0];
	while (--devnum) strs += (strlen(strs) + 1);
	return strs;
}

const char * getMusicianName(register unsigned char robotNum)
{
	register const char *	strs;

	strs = MusicianNameStrs;
	while (robotNum--) strs += (strlen(strs) + 1);
	return strs;
}

















// =============================================================

// General settings
// =============================================================

static uint32_t ctl_update_click(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, GuiApp->ClickSpeed);
	return 1;
}

static uint32_t ctl_set_click(register GUICTL * ctl)
{
	GuiCtlArrowsValue(GuiApp, ctl);
	GuiApp->ClickSpeed = ctl->Attrib.Value;

	// Done setting this parameter. Let caller redraw the ctl
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_clock(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, (AppFlags2 & APPFLAG2_CLOCKMASK));
	set_clock_type();
	return 1;
}

static uint32_t ctl_set_clock(register GUICTL * ctl)
{
	GuiCtlArrowsValue(GuiApp, ctl);
	AppFlags2 = (AppFlags2 & ~APPFLAG2_CLOCKMASK) | ctl->Attrib.Value;

	// Done setting this parameter. Let caller redraw the ctl
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_flash(register GUICTL * ctl)
{
	ctl->Attrib.Value = (AppFlags2 & APPFLAG2_TIMED_ERR) ? 1 : 0;
	return 1;
}

static uint32_t ctl_set_flash(register GUICTL * ctl)
{
	AppFlags2 ^= APPFLAG2_TIMED_ERR;

	// Done setting this parameter. Let caller redraw the ctl
	return CTLMASK_SETCONFIGSAVE;
}


#ifndef NO_REVERB_SUPPORT

static uint32_t ctl_set_rsize(register GUICTL * ctl)
{
	setReverb(ctl, REVPARAM_SIZEMASK);
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_rsize(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, getReverb(REVPARAM_SIZEMASK));
	return 1;
}

static uint32_t ctl_set_rdamp(register GUICTL * ctl)
{
	setReverb(ctl, REVPARAM_DAMPINGFREQMASK);
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_rdamp(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, getReverb(REVPARAM_DAMPINGFREQMASK));
	return 1;
}

static uint32_t ctl_set_rdecay(register GUICTL * ctl)
{
	setReverb(ctl, REVPARAM_DECAYMASK);
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_rdecay(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, getReverb(REVPARAM_DECAYMASK));
	return 1;
}

static uint32_t ctl_set_rpredelay(register GUICTL * ctl)
{
	setReverb(ctl, REVPARAM_PREDELAYMASK);
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_rpredelay(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, getReverb(REVPARAM_PREDELAYMASK));
	return 1;
}

static uint32_t ctl_set_rearly(register GUICTL * ctl)
{
	setReverb(ctl, REVPARAM_PREDELAYMASK);
	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_rearly(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, getReverb(REVPARAM_PREDELAYMASK));
	return 1;
}

static const char RoomSizeStr[] = "Room size";
static const char EarlyStr[] = "Early reflect";
static const char DecayStr[] = "Decay";
static const char PreDelayStr[] = "Pre-Delay";
static const char DampingStr[] = "Damping Freq";

static GUICTLDATA	ReverbSize = {ctl_update_rsize, ctl_set_rsize};
static GUICTLDATA	ReverbDamping = {ctl_update_rdamp, ctl_set_rdamp};
static GUICTLDATA	ReverbPreDelay = {ctl_update_rpredelay, ctl_set_rpredelay};
static GUICTLDATA	ReverbDecay = {ctl_update_rdecay, ctl_set_rdecay};
static GUICTLDATA	ReverbEarly = {ctl_update_rearly, ctl_set_rearly};
#endif

static GUICTLDATA	ClickFunc = {ctl_update_click, ctl_set_click};
static GUICTLDATA	FlashFunc = {ctl_update_flash, ctl_set_flash};
static GUICTLDATA	ClockFunc = {ctl_update_clock, ctl_set_clock};

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
static GUICTLDATA	AudioOutFunc = {ctl_update_nothing, ctl_set_intsynth_dev};
#endif
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
static GUICTLDATA	MidiOutFunc = {ctl_update_nothing, ctl_set_midiout_dev};
#endif
#ifndef NO_MIDICLOCK_IN
static const char ClockStrs[] = "Clock\0Normal\0Fast\0Fastest\0MIDI";
#else
static const char ClockStrs[] = "Clock\0Normal\0Fast\0Fastest";
#endif
static const char EnableStr[] = "Enable";

static GUICTL		GlobalCtls[] = {
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
#if defined(NO_MIDI_OUT_SUPPORT) && defined(NO_SEQ_SUPPORT)
	{.Type=CTLTYPE_STATIC, .Y=1,	.Label="Internal Synth:",		.Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_PUSH, .Y=1,	.Label="Playback\nDevice",	.Ptr=&AudioOutFunc,			.Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_NOPADDING},
#else
 	{.Type=CTLTYPE_PUSH, .Y=1,	.Label="Internal\nSynth",	.Ptr=&AudioOutFunc,			.Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_GROUPSTART},
#endif
#endif
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
#if defined(NO_ALSA_AUDIO_SUPPORT) && defined(NO_JACK_SUPPORT)
 	{.Type=CTLTYPE_PUSH, .Y=1,	.Label="External\nSynth 1",		.Ptr=&MidiOutFunc,			.Attrib.NumOfLabels=1	.Flags.Global=CTLGLOBAL_GROUPSTART},
#else
 	{.Type=CTLTYPE_PUSH, .Y=1,	.Label="External\nSynth 1",		.Ptr=&MidiOutFunc,			.Attrib.NumOfLabels=1},
#endif
 	{.Type=CTLTYPE_PUSH, .Y=1,	.Label="External\nSynth 2",		.Ptr=&MidiOutFunc,			.Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Type=CTLTYPE_PUSH, .Y=1,	.Label="External\nSynth 3",		.Ptr=&MidiOutFunc,			.Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Type=CTLTYPE_PUSH, .Y=1,	.Label="External\nSynth 4",		.Ptr=&MidiOutFunc,			.Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Type=CTLTYPE_GROUPBOX, .Y=1,	.Label="Playback Devices"},
#endif

 	{.Type=CTLTYPE_ARROWS,  .Y=2,	.Label=VolStr,		.Ptr=(void *)CTLSTR_MASTERVOL,	.Attrib.NumOfLabels=100+1, .Flags.Local=CTLFLAG_NOSTRINGS|CTLFLAG_NO_UP,	.Flags.Global=CTLGLOBAL_PRESET|CTLGLOBAL_GROUPSTART},
 	{.Type=CTLTYPE_ARROWS,	.Y=2, .Label=TransStr,	.Ptr=(void *)CTLSTR_TRANSPOSE,	.Attrib.NumOfLabels=10, .Flags.Global=CTLGLOBAL_PRESET},
 	{.Type=CTLTYPE_GROUPBOX, .Y=2, .Label="Master"},

#ifndef NO_MIDICLOCK_IN
 	{.Type=CTLTYPE_ARROWS,	.Y=2,	.Label=ClockStrs,	.Ptr=&ClockFunc,						.Attrib.NumOfLabels=4},
#else
 	{.Type=CTLTYPE_ARROWS,	.Y=2,	.Label=ClockStrs,	.Ptr=&ClockFunc,						.Attrib.NumOfLabels=3},
#endif
	{.Type=CTLTYPE_CHECK, .Y=2,	.Label="Flash error",	.Ptr=&FlashFunc,		.Attrib.NumOfLabels=1},

#ifndef NO_REVERB_SUPPORT
	{.Type=CTLTYPE_CHECK, 	.Y=3, .Label=EnableStr,		.Ptr=(void *)CTLSTR_REVERBMUTE, .Attrib.NumOfLabels=1,		.Flags.Global=CTLGLOBAL_PRESET|CTLGLOBAL_GROUPSTART},
 	{.Type=CTLTYPE_ARROWS,	.Y=3, .Label=VolStr,			.Ptr=(void *)CTLSTR_REVERBVOL,  .Attrib.NumOfLabels=100+1, .Flags.Local=CTLFLAG_NOSTRINGS|CTLFLAG_NO_UP,	.Flags.Global=CTLGLOBAL_PRESET},
 	{.Type=CTLTYPE_ARROWS,	.Y=3, .Label=RoomSizeStr,	.Ptr=&ReverbSize,  					.Attrib.NumOfLabels=100+1, .Flags.Local=CTLFLAG_NOSTRINGS},
 	{.Type=CTLTYPE_ARROWS,	.Y=3, .Label=EarlyStr,		.Ptr=&ReverbEarly,					.Attrib.NumOfLabels=100+1, .Flags.Local=CTLFLAG_NOSTRINGS},
 	{.Type=CTLTYPE_ARROWS,	.Y=4, .Label=DecayStr,		.Ptr=&ReverbDecay,  					.Attrib.NumOfLabels=100+1, .Flags.Local=CTLFLAG_NOSTRINGS},
 	{.Type=CTLTYPE_ARROWS,	.Y=4, .Label=PreDelayStr,	.Ptr=&ReverbPreDelay,  				.Attrib.NumOfLabels=100+1, .Flags.Local=CTLFLAG_NOSTRINGS},
 	{.Type=CTLTYPE_ARROWS,	.Y=4, .Label=DampingStr,	.Ptr=&ReverbDamping, 				.Attrib.NumOfLabels=100+1, .Flags.Local=CTLFLAG_NOSTRINGS},
 	{.Type=CTLTYPE_GROUPBOX, .Y=4, .Label=ReverbStr},
#endif
 	{.Type=CTLTYPE_ARROWS,	.Y=6, .Label="Click delay",	.Ptr=&ClickFunc,  	.Attrib.NumOfLabels=255, 	.Flags.Local=CTLFLAG_NOSTRINGS},
	{.Type=CTLTYPE_STATIC, .Y=6,	.Label=VERSIONSTRING,		.Attrib.NumOfLabels=1},
	{.Type=CTLTYPE_END},
};

static const char GeneralHelpStr[] = "\2Flash error \1automatically dismisses any error message after 5 seconds. Be sure to enable this if you're running BackupBand without a computer \
keyboard or mouse.\n\2Clock \1offers 3 settings for timing. The faster settings invoke less overhead, but may result in the robots playing at an erratic tempo, depending upon your \
system. Choose the fastest clock that doesn't adversely affect the robots' rhythm. \2MIDI \1clock is used only if you wish to slave BackupBand to some other hardware/software's tempo, \
using MIDI clock messages. You must set the tempo, and start/stop play, from the other device.\nIncreasing \2Click delay \1 causes BackupBand to be less sensitive to mouse button \
double-clicks. Adjust this setting if you're using a touchscreen that tends to generate false double-clicks, or when using a USB pedal configured as a mouse it does \
likewise.\n\2Transpose \1 transposes the drum, guitar, pad, and human solo instruments up/down by half steps. Unlike the Transpose setting in the main screen, the Setup screen's \
transpose is maintained each time you run BackupBand.";

static void updateBussBtns(void)
{
	register GUICTL *		ctls;
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
#if defined(NO_MIDI_OUT_SUPPORT) && defined(NO_SEQ_SUPPORT)
	ctls = &GlobalCtls[1];
#else
	ctls = &GlobalCtls[0];
#endif
	ctls->Attrib.Color = SoundDev[DEVNUM_AUDIOOUT].Handle ? GUICOLOR_LIGHTBLUE | (GUICOLOR_BLACK << 4) : GUICOLOR_GRAY | (GUICOLOR_BLACK << 4);
#endif
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	{
	register uint32_t		i;

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	ctls++;
#else
	ctls = &GlobalCtls[0];
#endif

	for (i=DEVNUM_MIDIOUT1; i<=DEVNUM_MIDIOUT4; i++, ctls++)
		ctls->Attrib.Color = SoundDev[i].Handle ? GUICOLOR_LIGHTBLUE | (GUICOLOR_BLACK << 4) : GUICOLOR_GRAY | (GUICOLOR_BLACK << 4);
	}
#endif
}








// ===================================================
// Commands
// ===================================================

static void actionsListFunc(register GUICTL *, register unsigned char);
static void update_cmd_ctls(void);

static const char CurrAssignStr[] = "Assigned:";
#ifndef NO_MIDI_IN_SUPPORT
#define NUM_ACTION_LABELS	4
static const char ActionsStr[] = "Mouse Button\0PC Key\0Midi Knob\0Midi Note\0Assign action to";
#else
#define NUM_ACTION_LABELS	2
static const char ActionsStr[] = "Mouse Button\0PC Key\0Assign action to";
#endif
static const char CmdNoteStr1[] = "Play the key that will switch into command mode. (Typically, the lowest note.) Or click delete to disable command mode.";
static const char CmdNoteStr2[] = "Play the key that will split your controller between chords and commands. (Typically, the center note.) Or click delete to disable command mode.";
static const char CmdGroupStr[] = "Command notes";
static const char CmdStr[] = "Switch\nNote:";

static uint32_t ctl_set_erase1(register GUICTL * ctl)
{
	eraseSelAction();
	return CTLMASK_SETCONFIG_OTHER;
}

static uint32_t ctl_set_list_asn(register GUICTL * ctl)
{
	clearMainWindow();
	chooseActionsList(ctl->Attrib.Value - 1);
	resetActionsList();
	update_cmd_ctls();
	return CTLMASK_NONE;
}

static uint32_t ctl_set_cmd_switch(register GUICTL * ctl)
{
	BytePtr = &CmdSwitchNote;
	ReturnFunc = &showSetupScreen;
	doNoteScreen((MasterMidiChan > 15 ? &CmdNoteStr1[0] : &CmdNoteStr2[0]), 1);
	return CTLMASK_SETCONFIGSAVE;
}

static const char * getCmdSwitchStr(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	if (CmdSwitchNote > 127) return "Off";
	numToPitch(buffer, CmdSwitchNote);
	return buffer;
}

static uint32_t ctl_update_tooltips(register GUICTL * ctl)
{
	ctl->Attrib.Value = (AppFlags4 & APPFLAG4_NO_TIPS) ? 0 : 1;
	return 1;
}

static uint32_t ctl_set_tooltips(register GUICTL * ctl)
{
	AppFlags4 ^= APPFLAG4_NO_TIPS;
	return CTLMASK_SETCONFIGSAVE;
}


static uint32_t ctl_update_cmd_always(register GUICTL * ctl)
{
	ctl->Attrib.Value = (AppFlags4 & APPFLAG4_CMD_ALWAYS_ON) ? 1 : 0;
	return 1;
}

static uint32_t ctl_set_cmd_always(register GUICTL * ctl)
{
	AppFlags4 ^= APPFLAG4_CMD_ALWAYS_ON;
	return CTLMASK_SETCONFIGSAVE;
}

static GUICTLDATA	CmdSwitchFunc = {ctl_update_nothing, ctl_set_cmd_switch};
static GUICTLDATA	CmdOnFunc = {ctl_update_cmd_always, ctl_set_cmd_always};
static GUICTLDATA	TooltipsFunc = {ctl_update_tooltips, ctl_set_tooltips};
static GUICTLDATA	AsnListFunc = {ctl_update_nothing, ctl_set_list_asn};
static GUICTLDATA	Erase1Func = {ctl_update_nothing, ctl_set_erase1};

static GUICTL		CmdCtls[] = {
 	{.Type=CTLTYPE_RADIO,	 .X=1,	.Label=ActionsStr, .Ptr=&AsnListFunc, .Attrib.Value=1, .Attrib.NumOfLabels=NUM_ACTION_LABELS, .Flags.Local=CTLFLAG_LABELBOX, .Flags.Global=CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_STATIC,	.Label=CurrAssignStr,		.Attrib.NumOfLabels=1},
	{.Type=CTLTYPE_STATIC,	.Y=2, .Label=CmdStr,						.Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_GROUPSTART},
 	{.Type=CTLTYPE_PUSH,		.Y=2, .BtnLabel=getCmdSwitchStr,	.Ptr=&CmdSwitchFunc,	.Attrib.NumOfLabels=1, .Width=7,	.Flags.Global=CTLGLOBAL_NOPADDING|CTLGLOBAL_GET_LABEL},
	{.Type=CTLTYPE_CHECK,	.Y=2, .Label="Tooltips",		 	.Ptr=&TooltipsFunc,	.Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_AUTO_VAL},
	{.Type=CTLTYPE_CHECK,	.Y=2, .Label="Always on",		 	.Ptr=&CmdOnFunc,	.Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_GROUPBOX, .Y=2,	.Label=CmdGroupStr},
  	{.Type=CTLTYPE_PUSH,  	 .Y=3,	.Label="Remove\nAssignment",		.Ptr=&Erase1Func,		.Attrib.NumOfLabels=1,		.Flags.Global=CTLGLOBAL_NOPADDING},
	{.Type=CTLTYPE_AREA, 	.Y=4, .ListDraw = drawActionsList, .Ptr=&actionsListFunc, .Flags.Local=CTLFLAG_AREA_LIST|CTLFLAG_AREA_FULL_SIZE|CTLFLAG_AREA_EVENHEIGHT, .Flags.Global=CTLGLOBAL_APPUPDATE|CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_ARROWS,	.Y=4, .Attrib.NumOfLabels=1, .Ptr=&actionsListFunc, .Flags.Local=CTLFLAG_AREA_HELPER, .Flags.Global=CTLGLOBAL_AUTO_VAL},
	{.Type=CTLTYPE_END},
};

static GUIBOX	HelpBox;

static unsigned chk_rect(void)
{
	HelpBox.Y += GuiWinGetBound(GuiApp, MainWin, GUIBOUND_UPPER);
	if (HelpBox.X + (GuiApp->GuiFont.CharWidth * 4) <= MainWin->WinPos.Width &&
		 HelpBox.Y + HelpBox.Height < MainWin->WinPos.Height)
	{
		HelpBox.Width = MainWin->WinPos.Width - (HelpBox.X + (GuiApp->GuiFont.CharWidth >> 1));
		return 1;
	}
	return 0;
}

static unsigned get_help_pos(void)
{
	register GUICTL *		ctl;

	ctl = &CmdCtls[0];
	do
	{
		if (ctl->Label == &CmdGroupStr[0])
		{
			HelpBox.X = ctl->X + ctl->Width + GuiWinGetBound(GuiApp, MainWin, GUIBOUND_XSPACE);
			HelpBox.Y = ctl->Y;
			HelpBox.Height = 4 * GuiApp->GuiFont.Height;
			return chk_rect();
		}
		ctl++;
	} while (ctl->Type);
	return 0;
}

static unsigned get_help_pos2(void)
{
	register GUICTL *		ctl;

	ctl = &CmdCtls[0];
	do
	{
		if (ctl->Label == CurrAssignStr)
		{
			HelpBox.X = ctl->X + ctl->Width + GuiApp->GuiFont.CharWidth;
			HelpBox.Y = ctl->Y;
			HelpBox.Height = GuiApp->GuiFont.Height;
			return chk_rect();
		}
		ctl++;
	} while (ctl->Type);
	return 0;
}

static void draw_helpbox(void)
{
	if (makeAsnHelpStr())
	{
		if (get_help_pos())
			GuiTextDrawMsg(GuiApp, (char *)TempBuffer, &HelpBox, (GUICOLOR_VIOLET << 24)|GUIDRAW_SMALLTEXT);
		if (get_help_pos2())
			drawActionAssign(HelpBox.X, HelpBox.Y);
	}
}

/**************** actionsListFunc() *******************
 * Called by cmd_mouse() to handle a mouse
 * event upon the Actions List control.
 */

static void actionsListFunc(register GUICTL * ctl, register unsigned char action)
{
	switch (action)
	{
		case GUILIST_SELECTION:
		{
			switch (CmdCtls[0].Attrib.Value)
			{
		#ifndef NO_MIDI_IN_SUPPORT
				case 4:
					doMidiNoteAsn();
					break;
				case 3:
					doMidiMsgAsn();
					break;
		#endif
				case 2:
					doPcKeyAsn();
					break;
				case 1:
					doMouseBtnAsn();
			}
			break;
		}

		case GUILIST_SCROLL:
		case GUILIST_CLICK:
		{
			// Force redraw of description, and any current assignment
			if (get_help_pos())
				GuiWinAreaUpdate(GuiApp, MainWin, &HelpBox);
			if (get_help_pos2())
				GuiWinAreaUpdate(GuiApp, MainWin, &HelpBox);
//			GuiCtlShow(GuiApp, MainWin, &CmdCtls[CTLID_CMDDELETE], isActionSelected() ? 0 : CTLTYPE_HIDE);
		}
	}
}

static void update_cmd_ctls(void)
{
	register GUICTL * 		ctl;

	// Hide the "Command Note" related controls if user not displaying Command note listing
	ctl = &CmdCtls[0];
	do
	{
		if (ctl->Label == &CmdStr[0])
		{
			for (;;)
			{
				if (CmdCtls[0].Attrib.Value == 4)
					ctl->Type &= ~CTLTYPE_HIDE;
				else
					ctl->Type |= CTLTYPE_HIDE;

				if (ctl->Ptr == &TooltipsFunc) ctl->Attrib.Value = (AppFlags4 & APPFLAG4_NO_TIPS) ? 0 : 1;
				if (ctl->Label == &CmdGroupStr[0]) break;
				ctl++;
			}
			break;
		}
		ctl++;
	} while (ctl->Type);
}

static void cmd_mouse(register GUIMSG * msg)
{
	register GUICTL * 		ctl;

	ctl = msg->Mouse.SelectedCtl;

	// Action list?
	if (ctl->Ptr == &actionsListFunc)
		actionsListFunc(ctl, msg->Mouse.ListAction);
	else
		handle_mouse(msg);
}

static GUIFUNCS SetupCmdFuncs = {draw_helpbox,
cmd_mouse,
dummy_keypress,
0};

static const char CommandsHelpStr[] = "Set \2Switch Note \1only \
if you want to use a range of notes for issuing commands to BackupBand (instead of playing sampled instruments), such as start/stop play, transpose, increase/decrease tempo, etc.";






// ============================================================
// Robots settings
// ============================================================

static const char	RobotStr[] = "Robot";
static const char	PolStrs[] = "Pos\0Neg\0HiHat Polarity";

static GUICTL * getCurrSelRobotCtl(register GUICTL *);
static unsigned char getCurrSelRobot(register GUICTL *);

static uint32_t ctl_set_polarity(register GUICTL * ctl)
{
	AppFlags2 ^= APPFLAG2_POLARITY;

	return CTLMASK_SETCONFIGSAVE;
}

static uint32_t ctl_update_polarity(register GUICTL * ctl)
{
	register unsigned char	flag;

	if (!(flag = getCurrSelRobot(ctl)) && DevAssigns[PLAYER_DRUMS])
		ctl->Attrib.Value = ((AppFlags2 & APPFLAG2_POLARITY) ? 1 : 2);
	else
		flag = CTLTYPE_HIDE;
	GuiCtlShow(GuiApp, MainWin, ctl, flag);
	return 1;
}

static uint32_t ctl_update_patch_chg(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, (AppFlags2 & (APPFLAG2_PATCHCHG_MANUAL|APPFLAG2_PATCHCHG_STOPPED)) >> 6);
	return 1;
}

static uint32_t ctl_set_patch_chg(register GUICTL * ctl)
{
	AppFlags2 = (AppFlags2 & ~(APPFLAG2_PATCHCHG_MANUAL|APPFLAG2_PATCHCHG_STOPPED)) | (ctl->Attrib.Value << 6);

	// Done setting this parameter. Let caller redraw the ctl
	return CTLMASK_SETCONFIGSAVE;
}

/**************** show_robotctls() ****************
 * Shows/hides all robot individual ctls based upon
 * whether the robot's Playback destination ("Playback on")
 * is set to "Off".
 */

static void show_robotctls(register GUICTL * ctl, register unsigned char musicianNum)
{
	register unsigned char	flag;

	flag = DevAssigns[musicianNum] ? 0 : CTLTYPE_HIDE;
	ctl++;
	while (ctl->Label != &RobotStr[0])
	{
		if (ctl->Label == &PolStrs[0] && musicianNum) flag = CTLTYPE_HIDE;
		GuiCtlShow(GuiApp, MainWin, ctl, flag);
		ctl++;
	}
}

static uint32_t ctl_set_robot_playdev(register GUICTL * ctl)
{
	register unsigned char	musicianNum;

	musicianNum = getCurrSelRobot(ctl);
	return set_playdev_assign(ctl, musicianNum);
}

static uint32_t ctl_update_robot_playdev(register GUICTL * ctl)
{
	register unsigned char	musicianNum;

	musicianNum = getCurrSelRobot(ctl);
	show_robotctls(ctl, musicianNum);
	return get_playdev_assign(ctl, musicianNum);
}

static uint32_t ctl_set_robot_midichan(register GUICTL * ctl)
{
	register unsigned char	musicianNum;

	musicianNum = getCurrSelRobot(ctl);
	return set_robot_midichan(ctl, musicianNum);
}

static uint32_t ctl_update_robot_midichan(register GUICTL * ctl)
{
	register unsigned char	musicianNum;

	musicianNum = getCurrSelRobot(ctl);
	return update_robot_midichan(ctl, musicianNum);
}

static const char * getRobotChanStr(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	register unsigned char	musicianNum;

	musicianNum = getCurrSelRobot(ctl);
	formatMidiChanLabel(musicianNum, buffer);
	return buffer;
}

static uint32_t ctl_set_CurrentRobot(register GUICTL * ctl)
{
	register unsigned char	musicianNum;

	musicianNum = ctl->Attrib.Value - 1;

	ctl++;
	while (ctl->Label != &RobotStr[0])
	{
		if (ctl->Label == VolStr) ctl->Ptr = getVolCtlDataPtr(musicianNum);
		if (ctl->Ptr && ((GUICTLDATA *)ctl->Ptr)->UpdateFunc(ctl)) GuiCtlUpdate(GuiApp, 0, ctl, 0, 0);
		ctl++;
	}

	return CTLMASK_SETCONFIG_OTHER;
}

static uint32_t ctl_set_robot_mute(register GUICTL * ctl)
{
	register unsigned char	musicianNum;

	musicianNum = getCurrSelRobot(ctl);
	setAppFlags(APPFLAG3_NODRUMS << musicianNum);
	return 1;
}

static uint32_t ctl_update_robot_mute(register GUICTL * ctl)
{
	ctl->Attrib.Value = ((AppFlags3 & (APPFLAG3_NODRUMS << getCurrSelRobot(ctl))) ? 1 : 0);
	return 1;
}

static uint32_t ctl_update_autostart(register GUICTL * ctl)
{
	register unsigned char	val;

	if (AppFlags3 & APPFLAG3_AUTOSTARTARM)
		val = 2;
	else
		val = ((AppFlags3 & APPFLAG3_NOAUTOSTART) >> AUTOSTART_SHIFT) ^ 0x01;
	GuiCtlArrowsInit(ctl, val);
	return 1;
}

static uint32_t ctl_set_autostart(register GUICTL * ctl)
{
	AppFlags3 &= ~(APPFLAG3_AUTOSTARTARM|APPFLAG3_NOAUTOSTART);
	TempFlags &= ~(TEMPFLAG_AUTOSTART);

	switch (ctl->Attrib.Value)
	{
		case 2:
			AppFlags3 |= APPFLAG3_AUTOSTARTARM;
		case 1:
			TempFlags |= TEMPFLAG_AUTOSTART;
			break;
		case 0:
			AppFlags3 |= APPFLAG3_NOAUTOSTART;
			break;
	}
	SaveConfigFlag |= SAVECONFIG_OTHER;
	return 0;
}

static const char	AutostartStr[] = "Autostart\0Off\0On\0Toggle";
static const char	RobotMuteStr[] = "Mute robot";
static const char	PatchChgStr[] = "Change\0Always\0When stopped\0Manually\0Articulations";

static GUICTLDATA	PatchChgFunc = {ctl_update_patch_chg, ctl_set_patch_chg};
static GUICTLDATA	AutostartFunc = {ctl_update_autostart, ctl_set_autostart};
static GUICTLDATA	HHPolFunc = {ctl_update_polarity, ctl_set_polarity};
static GUICTLDATA	CurrentRobotFunc = {ctl_update_nothing, ctl_set_CurrentRobot};
static GUICTLDATA	RobotMuteFunc = {ctl_update_robot_mute, ctl_set_robot_mute};
static GUICTLDATA	RobotPlayDevFunc = {ctl_update_robot_playdev, ctl_set_robot_playdev};
static GUICTLDATA	RobotMidiChanFunc = {ctl_update_robot_midichan, ctl_set_robot_midichan};

static GUICTL		RobotCtls[] = {
	{.Type=CTLTYPE_ARROWS,	 .Label=AutostartStr,	 	.Ptr=&AutostartFunc,				 		.Attrib.NumOfLabels=3, 		.Flags.Global=CTLGLOBAL_AUTO_VAL},

 	{.Type=CTLTYPE_ARROWS,	 .Label=PatchChgStr,			.X=2, .Ptr=&PatchChgFunc,						.Attrib.NumOfLabels=4,	.Flags.Global=CTLGLOBAL_GROUPSTART|CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_GROUPBOX, .Label="Instruments"},

	{.Type=CTLTYPE_CHECK, .Label="Variation Cycle",	 	.Ptr=(void *)CTLSTR_CYCLEVARIATION,	.Attrib.NumOfLabels=1, 		.Flags.Global=CTLGLOBAL_PRESET|CTLGLOBAL_AUTO_VAL},

	// ==================Individual robot settings
 	{.Type=CTLTYPE_RADIO,	.Y=3,		 	.Label=&MusicianNameStrs[0],.Ptr=&CurrentRobotFunc,		.Attrib.NumOfLabels=4,.Attrib.Value=1,	 .Flags.Global=CTLGLOBAL_GROUPSTART|CTLGLOBAL_AUTO_VAL},
  	{.Type=CTLTYPE_ARROWS,	.Y=4,			.BtnLabel=getRobotChanStr,	.Ptr=&RobotMidiChanFunc,		.Attrib.NumOfLabels=1, 		 .Flags.Global=CTLGLOBAL_GET_LABEL},
	{.Type=CTLTYPE_CHECK,	.Y=4,			.Label=RobotMuteStr,			.Ptr=&RobotMuteFunc, 			.Attrib.NumOfLabels=1, 		 .Flags.Global=CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_ARROWS,	.Y=4,			.Label=VolStr,					.Ptr=(void *)CTLSTR_DRUMVOL,	.Attrib.NumOfLabels=89,		 .Flags.Local=CTLFLAG_NOSTRINGS,	.Flags.Global=CTLGLOBAL_PRESET},
	{.Type=CTLTYPE_ARROWS,	.Y=5,			.Label=PlayDevStr,			.Ptr=&RobotPlayDevFunc,			.Attrib.NumOfLabels=BUSSCNT,.Flags.Local=CTLFLAG_NO_DOWN,	.Flags.Global=CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_RADIO,	.Y=5,	.X=1,	.Label=&PolStrs[0],			.Ptr=&HHPolFunc,					.Attrib.NumOfLabels=2,		 .Flags.Local=CTLFLAG_LABELBOX},
	{.Type=CTLTYPE_GROUPBOX, .Y=5, .Label=RobotStr},
	{.Type=CTLTYPE_END},
};

/*************** getCurrSelRobotCtl() *************
 * Only 1 robot's individual settings are displayed
 * at any given moment. The robot selection RADIO
 * ctl determines which one (Drums, Bass, Guitar, or)
 * Pad).
 */

static GUICTL * getCurrSelRobotCtl(register GUICTL * ctl)
{
	do
	{
		--ctl;
	} while (ctl->Ptr != &CurrentRobotFunc);
	return ctl;
}

static unsigned char getCurrSelRobot(register GUICTL * ctl)
{
	return (getCurrSelRobotCtl(ctl)->Attrib.Value - 1);
}

static const char RobotsHelpStr[] = "\7Instruments Change \1determines when the robots' instruments change automatically. \2Always \1is the default behavior. The robots automatically \
change instruments whenever the style is selected, during play or stopped. \2When stopped \1changes the instruments only when you change styles with playback stopped. Instruments do \
not change automatically during play. With \2Manually, \1the instruments never change automatically. They change only when you manually change them. \2Articulations \1is like \
\2Manually \1except that articulations can change automatically during play. (ie, The guitarist can mute strings on one Variation, and unmute on another. But he can't change the \
instrument itself, for example, change from a Les Paul to a Nylon string. He must play the instrument you manually choose.)\nWhen \2Autostart \1is \2On, \1as soon as you play a chord, \
the robots all start to play along (assuming you haven't muted/disabled them) in the current style. When Autostart is \2Off, \1the drummer does nothing and the bass/guitar strum rubato \
chords (in time with your chords), until you manually start play. Only when you start play do the robots all begin playing in the current style. \2Toggle \1automatically \
turns on Autostart whenever you select a style (while play is stopped), and then automatically turns it off when play starts. This allows you to play rubato chords at the ending of \
a style without causing play to automatically restart.";

















static uint32_t ctl_set_edstyle(register GUICTL * ctl)
{
	doStyleEdit();
	return 0;
}

static uint32_t ctl_set_reloads(register GUICTL * ctl)
{
	freeAccompStyles();
	loadDataSets(LOADFLAG_STYLES|LOADFLAG_SONGS);
	doSetupScreen();
	return 0;
}

static GUICTLDATA	StyleEdFunc = {ctl_update_nothing, ctl_set_edstyle};
static GUICTLDATA	ReloadStyleFunc = {ctl_update_nothing, ctl_set_reloads};

static GUICTL		EditorCtls[] = {
 	{.Type=CTLTYPE_PUSH,		 .Y=1,	.Label="Edit",			.Ptr=&StyleEdFunc,			.Attrib.NumOfLabels=1,	.Flags.Global=CTLGLOBAL_GROUPSTART},
 	{.Type=CTLTYPE_PUSH, 	 .Y=1,	.Label="Reload all",	.Ptr=&ReloadStyleFunc,		.Attrib.NumOfLabels=1},
  	{.Type=CTLTYPE_GROUPBOX, .Y=2,	.Label="Styles"},
	{.Type=CTLTYPE_END},
};








/********************** handle_mouse() ************************
 * Called by GUI thread when user operates a ctl in the Setup
 * main screens.
 */

static void handle_mouse(register GUIMSG * msg)
{
	register GUICTL * 		ctl;

	ctl = msg->Mouse.SelectedCtl;

	// Exit setup btn?
	if (ctl->Flags.Global & CTLGLOBAL_PRESET)
	{
		midiviewDone();

		if (MusicianAssignChange)
		{
			loadDataSets(LOADFLAG_INSTRUMENTS);
			updChansInUse();
		}

		ctl = get_trigger_by();
		if (AppFlags & APPFLAG_WINDCTL)
		{
			if (ctl->Attrib.Value) goto midi;
			ChordTrigger = setMouseBtn(0, 0xff) + 0xf1;
		}
		else if (ctl->Attrib.Value != 2)
			ChordTrigger = (ctl->Attrib.Value + 9) << 4;
		else
midi:		ChordTrigger = TempChordTrigger;

		showMainScreen();
	}
	else
		handle_panel_mouse(msg);
}








//====================================
// menu bar
//====================================

static GUIFUNCS SetupFuncs = {dummy_drawing,
handle_mouse,
dummy_keypress,
0};

void play_off_state(void)
{
	endCmdMode();
	stop_play(1, GUITHREADID);
	selectSongSheet(0, GUITHREADID);
}

static void doGeneralDlg(GUIAPPHANDLE app)
{
	MainWin->Ctls = &GlobalCtls[0];
	GuiFuncs = &SetupFuncs;
}

static void doRobotsDlg(GUIAPPHANDLE app)
{
	MainWin->Ctls = &RobotCtls[0];
	GuiFuncs = &SetupFuncs;
}

static void doCommandsDlg(GUIAPPHANDLE app)
{
	play_off_state();
	MainWin->Flags = GUIWIN_LIST_KEY|GUIWIN_SHIFTTAB_KEY|GUIWIN_TAB_KEY|GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY|GUIWIN_HELP_BTN|GUIWIN_HELP_KEY;
	MainWin->Ctls = &CmdCtls[0];
	GuiFuncs = &SetupCmdFuncs;
	resetActionsList();
	update_cmd_ctls();
}

static void doHumanDlg(GUIAPPHANDLE app)
{
	MainWin->Ctls = &HumanCtls[0];
	GuiFuncs = &SetupFuncs;
}

static void doEditorDlg(GUIAPPHANDLE app)
{
	play_off_state();
	MainWin->Ctls = &EditorCtls[0];
	GuiFuncs = &SetupFuncs;
}

#define NUM_MENU_LABELS 5
static const char *		SetupMenuLabels[NUM_MENU_LABELS] = {"General", "Robots", "Human", "Commands", "Editor"};
static unsigned short	SetupMenuWidths[NUM_MENU_LABELS];
static GUIMENUFUNC *		SetupMenuFuncs[NUM_MENU_LABELS] = {doGeneralDlg, doRobotsDlg, doHumanDlg, doCommandsDlg, doEditorDlg};
static GUIMENU			SetupMenu = {SetupMenuLabels, SetupMenuWidths, &SetupMenuFuncs[0], 0, 0, NUM_MENU_LABELS, (GUICOLOR_BLACK << 4) | GUICOLOR_GREEN};

void init_setup_menu(void)
{
	MainWin->Menu = &SetupMenu;
	GuiWinState(GuiApp, 0, GUISTATE_MENU);
}

static const char *	HelpStrs[NUM_MENU_LABELS] = {&GeneralHelpStr[0], &RobotsHelpStr[0], &HumanHelpStr[0], &CommandsHelpStr[0], 0};

void showSetupHelp(void)
{
	register const char **	ptr;

	ptr = (const char **)GuiBuffer;

	if ((*ptr = HelpStrs[SetupMenu.Select]))
		GuiHelpShow(GuiApp, MainWin, 0);
}

// ====================================================




/********************* doSetupScreen() ***********************
 * Shows/operates the "Setup" screen.
 */

void doSetupScreen(void)
{
	midiviewDone();
	endCmdMode();
	updateBussBtns();
	setShownCtls(0);
	ReturnFunc = doSetupScreen;
	MainWin->LowerPresetBtns = GUIBTN_HELP_SHOW|GUIBTN_COPY_SHOW|GUIBTN_CENTER;
	MainWin->Flags = GUIWIN_SHIFTTAB_KEY|GUIWIN_TAB_KEY|GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY|GUIWIN_HELP_BTN|GUIWIN_HELP_KEY;
	MainWin->Menu = &SetupMenu;
	(SetupMenuFuncs[SetupMenu.Select])(GuiApp);
	clearMainWindow();
	headingShow(0);
	GuiCtlSetSelect(GuiApp, 0, 0, (GUICTL *)0 + GUIBTN_COPY);
}

static void updateAllCtls(register GUICTL * ctl)
{
	register GUICTLDATA *	data;

	do
	{
		if (ctl->Type != CTLTYPE_AREA && (data = (GUICTLDATA *)ctl->Ptr) && (data != (void *)&actionsListFunc) && data->UpdateFunc) data->UpdateFunc(ctl);
		ctl++;
	} while (ctl->Type);
}

void showSetupScreen(void)
{
	register GUICTL * ctl;

	ctl = get_trigger_by();
	TempChordTrigger = ChordTrigger;
	if (AppFlags & APPFLAG_WINDCTL)
	{
		if (TempChordTrigger < 128)
		{
			ctl->Attrib.Value = 1;
			setMouseBtn(0, 1);
		}
		else
		{
			setMouseBtn(0, TempChordTrigger - 0xf1);
			ctl->Attrib.Value = 0;
		}
	}
	else
	{
		ctl->Attrib.Value = 2;
		if (TempChordTrigger > 127)
			ctl->Attrib.Value = (TempChordTrigger >> 4) - 9;
	}

	MusicianAssignChange = 0;
	setShownCtls(0);
	clearMainWindow();
	updateAllCtls(&GlobalCtls[0]);
	updateAllCtls(&RobotCtls[0]);
	updateAllCtls(&HumanCtls[0]);
	updateAllCtls(&CmdCtls[0]);
//	updateAllCtls(&EditorCtls[0]);
	doSetupScreen();
}



/*************** positionSetupGui() ****************
 * Sets initial position/scaling of GUI ctls based

 * upon font size.
 */

void positionSetupGui(void)
{
	register GUICTL * ctl;
	register GUICTL * ctl2;

	fixup_setup_ctls(&GlobalCtls[0]);
	GuiCtlScale(GuiApp, MainWin, &GlobalCtls[0], -1);
	fixup_setup_ctls(&HumanCtls[0]);
	GuiCtlScale(GuiApp, MainWin, &HumanCtls[0], -1);
	fixup_setup_ctls(&RobotCtls[0]);
	GuiCtlScale(GuiApp, MainWin, &RobotCtls[0], -1);
//	fixup_setup_ctls(&CmdCtls[0]);
	GuiCtlScale(GuiApp, MainWin, &CmdCtls[0], -1);
	GuiCtlScale(GuiApp, MainWin, &EditorCtls[0], -1);

	ctl2 = ctl = get_trigger_by();
	do
	{
		ctl2++;
	} while (ctl2->Label != &ChordSensStr[0]);
	ctl2->X = ctl->X;
}



int isRobotSetup(void)
{
	return (GuiFuncs == &SetupFuncs);
}
