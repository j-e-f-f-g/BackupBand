#include "Options.h"
#include "Main.h"
#include "PickDevice.h"
#include "FileLoad.h"
#include "MidiIn.h"
#include "Setup.h"
#include "AudioPlay.h"
#include "Editor.h"

// ============================================================
// Style Editor
// We simply put BackupBand into a mode where it uses ALSA's SEQ API
// to connect to a MIDI editor like Muse, QTractor, or Rosegarden, and
// allows us to function like a sound module while the user constructs
// his drum, bass, and gtr variation/ptns in that other app.

static struct SOUNDDEVINFO		TempMidiDev;
static struct SOUNDDEVINFO		TempAudioDev;

static const unsigned char	Chords[] = {7, 5, 0,//power5
0,
8, 5, 0,
0,0,
4, 3, 2, 1, 0,
0,
1, 2, 3, 4, 0,
0,
6, 5, 4, 3, 2, 1, 0,
1, 2, 3, 4, 5, 6};
static const unsigned char GtrNotesInC[] = {76, 67, 60, 55, 48, 40, 43, 45};
static const unsigned char Pluck[] = {6,0,5,0,4,3,0,2,0,1,0,0};

static void * edStyleFilter(register void * signalHandle, register unsigned char * msg)
{
	register unsigned char	data, noteNum;

	data = msg[0];
	noteNum = msg[1];
	if (data < 0xA0)
	{
		data &= 0x0F;
		if (!data || data >= 5)
		{
			if (msg[2] && noteNum > 11) startDrumNote(0, noteNum, msg[2], MIDITHREADID);
		}
		else switch (data)
		{
			case PLAYER_PAD:
			{
				break;
			}
			case PLAYER_GTR:
			{
				if (noteNum >= 60 - (3*12) && noteNum <= 60 + (4*12))
				{
					if (msg[2])
					{
						if (noteNum >= 60)
						{
							noteNum -= (60 - 36);
							startGtrString(noteNum, noteNum, msg[2], MIDITHREADID);
						}
						else
						{
							register unsigned char	step;

							data = msg[2];
							noteNum -= (60 - (3*12));
							step = noteNum % 12;
							if (!(noteNum /= 12)) noteNum--;
							if (noteNum == 2) noteNum = 0;

							if (Pluck[step]) startGtrString(Pluck[step], noteNum, data, MIDITHREADID);
							else
							{
								register const unsigned char *	strings;

								strings = &Chords[0];
								while (--step)
								{
									while (*strings++);
								}

								do
								{
									startGtrString(*strings++, noteNum, data, MIDITHREADID);
								} while (*strings);
							}
						}
					}
					else
					{
						if (noteNum >= 60)
							stopGtrString(noteNum - (60 - 36), MIDITHREADID);
						else
						{
							noteNum -= (60 - (3*12));
							noteNum %= 12;

							if (Pluck[noteNum]) stopGtrString(Pluck[noteNum], MIDITHREADID);
							else stopGtrString(0, MIDITHREADID);
						}
					}
				}

				break;
			}
			case PLAYER_BASS:
			{
				if (noteNum > 37 && noteNum < 72)
				{
					if ((noteNum -= 12) < 26) noteNum += 12;
					if (msg[2])
						startBassNote(noteNum, msg[2], MIDITHREADID);
					else
						stopBassNote(noteNum, MIDITHREADID);
				}
//				break;
			}
		}
	}

	// Ignore this
	return (void *)-1;
}

static void restore_midi_connection(void)
{
	if (!TempMidiDev.Handle) closeMidiIn();

	memcpy(&SoundDev[DEVNUM_MIDIIN], &TempMidiDev, sizeof(TempMidiDev));
	if (!TempMidiDev.Handle)
		openMidiIn();
	else
		snd_seq_connect_from(SoundDev[DEVNUM_MIDIIN].Handle, SoundDev[DEVNUM_MIDIIN].Card, SoundDev[DEVNUM_MIDIIN].Dev, SoundDev[DEVNUM_MIDIIN].SubDev);
}

static void restore_audio_connection(void)
{
	freeAudio(0);
	memcpy(&SoundDev[DEVNUM_AUDIOOUT], &TempAudioDev, sizeof(struct SOUNDDEVINFO));
	loadDataSets(LOADFLAG_INSTRUMENTS);
	allocAudio();
}

static void retToSetup(void)
{
	headingShow(0);
	restore_audio_connection();
	restore_midi_connection();
	showSetupScreen();
}

static void edstyle_mouse(register GUIMSG * msg)
{
	register GUICTL * 		ctl;

	ctl = msg->Mouse.SelectedCtl;
	if (ctl->Flags.Global & CTLGLOBAL_PRESET)
	{
		setMidiInSiphon(&edStyleFilter, 0);
		retToSetup();
	}
	else
		handle_panel_mouse(msg);
}

static GUICTL		EdStyleCtls[] = {
	{.Type=CTLTYPE_STATIC, .Y=1, .Label="Drum kit:", .Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_AREA, .Y=2, .Ptr=(void *)CTLSTR_DRUMLIST,	.Flags.Global=CTLGLOBAL_PRESET|CTLGLOBAL_APPUPDATE},
 	{.Type=CTLTYPE_STATIC, .Y=3, .Label="Bass:", .Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_AREA, .Y=4, .Ptr=(void *)CTLSTR_BASSLIST,	.Flags.Global=CTLGLOBAL_PRESET|CTLGLOBAL_APPUPDATE},
	{.Type=CTLTYPE_STATIC, .Y=5, .Label="Guitar:", .Attrib.NumOfLabels=1},
 	{.Type=CTLTYPE_AREA, .Y=6, .Ptr=(void *)CTLSTR_GTRLIST,	.Flags.Global=CTLGLOBAL_PRESET|CTLGLOBAL_APPUPDATE},
 	{.Type=CTLTYPE_END},
};

static GUIFUNCS EdStyleFuncs = {dummy_drawing,
edstyle_mouse,
dummy_keypress,
0};

static void styleEdit(void)
{
	setShownCtls(CTLMASK_DRUMLIST|CTLMASK_BASSLIST|CTLMASK_GTRLIST);
	setMidiInSiphon(&edStyleFilter, 1);
	MainWin->Flags = GUIWIN_TAB_KEY|GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY;
	MainWin->Ctls = &EdStyleCtls[0];
	GuiFuncs = &EdStyleFuncs;
	MainWin->LowerPresetBtns = GUIBTN_OK_SHOW|GUIBTN_CENTER;
	MainWin->Menu = 0;
	clearMainWindow();
	GuiCtlSetSelect(GuiApp, 0, 0, (GUICTL *)0 + GUIBTN_OK);

	copy_gtrnotes(&GtrNotesInC[0]);
}

static void styleDevice(register struct SOUNDDEVINFO * sounddev, register unsigned char op)
{
	switch (op)
	{
		case 1:
		{
			if ((SoundDev[DEVNUM_MIDIIN].DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_SEQ)
			{
				if (!(SoundDev[DEVNUM_MIDIIN].Handle = TempMidiDev.Handle))
					openMidiIn();
				else if (SoundDev[DEVNUM_MIDIIN].DevHash)
					snd_seq_connect_from(SoundDev[DEVNUM_MIDIIN].Handle, SoundDev[DEVNUM_MIDIIN].Card, SoundDev[DEVNUM_MIDIIN].Dev, SoundDev[DEVNUM_MIDIIN].SubDev);

				headingShow(0);
				styleEdit();
				break;
			}
		}

		// Selection cancel
		case 0:
			retToSetup();
	}
}

void doStyleEdit(void)
{
	// If not using JACK already, then we need to switch to JACK since apps like Muse, QTractor, etc
	// seem to require audio output via JACK. And we can't share the audio device via ALSA
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	memcpy(&TempAudioDev, &SoundDev[DEVNUM_AUDIOOUT], sizeof(struct SOUNDDEVINFO));
	if (SoundDev[DEVNUM_AUDIOOUT].Handle)
	{
		if (!SoundDev[DEVNUM_AUDIOOUT].DevHash) goto gotJack;
		freeAudio(0);
		SoundDev[DEVNUM_AUDIOOUT].DevHash = 0;
		TempAudioDev.Handle = 0;
	}

	if (DevAssigns[PLAYER_DRUMS] == &SoundDev[DEVNUM_AUDIOOUT] ||
		DevAssigns[PLAYER_BASS] == &SoundDev[DEVNUM_AUDIOOUT] ||
		DevAssigns[PLAYER_GTR] == &SoundDev[DEVNUM_AUDIOOUT])
	{
		register const char *	err;

		if ((err = open_libjack()))
		{
			show_msgbox(err);
			restore_audio_connection();
			showSetupScreen();
			return;
		}

		loadDataSets(LOADFLAG_INSTRUMENTS);
		allocAudio();
	}

gotJack:
#endif

	// Do we already have MIDI IN set up to use the SEQ API?
	memcpy(&TempMidiDev, &SoundDev[DEVNUM_MIDIIN], sizeof(struct SOUNDDEVINFO));
	if (SoundDev[DEVNUM_MIDIIN].Handle && (SoundDev[DEVNUM_MIDIIN].DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_SEQ)
	{
		// Yes, so just disconnect from whomever we're connected to
		snd_seq_disconnect_from(SoundDev[DEVNUM_MIDIIN].Handle, SoundDev[DEVNUM_MIDIIN].Card, SoundDev[DEVNUM_MIDIIN].Dev, SoundDev[DEVNUM_MIDIIN].SubDev);
	}
	else
	{
		closeMidiIn();
		TempMidiDev.Handle = 0;
	}

	// Prompt user for the seq app to connect to
	SoundDev[DEVNUM_MIDIIN].DevFlags = DEVFLAG_INPUTDEV | DEVFLAG_SOFTMIDI | DEVTYPE_SEQ;
	SoundDev[DEVNUM_MIDIIN].DevHash = 0;
	SoundDev[DEVNUM_MIDIIN].Handle = 0;

	headingCopyTo("Pick your Midi editor program", 0);
	headingShow(GUICOLOR_GOLD);
	doPickSoundDevDlg(&SoundDev[DEVNUM_MIDIIN], styleDevice);
}












///////////////////// Song Editor //////////////////////


static const unsigned char BoxesPerQuarter[] = {24, 1, -3, 2, 3, 4, 6, 8};
static const char	SongTypesStr[] = "Chord\0Style\0Variation\0Tempo\0Fill\0Pad\0Bass\0Guitar\0Drums\0TimeSig\0Repeat\0End";
static const char	ResolutionsStr[] = "Off\0Quarter\0Quarter triplet\0Eighth\08th triplet\0Sixteenth\016th triplet\032nd\0";

static GUICTLDATA	QuantizeFunc = {ctl_update_songtype, ctl_set_songtype};
static GUICTLDATA	ReloadStyleFunc = {ctl_update_nothing, ctl_set_quantize};


static GUICTL		SongEditCtls[] = {
	{.Type=CTLTYPE_CHECK, 	.Y=0,	.Label=SongTypesStr,		.Ptr=&SongTypesFunc,	.Attrib.NumOfLabels=12,	.Flags.Global=CTLGLOBAL_AUTO_VAL},
	{.Type=CTLTYPE_ARROWS,	.Y=1,	.Label=ResolutionsStr, 	.Ptr=&QuantizeFunc,	.Attrib.NumOfLabels=8,	.Flags.Global=CTLGLOBAL_AUTO_VAL},
	{.Type=CTLTYPE_END},
};

static GUIFUNCS EdStyleFuncs = {dummy_drawing,
edsong_mouse,
dummy_keypress,
0};

static void songEdit(void)
{
//	setShownCtls(CTLMASK_DRUMLIST|CTLMASK_BASSLIST|CTLMASK_GTRLIST);
	setMidiInSiphon(&edSongFilter, 1);
	MainWin->Flags = GUIWIN_TAB_KEY|GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY;
	MainWin->Ctls = &SongEditCtls[0];
	GuiFuncs = &EdStyleFuncs;
	MainWin->LowerPresetBtns = GUIBTN_OK_SHOW|GUIBTN_CENTER;
	MainWin->Menu = 0;
	clearMainWindow();
	GuiCtlSetSelect(GuiApp, 0, 0, (GUICTL *)0 + GUIBTN_OK);
}



void positionEditorGui(void)
{
	fixup_setup_ctls(&EdStyleCtls[0]);
	GuiCtlScale(GuiApp, MainWin, &EdStyleCtls[0], -1);
	GuiCtlScale(GuiApp, MainWin, &SongEditCtls[0], -1);
}
