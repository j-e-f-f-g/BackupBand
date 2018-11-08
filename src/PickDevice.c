// PickDevice.c for Linux
// Copyright 2013 Jeff Glatt

// PickDevice is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// PickDevice is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with PickDevice. If not, see <http://www.gnu.org/licenses/>.

#include "Options.h"
#include "Main.h"
#include "PickDevice.h"

static struct SOUNDDEVINFO		SndDevCopy;
static DEVRETFUNC					RetFunc;
static GUICTL *					ListCtlPtr;
static unsigned char				DoneOnce;
static unsigned char				OrigSelIsJack;

static const char	OffOnStr[] = "Off\0On\0Enable";
static const char	DevTypeStrs[] = "None\0Audio\0MIDI\0Software";
static const char	DevTypeBoxStr[] = "Device type";

#ifndef NO_ALSA_AUDIO_SUPPORT
// User's choice of ALSA buffer period size
static unsigned char OrigFrameSize;
static unsigned char	FrameSize;
static char	FrameStrs[] = {'B','u','f','f','e','r',0,  0,0,0,0,0,0};
#ifndef NO_SAMPLERATE_SUPPORT
static unsigned char OrigRate;
static const char	RateStrs[] = {'K','H','z',' ','R','a','t','e',0,'4','4',0,'4','8',0,'8','8',0,'9','6',0};
#endif
#endif
#define	EmptyStr	&HwCtlSpec[5]
static const char	HwCtlSpec[] = "hw:%i";

static char		CtlLabelBuffer[40];








#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
static GUILIST * pickNoDev(GUIAPPHANDLE, GUICTL *, GUIAREA *);
#endif
#if !defined(NO_MIDI_IN_SUPPORT) || !defined(NO_MIDI_OUT_SUPPORT)
static GUILIST * pickMidiDev(GUIAPPHANDLE, GUICTL *, GUIAREA *);
#endif
#ifndef NO_SEQ_SUPPORT
static GUILIST * pickSeqDev(GUIAPPHANDLE, GUICTL *, GUIAREA *);
#endif
static uint32_t ctl_set_refresh(register GUICTL *);
static uint32_t ctl_set_devtype(register GUICTL *);
static GUICTLDATA	RefreshFunc = {ctl_update_nothing, ctl_set_refresh};
static GUICTLDATA	DevTypeFunc = {ctl_update_nothing, ctl_set_devtype};








struct SOUNDDEVINFO * get_temp_sounddev(void)
{
	return &SndDevCopy;
}





/********************* ctl_set_refresh() *********************
 * Called when user clicks on the "Refresh" button.
 */

static uint32_t ctl_set_refresh(register GUICTL * ctl)
{
	DoneOnce = 0;
	GuiListItemWidth(GuiApp, MainWin, &List, 0);

	// Redraw list
	do
	{
	} while ((++ctl)->Type);
	GuiCtlUpdate(GuiApp, 0, ctl->Next, 0, ctl->Y);
	return 0;
}


#ifndef NO_ALSA_AUDIO_SUPPORT

static GUILIST * pickAudioDev(GUIAPPHANDLE, GUICTL *, GUIAREA *);

/******************* setFrameSize() *********************
 * Sets ALSA buffer size. If 0, queries.
 */

unsigned char setFrameSize(register unsigned char size)
{
	if (size)
	{
		if (size == 1) return OrigFrameSize == FrameSize;
		OrigFrameSize = FrameSize = size;
	}
	return FrameSize;
}

static uint32_t ctl_update_frame(register GUICTL * ctl)
{
	sprintf(&FrameStrs[7], "%u", (FrameSize << 3) + 12);
	return 1;
}

static uint32_t ctl_set_frame(register GUICTL * ctl)
{
	register unsigned char	frameSize;

	frameSize = FrameSize;
	ctl->Flags.Local &= ~(CTLFLAG_NO_UP|CTLFLAG_NO_DOWN);
	if (ctl->Flags.Local & CTLFLAG_UP_SELECT)
	{
		if (frameSize < 255)
		{
			frameSize++;

good:		FrameSize = frameSize;

			ctl_update_frame(ctl);

			// Done setting this parameter. Let caller redraw the ctl
			return (uint32_t)-1;
		}
		ctl->Flags.Local |= CTLFLAG_NO_UP;
	}

	else
	{
		if (--frameSize) goto good;
		ctl->Flags.Local |= CTLFLAG_NO_DOWN;
	}
	return 0;
}

static GUICTLDATA	FrameFunc = {ctl_update_frame, ctl_set_frame};

#ifndef NO_SAMPLERATE_SUPPORT
static uint32_t ctl_update_rate(register GUICTL * ctl)
{
	return 1;
}

static uint32_t ctl_set_rate(register GUICTL * ctl)
{
	return 0;
}

static GUICTLDATA	SRateFunc = {ctl_update_rate, ctl_set_rate};
#endif // NO_SAMPLERATE_SUPPORT
#endif // NO_ALSA_AUDIO_SUPPORT

#define CTLID_DEVTYPE	0
#define CTLID_FRAMES		2
#define CTLID_RATE		3

static GUICTL		DevGuiCtls[] = {
 	{.Type=CTLTYPE_RADIO,	.X=1, .Label=&DevTypeStrs[0],	.Ptr=&DevTypeFunc,	.Attrib.NumOfLabels=4, .Flags.Local=CTLFLAG_LABELBOX,	.Flags.Global=CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_PUSH, 	.Label="Refresh",			.Ptr=&RefreshFunc,	.Attrib.NumOfLabels=1},
#ifndef NO_ALSA_AUDIO_SUPPORT
 	{.Type=CTLTYPE_ARROWS, .Label=FrameStrs,			.Ptr=&FrameFunc,		.Attrib.NumOfLabels=1},
#ifndef NO_SAMPLERATE_SUPPORT
 	{.Type=CTLTYPE_ARROWS, .Label=RateStrs,			.Ptr=&SRateFunc,		.Attrib.NumOfLabels=4, .Flags.Global=CTLGLOBAL_AUTO_VAL},
#endif
#endif
	{.Type=CTLTYPE_END},
};

#ifndef NO_SAMPLERATE_SUPPORT
unsigned char userSampleRate(register unsigned char rate)
{
	if (rate < 4)
		GuiCtlArrowsInit(&DevGuiCtls[CTLID_RATE], rate);
	return DevGuiCtls[CTLID_RATE].Attrib.Value;
}
#endif





#if !defined(NO_JACK_SUPPORT) && !defined(NO_ALSA_AUDIO_SUPPORT)
static void show_audio_related(void)
{
	register GUICTL *			ctl;
	register unsigned char	flag, stop;

	// If "Use JACK" selected, hide buffer and sample rate ctls. Ditto if not an audio device type
	flag = (SndDevCopy.Card == DEVTYPE_AUDIO && (!(SndDevCopy.DevFlags & DEVFLAG_JACK) || List.CurrItemNum)) ? 0 : CTLTYPE_HIDE;
	ctl = &DevGuiCtls[0];
	stop = 0;
	do
	{
		if (ctl->Ptr == &FrameFunc || ctl->Ptr == &SRateFunc)
		{
			GuiCtlShow(GuiApp, MainWin, ctl, flag);
			if (++stop >= 2) break;
		}
		ctl++;
	} while (ctl->Type);
}
#endif




/*********************** dspDevName() *********************
 * Displays the specified device name.
 *
 * RETURN: 0 if another name can be displayed.
 */

static uint32_t dspDevName(const char * cardName, const char * devName, GUICTL * ctl, GUIAREA * area)
{
	register char *			temp;
	register const char *	name;

	// Counting devices?
	if (!DoneOnce) List.NumOfItems++;

	name = cardName;
	while (*name && *name++ == *devName) ++devName;
	while (*devName && *devName == ' ') ++devName;

	name = temp = GuiBuffer;
	while ((*temp++ = *cardName++));
	if (*devName)
	{
		*(temp - 1) = ' ';
		strcpy(temp, devName);
	}

	return GuiListDrawItem(GuiApp, ctl, area, name);
}




static void set_defaults(void)
{
	SndDevCopy.DevHash = SndDevCopy.Dev = SndDevCopy.SubDev = 0;
}




/****************** pickNoDev() *****************
 * For when the user has selected the "Off or "None"
 * RADIO btn.
 */

static GUILIST * pickNoDev(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app && !area)
	{
		set_defaults();
		SndDevCopy.DevFlags &= ~DEVFLAG_DEVTYPE_MASK;	// DEVTYPE_NONE
		SndDevCopy.Card = -1;
		return 0;
	}

	return &List;
}




/****************** compare_orig() ****************
 * Checks if the user's choices are the same as
 * whatever device he has already chosen (ie, same
 * device, with same settings)
 */

static int compare_orig(void)
{
	register struct SOUNDDEVINFO *	orig;

	// For an audio dev, check if frame size or sample rate changed
#if !defined(NO_JACK_SUPPORT) && !defined(NO_ALSA_AUDIO_SUPPORT)
	if ((SndDevCopy.DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_AUDIO &&
		(!(SndDevCopy.DevFlags & DEVFLAG_JACK) || SndDevCopy.DevHash) &&
		((DevGuiCtls[CTLID_RATE].Attrib.Value != OrigRate || FrameSize != OrigFrameSize)))
	{
		goto chg;
	}
#endif

	// Check if the dev changed
	orig = SndDevCopy.Original;
	if (SndDevCopy.DevFlags == orig->DevFlags &&
		orig->DevHash == SndDevCopy.DevHash &&
		((SndDevCopy.DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_SEQ ||
		(orig->Dev == SndDevCopy.Dev && orig->SubDev == SndDevCopy.SubDev)))
	{
		// Not changed
		return 1;
	}

#if !defined(NO_JACK_SUPPORT) && !defined(NO_ALSA_AUDIO_SUPPORT)
chg:
#endif
	return 0;
}




static void check_selection(register GUIAREA * area)
{
	if (List.CurrItemNum == -1)
	{
		register struct SOUNDDEVINFO *	orig;

		// If no item yet selected, see if the current item matches the original SOUNDDEVINFO
		orig = SndDevCopy.Original;
		if (SndDevCopy.Card == (orig->DevFlags & DEVFLAG_DEVTYPE_MASK) &&
			orig->DevHash == SndDevCopy.DevHash &&
			// SEQ type matches only hash
			(SndDevCopy.Card == DEVTYPE_SEQ || (orig->Dev == SndDevCopy.Dev && orig->SubDev == SndDevCopy.SubDev)))
		{
			List.CurrItemNum = area->ItemIndex;
		}
	}
}




/**************** openAlsaSeq, closeAlsaSeq ***********
 * We may create several ALSA SEQ midi ports, and for all
 * of them, we want to uae just one SEQ master handle.
 * This doles out that one handle.
 */

#if !defined(NO_SEQ_SUPPORT)

static snd_seq_t *			SeqHandle;

snd_seq_t * openAlsaSeq(register const char * name)
{
	snd_seq_t *			seq;

	if (!SeqHandle && snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT|SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK) >= 0)
	{
		SeqHandle = seq;

		if (name) snd_seq_set_client_name(seq, name);
	}

	return SeqHandle;
}

void closeAlsaSeq(void)
{
	if (SeqHandle) snd_seq_close(SeqHandle);
	SeqHandle = 0;
}

/****************** findSeqClient() *****************
 * Searches ALSA's list of SEQ clients for one (or more)
 * clients we wish to connect to. Our DEVMATCHFUNC is
 * called with the hash of each client's ports.
 */

void * findSeqClient(register DEVMATCHFUNC func, register unsigned char type)
{
	SndDevCopy.DevFlags = type;
	return pickSeqDev(GuiApp, (GUICTL *)func, 0);
}

/******************** pickSeqDev() *********************
 * Displays the list of Sequencer devices, or handles the user
 * picking a particular device from the displayed list.
 *
 * area and ctl =	0 if picking a dev name. Only area = 0 if
 * enumerating the list via findSeqClient().
 *
 * RETURN: 0 for success if choosing a dev.
 */

static GUILIST * pickSeqDev(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app)
	{
		register snd_seq_t *			seq;
		snd_seq_t *						origseq;

		set_defaults();
		if (!area)
		{
			if (!ctl && !List.CurrItemNum--) goto internal;
		}
		else
		{
			check_selection(area);
			dspDevName("<Manually connect>", EmptyStr, ctl, area);
		}

		origseq = SeqHandle;
		if ((seq = openAlsaSeq(0)) >= 0)
		{
			snd_seq_client_info_t *	cinfo;
			snd_seq_port_info_t *	pinfo;
			register uint32_t			type;

			snd_seq_client_info_alloca(&cinfo);
			snd_seq_port_info_alloca(&pinfo);

			type = (SndDevCopy.DevFlags & DEVFLAG_INPUTDEV) ? (SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ) : (SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE);

			snd_seq_client_info_set_client(cinfo, -1);
			while (snd_seq_query_next_client(seq, cinfo) >= 0)
			{
				SndDevCopy.Dev = snd_seq_client_info_get_client(cinfo);

				// Don't list BackupBand's ports
				if (SndDevCopy.Dev != snd_seq_client_id(seq) && snd_seq_client_info_get_type(cinfo) == SND_SEQ_USER_CLIENT)
				{
					snd_seq_port_info_set_client(pinfo, SndDevCopy.Dev);
					snd_seq_port_info_set_port(pinfo, -1);

					while (snd_seq_query_next_port(seq, pinfo) >= 0)
					{
						if ((snd_seq_port_info_get_capability(pinfo) & (type|SND_SEQ_PORT_TYPE_SPECIFIC|SND_SEQ_PORT_SYSTEM_ANNOUNCE)) == type)
						{
							SndDevCopy.SubDev = snd_seq_port_info_get_port(pinfo);

							// For DEVTYPE_SEQ, the hash is the client name with port name appended
							strcpy((char *)TempBuffer, snd_seq_client_info_get_name(cinfo));
							strcat((char *)TempBuffer, snd_seq_port_info_get_name(pinfo));
							SndDevCopy.DevHash = hash_string(TempBuffer);

							// If picking a dev by index, see if we've got our match
							if (!area)
							{
								// Config at program start? (Devices pref file)
								if (ctl)
								{
									if (((DEVMATCHFUNC)ctl)(&SndDevCopy)) goto found;
								}

								else if (!List.CurrItemNum--)
								{
found:							if (!origseq) closeAlsaSeq();

									//  Mark it as a SEQ dev
internal:						SndDevCopy.DevFlags = (SndDevCopy.DevFlags & ~DEVFLAG_DEVTYPE_MASK) | DEVTYPE_SEQ;
									SndDevCopy.Card = 0;

									return 0;
								}
							}
							else
							{
								check_selection(area);
								if (dspDevName(snd_seq_client_info_get_name(cinfo), snd_seq_port_info_get_name(pinfo), ctl, area))
									goto out;
							}
						}
					}
				}
			}

out:		if (!origseq) closeAlsaSeq();
		}
	}

	return &List;
}
#else
static GUILIST * pickSeqDev(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app && !area && !List.CurrItemNum)
	{
		// Set to "<Manually connect>"
		set_defaults();
		SndDevCopy.Card = 0;
		SndDevCopy.DevFlags = (SndDevCopy.DevFlags & ~DEVFLAG_DEVTYPE_MASK) | DEVTYPE_SEQ;
		return 0;
	}

	return &List;
}
#endif


#if !defined(NO_MIDI_IN_SUPPORT) || !defined(NO_MIDI_OUT_SUPPORT)

/********************* pickMidiDev() *********************
 * Displays the list of MIDI devices, or handles the user
 * picking a particular device from the displayed list.
 *
 * area =	0 if picking a dev name.
 *
 * RETURN: 0 for success if choosing a dev.
 */

static GUILIST * pickMidiDev(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app)
	{
		snd_ctl_card_info_t *	cardInfo;
		snd_rawmidi_info_t *		rawMidiInfo;
		int							cardNum;

		// Start with first card
		cardNum = -1;

		// We need to get a snd_ctl_card_info_t. Just alloc it on the stack
		snd_ctl_card_info_alloca(&cardInfo);

		// To get some info about the subdevices of this MIDI device (on the card), we need a
		// snd_rawmidi_info_t, so let's allocate one on the stack
		snd_rawmidi_info_alloca(&rawMidiInfo);

		for (;;)
		{
			snd_ctl_t *				ctlHandle;

			// Get next sound card's card number. When "cardNum" == -1, then ALSA
			// fetches the first card
			if (snd_card_next(&cardNum) < 0 ||

			// No more cards? ALSA sets "cardNum" to -1 if so
				cardNum < 0)
			{
				break;
			}

			// Open this card's control interface. We specify only the card number -- not any device nor sub-device too
			sprintf(GuiBuffer, &HwCtlSpec[0], cardNum);
			if (snd_ctl_open(&ctlHandle, GuiBuffer, 0) < 0) continue;

			{
			int							devNum;

			// Start with the first device on this card, and list all MIDI devices
			devNum = -1;
			for (;;)
			{
				register int		subDevCount, i;

				// Get the number of the next audio device on this card
				if (snd_ctl_rawmidi_next_device(ctlHandle, &devNum) < 0 ||

				// No more MIDI devices on this card? ALSA sets "devNum" to -1 if so.
					devNum < 0)
				{
					break;
				}

				memset(rawMidiInfo, 0, snd_rawmidi_info_sizeof());

				// Tell ALSA which device (number) we want info about
				snd_rawmidi_info_set_device(rawMidiInfo, devNum);

				// Get info on the MIDI out or in section of this device
				snd_rawmidi_info_set_stream(rawMidiInfo, (SndDevCopy.DevFlags & DEVFLAG_INPUTDEV) ? 1 : 0);

				i = -1;
				subDevCount = 1;

				// More subdevices?
				while (++i < subDevCount)
				{
					// Tell ALSA to fill in our snd_rawmidi_info_t with info on this subdevice
					snd_rawmidi_info_set_subdevice(rawMidiInfo, i);
					if (snd_ctl_rawmidi_info(ctlHandle, rawMidiInfo) < 0) continue;

					// Get how many subdevices (once only)
					if (!i)
					{
						subDevCount = snd_rawmidi_info_get_subdevices_count(rawMidiInfo);

						// Tell ALSA to fill in our snd_ctl_card_info_t with info about this card
						if (snd_ctl_card_info(ctlHandle, cardInfo) < 0) break;
					}

					// Store chosen values
					SndDevCopy.Dev = devNum;
					SndDevCopy.SubDev = i;
					SndDevCopy.DevHash = hash_string((unsigned char *)snd_ctl_card_info_get_id(cardInfo));

					// If picking an output by index, see if we've got our match
					if (!area)
					{
						if (!List.CurrItemNum--)
						{
							snd_ctl_close(ctlHandle);

							SndDevCopy.Card = cardNum;
							SndDevCopy.DevFlags = (SndDevCopy.DevFlags & ~DEVFLAG_DEVTYPE_MASK) | DEVTYPE_MIDI;

							return 0;
						}
					}
					else
					{
						check_selection(area);
						if (dspDevName(snd_ctl_card_info_get_name(cardInfo), snd_rawmidi_info_get_subdevice_name(rawMidiInfo), ctl, area))
						{
							snd_ctl_close(ctlHandle);
							goto out;
						}
					}
				}
			}
			}

			// Close the card's control interface after we're done listing its subdevices
			snd_ctl_close(ctlHandle);
		}

out:	DoneOnce = 1;
	}

	return &List;
}
#endif





#if !defined(NO_ALSA_AUDIO_SUPPORT)

/********************* pickAudioDev() *********************
 * Displays the list of audio devices, or picks a particular
 * device by user picking from the list.
 *
 *
 * RETURN: 0 for success if choosing a dev.
 */

static GUILIST * pickAudioDev(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app)
	{
		snd_ctl_card_info_t *	cardInfo;
		snd_pcm_info_t *			pcmInfo;
		int							cardNum;

#ifndef NO_JACK_SUPPORT
		if (SndDevCopy.DevFlags & DEVFLAG_JACK)
		{
			set_defaults();
			if (!area)
			{
				cardNum = 0;
				if (!List.CurrItemNum--) goto jack;
			}
			else
			{
				check_selection(area);
				dspDevName("Use JACK", EmptyStr, ctl, area);
			}
		}
#endif
		snd_ctl_card_info_alloca(&cardInfo);

		snd_pcm_info_alloca(&pcmInfo);

		cardNum = -1;
		for (;;)
		{
			snd_ctl_t *			ctlHandle;
			int					devNum;

			if (snd_card_next(&cardNum) < 0 || cardNum < 0) break;

			sprintf(GuiBuffer, &HwCtlSpec[0], cardNum);
			if (snd_ctl_open(&ctlHandle, GuiBuffer, 0) < 0) continue;

			devNum = -1;
			for (;;)
			{
				register int		subDevNum;

				if (snd_ctl_pcm_next_device(ctlHandle, &devNum) < 0 || devNum < 0) break;

				memset(pcmInfo, 0, snd_pcm_info_sizeof());

				snd_pcm_info_set_device(pcmInfo, devNum);

				snd_pcm_info_set_stream(pcmInfo, (SndDevCopy.DevFlags & DEVFLAG_INPUTDEV) ? 1 : 0);

				subDevNum = 0;
				for (;;)
				{
					snd_pcm_info_set_subdevice(pcmInfo, subDevNum);
					if (snd_ctl_pcm_info(ctlHandle, pcmInfo) < 0) break;

					if (!subDevNum && snd_ctl_card_info(ctlHandle, cardInfo) < 0) break;

					if (!strcmp(snd_ctl_card_info_get_id(cardInfo), "Loopback")) break;

					if (snd_pcm_info_get_stream(pcmInfo) == ((SndDevCopy.DevFlags & DEVFLAG_INPUTDEV) ? 1 : 0))
					{
						SndDevCopy.Dev = devNum;
						SndDevCopy.SubDev = subDevNum;
						SndDevCopy.DevHash = hash_string((unsigned char *)snd_ctl_card_info_get_id(cardInfo));
						if (!area)
						{
							if (!List.CurrItemNum--)
							{
								snd_ctl_close(ctlHandle);
#ifndef NO_JACK_SUPPORT
jack:
#endif
								SndDevCopy.Card = cardNum;
								SndDevCopy.DevFlags = (SndDevCopy.DevFlags & ~DEVFLAG_DEVTYPE_MASK) | DEVTYPE_AUDIO;

								return 0;
							}
						}
						else
						{
							check_selection(area);

							if (dspDevName(snd_ctl_card_info_get_name(cardInfo), snd_pcm_info_get_name(pcmInfo), ctl, area))
							{
								snd_ctl_close(ctlHandle);
								goto out;
							}
						}
					}

					++subDevNum;
				}
			}

			snd_ctl_close(ctlHandle);
		}
//
out:	DoneOnce = 1;
	}

	return &List;
}
#elif !defined(NO_JACK_SUPPORT)
// For when JACK is the only audio choice
static GUILIST * pickAudioDev(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app && !area && !List.CurrItemNum)
	{
		// Set to "Use Jack"
		set_defaults();
		SndDevCopy.Card = 0;
		SndDevCopy.DevFlags = (SndDevCopy.DevFlags & ~DEVFLAG_DEVTYPE_MASK) | DEVTYPE_AUDIO;
		return 0;
	}

	return &List;
}
#endif





/********************* handle_mouse() **********************
 * Called by GUI thread to process user mouse input in
 * Pick Dev screen.
 */

static void handle_mouse(register GUIMSG * msg)
{
	register GUICTL *		ctl;

	ctl = msg->Mouse.SelectedCtl;
	if (ctl->Flags.Global & CTLGLOBAL_PRESET)
	{
		if (ctl->PresetId == GUIBTN_CANCEL) goto cancel;
		goto ok;
	}

	if (ctl->Type != CTLTYPE_AREA)
	{
		if (ctl->Ptr && ((GUICTLDATA *)ctl->Ptr)->SetFunc(ctl))
			GuiCtlUpdate(GuiApp, 0, ctl, 0, 0);
	}
	else switch (msg->Mouse.ListAction)
	{
		case GUILIST_SCROLL:
		case GUILIST_CLICK:
#if !defined(NO_JACK_SUPPORT) && !defined(NO_ALSA_AUDIO_SUPPORT)
			show_audio_related();
#endif
			break;

		case GUILIST_SELECTION:
		{
			register GUILISTFUNC *	func;

ok:		func = setListDrawFunc(0);
			if (func(GuiApp, 0, 0))
			{
				// If looking for a dev, but didn't find it, then refresh list
				ctl_set_refresh(&DevGuiCtls[0]);
				break;
			}

			// If not the same as orig, return it
			if (!compare_orig())
			{
				snd_config_update_free_global();

				// Close original dev
				if (SndDevCopy.Original->Handle) RetFunc(SndDevCopy.Original, 2);

				// Open new dev
				memcpy(SndDevCopy.Original, &SndDevCopy, sizeof(struct SOUNDDEVINFO));
				SndDevCopy.Original->Handle = 0;
				RetFunc(SndDevCopy.Original, 1);
				break;
			}

			// Fall through
		}

		case GUILIST_ABORT:
cancel:	snd_config_update_free_global();

			// Dismiss the PickDevice screen
			RetFunc(SndDevCopy.Original, 0);

			// Restore orig vals
			setFrameSize(OrigFrameSize);
#ifndef NO_SAMPLERATE_SUPPORT
			userSampleRate(OrigRate);
#endif
	}
}





/********************* ctl_set_devtype() *********************
 * Called when user clicks on the "None/Audio/MIDI/Software" device
 * type radio buttons.
 */

static uint32_t ctl_set_devtype(register GUICTL * ctl)
{
	register const char *		str;
	register unsigned char		type;

	// Translate value string to DEVTYPE_NONE, DEVTYPE_AUDIO, DEVTYPE_MIDI, or DEVTYPE_SEQ
	str = ctl->Label;
	type = ctl->Attrib.Value;
	while (--type)
	{
		while (*str++);
	}

	type = DEVTYPE_NONE;
	setListDrawFunc(pickNoDev);

	switch (*str)
	{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
		case 'A':
		{
setaudio:
			type = DEVTYPE_AUDIO;
			setListDrawFunc(pickAudioDev);
			break;
		}
#endif

		// Off or On
		case 'O':
		{
			if (*(str + 1) != 'f')
			{
				// On. Determine AUDIO, raw MIDI, or Software (ALSA Seq)
				switch (SndDevCopy.DevFlags & (DEVFLAG_SOFTMIDI|DEVFLAG_AUDIOTYPE|DEVFLAG_MIDITYPE))
				{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
					case DEVFLAG_AUDIOTYPE:
						goto setaudio;
#endif
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_MIDI_IN_SUPPORT)
					case DEVFLAG_MIDITYPE:
						goto setmidi;
#endif
#ifndef NO_SEQ_SUPPORT
					case DEVFLAG_SOFTMIDI:
						goto setseq;
#endif
				}
			}
			break;
		}

#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_MIDI_IN_SUPPORT)
		case 'M':
		{
			if (SndDevCopy.DevFlags & DEVFLAG_MIDITYPE)
			{
setmidi:		type = DEVTYPE_MIDI;
				setListDrawFunc(pickMidiDev);
			}
			break;
		}
#endif

#ifndef NO_SEQ_SUPPORT
		case 'S':
		{
setseq:	if (SndDevCopy.DevFlags & DEVFLAG_SOFTMIDI)
			{
				type = DEVTYPE_SEQ;
				setListDrawFunc(pickSeqDev);
			}
//			break;
		}
#endif
	}

	// ===========================
	if (SndDevCopy.Card != type)
	{
		// We need to reset the list
		DoneOnce = 0;
		GuiListItemWidth(GuiApp, MainWin, &List, 0);

		// "Device Type" RADIO ctl must be first ctl
		ctl++;

		// If "None" device type selected, then remove all ctls except the device type. Otherwise
		// show all except perhaps sample rate and framesize
		if (!(SndDevCopy.Card = type))
		{
			do
			{
				GuiCtlShow(GuiApp, MainWin, ctl, CTLTYPE_HIDE);
				ctl++;
			} while (ctl->Type);
			ctl->Next = 0;
		}
		else
		{
			type = (type != DEVTYPE_AUDIO || OrigSelIsJack) ? CTLTYPE_HIDE : 0;
			do
			{
				GuiCtlShow(GuiApp, MainWin, ctl, (ctl->Ptr == &FrameFunc || ctl->Ptr == &SRateFunc) ? type : 0);
				ctl++;
			} while (ctl->Type);

			// Append the list AREA
			append_list_ctl(ctl);
		}

		clearMainWindow();
	}

	return 0;
}




static GUIFUNCS DevGuiFuncs = {dummy_drawing,
handle_mouse,
dummy_keypress,
0};

/******************** doPickSoundDevDlg() ******************
 * Shows/operates the "Pick MIDI In/Out..." screen. Allows
 * user to pick a new MIDI In/Out device.
 *
 * Alternately, shows/operates the "Pick Audio In/Out..."
 * screen. Allows user to pick a new Audio In/Out device.
 */

void doPickSoundDevDlg(register struct SOUNDDEVINFO * sounddev, register DEVRETFUNC func)
{
	memcpy(&SndDevCopy, sounddev, sizeof(struct SOUNDDEVINFO));
	SndDevCopy.Original = sounddev;
	OrigSelIsJack = ((SndDevCopy.Card = SndDevCopy.DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_AUDIO && !sounddev->DevHash) ? 1 : 0;
	RetFunc = func;

	DevGuiCtls[CTLID_DEVTYPE].Attrib.Value = 1;
	DevGuiCtls[CTLID_DEVTYPE].Type = CTLTYPE_RADIO;

	// For the device type radio button, show only the ones the caller wants:
	// DEVFLAG_NODEVICE = "None"
	// DEVFLAG_AUDIOTYPE = "Audio"
	// DEVFLAG_MIDITYPE = "Midi"
	// DEVFLAG_SOFTMIDI = "Software"
	// NOTE: DEVFLAG_NODEVICE can't appear alone
	{
	register char *			to;
	register const char *	from;
	register unsigned char	flag, count;

	flag = count = 0;
	DevGuiCtls[CTLID_DEVTYPE].Label = to = &CtlLabelBuffer[0];
	from = &DevTypeStrs[0];
	do
	{
		if ((DEVFLAG_NODEVICE << flag) & SndDevCopy.DevFlags)
		{
			while ((*to++ = *from++));
			count++;
			if (flag == SndDevCopy.Card)
				DevGuiCtls[CTLID_DEVTYPE].Attrib.Value = count;
		}
		else
			while (*from++);
	} while (++flag < 4);

	// If only 2 choices, and one of them is "None", then just show "Off" and "On"
	if (count >= 3)
	{
		from = &DevTypeBoxStr[0];
		while ((*to++ = *from++));
	}
	else
	{
		if (DEVFLAG_NODEVICE & SndDevCopy.DevFlags) DevGuiCtls[CTLID_DEVTYPE].Label = &OffOnStr[0];

		// If only 1 type, then hide radio btns
		if (count < 2)
			DevGuiCtls[CTLID_DEVTYPE].Type = CTLTYPE_RADIO|CTLTYPE_HIDE;
	}
	DevGuiCtls[CTLID_DEVTYPE].Attrib.NumOfLabels = count;
	}

	// Reset list of dev names
	ListCtlPtr = showPickItemList(pickNoDev);

	MainWin->Ctls = &DevGuiCtls[0];
	GuiFuncs = &DevGuiFuncs;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-conversion"
	MainWin->Menu = MainWin->UpperPresetBtns = 0;
#pragma GCC diagnostic pop
	MainWin->LowerPresetBtns = GUIBTN_OK_SHOW|GUIBTN_CANCEL_SHOW|GUIBTN_CENTER;
	MainWin->Flags = GUIWIN_ESC_KEY|GUIWIN_LIST_KEY|GUIWIN_ENTER_KEY|GUIWIN_TAB_KEY;

	clearMainWindow();

	// Init which device list gets shown
	SndDevCopy.Card = -1;
	ctl_set_devtype(&DevGuiCtls[CTLID_DEVTYPE]);

	// Init "Buffer Size" and "Sample rate" ctls
#ifndef NO_ALSA_AUDIO_SUPPORT
	ctl_update_frame(0);
	OrigFrameSize = FrameSize;
#ifndef NO_SAMPLERATE_SUPPORT
	OrigRate = userSampleRate(0xff);
#endif
#endif

	GuiCtlSetSelect(GuiApp, 0, 0, (GUICTL *)0 + GUIBTN_CANCEL);
}





/*************** positionPickDevGui() ****************
 * Sets initial position/scaling of GUI ctls based
 * upon font size.
 */

void positionPickDevGui(void)
{
	GuiCtlScale(GuiApp, MainWin, &DevGuiCtls[0], -1);

#if !defined(NO_SEQ_SUPPORT)
	SeqHandle = 0;
#endif

#ifndef NO_ALSA_AUDIO_SUPPORT
	OrigFrameSize = FrameSize = (128/4) - 12;
#endif
}
