// Backup Band for Linux
// Copyright 2013 Jeff Glatt

// Backup Band is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Backup Band is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of:::
// You should have received a copy of the GNU General Public License
// along with Backup Band. If not, see <http://www.gnu.org/licenses/>.

//#define TEST_AUDIO_MIX
//#define JG_NOTE_DEBUG

#include <dlfcn.h>
#include "Options.h"
#include "Main.h"
#include "PickDevice.h"
#include "MidiIn.h"
#include "AudioPlay.h"
#include "AccompSeq.h"
#include "Setup.h"
#include "Prefs.h"
#include "StyleData.h"
#include "MVerb.h"
#include "FileLoad.h"
#include "SongSheet.h"
#ifndef O_NOATIME
#define O_NOATIME        01000000
#endif
#pragma pack(1)

#define WAVEFLAG_STEREO		0x01
#define WAVEFLAG_48000		0x10
#define WAVEFLAG_88200		0x20
#define WAVEFLAG_96000		0x30

// Holds info about one loaded waveform
typedef struct _WAVEFORM_INFO {
	struct _WAVEFORM_INFO *	Next;
	uint32_t					WaveformLen;	// Size of data in 16-bit samples -- not frames nor bytes
	uint32_t					CompressPoint;	// Sample offset to 8-bit bytes
	uint32_t					LoopBegin;		// Sample offset to loop start. If LoopBegin >= WaveformLen, then no loop
	uint32_t					LoopEnd;			// Sample offset to loop end
	uint32_t					LegatoOffset;	// Offset (past the initial attack of the wave) to where a hammer-on/legato note would begin. In 16-bit samples
	unsigned char			WaveFlags;
	unsigned char			Unused;
	char						WaveForm[1];	// Loaded wave data. Size=WaveformLen*2
} WAVEFORM_INFO;

// Holds info about one Zone in an instrument/kit
typedef struct _PLAYZONE_INFO {
	struct _PLAYZONE_INFO *	Next;			// For a single linked list of all Zones
	unsigned char			HighNote;		// High MIDI Note number for the zone. 0 = RootNote only for drums
	unsigned char			RootNote;		// MIDI Note/CC number that triggers the zone. 1 to 127. -1 to -127 for Note-off triggered. 0 is reserved
	unsigned char			RangeCount;		// # of Ranges in this Zone. 0 to 255
	unsigned char			Volume;			// 1 to 255
	unsigned char			PanPosition;	// 0 to 127 where 0 = full left, 64 = center, 127 = full right
	unsigned char			Groups;			// Zone group bits, or 0 if none
	unsigned char			MuteGroups;		// MUTE group bits, or 0 if none
	unsigned char			FadeOut;			// Release envelope time. 0 to 255
	unsigned char			Flags;			// See PLAYZONEFLAG_ #define
	unsigned char			Reverb;			// 1 to 255
	// When alloc'ed, the following fields
	// are appended:
//	WAVEFORM_INFO *		WaveInfoLists[RangeCount];	// Linked list (head) of round-robin waves for each range
//	WAVEFORM_INFO *		WaveQueue[RangeCount];		// Next round-robin wave for each range (ie, a moving ptr)
//	unsigned char			Ranges[RangeCount];			// The Midi upper note velocity for each Range
} PLAYZONE_INFO;

#define INSFLAG_HIDDEN	0x02

typedef struct {
	char						Transpose;
	unsigned char			BankMsb;			// MIDI Bank MSB # that selects the instrument. 0x00 to 0x7F, bit 7 set for ignore
	unsigned char			BankLsb;
	unsigned char			Flags;
} PATCH_INFO;

// Holds info about one Drum Kit or Instrument
typedef struct _INS_INFO {
	struct _INS_INFO *	Next;				// For a single linked list of all instruments
	PLAYZONE_INFO *		Zones;			// The zones for this instrument
	PLAYZONE_INFO *		ReleaseZones;	// A set of release samples, or 0 if none
	PLAYZONE_INFO *		LayerZones;		// A set of samples for a dual layer, or 0 if none
	union {
	uint32_t					Hash;				// Used temp for kits
	struct _INS_INFO *	Kit;				// Any sub-kit for this kit
	PATCH_INFO				Patch;			// Used for instrument (except drums)
	} Sub;
	unsigned char			PgmNum;			// MIDI Pgm # that selects the instrument. 0x00 to 0x7F
	char						Name[1];			// Nul-terminated instrument name
} INS_INFO;

#define VOICECHANGEFLAG_VOL			0x01	// A volume change has happened for this robot
static unsigned char		VoiceChangeFlags;

// VOICE_INFO AudioFuncFlags
#define AUDIOPLAYFLAG_QUEUED				0x01	// VOICE_INFO is already in audio thread's list
#define AUDIOPLAYFLAG_LEGATO				0x20
#define AUDIOPLAYFLAG_FINAL_FADE			0x40	// Audio thread is doing a fast fade of the voice
#define AUDIOPLAYFLAG_SKIP_RELEASE		0x80	// Fade out is not subject to change. Basically, a "oneshot"

// VOICE_INFO Flags
#define VOICEFLAG_VOL_CHANGE				0x10	// Volume has changed for the voice
#define VOICEFLAG_SUSTAIN_PEDAL			0x20	// Voice is off, but currently being held only by sustain pedal
#define VOICEFLAG_FASTRELEASE				0x40	// Fade the voice with a fast release env, usually because a note-off received
#define VOICEFLAG_SUSTAIN_INFINITE		0x80	// Loop sustains until note off -- no loop fade

// Holds info about one, currently playing waveform. We have an
// array of these. The size of the array is determined by the
// (voices) polyphony we allow (MAX_AUDIO_POLYPHONY)
typedef struct _VOICE_INFO {
	struct _VOICE_INFO * Next;						// For audio thread's queue
	WAVEFORM_INFO *		Waveform;				// WAVEFORM_INFO of the waveform this voice is currently playing
	PLAYZONE_INFO *		Zone;
	INS_INFO *				Instrument;
	uint32_t					CurrentOffset;			// Current read ptr for this wave. Used for copying data to the mix buffer
	uint32_t					TransposeIncrement;	// For linear interpolation
	uint32_t					TransposeFracPos;		// For linear interpolation
	uint32_t					ReleaseTime;			// Loop fadeout speed, or note release speed
	float						AttackLevel;			// If not 0, then initial attack fades in until this vol
	float						VolumeFactor;			// Volume of this voice
	unsigned char			Lock;						// To arbitrate thread access to this voice
	unsigned char			AttackDelay;			// Initial delay before attack
	unsigned char			FadeOut;					// Release envelope time. 1 to 255
	union {
	unsigned char			GtrNoteSpec;			// PLAYER_GTR
	unsigned char			ActualNote;				// PLAYER_SOLO/PLAYER_PAD
	};
	unsigned char			TriggerTime;			// Time that this voice was triggered. Used for voice stealing
	unsigned char			AudioFuncFlags;		// Set if added to audio thread list. Audio thread clears when note is removed
	unsigned char			ClientFlags;			// VOICEFLAG_XXX
	unsigned char			NoteNum;					// Note number that triggered this voice, 1 to 127. Bit 7 set if the voice
															// has been turned off with MIDI noteoff, but may still be playing in release env
	unsigned char			Velocity;				// MIDI note velocity
	unsigned char			Musician;				// Musician using this voice (PLAYER_xxx)
} VOICE_INFO;

#pragma pack()






// Audio (Vocal) In, Audio Out (Internal Synth), 4 External (MIDI) Synths, MIDI In (Controller)
struct SOUNDDEVINFO			SoundDev[7];

// Outputs for Drums, Bass, Gtr, Pad, Human's Instrument (All default to "Internal Synth")
#if defined(NO_ALSA_AUDIO_SUPPORT) && defined(NO_JACK_SUPPORT)
#define DEVNUM_DEFAULT DEVNUM_MIDIOUT1
#else
#define DEVNUM_DEFAULT DEVNUM_AUDIOOUT
#endif
struct SOUNDDEVINFO *		DevAssigns[5] = {&SoundDev[DEVNUM_DEFAULT], &SoundDev[DEVNUM_DEFAULT], &SoundDev[DEVNUM_DEFAULT],
															&SoundDev[DEVNUM_DEFAULT], &SoundDev[DEVNUM_DEFAULT]};

// Linked lists of loaded DRUMKITs, Basses, Guitars, Pads, and Soloist's Instruments
static INS_INFO *				InstrumentLists[5];

// Currently selected kit, bass..., solo
static INS_INFO *				CurrentInstrument[5];

// For the "Background Pad" RADIO GUICTL of None/Strings/Brass/Organ/Choir
static INS_INFO *				CachedPads[3];

// For toggling between 2 instruments
static INS_INFO *				PrevInstrument[5];

// Instrument (Patch) volume of drums/bass/gtr/pad/solo (0 to 100)
#define MAX_VOL_ADJUST	100
static unsigned char		MasterVolAdjust = MAX_VOL_ADJUST - 15;
static unsigned char		VolAdjust[5] = {MAX_VOL_ADJUST - 15, MAX_VOL_ADJUST - 15, MAX_VOL_ADJUST - 15, MAX_VOL_ADJUST - 15, MAX_VOL_ADJUST - 15};

// For boosting vol on Soloist's instrument during solos
static unsigned char		VolBoost = 0;

// For sustain/legato pedals
unsigned char				SustainPedal = 0;
unsigned char				LegatoPedal = 0;

// For PLAYER_PAD accomp
static const unsigned char		PadPatchNums[] = {48,61,16};

// Current MIDI Bank msb/lsb nums for Drums, Bass, Gtr, Pad, Solo. 0x00 to 0x7F, bit 7 set for ignore
unsigned char				BankNums[2*5];

// =======================================================
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

// All the VOICE_INFOs used for playback (allocated as one large block)
static VOICE_INFO *			VoiceLists[PLAYER_SOLO + 2];
static VOICE_INFO *			VoiceListSolo;

// Where the beat/midi/gui threads formulate the list of VoiceInfos to be put into play
static VOICE_INFO *			VoicePlayQueue;

// The Audio thread's queue of playing voices
static VOICE_INFO *			AudioThreadQueue;

// Ptr to an allocated buffer for our "playback (output) mix"
static char *					MixBuffPtr;
static char *					MixBuffEnd;

// For MVerb reverb code
#ifndef NO_REVERB_SUPPORT
static REVERBHANDLE			Reverb = 0;
static char *					ReverbBuffPtr;
#endif

// For arbitrating voice access between threads
static int						SecondaryThreadPriority, AudioThreadPriority;

// Ptr to a buffer where we mix the currently playing (out) waveforms.
// Ideally will point to the soundcard's 32-bit MMAP buffer
static int32_t *				MixBufferPtr[2];

// Linear interpolation for transposing a wave
#define PCM_TRANSPOSE_LIMIT	12		// allow +- 1 octave
#define UPSAMPLE_BITS			13
#define UPSAMPLE_FACTOR			(0x00000001 << UPSAMPLE_BITS)
static float					TransposeTable[UPSAMPLE_FACTOR][2];

// User-chosen sample rate. Default 44.1 KHz
static const uint32_t		Rates[] = {44100,48000,88200,96000};
static uint32_t				DecayRate = 44100/22050;
/*
static float DecayFactors[128] = {
	0.0, 0.000062, 0.000248, 0.000558001, 0.000992002,
	0.00155, 0.002232, 0.00303801, 0.00396801, 0.00502201,
	0.00620001, 0.00750202, 0.00892802, 0.010478, 0.012152,
	0.01395, 0.015872, 0.017918, 0.020088, 0.022382,
	0.0248, 0.0273421, 0.0300081, 0.0327981, 0.0357121,
	0.0387501, 0.0419121, 0.0451981, 0.0486081, 0.0521421,
	0.0558001, 0.0595821, 0.0634881, 0.0675181, 0.0716721,
	0.0759502, 0.0803522, 0.0848782, 0.0895282, 0.0943022,
	0.0992002, 0.104222, 0.109368, 0.114638, 0.120032,
	0.12555, 0.131192, 0.136958, 0.142848, 0.148862,
	0.155, 0.161262, 0.167648, 0.174158, 0.180792,
	0.18755, 0.194432, 0.201438, 0.208568, 0.215822,
	0.2232, 0.230702, 0.238328, 0.246078, 0.253953,
	0.261951, 0.270073, 0.278319, 0.286689, 0.295183,
	0.303801, 0.312543, 0.321409, 0.330399, 0.339513,
	0.348751, 0.358113, 0.367599, 0.377209, 0.386943,
	0.396801, 0.406783, 0.416889, 0.427119, 0.437473,
	0.447951, 0.458553, 0.469279, 0.480129, 0.491103,
	0.502201, 0.513423, 0.524769, 0.536239, 0.547833,
	0.559551, 0.571393, 0.583359, 0.595449, 0.607663,
	0.620001, 0.632463, 0.645049, 0.657759, 0.670593,
	0.683551, 0.696633, 0.709839, 0.723169, 0.736623,
	0.750202, 0.763904, 0.77773, 0.79168, 0.805754,
	0.819952, 0.834274, 0.84872, 0.86329, 0.877984,
	0.892802, 0.907744, 0.92281, 0.938, 0.953314,
	0.968752, 0.984314, 1.0};
*/

// For volume calculation
static float VolFactors[128] = {
0.000000f, 0.001488f, 0.005952f, 0.013392f, 0.023808f, 0.037200f, 0.053568f, 0.072912f, 0.095232f, 0.120528f,
0.148800f, 0.180048f, 0.214272f, 0.251472f, 0.291648f, 0.334800f, 0.380928f, 0.430032f, 0.482112f, 0.537168f,
0.595200f, 0.656210f, 0.720194f, 0.787154f, 0.857090f, 0.930002f, 1.005890f, 1.084754f, 1.166594f, 1.251410f,
1.339202f, 1.429970f, 1.523714f, 1.620434f, 1.720130f, 1.822805f, 1.928453f, 2.037077f, 2.148677f, 2.263253f,
2.380805f, 2.501328f, 2.624832f, 2.751312f, 2.880768f, 3.013200f, 3.148608f, 3.286992f, 3.428352f, 3.572688f,
3.720000f, 3.870288f, 4.023552f, 4.179792f, 4.339008f, 4.501200f, 4.666368f, 4.834512f, 5.005632f, 5.179728f,
5.356800f, 5.536848f, 5.719872f, 5.905872f, 6.094872f, 6.286824f, 6.481752f, 6.679656f, 6.880536f, 7.084392f,
7.291224f, 7.501032f, 7.713816f, 7.929576f, 8.148312f, 8.370024f, 8.594712f, 8.822376f, 9.053016f, 9.286632f,
9.523224f, 9.762792f, 10.005336f, 10.250856f, 10.499352f, 10.750824f, 11.005272f, 11.262696f, 11.523096f, 11.786472f,
12.052824f, 12.322152f, 12.594456f, 12.869736f, 13.147992f, 13.429224f, 13.713432f, 14.000616f, 14.290776f, 14.583912f,
14.880024f, 15.179112f, 15.481176f, 15.786216f, 16.094233f, 16.405224f, 16.719193f, 17.036137f, 17.356056f, 17.678951f,
18.004848f, 18.333696f, 18.665520f, 19.000320f, 19.338097f, 19.678848f, 20.022575f, 20.369280f, 20.718960f, 21.071615f,
21.427248f, 21.785856f, 22.147440f, 22.511999f, 22.879536f, 23.250048f, 23.623535f, 24.000000f,
};

// Flags which robots have loaded waveforms playing on the internal synth
// (as opposed to being output to external MIDI hardware/software)
unsigned char		WavesLoadedFlag;

// Simulating guitar picking delays
unsigned char				PickAttack;

// Arbitrates access between 2 threads
static unsigned char		VoicePlayLock;

static unsigned char		NumOfInstruments[5];

static unsigned char		SampleRateFactor = 0;

// The # of waveform audio notes (ie, voices) that can play simultaneously
#define MAX_DRUM_POLYPHONY		24
#define MAX_BASS_POLYPHONY		4
#define MAX_GUITAR_POLYPHONY	(6*2)
#define MAX_PAD_POLYPHONY		(4*2)
#define MAX_HUMAN_POLYPHONY	64
static unsigned char		Polyphony[5] = {MAX_DRUM_POLYPHONY, MAX_BASS_POLYPHONY, MAX_GUITAR_POLYPHONY, MAX_PAD_POLYPHONY, MAX_HUMAN_POLYPHONY};

// ==============================================
#ifndef NO_ALSA_AUDIO_SUPPORT

// Index into a buffer where we store audio input
static uint32_t				InputIndex;

// Allows us to wait for ALSA to signal when it needs audio output,
// or has audio input
static struct pollfd *		DescPtrs;
static uint32_t				NumPlayDesc;
static uint32_t				NumInDesc;

// ALSA MMAP buffer 'chunk' size
static snd_pcm_uframes_t	FramesPerPeriod;

// Whether we must do non-interleaved output
static unsigned char			NonInterleaveFlag;

// # of channels for audio out. Ideally 2 for stereo
static unsigned char			NumChans;

// # of channels for audio in. 1 or 2
static unsigned char			NumInChans;

// Handle to our thread that does ALSA audio in/out, and a
// flag that tells the thread when to abort
static unsigned char		AudioThreadFlags;
static pthread_t			AudioThreadHandle;

// ALSA underruns
static unsigned char		CurrentXRuns;
static unsigned char		PreviousXRuns;

#endif	// !defined(NO_ALSA_AUDIO_SUPPORT)

#endif	// !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

// To keep track of note-offs that need to be sent to
// some midi out buss
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
static unsigned char LastDrumNotes[9];
static unsigned char LastBassNote = 0;
static unsigned char	PadNotes[3];
static unsigned char CurrentGtrPgm;
static unsigned char GtrNotes[6];
#endif

// We output directly to ALSA card. No resampling/reformatting data
const char					CardSpec[] = "hw:%i,%u,%u";
// ALSA card name
const char					CardCtlSpec[] = "hw:%i";

// For loading the drumkits/instruments
static PLAYZONE_INFO *	LoadedZones;
static uint32_t			SubKit;
static unsigned char		ListNum;
static char					TransposeVal;
static const char			TxtExtension[] = ".txt";
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
static const char 		Extension[] = ".cmp";
static const char			DidNotOpen[] = " didn't open";
#endif

#define NUM_OF_ZONE_IDS	12
#define ZONE_ID_REL		0
#define ZONE_ID_PAN		1
#define ZONE_ID_VOL		2
#define ZONE_ID_REV		3
#define ZONE_ID_RANGES	4
#define ZONE_ID_GROUP	5
#define ZONE_ID_MUTE 	6
#define ZONE_ID_NAME		7
#define ZONE_ID_NOTE 	8
#define ZONE_ID_HIGH 	9
#define ZONE_ID_HH		10
#define ZONE_ID_OFFSET	11

#define NUM_OF_HDR_IDS	13
#define HDR_ID_LAYER	0
#define HDR_ID_PGM 	1
#define HDR_ID_OCT 	2
#define HDR_ID_MSB	3
#define HDR_ID_LSB	4
#define HDR_ID_OPT	5
#define HDR_ID_REL	6
#define HDR_ID_PAN	7
#define HDR_ID_VOL	8
#define HDR_ID_REV	9
#define HDR_ID_RANGES	10
#define HDR_ID_GROUP		11
#define HDR_ID_MUTE 		12

static const uint32_t	HdrIds[] = {
0x79C708EF,	// LAYER
0x00A10E01,	// PGM
0x6482C117, // OCT
0x36494D94,	// MSB
0x2417AF38,	// LSB
0xC7DD9924,	// OPT
//================ ZoneIds[] = {
0xCDC97E32,		// REL
0x34E981B5,		// PAN
0x2109F940,		// VOL
0x5F1F20D7,		// REV
0x1CFBDC1F,		// RANGES
0xECE7E911,		// GROUP
0x289EA317,		// MUTE
0x1DAD61B1,		// NAME
0x028AE078,		// NOTE
0x57F7AFBD,		// HIGH
0x4600BA79,		// HH
0x0405B7A2};	// OFFSET

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
#define NUM_OF_RANGE_IDS	4
#define RANGE_ID_NAME	0
#define RANGE_ID_VEL 1
#define RANGE_ID_ROUND 2
#define RANGE_ID_OFFSET 3
static const uint32_t	RangeIds[] = {0x1DAD61B1,	// NAME
0xABEE62FA,								// VEL
0x288D6234,							// ROUND
0x0405B7A2};	// OFFSET
#endif

#define NUM_OF_OPT_IDS	3
static const uint32_t	OptIds[] = {0x3707C16C,	// KEYOFF
0x9A97366B,		// HIDE
0x74602C99};	// NOTENAMES

static unsigned char		Options;

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

#if !defined(NO_ALSA_AUDIO_SUPPORT)

static void voiceToPlayQueue(register VOICE_INFO *, register unsigned char);
#ifdef JG_NOTE_DEBUG
static void lockVoice(register VOICE_INFO *, register unsigned char, const char *, unsigned char);
#else
static void lockVoice(register VOICE_INFO *, register unsigned char);
#endif
#endif
static void clear_mix_buf(snd_pcm_uframes_t);
#endif
static void cache_pads(void);

#ifdef TEST_AUDIO_MIX
#include "WaveDebug.c"
#endif











#ifndef NO_JACK_SUPPORT

// ============================= JACK support ================================
#include <jack/jack.h>

static void mixPlayingVoices(snd_pcm_uframes_t);
static void setupReverb(void);

typedef jack_client_t * (JACKOPENCLIENT)(const char *, jack_options_t, jack_status_t *);
typedef void (JACKCLOSECLIENT)(jack_client_t *);
typedef jack_nframes_t (JACKSAMPLERATE)(jack_client_t *);
typedef jack_port_t * (JACKPORT)(jack_client_t *, const char *, const char *,	unsigned long, unsigned long);
typedef void (JACKSETPROCESS)(jack_client_t *, JackProcessCallback, void *);
typedef jack_default_audio_sample_t * (JACKGETBUFFER)(jack_port_t *, jack_nframes_t);
typedef const char * (JACKPORTNAME)(jack_port_t *);
typedef const char ** (JACKGETPORTS)(jack_client_t *, const char *, const char *, unsigned long);
typedef int (JACKACTIVATE)(jack_client_t *);
typedef int (JACKCONNECT)(jack_client_t *, const char *, const char *);
typedef jack_nframes_t (JACKBUFSIZE)(jack_client_t *);


static jack_client_t *	JackClient;
//static jack_port_t *		JackInPort;
static jack_port_t *		JackOut1Port;
static jack_port_t *		JackOut2Port;
static JACKGETBUFFER *	JackGetBufPtr;

static int jackProcessFunc(jack_nframes_t nframes, void * arg)
{
	MixBufferPtr[0] = (int32_t *)JackGetBufPtr(JackOut1Port, nframes);
	MixBufferPtr[1] = (int32_t *)JackGetBufPtr(JackOut2Port, nframes);
	clear_mix_buf(nframes);
	/* if (AudioThreadFlags & 0x01) */ mixPlayingVoices(nframes);
	return 0;
}

static void shutdown_jack(void)
{
	system("jack_control stop");
}

static void unload_libjack(void)
{
	if (SoundDev[DEVNUM_AUDIOOUT].Handle && !SoundDev[DEVNUM_AUDIOOUT].DevHash)
	{
		if (JackClient)
		{
			register JACKCLOSECLIENT *		ptr;

			ptr = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_client_close");
			ptr(JackClient);
			JackClient = 0;
		}
		dlclose(SoundDev[DEVNUM_AUDIOOUT].Handle);
		SoundDev[DEVNUM_AUDIOOUT].Handle = 0;


		// Wait for this bloated mess of an api to shutdown. It takes a horrifically long time
		// for it to close its alsa handles. So if you try to open the same alsa device too
		// soon after closing jack, you'll get an EBUSY. This thing is slow, inefficient, and
		// incohesive.
		shutdown_jack();
//		GuiErrShow(GuiApp, "Waiting for JACK to shutdown. It's horrifically slow.", GUIBTN_ESC_SHOW|GUIBTN_TIMEOUT_SHOW|GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
	}
}

const char * open_libjack(void)
{
	register const char *	msg;

	system("jack_control start");

	if (!(SoundDev[DEVNUM_AUDIOOUT].Handle = dlopen("libjack.so.0", RTLD_LAZY)))
	{
err:	msg = "Can't get JACK running";
out:	return msg;
	}

	{
	jack_status_t status;
	register JACKOPENCLIENT *		ptr;

	if (!(ptr = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_client_open")))
	{
		unload_libjack();
		goto err;
	}
	if (!(JackClient = ptr(WindowTitle, JackNullOption, &status)))
	{
		msg = "jack_client_open() failed";
bad:	unload_libjack();
		goto out;
	}
	if (status & JackServerFailed)
	{
		msg = "Unable to connect to JACK server";
		goto bad;
	}
	}

	{
	register JACKSAMPLERATE *		ptr;
	register uint32_t					rate;

	if ((ptr = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_get_sample_rate")))
	{
		register unsigned char	i;

		rate = ptr(JackClient);
		for (i=0; i < 4; i++)
		{
			if (rate == Rates[i])
			{
				setSampleRateFactor(i);
				goto good;
			}
		}

		unload_libjack();
		sprintf((char *)GuiBuffer, "JACK sample rate of %u isn't supported", rate);
		msg = (char *)GuiBuffer;
		goto bad;
	}
	}

good:
	return 0;
}

static const char * load_libjack(void)
{
	register const char *	msg;

	if (!JackClient && (msg = open_libjack())) goto out;

	{
	register JACKSETPROCESS *		ptr;

	ptr = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_set_process_callback");
	ptr(JackClient, jackProcessFunc, 0);
	}

	{
	register JACKPORT *		ptr;

	ptr = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_port_register");
	JackGetBufPtr = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_port_get_buffer");
//		JackInPort = ptr(JackClient, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	JackOut1Port = ptr(JackClient, "out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	JackOut2Port = ptr(JackClient, "out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	if (!JackOut1Port || !JackOut2Port)
out:	return msg;
	}

#ifndef NO_REVERB_SUPPORT
	// We need a stereo float mixing buffer for the reverb and intermediate mix
	{
	register JACKBUFSIZE *		ptr;
	register unsigned int	size;

	ptr = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_get_buffer_size");
	size = ptr(JackClient);
	if (!(MixBuffPtr = (char *)malloc(size * 2 * sizeof(float) * 2)))
	{
		msg = &NoMemStr[0];
		goto out;
	}
	ReverbBuffPtr = MixBuffPtr + (size * 2 * sizeof(float));
	}
#endif
	{
	register JACKACTIVATE *		ptr;

	ptr = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_activate");

#ifndef NO_ALSA_AUDIO_SUPPORT
	NonInterleaveFlag = 1;
#endif
#ifndef NO_REVERB_SUPPORT
	setupReverb();
#endif

	if (ptr(JackClient))
	{
		msg = "Can't activate JACK client";
		goto out;
	}
	}

	// Connect to stereo audio out hardware
	{
	register const char **	ports;
	{
	register JACKGETPORTS *		ptr;

	ptr = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_get_ports");
	ports = ptr(JackClient, NULL, NULL, JackPortIsPhysical|JackPortIsInput);
	}

	if (ports)
	{
		register JACKCONNECT *		ptr;
		register JACKPORTNAME *		ptr2;

		ptr = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_connect");
		ptr2 = dlsym(SoundDev[DEVNUM_AUDIOOUT].Handle, "jack_port_name");
		ptr(JackClient, ptr2(JackOut1Port), ports[0]);
		ptr(JackClient, ptr2(JackOut2Port), ports[1]);
		free(ports);
	}
	}

	return 0;
}
#endif














static void unloadZones(register PLAYZONE_INFO *);

void clear_banksel(void)
{
	register unsigned char	i;

	i = PLAYER_SOLO * 2;
	do
	{
		BankNums[--i] = 0x80;
	} while (i);
}

static void unloadMusician(register unsigned char roboNum, register unsigned char fullFlag)
{
	// If the Human Soloist, he shares the Pad and gtr lists. Don't double-free
	if (roboNum != PLAYER_SOLO)
	{
		register INS_INFO *		temp;

		if ((temp = InstrumentLists[roboNum]))
		{
			register INS_INFO *		next;

			if (fullFlag && temp == InstrumentLists[PLAYER_SOLO])
				InstrumentLists[PLAYER_SOLO] = CurrentInstrument[PLAYER_SOLO] = PrevInstrument[PLAYER_SOLO] = 0;
			do
			{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
				unloadZones(temp->Zones);
				unloadZones(temp->ReleaseZones);
				temp->Zones = temp->ReleaseZones = 0;
#endif
				next = temp->Next;
				if (fullFlag) free(temp);
			} while ((temp = next));
		}
	}

	if (fullFlag)
	{
		InstrumentLists[roboNum] = 0;
		NumOfInstruments[roboNum] = 0;
	}

	WavesLoadedFlag &= ~(0x04 << roboNum);
}

static void initInstrumentLists(void)
{
	WavesLoadedFlag = 0;

	memset(&NumOfInstruments[0], 0, sizeof(NumOfInstruments));
	memset(InstrumentLists, 0, sizeof(InstrumentLists));
	memset(CurrentInstrument, 0, sizeof(CurrentInstrument));
}

/********************** unloadAllInstruments() *********************
 * Unloads all instruments (kits/bass/etc) and clears pointers.
 */

static void unloadAllInstruments(void)
{
	register uint32_t		i;

	i = PLAYER_SOLO + 1;
	do
	{
		unloadMusician(--i, 1);
	} while (i);

	initInstrumentLists();
}

/************************ loadDataSets() *********************
 * Called by main thread to kick off a thread to load waves,
 * songs, styles, etc.
 */

static void finishWaveLoad(void);

void loadDataSets(register unsigned char loadFlags)
{
	register char *			str;
	register unsigned char	i;

	headingCopyTo(&TxtExtension[4], 0);
	WhatToLoadFlag = loadFlags;

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

	if (loadFlags & LOADFLAG_INSTRUMENTS)
	{
		// See if we need any waveforms loaded/unloaded. We'll do the latter right now. Load
		// thread will do the former
		i = 0;
		str = (char *)TempBuffer;
		do
		{
			// If this robot is off, make sure its instruments are fully unloaded
			if (!DevAssigns[i])
				unloadMusician(i, 1);
			else
			{
				// If this robot is set to a midi out, then make sure its INS_INFOs are
				// loaded so that he can apply the instrument name, pgm/bank #s, and
				// transpose, split to external synths. But don't load the zones (waves)
				if (DevAssigns[i] != &SoundDev[DEVNUM_AUDIOOUT])
				{
					if (InstrumentLists[i])
						unloadMusician(i, 0);
					else
						WhatToLoadFlag |= (0x10 << i);
				}
				else
				{
					// This robot is set to the internal synth. If user has disabled internal synth,
					// this is error. Otherwise make sure the robot's waves are loaded. If
					// not, mark it for load
					if (SoundDev[DEVNUM_AUDIOOUT].DevFlags & DEVFLAG_DEVTYPE_MASK)
					{
						if (!(WavesLoadedFlag & (0x04 << i)))
						{
							if (i != PLAYER_SOLO) WhatToLoadFlag |= (0x10 << i);
							unloadMusician(i, 1);
						}
					}
					else
					{
						unloadMusician(i, 1);
						if (str == (char *)TempBuffer)
							strcpy(str, "Warning: You disabled the Internal Synth. But ");
						else
							strcpy(str, ", ");
						str += strlen(str);
						strcpy(str, getMusicianName(i));
						str += strlen(str);
						DevAssigns[i] = 0;
					}
				}
			}
		} while (++i <= PLAYER_SOLO);

		if (str != (char *)TempBuffer)
		{
			strcpy(str, " are assigned to it. I'll turn these musician(s) off.");
			show_msgbox((char *)TempBuffer);
		}

		// If no waves loaded yet, note the sample rate we're going to use
		if (!(WavesLoadedFlag & 0xFC)) WavesLoadedFlag = SampleRateFactor;

		// Otherwise, if the sample rate is wrong, we need to dump everything and reload
		else if ((WavesLoadedFlag & 0x03) != SampleRateFactor)
			unloadAllInstruments();
	}
#endif

	if (WhatToLoadFlag)
	{
		WhatToLoadFlag |= LOADFLAG_INPROGRESS;

		// Load data files in secondary (Load) thread
		if (!startLoadThread())
		{
			doLoadScreen();
			do
			{
				do_msg_loop();
				if (GuiLoop == GUIBTN_QUIT)
				{
					abort_load();
					clearMainWindow();
				}
			} while (serviceLoadThread());

			// Do final load processing for waveforms
			if (loadFlags & LOADFLAG_INSTRUMENTS) finishWaveLoad();

			// Now that the sounds are loaded, go through the style/instrument names to
			// determine the minimum width of a selection box
			calcMinBox(loadFlags & (LOADFLAG_STYLES|LOADFLAG_INSTRUMENTS));
		}
	}
}






// ========================= Instrument Loading ========================

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

unsigned char setSampleRateFactor(register unsigned char rate)
{
	if (rate < 4)
	{
		SampleRateFactor = rate;
		DecayRate = Rates[rate]/(Rates[rate] >> 1);
	}
	return SampleRateFactor;
}

/********************** unloadZones() *********************
 * Unloads the PLAYZONEs/WAVEFORMs files for specified zone
 * in the linked list.
 */

static void unloadZones(register PLAYZONE_INFO * zones)
{
	register PLAYZONE_INFO *			tempZone;

	if ((tempZone = zones))
	{
		do
		{
			register WAVEFORM_INFO **	waveInfoTable;
			register unsigned char		rangeCount;

			// Free all the WAVEFORM_INFOs
			rangeCount = tempZone->RangeCount;
			waveInfoTable = (WAVEFORM_INFO **)((char *)tempZone + sizeof(PLAYZONE_INFO));
			while (rangeCount--)
			{
				register WAVEFORM_INFO *	waveInfo;

				while ((waveInfo = *waveInfoTable))
				{
					*waveInfoTable = waveInfo->Next;
					free(waveInfo);
				}

				waveInfoTable += 2;
			}

			// Free the PLAYZONE_INFO
			tempZone = tempZone->Next;
			free(zones);

			// Next PLAYZONE_INFO
		} while ((zones = tempZone));
	}
}



static void initVoices(void)
{
	register uint32_t			total;
	register VOICE_INFO *	mem;

	AudioThreadQueue = VoicePlayQueue = 0;
	VoicePlayLock = 0;

	if ((mem = VoiceLists[0]))
	{
		total = Polyphony[PLAYER_DRUMS] + Polyphony[PLAYER_BASS] + Polyphony[PLAYER_GTR] + Polyphony[PLAYER_PAD] + Polyphony[PLAYER_SOLO];
		do
		{
			mem->Lock = mem->AudioFuncFlags = 0;
			mem->NoteNum = 0x80;
			mem++;
		} while (--total);

		VoiceListSolo = VoiceLists[PLAYER_SOLO];
	}
}

static void freeVoices(void)
{
	if (VoiceLists[0]) free(VoiceLists[0]);
	VoiceLists[0] = 0;
	initVoices();
}

static void * allocVoices(void)
{
	register VOICE_INFO *	mem;
	register uint32_t			total;

	if ((mem = VoiceLists[0])) goto init;

	total = Polyphony[PLAYER_DRUMS] + Polyphony[PLAYER_BASS] + Polyphony[PLAYER_GTR] + Polyphony[PLAYER_PAD] + Polyphony[PLAYER_SOLO];

	if ((mem = malloc(total * sizeof(VOICE_INFO))))
	{
init:	total = 0;
		goto loop;
		do
		{
			register VOICE_INFO *	temp;

			temp = mem;
			mem += Polyphony[total];
			do
			{
				memset(temp, 0, sizeof(VOICE_INFO));
				temp->Musician = total;
			} while (++temp < mem);
			total++;
loop:		VoiceLists[total] = mem;
		} while (total < 5);
	}
	return mem;
}



#pragma pack(1)
typedef struct {
	uint32_t					WaveformLen;	// Size of data in 16-bit samples -- not frames nor bytes
	uint32_t					CompressPoint;	// Sample offset to 8-bit bytes
	uint32_t					LoopBegin;		// Sample offset to loop start. If LoopBegin >= WaveformLen, then no loop
	uint32_t					LoopEnd;			// Sample offset to loop end
	unsigned char			WaveFlags;
} CMPWAVEFILE;
#pragma pack()

/************************ waveLoad() ********************
 * Reads in a compressed WAVE file, and stores the info in
 * a WAVEFORM_INFO.
 *
 * fn =					Filename to load.
 * waveInfoTable =	Ptr to prev WAVEFORM_INFO in the list.
 *
 * If an error, copies a nul-terminated msg to TempBuffer[].
 */

static void waveLoad(char * fn, WAVEFORM_INFO ** waveInfoTable)
{
	register WAVEFORM_INFO *waveInfo;
	register const char *	message;
	unsigned long				size;
	CMPWAVEFILE					drum;
	register int				inHandle;

	message = &DidNotOpen[0];

	// Open the WAVE file for reading
	if ((inHandle = open(fn, O_RDONLY|O_NOATIME)) != -1)
	{
		message = " is a bad WAVE file";

		// Read in File header
		if (read(inHandle, &drum, sizeof(CMPWAVEFILE)) == sizeof(CMPWAVEFILE))
		{
			struct stat			buf;

			if (drum.WaveformLen >= drum.CompressPoint && (drum.LoopBegin == (uint32_t)-1 || drum.LoopBegin < drum.WaveformLen) && (drum.LoopEnd == (uint32_t)-1 || drum.LoopEnd > drum.LoopBegin))
			{
				// Allocate a buffer to load in the wave data, and load it
				fstat(inHandle, &buf);
				size = buf.st_size - sizeof(CMPWAVEFILE);
				if (!(waveInfo = (WAVEFORM_INFO *)malloc((SampleRateFactor > 1 ? size : 0) + size + sizeof(WAVEFORM_INFO) - 1)))
				{
					setMemErrorStr();
					close(inHandle);
					goto badout;
				}

				// Make sure all waves are the same rate. We don't bother with on-the-fly
				// rate conversion. User is expected to use the same rate for all waves
				if ((SampleRateFactor > 1 ? SampleRateFactor - 2 : SampleRateFactor) != (drum.WaveFlags >> 4))
				{
					message = " is not the correct sample rate";
					goto end;
				}

				// Link it into the list
				memset(waveInfo, 0, sizeof(WAVEFORM_INFO));
				waveInfo->Next = *waveInfoTable;
				*waveInfoTable = waveInfo;
				waveInfo->WaveformLen = drum.WaveformLen;
				waveInfo->CompressPoint = drum.CompressPoint;
				waveInfo->LoopBegin = drum.LoopBegin;
				waveInfo->LoopEnd = drum.LoopEnd;
				waveInfo->WaveFlags = drum.WaveFlags & WAVEFLAG_STEREO;
//printf("%s Len=%u Comp=%u Begin=%u End=%u %s\r\n", fn, drum.WaveformLen << 1, drum.CompressPoint << 1,
//drum.LoopBegin==(uint32_t)-1?0:drum.LoopBegin<<1, drum.LoopEnd==(uint32_t)-1?0:drum.LoopEnd<<1, waveInfo->WaveFlags ? "Stereo" : "");

				if (read(inHandle, &waveInfo->WaveForm[(SampleRateFactor > 1 ? size : 0)], size) == size)
				{
					register short *	to;
					register short *	from;
					register short		pt;

					if (SampleRateFactor > 1)
					{
						buf.st_size <<= 1;
						from = (short *)(&waveInfo->WaveForm[size]);
						to = (short *)(&waveInfo->WaveForm[0]);
						while ((char *)from < &waveInfo->WaveForm[buf.st_size])
						{
							pt = *from++;
							*to++ = pt;
							*to++ = pt;
						}
						waveInfo->WaveformLen <<= 1;
						waveInfo->CompressPoint <<= 1;
						waveInfo->LoopBegin <<= 1;
						waveInfo->LoopEnd <<= 1;
					}
					message = 0;
				}
			}
		}
end:
		close(inHandle);
	}

  	if (message)
	{
		// Error
		register char *temp;
		register char *dest;
		register unsigned char	numFileLevels;

		numFileLevels = 3;
		temp = fn + strlen(fn);
		dest = (char *)TempBuffer;
		while (temp > fn)
		{
			if (*(--temp) == '/')
			{
				if (!(--numFileLevels))
				{
					++temp;
					break;
				}
			}
		}

		while ((*(dest)++ = *(temp)++));
		--dest;
		while ((*(dest)++ = *(message)++));
		setErrorStr((char *)TempBuffer);
	}
badout:
	return;
}



static unsigned char	NtnNams[] = {'C',0,'D','b','D',0,'E','b','E',0,'F',0,'G','b','G',0,'A','b','A',0,'B','b','B',0,'C',0};
static unsigned char	NtnVals[] = {9,11,0,2,4,5,7};

static void numToPitchFn(register char * ptr, register unsigned char noteNum)
{
	register unsigned char *	name;

	*(ptr)++ = ('0' - 1) + (noteNum/12);
	*(ptr)++ = '_';
	name = &NtnNams[(noteNum % 12) << 1];
	*(ptr)++ = *name++;
	*(ptr)++ = *name;
	*ptr = 0;
}

static unsigned char pitchToNum(register unsigned char ** note)
{
	register unsigned char  *	ptr;
	register unsigned char		chr;
	register unsigned char		noteNum;

	ptr = *note;
	if ((chr = *ptr) && chr >= '0' && chr <= '9')
	{
		noteNum = (chr - ('0' - 1)) * 12;
		chr = *(++ptr);
		if (chr == '_')
		{
			if ((chr = *(++ptr) & 0x5F) && chr >= 'A' && chr <= 'G')
			{
				if (*(++ptr) == 'b')
				{
					noteNum--;
					ptr++;
				}
				noteNum += NtnVals[(chr - 'A')];
				goto out;
			}
		}
	}
	ptr = 0;
out:
	*note = ptr;
	return noteNum;
}
#endif	// !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)




typedef struct {
	uint32_t					LegatoOffset;
	unsigned char			HighNote;
	unsigned char			RootNote;
	unsigned char			RangeCount;
	unsigned char			Volume;
	unsigned char			PanPosition;
	unsigned char			Groups;
	unsigned char			MuteGroups;
	unsigned char			FadeOut;
	unsigned char			Flags;
	unsigned char			Reverb;
} TEMP_PLAYZONE_INFO;




/**************** parse_common() ******************
 * Parses fields that can appear in both a ZONE
 * line and PATCH HEADER line.
 */

static unsigned char * parse_common(unsigned char * ptr, register TEMP_PLAYZONE_INFO * zone, TEMP_PLAYZONE_INFO * master, register uint32_t op)
{
	switch (op)
	{
		// REL
		case ZONE_ID_REL:
		{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
			zone->Flags &= (ListNum ? ~(PLAYZONEFLAG_ALWAYS_FADE|PLAYZONEFLAG_SUSLOOP) : ~(PLAYZONEFLAG_ALWAYS_FADE));
			if (*ptr == 'F')
			{
				// Always fade
				zone->Flags |= PLAYZONEFLAG_ALWAYS_FADE;
				goto skipsus;
			}
			if (*ptr == 'H')	// Infinite loop
			{
				// Drums do not support hold loop. Neither do release map
				if (!ListNum || PickAttack) goto badval;
				zone->Flags |= PLAYZONEFLAG_SUSLOOP;
skipsus:		++ptr;
			}
			if ((op = asciiToNum(&ptr)) < 4) op = 4;
			if (op > 255 || !ptr) goto badval;
			zone->FadeOut = (unsigned char)op;
#endif
			break;
		}

		// PAN
		case ZONE_ID_PAN:
		{
			register unsigned char	dir;

			dir = 0;
			if (*ptr == 'R') goto skippan;
			if (*ptr == 'L')
			{
				dir = 1;
skippan:		++ptr;
			}
			op = asciiToNum(&ptr);
			if (op > 63 || !ptr) goto badval;
			if (!dir) op = -op;
			zone->PanPosition = (unsigned char)op + 64;
			break;
		}

		// VOL
		case ZONE_ID_VOL:
		{
			register char	flag;

			flag = 0;
			if (master)
			{
				// if a '+' or '-', it gets added to the global vol
				if (*ptr == '+')
				{
					flag = 1;
					goto skipvol;
				}
				if (*ptr == '-')
				{
					flag = -1;
skipvol:			++ptr;
				}
			}
			op = asciiToNum(&ptr);
			if (op > 100 || !ptr) goto badval;
			if (flag == 1) op += master->Volume;
			if (flag == -1) op = master->Volume - op;
			if (op > 100 || (flag && !op)) goto badval;
			zone->Volume = (unsigned char)op;
			break;
		}

		// REV
		case ZONE_ID_REV:
		{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
#ifndef NO_REVERB_SUPPORT
			op = asciiToNum(&ptr);
			if (op > 255 || !ptr) goto badval;
			zone->Reverb = (unsigned char)op;
#endif
#endif
			break;
		}

		// RANGES
		case ZONE_ID_RANGES:
		{
#if defined(NO_ALSA_AUDIO_SUPPORT) && defined(NO_JACK_SUPPORT)
			if (ListNum) break;
#endif
			op = asciiToNum(&ptr);
			if (op > 255 || !ptr)
badval:		ptr = 0;
			else
			{
				zone->RangeCount = (unsigned char)op;
				if (master) master->Flags |= PLAYZONEFLAG_HHCLOSED;
			}
			break;
		}

		// GROUP
		case ZONE_ID_GROUP:
		{
			for (;;)
			{
				op = asciiToNum(&ptr);
				if (!op || op > 8 || !ptr) goto badval;
				zone->Groups |= 0x01 << (op - 1);
				ptr = skip_spaces(ptr);
				if (*ptr != '+') break;
				ptr = skip_spaces(++ptr);
			}

			break;
		}

		// MUTE
		case ZONE_ID_MUTE:
		{
			for (;;)
			{
				op = asciiToNum(&ptr);
				if (!op || op > 8 || !ptr) goto badval;
				zone->MuteGroups |= 0x01 << (op - 1);
				ptr = skip_spaces(ptr);
				if (*ptr != '+') break;
				ptr = skip_spaces(++ptr);
			}

			break;
		}

		// OFFSET
		case ZONE_ID_OFFSET:
		{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
			zone->LegatoOffset = (asciiToNum(&ptr) << 1);
#endif
		}
	}

	return ptr;
}





/********************* loadZones() ********************
 * Loads the zones/waveforms for an Instrument. Called by
 * the Load thread.
 *
 * fn =			Instrument txt file's nul-terminated full pathname.
 * offset =		Byte offset to the end of dir in 'fn'.
 * TempBuffer = Parsing buffer.
 *
 * If an error, copies a msg to TempBuffer[] and alerts
 * Main thread.
 */

static void loadZones(char * fn, uint32_t offset)
{
	register unsigned char *	field;
	unsigned char *				ptr;
	register uint32_t				lineNum, temp;
	TEMP_PLAYZONE_INFO			defZone;
	register PLAYZONE_INFO *	zone;

	zone = LoadedZones = 0;

	// Load the file into TempBuffer, nul-terminated
	if (!load_text_file(fn, 5))
bad:	return;

	ptr = TempBuffer;	// Must reget after load_text_file() or file_text_err()

	// =====================================================
	// Parse PATCH HEADER line

	// Skip comments and blank lines
	lineNum = skip_lines(&ptr) + 1;
	if (!*ptr)
	{
		sprintf((char *)TempBuffer, "%s txt file is missing the header line", NamePtr);
		setErrorStr((char *)TempBuffer);
		goto bad;
	}

	SubKit = TransposeVal = Options = 0;
	BankNums[4*2] = BankNums[(4*2)+1] = 0x80;
	memset(&defZone, 0, sizeof(TEMP_PLAYZONE_INFO));
	defZone.PanPosition = 64;
	defZone.FadeOut = 1;
	defZone.Reverb = 100;

	// Look for pgm #, any LAYER name, octave shift, etc
	do
	{
		unsigned char	id;

		field = ptr;
		id = NUM_OF_HDR_IDS;
		ptr = get_field_id(ptr, &HdrIds[0], &id, 0);
		if (id >= NUM_OF_HDR_IDS)
		{
			if (id == FILEFIELD_END) break;
			if (id == FILEFIELD_UNKNOWN) goto unknown;
			/* if (id == FILEFIELD_BADID) */ goto bad_id;
		}

		if (!*ptr || *ptr == '\n') goto missing;
		switch (id)
		{
			case HDR_ID_LAYER:
			{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
				if (ListNum) goto bad_id;
				field = ptr;
				while (*ptr >= ' ') ++ptr;
				while (ptr > field && *(ptr - 1) <= ' ') --ptr;
				*ptr++ = 0;
				SubKit = hash_string(field);
#endif
				goto donehdr;
			}

			case HDR_ID_PGM:
			{
				temp = asciiToNum(&ptr);
				if (temp > 127 || !ptr) goto badval;
				VolBoost = (unsigned char)temp;
				break;
			}

			case HDR_ID_OCT:
			{
				if (!ListNum) goto bad_id;
				if (*ptr == '+') ++ptr;
				TransposeVal = asciiToNum(&ptr);
				if (TransposeVal < -6 || TransposeVal > 6 || !ptr) goto badval;
				break;
			}

			case HDR_ID_MSB:
			{
				if (!ListNum) goto bad_id;
				temp = asciiToNum(&ptr);
				if (temp > 127 || !ptr) goto badval;
				BankNums[4*2] = (unsigned char)temp;
				break;
			}

			case HDR_ID_LSB:
			{
				if (!ListNum) goto bad_id;
				temp = asciiToNum(&ptr);
				if (temp > 127 || !ptr) goto badval;
				BankNums[(4*2)+1] = (unsigned char)temp;
				break;
			}

			case HDR_ID_OPT:
			{
				field = ptr;
				id = 0x80 | NUM_OF_OPT_IDS;
				ptr = get_field_id(ptr, &OptIds[0], &id, 0);
				if (id >= NUM_OF_OPT_IDS) goto bad_id;
				Options |= (0x01 << id);
				break;
			}

			default:
			{
				if (!(ptr = parse_common(ptr, &defZone, 0, id - HDR_ID_REL))) goto badval;
			}
		}
		ptr = skip_spaces(ptr);
	} while (*ptr && *ptr != '\n');

donehdr:
	// If the robot musician playing this instrument is not outputting to the "Internal Synth" buss, then don't
	// load the zones/waves
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	if (DevAssigns[ListNum] != &SoundDev[DEVNUM_AUDIOOUT])
#endif
	{
		SubKit = Options = 0;
		goto bad;
	}

#if defined(NO_ALSA_AUDIO_SUPPORT) && defined(NO_JACK_SUPPORT)
unknown:
	format_text_err("unknown text %s", lineNum, field);
	goto bad;
missing:
	format_text_err("missing %s value", lineNum, field);
	goto bad;
bad_id:
	format_text_err("unknown id %s", lineNum, field);
	goto bad;
badval:
	format_text_err("bad %s value", lineNum, field);
	goto bad;
#else

	// If the header specified RANGES, see if the next line(s) are range lines containing only
	// a VEL field. If so, all zones are implied these range lines unless the zone contains a
	// RANGES field
	// TODO

	for (;;)
	{
		TEMP_PLAYZONE_INFO			tempZone;
		unsigned char *				ranges;
		register uint32_t				count;

		// Main thread wants us to abort?
		if (!WhatToLoadFlag) goto bad;

		// Find the start of the next zone line. If no more, we're done
		lineNum += skip_lines(&ptr);
		if (!*ptr) break;

		zone = 0;
		defZone.Flags &= (~PLAYZONEFLAG_HHCLOSED);
		memcpy(&tempZone, &defZone, sizeof(TEMP_PLAYZONE_INFO));
		tempZone.RangeCount = 0;

		// =====================================================
		// Parse zone line

		// If the line starts with a number, then it's the MIDI Note #
		if (*ptr >= '0' && *ptr <= '9') goto notenum;

		do
		{
			unsigned char		id;

			// Break off the field id (upto '=')
			field = ptr;
			id = NUM_OF_ZONE_IDS;
			ptr = get_field_id(ptr, &HdrIds[HDR_ID_REL], &id, 0);
			if (id >= NUM_OF_ZONE_IDS)
			{
				if (id == FILEFIELD_END) break;

				if (id == FILEFIELD_UNKNOWN)
				{
unknown:			format_text_err("unknown text %s", lineNum, field);
					goto bad4;
				}

bad_id:		format_text_err("unknown id %s", lineNum, field);
				goto bad4;
			}

			// Get the field's value
			if (!*ptr || *ptr == '\n')
			{
missing:		format_text_err("missing %s value", lineNum, field);
				goto bad4;
			}

			switch (id)
			{
				case ZONE_ID_NAME:
				{
					field = ptr;

					temp = offset;
					while (*ptr >= ' ' && *ptr != '/' && *(ptr+1) != '/') fn[temp++] = *ptr++;
					while (temp > offset && fn[temp - 1] == ' ') --temp;
					fn[temp] = 0;

					goto donezone;
				}

				case ZONE_ID_NOTE:
				{
notenum:			temp = 0;
					if (*ptr == 'A')
					{
						temp = PLAYZONEFLAG_TOUCH_TRIGGER;
						goto status;
					}
					if (*ptr == 'C')
					{
						temp = PLAYZONEFLAG_CC_TRIGGER;
status:				++ptr;
					}
					tempZone.Flags |= (unsigned char)temp;

					// If the next char after the first number == '_' then the note is
					// specified as a name in the form Octave_NoteName (ie, 4_Db) as
					// opposed to a MIDI note #
					if (*(ptr + 1) == '_')
						temp = pitchToNum(&ptr);
					else
						temp = asciiToNum(&ptr);
					if (!temp || temp > 127 || !ptr) goto badval;
					tempZone.RootNote = (unsigned char)temp;
					while (*ptr == ' ') ptr++;
					if (*ptr < '0' || *ptr > '9') break;
				}

				case ZONE_ID_HIGH:
				{
					if (*(ptr + 1) == '_')
						temp = pitchToNum(&ptr);
					else
						temp = asciiToNum(&ptr);
					if (!temp || temp > 127 || !ptr)
					{
badval:				ptr = field;
						while (*ptr > ' ' && *ptr != '/' && *(ptr+1) != '/') ptr++;
						if (ptr - field > 128) ptr = &field[128];
						ptr[0] = 0;
						format_text_err("bad %s value", lineNum, field);
bad4:					if (zone) unloadZones(zone);
						goto bad;
					}

					tempZone.HighNote = (unsigned char)temp;
					break;
				}

				case ZONE_ID_HH:
				{
					if (ListNum) goto bad_id;
					ranges = ptr;
					while (*ptr > ' ' && *ptr != '/' && *(ptr+1) != '/') *ptr++ &= 0x5F;
					id = *ptr;
					*ptr = 0;
					temp = hash_string(ranges);

					if (temp == 0x36B85BD9)	// HALF
					{
						tempZone.Flags |= PLAYZONEFLAG_HHHALFOPEN;
						goto hihat;
					}
					if (temp == 0x9E48B810)	// OPEN
					{
						tempZone.Flags |= PLAYZONEFLAG_HHOPEN;
						goto hihat;
					}
					if (temp == 0x16861ACA)	// CLOSED
					{
						tempZone.Flags |= PLAYZONEFLAG_HHCLOSED;
						goto hihat;
					}
					if (temp == 0xCE3C7BA0)	// PEDALCLOSE
					{
						tempZone.Flags |= PLAYZONEFLAG_HHPEDALCLOSE;
						goto hihat;
					}
					if (temp == 0x1F50D8B9)	// PEDALOPEN
					{
						tempZone.Flags |= PLAYZONEFLAG_HHPEDALOPEN;
hihat:				*ptr = id;
						break;
					}

					goto badval;
				}

				default:
				{
					if (!(ptr = parse_common(ptr, &tempZone, &defZone, id))) goto badval;
				}
			}

			field = 0;
			ptr = skip_spaces(ptr);
		} while (*ptr && *ptr != '\n');

donezone:
		// If VOL=0, then regard this as simply a MUTE zone with 0 RANGES. After all, it
		// would make no sense to specify waves that play at 0 vol. So ignore any global
		// RANGES count from the header line. Just make sure he didn't explicitly specify
	// non-0 ranges, but did specify MUTE
		count = tempZone.RangeCount;
		if (!tempZone.Volume)
		{
			if (count)
			{
				format_text_err("VOL=0 on a zone with ranges", lineNum, 0);
				goto bad4;
			}

			if (!tempZone.MuteGroups)
			{
				format_text_err("VOL=0 on a zone with no MUTE groups", lineNum, 0);
				goto bad4;
			}
		}

		// If RangeCount=0, then this is a single range zone
		else if (!count)
		{
			if ((defZone.Flags & PLAYZONEFLAG_HHCLOSED) || !(tempZone.RangeCount = count = defZone.RangeCount)) count = 1;
		}

		// Alloc a PLAYZONE_INFO
		if  (!(zone = (PLAYZONE_INFO *)malloc(sizeof(PLAYZONE_INFO) + count + (count * 2 * sizeof(void *)))))
		{
			setMemErrorStr();
			goto bad4;
		}
		memset(zone, 0, sizeof(PLAYZONE_INFO) + count + (count * 2 * sizeof(void *)));

		zone->RangeCount = (unsigned char)count;
		zone->Groups = tempZone.Groups;
		zone->MuteGroups = tempZone.MuteGroups;
		zone->FadeOut = tempZone.FadeOut;
		zone->RootNote = tempZone.RootNote;
		zone->HighNote = tempZone.HighNote;
		zone->Volume = tempZone.Volume;
		zone->PanPosition = tempZone.PanPosition;
		zone->Flags = tempZone.Flags;
		zone->Reverb = tempZone.Reverb;

		// ============================================
		// Load the ranges
		if (count)
		{
			register WAVEFORM_INFO **	waveInfoTable;

			// If no zone NAME was provided, use the RootNote
			if (!field)
			{
				temp = offset;
				if (tempZone.Flags & (PLAYZONEFLAG_TOUCH_TRIGGER|PLAYZONEFLAG_CC_TRIGGER))
					fn[temp++] = (tempZone.Flags & PLAYZONEFLAG_CC_TRIGGER) ? 'C' : 'A';

				// NOTENAMES OPT
				if (Options & 0x04)
					numToPitchFn(&fn[temp], tempZone.RootNote);
				else
					sprintf(&fn[temp], "%u", tempZone.RootNote);
				temp += strlen(&fn[temp]);	// ignore uninitialized warning

				// If this is a release map, append an "_r"
				if (PickAttack)
				{
					fn[temp++] = '_';
					fn[temp++] = 'r';
					fn[temp] = 0;
				}
			}

			waveInfoTable = (WAVEFORM_INFO **)((char *)zone + sizeof(PLAYZONE_INFO));
			ranges = (unsigned char *)(&waveInfoTable[count * 2]);

			// If RANGES wasn't provided, then there's just one implied range of 0-127
			// with a single Wave. There are no range lines. The zone NAME is the filename
			if (!tempZone.RangeCount)
			{
				count = 0;
				*ranges = 127;
				strcpy(&fn[temp], &Extension[0]);	  // Ignore uninitialized warning
				goto loadit;
			}

			// Assume velocity boundaries set to evenly map
			// waves across the full vel range
			{
			unsigned char				dec, vel, spill;

			vel = 127;
			dec = 127 / count;
			spill = 127 % count;
			do
			{
				ranges[--count] = vel;
				vel -= dec;
				if (spill)
				{
					spill--;
					vel--;
				}
			} while (count);
			}

			// Parse next range line
			while (tempZone.RangeCount--)
			{
				register uint32_t		temp2;
				uint32_t					offset2;

				lineNum += skip_lines(&ptr);
				if (!*ptr)
				{
					format_text_err("missing range line", lineNum, 0);
					goto bad4;
				}

				count = 0;
				offset2 = temp;

				// If the line starts with a number, then it's the ROUND # for drums,
				// or VEL for instruments
				if (*ptr >= '0' && *ptr <= '9')
				{
					if (ListNum) goto velnum;
					goto roundnum;
				}

				do
				{
					unsigned char	id;

					field = ptr;
					id = NUM_OF_RANGE_IDS;
					ptr = get_field_id(ptr, &RangeIds[0], &id, 0);
					if (id >= NUM_OF_RANGE_IDS)
					{
						if (id == FILEFIELD_BADID) goto bad_id;
						break;
					}

					if (!*ptr || *ptr == '\n') goto missing;
					switch (id)
					{
						case RANGE_ID_NAME:
						{
							// Append the range name to the zone name
							temp2 = offset2;
							while (*ptr >= ' ' && *ptr != '/' && *(ptr+1) != '/') fn[offset2++] = *ptr++;
							while (offset2 > temp2 && fn[offset2 - 1] == ' ') --offset2;
							goto donerange;
						}
						case RANGE_ID_VEL:
						{
velnum:					temp2 = asciiToNum(&ptr);
							if (!temp2 || temp2 > 127 || !ptr) goto badval;
							*ranges = (unsigned char)temp2;
							break;
						}
						case RANGE_ID_ROUND:
						{
roundnum:				count = asciiToNum(&ptr);
							if (!count || count > 255 || !ptr) goto badval;
							break;
						}
						case RANGE_ID_OFFSET:
						{
							temp2 = asciiToNum(&ptr);
							if (!ptr) goto badval;
							tempZone.LegatoOffset = temp2;
//							break;
						}
					}
					ptr = skip_spaces(ptr);

				} while (*ptr && *ptr != '\n');

donerange:	// If no ROUND count (there's only 1 wave for this velocity range), use the RANGE name verbatim.
				// If no range NAME supplied, append an underscore and range # to the ZONE name. In either case
				// append .cmp extension
				if (!count)
				{
					if (offset2 == temp)
						sprintf(&fn[offset2], "_%u%s", zone->RangeCount - tempZone.RangeCount, &Extension[0]);
					else
						strcpy(&fn[offset2], &Extension[0]);
					goto loadit;
				}

				while (count)
				{
					if (!WhatToLoadFlag) goto bad4;

					// Append ROUND robin # to the RANGE name. If no range NAME supplied, append both range and
					// robin # to the ZONE name, separated by underscores. In either case append .cmp extension
					if (offset2 == temp)
						sprintf(&fn[offset2], "_%u_%u%s", zone->RangeCount - tempZone.RangeCount, count, &Extension[0]);
					else
						sprintf(&fn[offset2], "%u%s", count, &Extension[0]);
					count--;

					// Indicate this robot has waves
loadit:			WavesLoadedFlag |= (0x04 << ListNum);

					// Load the wave
					waveLoad(&fn[0], waveInfoTable);
					if (getErrorStr()) goto bad4;

					// If Instrument txt file didn't specify a legato offset, try to deduce one
					{
					register WAVEFORM_INFO *	waveInfo;
					register uint32_t				i;

					waveInfo = *waveInfoTable;
					// Skip beginning samples (ie, reduce the attack)?
					if (!(i = tempZone.LegatoOffset))
					{
						register short *		ptr;

						i = waveInfo->CompressPoint >> ((waveInfo->WaveFlags & WAVEFLAG_STEREO) ? 5 : 4);
						if (waveInfo->WaveFlags & WAVEFLAG_STEREO) i &= -2;
						ptr = (short *)&waveInfo->WaveForm[0];
						while (i && (ptr[i - 1] > 4000 || ptr[i - 1] < -4000))
skipmore:			i -= ((waveInfo->WaveFlags & WAVEFLAG_STEREO) ? 2 : 1);
						if ((waveInfo->WaveFlags & WAVEFLAG_STEREO) && i && (ptr[i] > 4000 || ptr[i] < -4000)) goto skipmore;
					}
					if (i < waveInfo->WaveformLen) waveInfo->LegatoOffset = i;
					}
				}

				waveInfoTable += 2;
				++ranges;
			}
		}

		// ============================================

		// Link the zone into the list per HighNote
		if (!zone->HighNote)
		{
			if (!ListNum)
			{
				zone->HighNote = zone->RootNote;
				goto sort;
			}
			zone->Next = LoadedZones;
			LoadedZones = zone;
		}
		else
		{
			register PLAYZONE_INFO *	prevZone;

sort:		prevZone = (PLAYZONE_INFO *)&LoadedZones;
			while (prevZone->Next && prevZone->Next->HighNote < zone->HighNote) prevZone = prevZone->Next;
			zone->Next = prevZone->Next;
			prevZone->Next = zone;
		}
	}
#endif
}





/****************** loadInstrument() ********************
 * Loads one Instrument (kit/bass/etc). Called by
 * the Load thread.
 *
 * path =		Nul-terminated full pathname to data dir.
 * offset =		Byte offset to the end of dir in 'path'.
 * TempBuffer = Parsing buffer.
 * NamePtr =	Instrument filename minus txt extension.
 * ListNum =	PLAYER_xxx
 *
 * If an error, copies a msg to TempBuffer[], and sets
 * ErrorStr to it. This alerts the Load thread to signal
 * the Main thread (with SIGNALMAIN_LOAD, as detected in
 * handleClientMsg). The Main thread then calls
 * serviceLoadThread to prompt the user whether to
 * continue load or abort. (Only the main thread does GUI).
 * Finally the Main thread informs the waiting Load thread
 * whether to resume or terminate.
 */

void loadInstrument(char * path, uint32_t offset, unsigned char roboNum)
{
	register uint32_t	len;

	ListNum = roboNum;

	// Append the instrument txt file name
	len = strlen(NamePtr);
	memcpy(&path[offset], NamePtr, len);
	strcpy(&path[offset+len], &TxtExtension[0]);

	// Load the zones/waves
	PickAttack = 0;
	loadZones(&path[0], offset);
	if (getErrorStr()) goto err;

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	if (LoadedZones)
#endif
	{
		register INS_INFO *	patch;
		register INS_INFO *	list;

		// For HIDE option, a blank kit name doesn't display
		if (!ListNum && (Options & INSFLAG_HIDDEN)) len = 0;

		// Alloc a INS_INFO and link it into the list, ordered by pgm #
		if (!(patch = (INS_INFO *)malloc(sizeof(INS_INFO) + len)))
		{
			setMemErrorStr();
err:
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
			unloadZones(LoadedZones);
#endif
			return;
		}

		list = (INS_INFO *)&InstrumentLists[ListNum];
		while (list->Next && list->Next->PgmNum < VolBoost) list = list->Next;
		patch->Next = list->Next;
		list->Next = patch;

		memcpy(patch->Name, NamePtr, len);
		patch->Name[len] = 0;

		patch->ReleaseZones = 0;
		patch->Zones = LoadedZones;

		if (len) NumOfInstruments[ListNum]++;

		if (!ListNum)
		{
			patch->Sub.Kit = 0;
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
			patch->Sub.Hash = SubKit;	// Resolve it later below after all kits loaded
#endif
			patch->PgmNum = VolBoost++;
		}
		else
		{
			patch->PgmNum = VolBoost;

			patch->Sub.Patch.Transpose = TransposeVal * 12;
			patch->Sub.Patch.BankMsb = BankNums[4*2];
			patch->Sub.Patch.BankLsb = BankNums[(4*2)+1];
			patch->Sub.Patch.Flags = Options;

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
			// load release map?
			if ((SoundDev[DEVNUM_AUDIOOUT].DevFlags & DEVFLAG_DEVTYPE_MASK) && (Options & 0x01))
			{
				memcpy(&path[offset], NamePtr, len);
				strcpy(&path[offset+len], ".rel");
				PickAttack = 1;
				loadZones(&path[0], offset);
				if (getErrorStr()) goto err;
				patch->ReleaseZones = LoadedZones;
			}
#endif
		}
	}
}




/********************** finishWaveLoad() *****************
 * Called by Main after the Load thread has finished.
 * Final load processing that doesn't take long, and may
 * require user interaction.
 */

static void finishWaveLoad(void)
{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register INS_INFO *		kit;

	// Resolve LAYER references
	kit = InstrumentLists[PLAYER_DRUMS];
	while (kit)
	{
//		printf("%s %u\r\n",kit->Name,kit->PgmNum);
		if (kit->Sub.Hash)
		{
			register INS_INFO *	temp;

			temp = InstrumentLists[PLAYER_DRUMS];
			do
			{
				if (temp != kit && kit->Sub.Hash == hash_string((unsigned char *)temp->Name)) break;
			} while ((temp = temp->Next));

			// Can't find the referenced kit?
			if (!(kit->Sub.Kit = temp) && !IgnoreFlag)
			{
				sprintf((char *)TempBuffer, "%s kit depends on a kit that isn't found. Click Ok to continue scanning for errors, or Abort to stop.", kit->Name);
				if ((GuiLoop = GuiErrShow(GuiApp, (char *)TempBuffer,
					(AppFlags2 & APPFLAG2_TIMED_ERR) ? GUIBTN_ESC_SHOW|GUIBTN_TIMEOUT_SHOW|GUIBTN_ABORT_SHOW|GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT :
					 GUIBTN_ESC_SHOW|GUIBTN_ABORT_SHOW|GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT)) == GUIBTN_QUIT) return;

				// If user clicked "Abort" or timeout, finish the processing without showing further errors
				IgnoreFlag = (GuiLoop != GUIBTN_OK);
			}
		}

 		kit = kit->Next;
	}
	}

// ======================================

	if (WavesLoadedFlag & ((0x04 << PLAYER_PAD) | (0x04 << PLAYER_GTR)))
		WavesLoadedFlag |= (0x04 << PLAYER_SOLO);

	// If the internal synth is enabled, and any of the robots are assigned to it,
	// but we haven't loaded any waves for those robots, then assume the user
	// hasn't installed instruments
	if ((SoundDev[DEVNUM_AUDIOOUT].DevFlags & DEVFLAG_DEVTYPE_MASK) && !ignoreErrors)
	{
		register unsigned char	i;
		register char *			str;

		str = (char *)TempBuffer;
		for (i = 0; i <= PLAYER_SOLO; i++)
		{
			if (DevAssigns[i] == &SoundDev[DEVNUM_AUDIOOUT] && !(WavesLoadedFlag & (0x04 << i)))
			{
				if (str == (char *)TempBuffer)
					strcpy(str, "Warning:  The ");
				else
					strcpy(str, ", ");
				str += strlen(str);
				strcpy(str, getMusicianName(i));
				str += strlen(str);
			}
		}

		if (str != (char *)TempBuffer)
		{
			strcpy(str, " robot(s) are assigned to the Internal Synth. But they have no sampled instruments. Put some sampled instruments in ");
			getInstrumentPath(GuiBuffer);
			strcat((char *)TempBuffer, GuiBuffer);
			show_msgbox((char *)TempBuffer);
		}
	}
#endif
	// Select first ins in list (robots only)
	{
	register unsigned char i;

	i = 0;
	do
	{
		PrevInstrument[i] = CurrentInstrument[i] = InstrumentLists[i];
	} while (++i < PLAYER_SOLO);
	}

	// Human solo instruments borrow from PLAYER_PAD and PLAYER_GTR,
	// ignoring hidden instruments
	{
	register unsigned char	cnt, flag;
	register INS_INFO *		ins;

	CurrentInstrument[PLAYER_SOLO] = 0;
	cnt = 0;
	flag = PLAYER_PAD;
again:
	if ((ins = InstrumentLists[flag]))
	{
		do
		{
			if (!(ins->Sub.Patch.Flags & INSFLAG_HIDDEN))
			{
				if (!CurrentInstrument[PLAYER_SOLO]) CurrentInstrument[PLAYER_SOLO] = ins;
				cnt++;
			}
		} while ((ins = ins->Next));
	}

	if (flag == PLAYER_PAD)
	{
		flag = PLAYER_GTR;
		goto again;
	}

	PrevInstrument[PLAYER_SOLO] = CurrentInstrument[PLAYER_SOLO];
	NumOfInstruments[PLAYER_SOLO] = cnt;
	VolBoost = 0;
	}

	// cache common pads
	cache_pads();

	// No MIDI bank yet received
	clear_banksel();

	// Set startDrumNote() to report note errs
	Options = 0xFF;

	// Backing pad off
	CurrentInstrument[PLAYER_PAD] = 0;

	// Remember what sample rate the waveforms were loaded as
	WavesLoadedFlag |= SampleRateFactor;
}





void ignoreErrors(void)
{
	Options = 0;
}
































#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

// =========================== Audio setup/playback =============================

static void clear_mix_buf(snd_pcm_uframes_t numFrames)
{
	MixBuffEnd = MixBuffPtr + (numFrames * 2 * sizeof(float));
	memset(MixBuffPtr, 0, MixBuffEnd - MixBuffPtr);
	memset(ReverbBuffPtr, 0, MixBuffEnd - MixBuffPtr);
}

void set_vol_factor(register VOICE_INFO * voiceInfo)
{
	register int32_t		volSum;

	voiceInfo->ClientFlags &= ~VOICEFLAG_VOL_CHANGE;
	if (!(voiceInfo->AudioFuncFlags & AUDIOPLAYFLAG_FINAL_FADE))
	{
		volSum = ((voiceInfo->Velocity +	// Note velocity
					voiceInfo->Zone->Volume + ((VolBoost && voiceInfo->Musician == PLAYER_SOLO) ? 27 : 0) +	// Instrument volume + boost
					VolAdjust[voiceInfo->Musician])			// MIDI volume ctrl
					*  128) / 340;
		if (volSum > 127) volSum = 127;
		voiceInfo->VolumeFactor = VolFactors[volSum] * 40.0f;
/*
		voiceInfo->VolumeFactor = VolFactors[voiceInfo->Velocity] *			// Note velocity
				VolFactors[voiceInfo->Zone->Volume + ((VolBoost && voiceInfo->Musician == PLAYER_SOLO) ? 27 : 0)] *	// Pgm volume + boost
				VolFactors[VolAdjust[voiceInfo->Musician]];					// MIDI volume ctrl
*/
		voiceInfo->AttackLevel = 0.0f;
	}
}

/******************** mixPlayingVoices() *******************
 * Fills the audio card's circular buffer with a mix of all
 * the currently playing waveform data.
 *
 * numFrames =		The number of frames to fill.
 *
 * NOTE: MixBufferPtr[0]/[1] point directly into the card's sound
 * Left/Right chan buffers.
 */

static void mixPlayingVoices(snd_pcm_uframes_t numFrames)
{
	{
	register VOICE_INFO *		voiceInfo;
	register VOICE_INFO *		temp;

	// Grab the VOICEINFOs that the other threads have added
	// to the "play" queue since the previous mixPlayingVoices(),
	// and clear that queue
	voiceInfo = 0;
	if (__atomic_or_fetch(&VoicePlayLock, 0x01, __ATOMIC_RELAXED) == 0x01)
	{
		voiceInfo = VoicePlayQueue;
		VoicePlayQueue = 0;
	}
	__atomic_and_fetch(&VoicePlayLock, ~0x01, __ATOMIC_RELAXED);

	// Prepend them to our private queue that only we access
	if ((temp = voiceInfo))
	{
		while (temp->Next) temp = temp->Next;
		temp->Next = AudioThreadQueue;
		AudioThreadQueue = voiceInfo;
	}
	}

	//========================================
	// Scan the currently playing notes (voices), and mix the waveforms accordingly
	// =======================================
	{
	register VOICE_INFO *		voiceInfo;
	register WAVEFORM_INFO *	waveInfo;
	VOICE_INFO *					queuePtr;

	queuePtr = (VOICE_INFO *)&AudioThreadQueue;
	waveInfo = 0;
again:
	voiceInfo = queuePtr->Next;
	while (voiceInfo)
	{
		// ========================================================
		// Mix the waveform (for this MIDI note #) into the "mix buffer"
		// ========================================================
		register uint32_t			i;
		register uint32_t			transposeFracPos;
		float *						mixBuffPtr;
		uint32_t						loopend, numWavePts;
		float							volumeFactor, s16;
#ifndef NO_REVERB_SUPPORT
		float *						revBuffPtr;
#endif
		// Lock this voice while we mix it into the output buffer. If Lock is already
		// > 1, then another thread wants to steal the voice, so do nothing with it
		if (__atomic_or_fetch(&voiceInfo->Lock, 0x01, __ATOMIC_RELAXED) != 0x01) goto nextVoice;

		// Get the mix buffer
		mixBuffPtr = (float *)MixBuffPtr;
#ifndef NO_REVERB_SUPPORT
		revBuffPtr = (float *)ReverbBuffPtr;
#endif

		// Delay the note? We check this once only on voice start
		if ((i = voiceInfo->AttackDelay))
		{
			// Calc delay
			i *= DecayRate * 2 * 8;

			// Overrun?
			if (i > numFrames)
			{
				voiceInfo->AttackDelay = (i - numFrames) / (8 * 2);
				goto nextVoice;
			}
			voiceInfo->AttackDelay = 0;		// Once only

			// Delay the note by starting at a later point in the buf. Code
			// above makes sure we don't overrun
			mixBuffPtr += (i * 2);
			revBuffPtr += (i * 2);
		}

		// Resume prev interp
		// We apply linear interpolation if we're playing the wave at a different
		// pitch than recorded
		transposeFracPos = voiceInfo->TransposeFracPos;

		// If volume changed, recalculate
		if (!voiceInfo->AttackLevel)
		{
			register unsigned char	flags;

			if ((flags = VoiceChangeFlags))
			{
				register VOICE_INFO *	temp;

				VoiceChangeFlags = 0;
				temp = AudioThreadQueue;
				do
				{
					if (flags & (0x01 << temp->Musician)) set_vol_factor(temp);
				} while ((temp = temp->Next));
			}
			if (voiceInfo->ClientFlags & VOICEFLAG_VOL_CHANGE) set_vol_factor(voiceInfo);
		}

		// Get volume level from the previous call
		volumeFactor = voiceInfo->VolumeFactor;

		// Get the WAVEFORM_INFO for this voice
		waveInfo = voiceInfo->Waveform;

		// Looping?
		loopend = waveInfo->LoopEnd;
		if (waveInfo->LoopBegin == (uint32_t)-1)
		{
			// No loop. loopend=-1 ensures looping checks below are ignored
			loopend = (uint32_t)-1;

			// Non-looped waves are assumed to fade themselves out, so no release env processing
			voiceInfo->AudioFuncFlags |= AUDIOPLAYFLAG_SKIP_RELEASE;
		}

		// Mix this voice
		numWavePts = waveInfo->WaveformLen;
		while ((char *)mixBuffPtr < MixBuffEnd)
		{
			register char *	sampPtr;
			register float		pt;
			uint32_t				i2;
			char *				sampPtr2;

			// Get current read position, using linear interpolation
			voiceInfo->CurrentOffset += transposeFracPos >> UPSAMPLE_BITS;
			i = voiceInfo->CurrentOffset * (waveInfo->WaveFlags ? 2 : 1);
			transposeFracPos = transposeFracPos & (UPSAMPLE_FACTOR - 1);

			// If we're at the end of the loop, wrap back to the loop start
			while (i >= loopend) i -= (loopend - waveInfo->LoopBegin);

			// If the end of the waveform, stop mixing it in upon the next buffer fill. NOTE: Looped waves
			// ignore this, and instead turn off when fade out, or by note-off
			if (i >= numWavePts ||

				// Has the release env fade out? If so, stop playing this voice, and free it for reuse
				(voiceInfo->AttackLevel + volumeFactor) < .09f)
			{
#ifdef JG_NOTE_DEBUG
				printf("Voice %u note %u off\r\n", (voiceInfo - VoiceLists[0]) + 1, voiceInfo->NoteNum & 0x7f);
#endif
				// Remove it from the voice list
				queuePtr->Next = voiceInfo->Next;
				voiceInfo->Next = 0;

				// Drums ignore note-off, so we can clear it now
				if (!voiceInfo->Musician) voiceInfo->NoteNum |= 0x80;

				// Let other threads know this voice is now free
				voiceInfo->AudioFuncFlags = 0;

				// Unlock the voice. This "wakes" any thread sleeping in lockVoice()
				__atomic_and_fetch(&voiceInfo->Lock, ~0x01, __ATOMIC_RELAXED);

				goto again;
			}

			// If we're already doing a fast release, then that's final
			if (!(voiceInfo->AudioFuncFlags & AUDIOPLAYFLAG_FINAL_FADE))
			{
				// Main thread wants a fast release of the voice?
				if (voiceInfo->ClientFlags & VOICEFLAG_FASTRELEASE)
				{
					// No more processing allowed
					voiceInfo->AudioFuncFlags |= AUDIOPLAYFLAG_FINAL_FADE;

					// Give lower notes a slower release. Note: INFINITE loops have a user-specified
					// rate (in the instrument's .txt file) for fast fadeout, so keep zone->FadeOut
					if (!(voiceInfo->ClientFlags & VOICEFLAG_SUSTAIN_INFINITE)) voiceInfo->FadeOut = 4 - ((voiceInfo->NoteNum & 0x7f) / 40);
					goto fade;
				}

				// If there's a loop, and it's not marked infinite sustain, then begin fading out if we're in the loop (ie "release envelope")
				if (!(voiceInfo->AudioFuncFlags & AUDIOPLAYFLAG_SKIP_RELEASE) && !(voiceInfo->ClientFlags & VOICEFLAG_SUSTAIN_INFINITE) && i >= waveInfo->LoopBegin)
				{
					voiceInfo->AudioFuncFlags |= AUDIOPLAYFLAG_SKIP_RELEASE;
					goto fade;
				}
			}

			// Are we in the release phase (ie, decay phase for waves without infinite sustain loop)
			if (voiceInfo->ReleaseTime)
			{
				if (!(--voiceInfo->ReleaseTime))
				{
					if (voiceInfo->AttackLevel)
					{
						volumeFactor *= 1.5f;
//						printf("%f < %f\n", volumeFactor, voiceInfo->AttackLevel);
						if (volumeFactor < voiceInfo->AttackLevel)
							voiceInfo->ReleaseTime = DecayRate * 4 - ((voiceInfo->NoteNum & 0x7f) / 40);
						else
						{
							volumeFactor = voiceInfo->AttackLevel;
							voiceInfo->AttackLevel = 0.0f;
							voiceInfo->AudioFuncFlags &= ~AUDIOPLAYFLAG_SKIP_RELEASE;
						}
					}
					else
					{
						volumeFactor *= .994;

#ifdef JG_NOTE_DEBUG
						printf("voice %u volume %f\r\n", (voiceInfo - VoiceLists[0]) + 1, volumeFactor);
#endif
fade:					voiceInfo->ReleaseTime = DecayRate * (uint32_t)voiceInfo->FadeOut;

					}
				}
			}

			// Get current 16-bit sample for the left chan
			if (i < waveInfo->CompressPoint)
			{
				sampPtr = (char *)waveInfo->WaveForm + (i << 1);
				s16 = *((short *)sampPtr);
			}
			else
			{
				// Expand "compressed" 8-bit to 16-bit
				sampPtr = ((char *)waveInfo->WaveForm) + (i - waveInfo->CompressPoint) + (waveInfo->CompressPoint << 1);
				s16 = *sampPtr;
			}
			sampPtr2 = sampPtr;

			// We need to factor in the next sample pt for linear interpolation, so get that sample
			i2 = i + (waveInfo->WaveFlags ? 2 : 1);
			while (i2 >= loopend) i2 -= (loopend - waveInfo->LoopBegin);
			if (i2 < waveInfo->CompressPoint)
			{
				sampPtr = (char *)waveInfo->WaveForm + (i2 << 1);
				pt = *((short *)sampPtr);
			}
			else
			{
				sampPtr = (char *)waveInfo->WaveForm + (i2 - waveInfo->CompressPoint) + (waveInfo->CompressPoint << 1);
				pt = *sampPtr;
			}

			// Mix the sample into mix-out buffer, applying vol. Also the reverb buf
			pt = (s16 * TransposeTable[transposeFracPos][1]) + (pt * TransposeTable[transposeFracPos][0]);

			{
			float		val;

			val = pt * volumeFactor;
#ifndef NO_REVERB_SUPPORT
			*revBuffPtr++ += (val * voiceInfo->Zone->Reverb) / 255.0f;
#endif
			*mixBuffPtr++ += val;

			// Repeat for the other (right) audio chan. If stereo wave, then we need to get that point
			if (waveInfo->WaveFlags)
			{
				// Note: no need to check for loop wrap -- it can't happen mid-chan. Ditto compress pt
				if (i < waveInfo->CompressPoint)
				{
					sampPtr2 += 2;
					s16 = *((short *)sampPtr2);
				}
				else
					s16 = *(++sampPtr2);
				if (i2 < waveInfo->CompressPoint)
				{
					sampPtr += 2;
					pt = *((short *)sampPtr);
				}
				else
					pt = *(++sampPtr);
				pt = (s16 * TransposeTable[transposeFracPos][1]) + (pt * TransposeTable[transposeFracPos][0]);
				val = pt * volumeFactor;
			}

#ifndef NO_REVERB_SUPPORT
			*revBuffPtr++ += (val * voiceInfo->Zone->Reverb) / 255.0f;
#endif
			*mixBuffPtr++ += val;
			}

			// Update pointer to next sample point, applying linear interpolation
			transposeFracPos += voiceInfo->TransposeIncrement;
		} // Mix this voice

		// Save position we left off (in the waveform), and vol, for next call here
		voiceInfo->TransposeFracPos = transposeFracPos;
		voiceInfo->VolumeFactor = volumeFactor;

nextVoice:
		queuePtr = voiceInfo;
		voiceInfo = queuePtr->Next;

		// Unlock the voice. This "wakes" any thread sleeping in lockVoice()
		__atomic_and_fetch(&queuePtr->Lock, ~0x01, __ATOMIC_RELAXED);
	} // while queuePtr->Next

	// If there are accompaniment chords being held at the end of play, but user has
	// released all notes, mute the chords
	if (!waveInfo && !BeatInPlay && (PlayFlags & PLAYFLAG_CHORDSOUND))
	{
		PlayFlags &= ~PLAYFLAG_CHORDSOUND;
		clearChord(0); // NOTE: When passing 0, the threadId isn't needed
	}
#if 0
	{
	register uint32_t	  i;
	i=0;
	voiceInfo = AudioThreadQueue;
	while (voiceInfo)
	{
		if (!i) printf("voice %u", (voiceInfo - VoiceLists[0]) + 1);
		else printf(", %u", (voiceInfo - VoiceLists[0]) + 1);
		voiceInfo = voiceInfo->Next;
		i++;
	}
	if (i) printf(" on\r\n");
	}
#endif
	}

	{
	register float *	mixBuffPtr;
	register float		mastervol;

	mixBuffPtr = (float *)MixBuffPtr;

#ifndef NO_REVERB_SUPPORT
	// Add reverb
	if (Reverb && !(APPFLAG3_NOREVERB & TempFlags))
		ReverbProcess(Reverb, (float *)ReverbBuffPtr, mixBuffPtr, numFrames);
#endif

	mastervol = VolFactors[MasterVolAdjust] * 2.5f;

	// Apply master vol
#ifndef NO_JACK_SUPPORT
#ifndef NO_ALSA_AUDIO_SUPPORT
	if (!SoundDev[DEVNUM_AUDIOOUT].DevHash)
#endif
	{
		register float *	pMixBuffL;
		register float *	pMixBuffR;

		pMixBuffL = (float *)MixBufferPtr[0];
		pMixBuffR = (float *)MixBufferPtr[1];
		while ((char *)mixBuffPtr < MixBuffEnd)
		{
			*pMixBuffL++ = (*mixBuffPtr++ * mastervol) / (float)INT_MAX;
			*pMixBuffR++ = (*mixBuffPtr++ * mastervol) / (float)INT_MAX;
		}
	}
#ifndef NO_ALSA_AUDIO_SUPPORT
	else
#endif
#endif
#ifndef NO_ALSA_AUDIO_SUPPORT
	{
		register int32_t *		pMixBuffL;
		register int32_t *		pMixBuffR;

		pMixBuffL = (int32_t *)MixBufferPtr[0];
		if (NonInterleaveFlag)
		{
			pMixBuffR = (int32_t *)MixBufferPtr[1];
			while ((char *)mixBuffPtr < MixBuffEnd)
			{
				*pMixBuffL++ = (int32_t)(*mixBuffPtr++ * mastervol);
				*pMixBuffR++ = (int32_t)(*mixBuffPtr++ * mastervol);
			}
		}
		else
		{
			while ((char *)mixBuffPtr < MixBuffEnd)
			{
				*pMixBuffL++ = (int32_t)(*mixBuffPtr++ * mastervol);
				*pMixBuffL++ = (int32_t)(*mixBuffPtr++ * mastervol);
				// Skip over interleaved channels we don't use
				pMixBuffL += (NumChans - 2);
			}
#ifdef TEST_AUDIO_MIX
			runWaveRecord((const char *)MixBufferPtr[0], numFrames * sizeof(int32_t) * 2);
#endif
		}
	}
	}
#endif
}




void set_thread_priority(void)
{
	struct sched_param	params;

	params.sched_priority = SecondaryThreadPriority;
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &params);
}





#ifndef NO_ALSA_AUDIO_SUPPORT

/************* xrun_count() ******************
 * Gets the # of xruns since the last time it
 * was called.
 *
 * reset = -1 for resetting the count. 1 for
 * the updated count. 0 for the total count of
 * xruns.
 */

uint32_t xrun_count(register int32_t reset)
{
	if (reset)
	{
		reset = CurrentXRuns - PreviousXRuns;
		if (reset < 0) CurrentXRuns = 0;
		PreviousXRuns = CurrentXRuns;
		return reset;
	}
	return CurrentXRuns;
}

/********************** audioRecovery() **********************
 * Called whenever we encounter an error in filling the sound
 * card's buffer with more audio data. ALSA only.
 */

static const char	AudioErrStrs[] = "Playback XRUN recovery failed\0\
SUSPEND recovery failed\0\
ALSA playback poll error\0\
ReStart error\0\
MMAP begin error\0\
MMAP commit error\0\
ALSA input poll error\0\
ALSA poll error\0\
Input XRUN recovery failed\0\
You need permission to set thread realtime priority\0\
Can't set thread realtime priority";

void show_audio_error(register unsigned char err)
{
	register const char *	msg;

	if (err)
	{
		msg = AudioErrStrs;
		while (-err) msg += strlen(msg) + 1;
		show_msgbox(msg);
	}
}

static int audioRecovery(register void * signalHandle, register int err, register int restart)
{
	if (!err)
	{
		switch (snd_pcm_state((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle))
		{
			case SND_PCM_STATE_XRUN:
			{
				err = -EPIPE;
				break;
			}

			case SND_PCM_STATE_SUSPENDED:
				err = -ESTRPIPE;

			default:
				break;
		}
	}

	if (err == -EINTR) goto good;

	// Under-run?
	if (err == -EPIPE)
	{
		// Let main thread know there are xruns
		if (CurrentXRuns < 255) CurrentXRuns++;
		else if (PreviousXRuns >= 255) PreviousXRuns--;

		if ((err = snd_pcm_prepare((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle)) >= 0)
		{
			if (restart && (err = snd_pcm_start((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle)) < 0) goto ret;

			// If beat play thread is running, let it signal the gui thread upon
			// the next downbeat. Otherwise, we must signal here. But don't wait...
			// if we fail to report 1 xrun, no big deal
			if (!BeatInPlay) drawGuiCtl(signalHandle, CTLMASK_XRUN, BEATTHREADID);

good:		return 0;
		}
	}

	// Audio suspended?
	else if (err == -ESTRPIPE)
	{
		// Wait until the suspend flag is released
		while ((err = snd_pcm_resume((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle)) == -EAGAIN) sleep(1);
		if (err >= 0 || (err = snd_pcm_prepare((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle)) >= 0) goto good;
	}
ret:
	return err;
}





/********************** audioThread() **********************
 * Our audio thread which handles whenever ALSA signals us the
 * sound card's buffer needs to be filled with more audio data
 * to play, or the card has audio in data.
 */

#define THREAD_WAITING_FOR_IN		0x01
#define THREAD_WAITING_FOR_OUT	0x02
#define THREAD_GOT_IN_XRUN			0x04
#define THREAD_GOT_OUT_XRUN		0x08

static void * audioThread(void * arg)
{
	register snd_pcm_uframes_t		size;
	register int						err;
	register unsigned char			flags;

//	arg = GuiWinSignal(GuiApp, 0, 0);
	arg = (void *)1;

	// Clear xrun errors
	setAudioDevErrNum(0, 0);

	{
	struct sched_param	params;

	// Set the priority to max, to minimize xruns
	params.sched_priority = AudioThreadPriority;
	err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &params);
	if (err) setAudioDevErrNum(arg, ((err == EPERM) ? 9+1 : 10+1));
	}

	// Fill the audio out hardware's buffer (before we start playback) for 1 period
	{
	snd_pcm_uframes_t		frames;

	size = FramesPerPeriod;
	while ((frames = size))
	{
		const snd_pcm_channel_area_t *	buffer;
		snd_pcm_uframes_t						offset;

		// Get how many frames the sound card needs us to update
		while ((err = snd_pcm_avail_update((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle)) < 0)
		{
			if ((err = audioRecovery(arg, err, 0))) goto urun;
		}

		if ((err = snd_pcm_mmap_begin((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle, &buffer, &offset, &frames)) < 0)
		{
			if ((err = audioRecovery(arg, err, 0))) goto pmap;
		}
		else
		{
			// Fill the buffer
			if (frames)
			{
				if (NonInterleaveFlag)
				{
					MixBufferPtr[0] = (int32_t *)(((unsigned char *)buffer[0].addr) + (offset * sizeof(int32_t)));
					MixBufferPtr[1] = (int32_t *)((unsigned char *)buffer[1].addr + (buffer[1].first / 8) + (offset * sizeof(int32_t)));
				}
				else
					MixBufferPtr[0] = (int32_t *)(((unsigned char *)buffer[0].addr) + (offset * sizeof(int32_t) * NumChans));
				clear_mix_buf(frames);
				/* if (AudioThreadFlags & 0x01) */ mixPlayingVoices(frames);
			}

			// Let ALSA access the data
			if ((err = snd_pcm_mmap_commit((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle, offset, frames)) < 0 || (snd_pcm_uframes_t)err != frames)
			{
				if ((err = audioRecovery(arg, err >= 0 ? -EPIPE : err, 0))) goto pcommit;
			}

			size -= frames;
		}
	}
	}

	// Start the playback/input
	if ((err = snd_pcm_start((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle)) < 0)
	{
starterr:
		setAudioDevErrNum(arg, 3+1);
		goto out;
	}

	for (;;)
	{
		flags = SoundDev[DEVNUM_AUDIOIN].Handle ? THREAD_WAITING_FOR_OUT|THREAD_WAITING_FOR_IN : THREAD_WAITING_FOR_OUT;

		do
		{
			register uint32_t			in, out;

			in = out = 0;

			// Tell ALSA to give us its FDs we need to poll on
			snd_pcm_poll_descriptors((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle, DescPtrs, NumPlayDesc);
			out = NumPlayDesc;
			if (SoundDev[DEVNUM_AUDIOIN].Handle)
			{
				snd_pcm_poll_descriptors((snd_pcm_t *)SoundDev[DEVNUM_AUDIOIN].Handle, DescPtrs + out, NumInDesc);
				in = NumInDesc;
			}

			// Wait (go to sleep) until ALSA signals that there is audio input data to read, or the soundcard
			// needs more audio output data. The call to poll() puts us to sleep. We're waiting for ALSA to set
			// one (or both) of ALSA's 2 file descriptors. ALSA sets one of the file descriptors when the
			// soundcard has input (recorded) audio data for us to read. ALSA sets the other file descriptor
			// when the soundcard needs us to give it more output data to play. Ultimately, we wait for both
			// to need servicing before we attend to either, so we can synch in/out
			err = poll(DescPtrs, in + out, -1);

			// We woke because the card has input and/or needs output. Or alsa could be trying to inform us
			// of an xrun with input or output. Or someone may be trying to terminate us. Or maybe something
			// went wrong with poll. Let's figure out which scenario.

			// Main thread want us to terminate?
			if (!AudioThreadFlags) goto out;

			// Someone trying to terminate us, or poll err?
			if (err < 0)
			{
				setAudioDevErrNum(arg, 7+1);
				goto out;
			}

			// If still waiting for out, see if it's ready
			if (out)
			{
				unsigned short		revents;

				if (snd_pcm_poll_descriptors_revents((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle, DescPtrs, out, &revents) < 0)
				{
					setAudioDevErrNum(arg, 2+1);
					goto out;
				}

				if (revents & POLLERR)
					flags |= THREAD_GOT_OUT_XRUN;

				if (revents & POLLOUT)
				{
					flags &= ~THREAD_WAITING_FOR_OUT;
		//			printf("playback needs data\r\n");
				}
			}

			// Do the same for in
			if (in)
			{
				unsigned short		revents;

				if (snd_pcm_poll_descriptors_revents((snd_pcm_t *)SoundDev[DEVNUM_AUDIOIN].Handle, DescPtrs + out, in, &revents) < 0)
				{
					setAudioDevErrNum(arg, 6+1);
					goto out;
				}

				if (revents & POLLERR)
					flags |= THREAD_GOT_IN_XRUN;

				if (revents & POLLIN)
				{
					flags &= ~THREAD_WAITING_FOR_IN;
		//			printf("input data arrived\r\n");
				}
			}

			// Keep waiting until both in and out need servicing
		} while ((flags & (THREAD_WAITING_FOR_OUT|THREAD_WAITING_FOR_IN)));

		// =========== Get # of input/output frames that are ready ==========
		{
		register snd_pcm_uframes_t		inputFrames;

		inputFrames = 0;
		if ((snd_pcm_t *)SoundDev[DEVNUM_AUDIOIN].Handle)
		{
			if ((inputFrames = snd_pcm_avail_update((snd_pcm_t *)SoundDev[DEVNUM_AUDIOIN].Handle)) < 0)
			{
				if (inputFrames == -EPIPE) flags |= THREAD_GOT_IN_XRUN;
				inputFrames = 0;
			}

			if ((flags & THREAD_GOT_IN_XRUN) && audioRecovery(arg, 0, 0))
			{
				setAudioDevErrNum(arg, 8+1);
				goto out;
			}
		}

		if ((size = snd_pcm_avail_update((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle)) < 0)
		{
			if (size == -EPIPE) flags |= THREAD_GOT_OUT_XRUN;
			size = 0;
		}

		if (size > FramesPerPeriod) size = FramesPerPeriod;

		if ((flags & THREAD_GOT_OUT_XRUN) && audioRecovery(arg, 0, 0))
		{
urun:		setAudioDevErrNum(arg, 0+1);
			goto out;
		}

		// =========== Get audio input ==========
		if (inputFrames)
		{
			snd_pcm_uframes_t					frames;
			register snd_pcm_uframes_t		count;

			InputIndex = 0;

			// Use the lower amount for both in and out so they're in sync
			if (inputFrames > FramesPerPeriod) inputFrames = FramesPerPeriod;
			if (inputFrames < size)
				size = inputFrames;
			else
				inputFrames = size;

			count = inputFrames;
			while ((frames = count))
			{
				const snd_pcm_channel_area_t *	buffer;
				snd_pcm_uframes_t						offset;

				// Get the pointer to the audio hardware's buffer where we need to read WAVE data,
				// and the amount of bytes to read. NOTE: If the buffer wraps, then the amount
				// of bytes to read may be less than a full block (in which case "frames" will be
				// less than "count")
				if ((err = snd_pcm_mmap_begin((snd_pcm_t *)SoundDev[DEVNUM_AUDIOIN].Handle, &buffer, &offset, &frames)) < 0)
				{
					if (audioRecovery(arg, err, 1) < 0) goto pmap;
				}
				else
				{
					if (frames > count) frames = count;

					// Read the audio hardware's buffer. "buffer" = address of the
					// interleaved buffers. Note: "offset" is in sample frames
//					readAudioData(buffer, offset, frames);

					// Done accessing the buffer
					if ((err = snd_pcm_mmap_commit((snd_pcm_t *)SoundDev[DEVNUM_AUDIOIN].Handle, offset, frames)) < 0 || (snd_pcm_uframes_t)err != frames)
					{
						if (audioRecovery(arg, err >= 0 ? -EPIPE : err, 1) < 0) goto pcommit;
					}
					else

						// Dec count of frames we still need to read to complete the block
						count -= frames;
				}
			}
		}

		// =========== Send audio output ==========

		// Fill the audio buffer with a block of WAVE data
		{
		snd_pcm_uframes_t		frames;

		while ((frames = size))
		{
			const snd_pcm_channel_area_t *	buffer;
			snd_pcm_uframes_t						offset;

			// Get the pointer to the audio hardware's buffer where we need to copy WAVE data,
			// and the amount of bytes to copy. NOTE: If the buffer wraps, then the amount
			// of bytes to copy may be less than a full block (in which case "frames" will be
			// less than "size")
			if ((err = snd_pcm_mmap_begin((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle, &buffer, &offset, &frames)) < 0)
			{
				if (audioRecovery(arg, err, 1) < 0)
				{
pmap:				setAudioDevErrNum(arg, 4+1);
					goto out;
				}

				continue;
			}

			// Audio card buffer is full?
			if (!frames) break;

			if (frames > size) frames = size;

			// Create the mix of playing voices in the audio hardware's buffer. Pass the address of the
			// interleaved buffers. Note: "offset" is in sample frames
			if (NonInterleaveFlag)
			{
				MixBufferPtr[0] = (int32_t *)(((unsigned char *)buffer[0].addr) + (offset * sizeof(int32_t)));
				MixBufferPtr[1] = (int32_t *)((unsigned char *)buffer[1].addr + (buffer[1].first / 8) + (offset * sizeof(int32_t)));
			}
			else
				MixBufferPtr[0] = (int32_t *)(((unsigned char *)buffer[0].addr) + (offset * sizeof(int32_t) * NumChans));
#if 0
			if (inputFrames)
			{
				// Copy mic in buffer to output buffer
				copy_input_buf(buffer, offset, frames);
				InputIndex += frames;
			}
#endif
			if (frames /* && (AudioThreadFlags & 0x01) */)
			{
				clear_mix_buf(frames);
				mixPlayingVoices(frames);
			}

			// Commit the data
			if ((err = snd_pcm_mmap_commit((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle, offset, frames)) < 0 || (snd_pcm_uframes_t)err != frames)
			{
				if (audioRecovery(arg, err >= 0 ? -EPIPE : err, 1) < 0)
				{
pcommit:			setAudioDevErrNum(arg, 5+1);
					goto out;
				}
			}

			// Dec count of frames we still need to write to complete the block
			size -= frames;

//			flags &= ~THREAD_GOT_OUT_XRUN;
		}
		}
		}

		if ((flags & THREAD_GOT_OUT_XRUN) && snd_pcm_start((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle) < 0) goto starterr;
	}

out:
	AudioThreadHandle = 0;

//	GuiWinSignal(GuiApp, arg, 0);

	return 0;
}





/********************** audio_On() **********************
 * Starts an audio thread that scans for playing "notes"
 * that trigger audio waveforms, and mixes down those
 * notes' waveforms to the audio card output. Also mixes
 * audio in to out, adding reverb. ALSA only.
 *
 * RETURN: Error msg if fail, 0 otherwise.
 */

static const char * audio_On(void)
{
	register const char *	msg;

	snd_pcm_prepare((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle);

	// Ask ALSA how many FDs it will give us when we call snd_pcm_poll_descriptors(). We must supply
	// an array alsa fills in
	NumPlayDesc = snd_pcm_poll_descriptors_count((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle);
	NumInDesc = SoundDev[DEVNUM_AUDIOIN].Handle ? snd_pcm_poll_descriptors_count((snd_pcm_t *)SoundDev[DEVNUM_AUDIOIN].Handle) : 0;
	if (!(DescPtrs = (struct pollfd *)malloc(sizeof(struct pollfd) * (NumPlayDesc + NumInDesc + 1))))
	{
		msg = &NoMemStr[0];
		goto out;
	}

	// Indicate in play
	AudioThreadFlags = 0x81;

	// Start our audio thread
	if (pthread_create(&AudioThreadHandle, 0, audioThread, 0))
	{
		msg = "Can't start audio thread";
		AudioThreadHandle = 0;
	}
	else
	{
		pthread_detach(AudioThreadHandle);
		msg = 0;
	}
out:
	return msg;
}





/*
unsigned char isAudioOn(void)
{
	return AudioThreadFlags;
}

unsigned char pauseAudio(register unsigned char pause)
{
	return AudioThreadFlags = pause ? 0x80 : 0x81;
}
*/





/********************** audio_Off() **********************
 * Stops the audio thread. ALSA only.
 */

static void audio_Off(void)
{
	if (AudioThreadHandle)
	{
		char	byt;

		// Let audio thread know we want it to terminate
		AudioThreadFlags = 0;

		// Force a wakeup if poll'ing
		snd_pcm_drop((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle);
		write(DescPtrs->fd, &byt, 1);

		// Wait for thread to end
		while (AudioThreadHandle) sleep(1);
//		pthread_cancel(AudioThreadHandle);
	}
}

#endif // NO_ALSA_AUDIO_SUPPORT
#endif // !NO_ALSA_AUDIO_SUPPORT || !NO_JACK_SUPPORT





/******************** initAudioVars() *******************
 * Initializes global vars. Called at program start, and
 * also when an audio card is closed in prep of opening
 * a different card. Not safe to call while audio thread
 * is running.
 */

static void resetAudioVars(void)
{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	VoiceChangeFlags = WavesLoadedFlag = 0;
	MixBuffPtr = 0;
#ifndef NO_ALSA_AUDIO_SUPPORT
	DescPtrs = 0;
	NonInterleaveFlag = 0;
	xrun_count(-1);
#endif
#ifndef NO_JACK_SUPPORT
	JackClient = 0;
#endif
#endif
}

void initAudioVars(void)
{
	AudioThreadPriority = sched_get_priority_max(SCHED_FIFO);
	SecondaryThreadPriority = sched_get_priority_min(SCHED_FIFO);

	resetAudioVars();
	initInstrumentLists();
	clear_banksel();
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	memset(LastDrumNotes, 0, sizeof(LastDrumNotes));
	memset(PadNotes, 0, sizeof(PadNotes));
	memset(GtrNotes, 0, sizeof(GtrNotes));
	CurrentGtrPgm = 0x80;
#endif
	{
	register uint32_t	i;

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	// ============== Linear interpolation
	for (i = 0; i < UPSAMPLE_FACTOR; i++)
	{
		TransposeTable[i][0] = (float)i / (float)UPSAMPLE_FACTOR;
		TransposeTable[i][1] = 1.0f - TransposeTable[i][0];
	}
/*
	for (i = 0; i < 128; i++)
	{
		 VolFactors[i] *= 24.0;
		 if (!(i % 10)) printf("\n");
		 printf("%ff, ", (float)VolFactors[i]);
	}
printf("\n");
*/
	// Internal synth's polyphonic voices
	VoiceLists[0] = 0;
	initVoices();
#endif

	// Set Playback devs to defaults
	i = 7-1;
	do
	{
		memset(&SoundDev[i], 0, sizeof(SoundDev));
#if !defined(NO_SEQ_SUPPORT) && !defined(NO_MIDI_OUT_SUPPORT)
		SoundDev[i].DevFlags = DEVFLAG_MIDITYPE|DEVFLAG_SOFTMIDI|DEVFLAG_NODEVICE|DEVTYPE_NONE;
#elif defined(NO_MIDI_OUT_SUPPORT)
		SoundDev[i].DevFlags = DEVFLAG_SOFTMIDI|DEVFLAG_NODEVICE|DEVTYPE_NONE;
#elif defined(NO_SEQ_SUPPORT)
		SoundDev[i].DevFlags = DEVFLAG_MIDITYPE|DEVFLAG_NODEVICE|DEVTYPE_NONE;
#endif
	} while (--i);
	SoundDev[DEVNUM_AUDIOIN].DevFlags = DEVFLAG_INPUTDEV|DEVFLAG_AUDIOTYPE|DEVFLAG_NODEVICE|DEVTYPE_NONE;

#if !defined(NO_JACK_SUPPORT) && !defined(NO_ALSA_AUDIO_SUPPORT)
	SoundDev[DEVNUM_AUDIOOUT].DevFlags = DEVFLAG_AUDIOTYPE|DEVFLAG_JACK|DEVFLAG_NODEVICE|DEVTYPE_NONE;
#elif defined(NO_ALSA_AUDIO_SUPPORT)
	SoundDev[DEVNUM_AUDIOOUT].DevFlags = DEVFLAG_JACK|DEVFLAG_NODEVICE|DEVTYPE_NONE;
#elif defined(NO_JACK_SUPPORT)
	SoundDev[DEVNUM_AUDIOOUT].DevFlags = DEVFLAG_AUDIOTYPE|DEVFLAG_NODEVICE|DEVTYPE_NONE;
#endif

#ifndef NO_SEQ_IN_SUPPORT
	SoundDev[DEVNUM_MIDIIN].DevFlags = DEVFLAG_INPUTDEV|DEVFLAG_MIDITYPE|DEVFLAG_SOFTMIDI|DEVFLAG_NODEVICE|DEVTYPE_NONE;
#else
	SoundDev[DEVNUM_MIDIIN].DevFlags = DEVFLAG_INPUTDEV|DEVFLAG_MIDITYPE|DEVFLAG_NODEVICE|DEVTYPE_NONE;
#endif
	}
}





/********************** freeAudio() *********************
 * Frees resources allocated by allocAudio().
 *
 * unload = Unload waveforms/reverb if 1, or 0 if not.
 *
 * Called by main thread only.
 */

void freeAudio(register unsigned char unload)
{
#ifndef NO_ALSA_AUDIO_SUPPORT
	// This is the Internal Synth buss. Make sure play is
	// stopped so the beat thread isn't accessing any globals we
	// change here
	if (BeatInPlay) PlayFlags |= (PLAYFLAG_FINAL_PTN|PLAYFLAG_STOP);

	// Terminate internal audio thread too, if running
	audio_Off();

	// Close mic input
	if (SoundDev[DEVNUM_AUDIOIN].Handle) snd_pcm_close((snd_pcm_t *)SoundDev[DEVNUM_AUDIOIN].Handle);
#endif

	// Close audio out dev
	if (SoundDev[DEVNUM_AUDIOOUT].Handle)
	{
#ifndef NO_JACK_SUPPORT
		if (!SoundDev[DEVNUM_AUDIOOUT].DevHash)
			unload_libjack();
#ifndef NO_ALSA_AUDIO_SUPPORT
		else
#endif
#endif
#ifndef NO_ALSA_AUDIO_SUPPORT
		{
			drawGuiCtl(0, CTLMASK_XRUN, 0);
			snd_pcm_close((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle);
		}
#endif
	}
	SoundDev[DEVNUM_AUDIOOUT].Handle = SoundDev[DEVNUM_AUDIOIN].Handle = 0;

#ifndef NO_ALSA_AUDIO_SUPPORT
	// Free poll() array
	if (DescPtrs) free(DescPtrs);
#endif

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	// Free the reverb/input buffer if alloc'ed
	if (MixBuffPtr) free(MixBuffPtr);
#endif
	if (unload)
	{
#ifndef NO_REVERB_SUPPORT
		// Unload reverb
		if (Reverb) ReverbFree(Reverb);
		Reverb = 0;
#endif

		// Free wave mem
		unloadAllInstruments();

		// Free VOICE_INFOs
		freeVoices();
	}

	// Clear variables
	resetAudioVars();

#ifdef TEST_AUDIO_MIX
	stopWaveRecord();
	freeWaveRecord();
#endif
}






#ifndef NO_REVERB_SUPPORT
void setReverb(register GUICTL * ctl, register uint32_t type)
{
	unsigned int	param[1];

	param[0] = ctl->Attrib.Value;
	ReverbSetParams(Reverb, type, &param[0]);
}

static void setupReverb(void)
{
	unsigned int	param[1];

	// Set the reverb's sample rate
	param[0] = Rates[SampleRateFactor];
	ReverbSetParams(Reverb, REVPARAM_RATEMASK, &param[0]);
}

uint32_t getReverb(register uint32_t type)
{
	unsigned int	param[1];

	ReverbGetParams(Reverb, type, &param[0]);
	return param[0];
}
#endif



#ifndef NO_ALSA_AUDIO_SUPPORT

/******************** setupAudioHardware() ********************
 * Sets up ALSA audio in or out.
 */

static uint32_t get_period_size(register unsigned char);

static const char * setupAudioHardware(register snd_pcm_t * audioHandle)
{
	register const char *		msg;
	snd_pcm_uframes_t				buffer_size, period_size;

	{
	snd_pcm_hw_params_t *		hw_params;

//	snd_pcm_nonblock(audioHandle, 0);

	// Get a snd_pcm_hw_params_t struct and fill it in with current hardware settings
	if (snd_pcm_hw_params_malloc(&hw_params) < 0)
	{
		msg = "Can't get audio hardware struct";
bad1:	return msg;
	}

	if (snd_pcm_hw_params_any(audioHandle, hw_params) < 0)
	{
		msg = "Can't init audio hardware struct";
bad2:	snd_pcm_hw_params_free(hw_params);
		goto bad1;
	}

	// Set SAMPLE RATE (KHz) playback
	if (snd_pcm_hw_params_set_rate(audioHandle, hw_params, Rates[SampleRateFactor], 0) < 0)
	{
		msg = "Can't set sample rate";
		goto bad2;
	}

	if (audioHandle == (snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle)
	{
		// Set interleaved data mode, and direct access to the soundcard's buffer
		if (snd_pcm_hw_params_set_access(audioHandle, hw_params, SND_PCM_ACCESS_MMAP_INTERLEAVED) < 0)
		{
			// Try non-interleaved, but still direct access
			if (snd_pcm_hw_params_set_access(audioHandle, hw_params, SND_PCM_ACCESS_MMAP_NONINTERLEAVED) < 0)
			{
				msg = "Can't set MMAP access";
				goto bad2;
			}

			NonInterleaveFlag = 1;
		}

		// Set stereo
		NumChans = 2;
		if (snd_pcm_hw_params_set_channels(audioHandle, hw_params, 2) < 0)
		{
			unsigned int		val;

			// Some cards require more than 2 channels to be serviced. For example,
			// the M-Audio Audiophile 24/96 uses the ICE1714 chip which has
			// 8 24-bit audio out channels (but only 2 are actually implemented on
			// this card) and 2 digital audio outs, for a total of 10
			// output channels. The ALSA driver requires all 10 channels to
			// be "fed" when outputting directly to the hardware, using 24-bit
			// data samples (aligned to 32-bit samples)
			if (snd_pcm_hw_params_get_channels_min(hw_params, &val) < 0 || val < 2 || val > 255 || snd_pcm_hw_params_set_channels(audioHandle, hw_params, val) < 0)
			{
				msg = "Can't set channels";
				goto bad2;
			}

			NumChans = (unsigned char)val;
		}

		// Set 32-bit
		if (snd_pcm_hw_params_set_format(audioHandle, hw_params, SND_PCM_FORMAT_S32_LE) < 0)
		{
			msg = "Can't set 32-bit";
			goto bad2;
		}

#ifndef NO_REVERB_SUPPORT
		setupReverb();
#endif
		// Set hardware buffer/period size
		period_size = get_period_size(NumChans);
		buffer_size = period_size * 2;
		//printf("period_size=%u, Buffer=%u\n", period_size, buffer_size);
		snd_pcm_hw_params_set_buffer_size_near(audioHandle, hw_params, &buffer_size);
		snd_pcm_hw_params_set_period_size_near(audioHandle, hw_params, &period_size, 0);

		// Get the # of frames in a "block" we "mix"
		FramesPerPeriod = period_size / (NumChans * sizeof(int32_t));

		// We need a stereo float mixing buffer for the reverb/input, and one for the output mix
		if (!(MixBuffPtr = (char *)malloc(FramesPerPeriod * 2 * sizeof(float) * 2)))
		{
			msg = &NoMemStr[0];
			goto bad2;
		}
		ReverbBuffPtr = MixBuffPtr + (FramesPerPeriod * 2 * sizeof(float));
	}

	// Input setup
	else
	{
		// Set mono
		NumInChans = 1;
		if ((AppFlags2 & APPFLAG2_STEREO_IN) || snd_pcm_hw_params_set_channels(audioHandle, hw_params, 1) < 0)
		{
			// Use stereo
			if (snd_pcm_hw_params_set_channels(audioHandle, hw_params, 2) < 0)
			{
				msg = "Can't set microphone channels";
				goto bad2;
			}

			NumInChans = 2;

			// If more than 1 chan, we need to set this the same as output
			if (snd_pcm_hw_params_set_access(audioHandle, hw_params, NonInterleaveFlag ? SND_PCM_ACCESS_MMAP_NONINTERLEAVED : SND_PCM_ACCESS_MMAP_INTERLEAVED) < 0)
			{
same:			msg = "Can't set audio input the same as audio out";
				goto bad2;
			}
		}

		if (snd_pcm_hw_params_set_format(audioHandle, hw_params, SND_PCM_FORMAT_S32_LE) < 0) goto same;

		// Set hardware buffer/period size
		get_period_size(NumInChans);
		buffer_size = period_size * 2;
		//printf("period_size=%u, Buffer=%u\n", period_size, buffer_size);
		snd_pcm_hw_params_set_buffer_size_near(audioHandle, hw_params, &buffer_size);
		snd_pcm_hw_params_set_period_size_near(audioHandle, hw_params, &period_size, 0);

		{
		register uint32_t		framesPerPeriod;

		// Get the # of frames in a "block" we input
		framesPerPeriod = period_size / (NumInChans * sizeof(int32_t));
		if (framesPerPeriod != FramesPerPeriod) goto same;
		}
	}

	if (snd_pcm_hw_params(audioHandle, hw_params) < 0)
	{
		msg = "Can't set hardware params";
		goto bad2;
	}

	snd_pcm_hw_params_free(hw_params);
	}

	// ===================== Set software parameters =====================
	{
	snd_pcm_sw_params_t *	sw_params;

	if (snd_pcm_sw_params_malloc(&sw_params) < 0)
	{
		msg = "Can't get audio software struct";
		goto bad1;
	}

	if (snd_pcm_sw_params_current(audioHandle, sw_params) < 0)
	{
		msg = "Can't init audio software struct";
bad3:	snd_pcm_sw_params_free(sw_params);
		goto bad1;
	}

	// Tell ALSA to wake us up whenever period_size or more frames of playback data can be written
	if (snd_pcm_sw_params_set_avail_min(audioHandle, sw_params, period_size) < 0)
	{
		msg = "Can't set audio block size";
		goto bad3;
	}

	// Tell ALSA that we'll start the device ourselves
	if (snd_pcm_sw_params_set_start_threshold(audioHandle, sw_params, 0) < 0)
	{
		msg = "Can't set start mode";
		goto bad3;
	}

	// Tell ALSA not to stop the device on underrun
	if (snd_pcm_sw_params_set_stop_threshold(audioHandle, sw_params, buffer_size) < 0)
	{
		msg = "Can't set stop mode";
		goto bad3;
	}

	if (snd_pcm_sw_params(audioHandle, sw_params) < 0)
	{
		msg = "Can't set software params";
		goto bad3;
	}

	snd_pcm_sw_params_free(sw_params);
	}

	return 0;
}

static uint32_t get_period_size(register unsigned char numChans)
{
	return sizeof(int32_t) * numChans * (12 + (setFrameSize(0) << 2));
}

/********************** openAudioIn() **********************
 * Opens and initializes audio in hardware, allocates 32-bit
 * input buffer.
 */

static const char * openAudioIn(void)
{
	// An audio card is selected?
	if (SoundDev[DEVNUM_AUDIOIN].Card != -1 && (SoundDev[DEVNUM_AUDIOIN].DevFlags & DEVFLAG_DEVTYPE_MASK))
	{
		register const char *	msg;
		register int				err;

		// Open audio hardware
		sprintf(GuiBuffer, &CardSpec[0], SoundDev[DEVNUM_AUDIOIN].Card, SoundDev[DEVNUM_AUDIOIN].Dev, SoundDev[DEVNUM_AUDIOIN].SubDev);
		if ((err = snd_pcm_open((snd_pcm_t **)&SoundDev[DEVNUM_AUDIOIN].Handle, GuiBuffer, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0)
		{
			switch (err)
			{
				case -EBUSY:
					msg = "Audio in card is already in use";
					break;
				default:
					msg = "Can't open audio in card";
			}
		}

		// Setup hardware
		else msg = setupAudioHardware((snd_pcm_t *)SoundDev[DEVNUM_AUDIOIN].Handle);

		return msg;
	}

	return 0;
}

#endif



#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

/********************** allocAudio() **********************
 * Opens and initializes audio in/out hardware, allocates
 * 32-bit buffers if needed, and init reverb.
 * Also starts our audio thread.
 *
 * NOTE: Displays any error msg.
 *
 * Called by main thread.
 */

unsigned char allocAudio(void)
{
	register const char *		msg;

	// An ALSA audio card (or JACK) selected?
	if (SoundDev[DEVNUM_AUDIOOUT].Card != -1 && (SoundDev[DEVNUM_AUDIOOUT].DevFlags & DEVFLAG_DEVTYPE_MASK))
	{
		register int	err;

#ifndef NO_REVERB_SUPPORT
		// Alloc struct for reverb if not already done
		msg = "Can't open reverb";
		if (!Reverb && !(Reverb = ReverbAlloc())) goto out;
#endif
		// Allocate voices if not done
		msg = &NoMemStr[0];
		if (!allocVoices()) goto out;
		initVoices();

#ifndef NO_ALSA_AUDIO_SUPPORT
		if (SoundDev[DEVNUM_AUDIOOUT].DevHash)
		{
			GuiLoop = GUIBTN_OK;
			// Open audio hardware
retry_in:
			sprintf(GuiBuffer, &CardSpec[0], SoundDev[DEVNUM_AUDIOOUT].Card, SoundDev[DEVNUM_AUDIOOUT].Dev, SoundDev[DEVNUM_AUDIOOUT].SubDev);
			if ((err = snd_pcm_open((snd_pcm_t **)&SoundDev[DEVNUM_AUDIOOUT].Handle, GuiBuffer, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0)
			{
				switch (err)
				{
					case -EBUSY:
					{
						if (GuiLoop == GUIBTN_OK)
						{
							shutdown_jack();
							GuiLoop = GUIBTN_CANCEL;
							goto retry_in;
						}
						if (GuiLoop == GUIBTN_CANCEL)
						{
							GuiLoop = GuiErrShow(GuiApp, "Audio playback card is already in use. Make sure JACK isn't running, then click retry.",
								(AppFlags2 & APPFLAG2_TIMED_ERR) ? GUIBTN_ESC_SHOW|GUIBTN_TIMEOUT_SHOW|GUIBTN_OK_SHOW|GUIBTN_RETRY_SHOW|GUIBTN_OK_DEFAULT :
								 GUIBTN_ESC_SHOW|GUIBTN_OK_SHOW|GUIBTN_RETRY_SHOW|GUIBTN_OK_DEFAULT);
							if (GuiLoop == GUIBTN_RETRY) goto retry_in;
							return 1;
						}
					}
					default:
						msg = "Can't open audio playback card";
				}

				goto out;
			}

			// Setup audio out
			if ((msg = setupAudioHardware((snd_pcm_t *)SoundDev[DEVNUM_AUDIOOUT].Handle)) ||

				// Open audio in, if chosen
				(msg = openAudioIn()) ||

				// Start audio thread
				(msg = audio_On()))
			{
#ifndef NO_JACK_SUPPORT
out2:
#endif
				freeAudio(0);
out:			show_msgbox(msg);
				return 1;
			}
		}
#endif
#ifndef NO_JACK_SUPPORT
#ifndef NO_ALSA_AUDIO_SUPPORT
		else
#endif
		// Use JACK
		if ((msg = load_libjack()))
#ifndef NO_ALSA_AUDIO_SUPPORT
			goto out2;
#else
		{
			freeAudio(0);
out:		show_msgbox(msg);
			return 1;
		}
#endif
#endif
	}

#if !defined(NO_ALSA_AUDIO_SUPPORT)
	xrun_count(-1);
#endif
	return 0;
}

#endif	// !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)





#if 0
/********************* stopDrumNote() ********************
 * Stops the drumm waveform assigned to the specified note
 * number from playing. (ie, Turns off the voice playing
 * the waveform mapped to this MIDI note number).
 */

void stopDrumNote(unsigned char noteNum)
{
	register VOICE_INFO *		voiceInfo;
	register VOICE_INFO *		match;

	if ((voiceInfo = VoiceLists[0]))
	{
		match = 0;
		do
		{
			if (voiceInfo->NoteNum == noteNum && !(voiceInfo->AudioFuncFlags & AUDIOPLAYFLAG_FINAL_FADE) && (!match || match->TriggerTime > voiceInfo->TriggerTime)) match = voiceInfo;
			++voiceInfo;
		} while (voiceInfo < VoiceLists[PLAYER_DRUMS + 1]);

		// NOTE: If we can't find a VOICE_INFO playing this note, the voice may have been stolen
		if (match

			// If no infinite sustain loop, then just let it fade out
		//	&& (match->ClientFlags & VOICEFLAG_SUSTAIN_INFINITE)
		)
		{
			match->ClientFlags |= VOICEFLAG_FASTRELEASE;
			voiceInfo->NoteNum |= 0x80;
		}
	}
	// Didn't find a wave assigned to this note number
}
#endif





#if 0
unsigned char countOnNotes(void)
{
	register VOICE_INFO *	voiceInfo;
	register VOICE_INFO *	end;
	register unsigned char	count;

	voiceInfo = VoiceLists[0];
	end = voiceInfo + (Polyphony[PLAYER_DRUMS] + Polyphony[PLAYER_BASS] + Polyphony[PLAYER_GTR] + Polyphony[PLAYER_PAD]);
	count = 0;
	do
	{
		if (voiceInfo->NoteNum) ++count;
		++voiceInfo;
	} while (voiceInfo < end);

	return count;
}
#endif

#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
static void stopAllDrumNotes(register unsigned char threadId)
{
	if (DevAssigns[PLAYER_DRUMS] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		register unsigned char *	ptr;
		unsigned char					msg[4];

		msg[0] = 0x90|MidiChans[PLAYER_DRUMS];
		msg[2] = 0;
		ptr = &LastDrumNotes[0];
		do
		{
			if ((msg[1] = *ptr))
				sendMidiOut(DevAssigns[PLAYER_DRUMS], msg, 3|threadId);
			*ptr = 0;
		} while (++ptr < &LastDrumNotes[8]);
	}
}
#endif





/*********************** startDrumNote() *********************
 * Starts playback of the drum waveform assigned to the
 * specified note number.
 *
 * trigger =	0 for note, PLAYZONEFLAG_TOUCH_TRIGGER for
 *					aftertouch, or PLAYZONEFLAG_CC_TRIGGER for cc.
 * noteNum =	MIDI note/cc #, or 0 for hihat pedal (CC4).
 * velocity =	MIDI note vel, or cc value.
 *
 * RETURN: Non-zero note number if it's not mapped to a
 * wave.
 *
 * NOTE: threadId OR'ed with 0x01 if attack reduction
 * desired. Not applicable to HH pedal, nor external
 * MIDI playback device.
 *
 * threadId OR'ed with PLAYZONEFLAG_TOUCH_TRIGGER for
 *	aftertouch, or PLAYZONEFLAG_CC_TRIGGER for cc.
 */

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
static void setupVoice(register PLAYZONE_INFO *, register VOICE_INFO *, unsigned char, unsigned char);
#endif

unsigned char startDrumNote(unsigned char trigger, unsigned char noteNum, unsigned char velocity, unsigned char threadId)
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	// If drums are assigned to a MIDI bus, output a MIDI msg
	if (DevAssigns[PLAYER_DRUMS] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		register unsigned char *	msgptr;
		unsigned char					msg[6];

		msg[0] = msg[3] = 0x90|MidiChans[PLAYER_DRUMS];
		msgptr = &msg[1];
		LastDrumNotes[8] = (LastDrumNotes[8] + 1) & 0x07;
		if (LastDrumNotes[LastDrumNotes[8]])
		{
			msgptr[0] = LastDrumNotes[LastDrumNotes[8]];
			msgptr[1] = 0;
			msgptr += 3;
		}

		LastDrumNotes[LastDrumNotes[8]] = *msgptr++ = noteNum;
		*msgptr++ = velocity;
		sendMidiOut(DevAssigns[PLAYER_DRUMS], msg, (unsigned char)(msgptr - msg)|threadId);
		goto out;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	if (DevAssigns[PLAYER_DRUMS] && VoiceLists[PLAYER_DRUMS])
	{
		register PLAYZONE_INFO *	zone;
		register VOICE_INFO *		end;
		register WAVEFORM_INFO *	waveInfo;

		{
		register INS_INFO *			kit;
		PLAYZONE_INFO *				potential;

		// A kit must be loaded/selected
		if (!(kit = CurrentInstrument[PLAYER_DRUMS])) goto out;

		end = VoiceLists[PLAYER_DRUMS+1];

		// A note # = 0 means that this a hihat pedal event
		if (!noteNum)
		{
			register unsigned char 	prevHH;

			// Update HHvalue, but keep prev value for analysis below
			prevHH = HHvalue;
			if (AppFlags2 & APPFLAG2_POLARITY) velocity = 127 - velocity;
			HHvalue = velocity;

			// If pedal is transitioning from closed to open, then we need to
			// trigger a "PEDALOPEN" or "OPEN".
			// If pedal is transitioning from open to closed, then we need to
			// trigger a "PEDALCLOSE" or "CLOSED", and silence all playing
			// "PEDALOPEN" and "OPEN".
			// If no transition, ignore this
			if (prevHH)
			{
				// There's already an open hh via prev HH evt.

				// PEDAL OPEN EVT (vel>0): Look for any currently playing open.
				// If already fade out, then he's moving non-vibrating cymbals,
				// which make no sound. Otherwise, inc its vol if the new pedal
				// event is > velocity. (ie, Try to model pedal opening)
				if (velocity)
				{
					register VOICE_INFO *		voiceInfo;

					voiceInfo = VoiceLists[PLAYER_DRUMS];
					do
					{
						if (voiceInfo->AudioFuncFlags == AUDIOPLAYFLAG_QUEUED && (voiceInfo->Zone->Flags & (PLAYZONEFLAG_HHOPEN|PLAYZONEFLAG_HHPEDALOPEN)))
						{
							if (voiceInfo->Velocity + 10 < velocity)
							{
								voiceInfo->Velocity = velocity;
								voiceInfo->ClientFlags |= VOICEFLAG_VOL_CHANGE;
							}

							break;
						}
					} while (++voiceInfo < end);

					goto out;
				}

				// PEDAL CLOSE EVT (vel=0): Use "velocity" of prev open pedal, and
				// look for PLAYZONEFLAG_HHPEDALCLOSE (or PLAYZONEFLAG_HHCLOSED)
				// sound to play
				velocity = prevHH;
				trigger = PLAYZONEFLAG_HHCLOSED|PLAYZONEFLAG_HHPEDALCLOSE;
			}
			else
			{
				// Pedal was already closed on prev HH evt.

				// If new evt is another close, ignore
				if (!velocity) goto out;

				// PEDAL OPEN EVT: look for PLAYZONEFLAG_HHPEDALOPEN (or PLAYZONEFLAG_HHOPEN)
				// sound to play
				trigger = PLAYZONEFLAG_HHOPEN|PLAYZONEFLAG_HHPEDALOPEN;
			}
		}

		// Find the waveform assigned to this note #
		waveInfo = 0;

		potential = 0;
		while (kit && (zone = kit->Zones))
		{
			// hihat pedal event?
			if (!noteNum)
			{
				do
				{
					if (zone->Flags & trigger)
					{
						// Is it specifically a hh pedal sound (ie PEDALOPEN instead of just
						// OPEN). If so, we found the pedal sound we need. Otherwise, we'll
						// use this sound only if we can't find that pedal sound
						if (zone->Flags & (PLAYZONEFLAG_HHPEDALOPEN|PLAYZONEFLAG_HHPEDALCLOSE)) goto gotHH2;
						if (!potential) potential = zone;
					}
				} while ((zone = zone->Next));
			}
			else do
			{
				// Is this zone triggered by the MIDI note/ctl #
				if (zone->RootNote == noteNum && (zone->Flags & (PLAYZONEFLAG_TOUCH_TRIGGER|PLAYZONEFLAG_CC_TRIGGER)) == trigger)
				{
					// Yes. Now we need to look for the matching velocity/ctl range

					// If this is a closed HH, change to
					// open HH if pedal is "open" (HHvalue not 0)
					if (HHvalue && (zone->Flags & PLAYZONEFLAG_HHCLOSED))
					{
						register unsigned char 	flag;

						// If pedal is open just slightly, look for a half-open sound instead of full open
						flag = (HHvalue < 40 ? PLAYZONEFLAG_HHHALFOPEN : PLAYZONEFLAG_HHOPEN);
againHH:				zone = kit->Zones;
						do
						{
							if (zone->Flags & flag)
							{
gotHH2:						noteNum = zone->RootNote;
								goto gotHH;
							}
						} while ((zone = zone->Next));

						// If no half-open sound, use full open
						if (flag == PLAYZONEFLAG_HHHALFOPEN)
						{
							flag = PLAYZONEFLAG_HHOPEN;
							goto againHH;
						}

						goto nextkit;
					}

					{
					register uint32_t		i;

gotHH:			if ((i = zone->RangeCount))
					{
						register unsigned char *	ranges;

						ranges = (unsigned char *)((char *)zone + sizeof(PLAYZONE_INFO) + (i * sizeof(void *) * 2));
						for (i=0; i < zone->RangeCount; i++)
						{
							if (velocity <= ranges[i])
							{
								register WAVEFORM_INFO **	waveInfoTable;

								// Check WaveQueue[] to get next round-robin wave
								waveInfoTable = (WAVEFORM_INFO **)((char *)zone + sizeof(PLAYZONE_INFO) + (i * sizeof(void *) * 2));
								if (!(waveInfo = waveInfoTable[1]))
								{
									// Must have cycled through all the waves, so move back to the head of the list
									if (!(waveInfo = waveInfoTable[0])) goto got_it;
								}

								waveInfoTable[1] = waveInfo->Next;
								goto got_it;
							}
						}
					}
					}

					goto got_it;
				}

				// if this is possible substitute sound, just make note of it for now
				else if (!potential && noteNum <= zone->HighNote && noteNum >= zone->RootNote) potential = zone;

				// Check next zone
			} while ((zone = zone->Next));
nextkit:
			kit = kit->Sub.Kit;
		}

		// We didn't find an exact match. But did we find a suitable substitute sound?
		if ((zone = potential))
		{
			// Yes, use it
			if (!noteNum) noteNum = zone->RootNote;
			goto gotHH;
		}
		}

		// Didn't find a wave assigned to this note number
		return Options & noteNum;

got_it:
		{
		register VOICE_INFO *	freeVoiceInfo;
		unsigned char				freeFlag;

		freeVoiceInfo = 0;

		// ===================================
		// If a CLOSED or PEDALCLOSE hihat, make sure that we cutoff any OPEN, HALF, and PEDALOPEN
		// ===================================
		if (zone->Flags & (PLAYZONEFLAG_HHCLOSED|PLAYZONEFLAG_HHPEDALCLOSE | PLAYZONEFLAG_HHHALFOPEN|PLAYZONEFLAG_HHOPEN))
		{
			register VOICE_INFO *		voiceInfo;

			voiceInfo = VoiceLists[0];
			do
			{
				// Playing?
				if (voiceInfo->AudioFuncFlags)
				{
					// Not already marked for fast release?
					if (!(voiceInfo->ClientFlags & VOICEFLAG_FASTRELEASE) &&
						(voiceInfo->Zone->Flags & (PLAYZONEFLAG_HHHALFOPEN|PLAYZONEFLAG_HHOPEN|PLAYZONEFLAG_HHPEDALOPEN)))
					{
						// Do a fast release
						voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
						voiceInfo->NoteNum |= 0x80;
					}
				}
				else
					// This is a free voice, so we can eliminate the search below
					freeVoiceInfo = voiceInfo;

			} while (++voiceInfo < end);
		}

		// ===================================
		// If a MUTE group, make sure that we cutoff any sound in those groups
		// ===================================
		else if (zone->MuteGroups)
		{
			register VOICE_INFO *		voiceInfo;

			voiceInfo = VoiceLists[0];
			do
			{
				if (voiceInfo->AudioFuncFlags)
				{
					if ((voiceInfo->Zone->Groups & zone->MuteGroups) && !(voiceInfo->ClientFlags & VOICEFLAG_FASTRELEASE))
					{
						// If this is exclusively a MUTE zone, then use the REL value and fade at that rate
						if (!zone->RangeCount)
						{
							voiceInfo->AudioFuncFlags |= AUDIOPLAYFLAG_SKIP_RELEASE;
							voiceInfo->FadeOut = zone->FadeOut;
							voiceInfo->ReleaseTime = DecayRate * (uint32_t)voiceInfo->FadeOut;
						}
						// Otherwise do a fast fade
						voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
						voiceInfo->NoteNum |= 0x80;
					}
				}
				else
					// This is a free voice, so we can eliminate the search below
					freeVoiceInfo = voiceInfo;

			} while (++voiceInfo < end);
		}

		// ===================================
		// Find an unused (not playing) VOICE_INFO, and set it to play this wave
		// ===================================

		if (waveInfo)
		{
			register VOICE_INFO *	voiceInfo;

			{
			register int32_t	transpose;

			// Transpose out of range?
			transpose = (int32_t)noteNum - (int32_t)zone->RootNote;
			if (transpose > PCM_TRANSPOSE_LIMIT || transpose < -PCM_TRANSPOSE_LIMIT) goto out;
			}

			{
			register uint32_t			i;

			// If above, we found a free voice, we're ready to go
			if ((voiceInfo = freeVoiceInfo)) goto use2;

			i = 0;
			voiceInfo = VoiceLists[0];
			do
			{
				// Free?
				if (!voiceInfo->AudioFuncFlags)
				{
					// Found a free voice. It needs to be added to audio thread's list
use2:				freeFlag = 1;

#ifdef JG_NOTE_DEBUG
printf("new drum note %u\r\n", noteNum);
#endif
use:				voiceInfo->Waveform = waveInfo;
					voiceInfo->NoteNum = noteNum;
					voiceInfo->CurrentOffset = (threadId & 0x01) ? waveInfo->LegatoOffset : 0;
					threadId &= ~0x01;

					setupVoice(zone, voiceInfo, noteNum, velocity);

					// Put it in audio thread's list if not already there
					voiceToPlayQueue(voiceInfo, threadId);

					// Unlock the voice if we locked it
					if (!freeFlag) __atomic_and_fetch(&voiceInfo->Lock, ~threadId, __ATOMIC_RELAXED);

					goto out;
				}

				if (voiceInfo->NoteNum == noteNum) i++;

			} while (++voiceInfo < end);

			// ====================================================================
			// Polyphony maxed out. We must steal a voice. We steal the voice
			// that is closest to finishing.

			// If this note is already playing more than 6 times, then steal the longest playing voice
			freeVoiceInfo = voiceInfo = VoiceLists[0];
			if (i > 6)
			{
				register uint32_t		percent, largest;

				largest = 0;
				do
				{
					if (voiceInfo->NoteNum == noteNum)
					{
						// Get what percentage of the waveform has yet to be played
						percent = (voiceInfo->CurrentOffset * 100000) / voiceInfo->Waveform->WaveformLen;
						if (percent > largest)
						{
							freeVoiceInfo = voiceInfo;
							largest = percent;
						}
					}
				} while (++voiceInfo < end);
			}
			else
			{
				register uint32_t		percent, largest;

				largest = 0;
				do
				{
					// Get what percentage of the waveform has yet to be played
					percent = (voiceInfo->CurrentOffset * 100000) / voiceInfo->Waveform->WaveformLen;
					if (percent > largest)
					{
						freeVoiceInfo = voiceInfo;
						largest = percent;
					}
				} while (++voiceInfo < end);
			}
			voiceInfo = freeVoiceInfo;
			}

			// We're going to lock it, in case it's already in audiothread's list
			freeFlag = 0;
#ifdef JG_NOTE_DEBUG
printf("stealing drum note %u\r\n", noteNum);
#endif
			// If audio thread is currently accessing the voice, wait for that to finish
			// before we rewrite it
#ifdef JG_NOTE_DEBUG
			lockVoice(voiceInfo, threadId & ~0x01,"Drums",noteNum);
#else
			lockVoice(voiceInfo, threadId & ~0x01);
#endif
			// We just stole the voice. Now use it
			goto use;
		} // if (waveInfo)
		}
	}	// if (DevAssigns[PLAYER_DRUMS])
#endif	// !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

out:
	// Success
	return 0;
}





#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

static void voiceToPlayQueue(register VOICE_INFO * voiceInfo, register unsigned char threadId)
{
#ifdef TEST_AUDIO_MIX
	startWaveRecord();
#endif
	// Is this VOICE_INFO already in audio thread's queue (because we stole it)
	if (!(voiceInfo->AudioFuncFlags & AUDIOPLAYFLAG_QUEUED))
	{
		// No. We need to add it to the play queue. If some other thread happens to
		// be assessing the queue, just busy wait because it should free quickly
		while (__atomic_or_fetch(&VoicePlayLock, threadId, __ATOMIC_RELAXED) != threadId)
		{
			// If we're waiting for the audio thread, we can just yield because it's a higher
			// priority thread. If it's the beat/midi/gui threads waiting for each other, we
			// need to sleep to task switch
			if ((VoicePlayLock & 0x01) && SecondaryThreadPriority < AudioThreadPriority) sched_yield();
			else usleep(100);
		}

		// Mark voice as in the play queue. Audio thread clears this when removed from queue
		voiceInfo->AudioFuncFlags |= AUDIOPLAYFLAG_QUEUED;

		voiceInfo->Next = VoicePlayQueue;
		VoicePlayQueue = voiceInfo;

		// Allow other threads to access the queue now
		__atomic_and_fetch(&VoicePlayLock, ~threadId, __ATOMIC_RELAXED);
	}
	else
		voiceInfo->AudioFuncFlags |= AUDIOPLAYFLAG_QUEUED;
}




static void setupVoice(register PLAYZONE_INFO * zone, register VOICE_INFO * voiceInfo, unsigned char noteNum, unsigned char velocity)
{
	voiceInfo->Zone = zone;

	// Set the release envelope
	voiceInfo->FadeOut = zone->FadeOut;
	voiceInfo->ReleaseTime = voiceInfo->AttackDelay = 0;
	voiceInfo->ClientFlags = (zone->Flags & PLAYZONEFLAG_SUSLOOP) ? VOICEFLAG_SUSTAIN_INFINITE : 0;

	// Store the volume
	voiceInfo->Velocity = velocity;
	set_vol_factor(voiceInfo);

	// Set the current play position to the start of the waveform data
	voiceInfo->TransposeFracPos = 0;
	if (voiceInfo->CurrentOffset)
	{
		voiceInfo->AttackLevel = voiceInfo->VolumeFactor;
		voiceInfo->VolumeFactor = 0.2f;
		voiceInfo->ReleaseTime = DecayRate * 4 - ((voiceInfo->NoteNum & 0x7f) / 40);
		voiceInfo->AudioFuncFlags |= AUDIOPLAYFLAG_SKIP_RELEASE;
	}

	// Prepare for transpose, with linear interpolation
	//voiceInfo->NoteNum = noteNum;
	{
	register float	virtualPitch;

	virtualPitch = (float)exp(0.69314718056f * ((float)((int32_t)noteNum - (int32_t)zone->RootNote) / 12.0f));
	voiceInfo->TransposeIncrement = (uint32_t)(UPSAMPLE_FACTOR * virtualPitch);
	}
}
#endif





/*********************** startBassNote() *********************
 * Plays the bass waveform assigned to the specified note
 * number.
 */

void startBassNote(unsigned char noteNum, unsigned char velocity, unsigned char threadId)
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	// If bass is assigned to a MIDI buss, output a MIDI msg
	if (DevAssigns[PLAYER_BASS] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		register unsigned char *	msgptr;
		unsigned char					msg[6];

		// Our bass is monophonic, so turn off any preceding note
		// still playing
		msg[0] = msg[3] = 0x90|MidiChans[PLAYER_BASS];
		msgptr = &msg[1];
		if (LastBassNote)
		{
			msgptr[0] = LastBassNote;
			msgptr[1] = 0;
			msgptr += 3;
		}

		*msgptr++ = LastBassNote = noteNum;
		*msgptr++ = velocity;
		sendMidiOut(DevAssigns[PLAYER_BASS], msg, (unsigned char)(msgptr - msg)|threadId);
		return;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register VOICE_INFO		*voiceInfo;

	if (DevAssigns[PLAYER_BASS] && (voiceInfo = VoiceLists[PLAYER_BASS]))
	{
		register unsigned char	flag;
		register PLAYZONE_INFO *zone;
		{
		register uint32_t			index;

		// Assume we'll use the first bass voice

		// If a bass note playing, make sure that we stop it.
		// If both voices playing (ie, one is turning off, and the other is
		// playing), then turn off the playing voice, and abruptly steal
		// the other
		if (!(index = voiceInfo->TriggerTime)) index = 4;
		index--;

		// If the previous bass note is the same as this note, and it's still playing,
		// do a legato note. Also do a legato if portamento pedal enabled on the bass chan
		flag = (noteNum == voiceInfo[index].NoteNum ? 1 : (LegatoPedal & (0x01 << PLAYER_BASS)));
		if (!flag)
			voiceInfo[index].ClientFlags = VOICEFLAG_FASTRELEASE;
		else
		{
			voiceInfo[index].AudioFuncFlags |= AUDIOPLAYFLAG_SKIP_RELEASE;
			voiceInfo[index].FadeOut = 6;
			voiceInfo[index].ReleaseTime = DecayRate * (uint32_t)6;
		}
		voiceInfo[index].NoteNum |= 0x80;
		index = voiceInfo->TriggerTime;
		if (index >= 3) index = 0;
		else index++;
		voiceInfo->TriggerTime = index;
		voiceInfo += index;
		}

		// Find the waveform assigned to this note #
		if (CurrentInstrument[PLAYER_BASS] && (zone = CurrentInstrument[PLAYER_BASS]->Zones))
		{
			register WAVEFORM_INFO	*waveInfo;

			do
			{
				// Within note range?
				if (zone->HighNote >= noteNum)
				{
					{
					register int32_t	transpose;

					// Transpose out of range?
					transpose = (int32_t)noteNum - (int32_t)zone->RootNote;
					if (transpose > PCM_TRANSPOSE_LIMIT || transpose < -PCM_TRANSPOSE_LIMIT) goto out;
					}

					{
					register uint32_t		i;

					// Now we need to look for the matching velocity range
					if ((i = zone->RangeCount))
					{
						register unsigned char *	ranges;

						ranges = (unsigned char *)((char *)zone + sizeof(PLAYZONE_INFO) + (i * sizeof(void *) * 2));
						for (i=0; i < zone->RangeCount; i++)
						{
							if (velocity <= ranges[i])
							{
								register WAVEFORM_INFO **	waveInfoTable;

								// Check WaveQueue[] to get next round-robin wave
								waveInfoTable = (WAVEFORM_INFO **)((char *)zone + sizeof(PLAYZONE_INFO) + (i * sizeof(void *) * 2));
								if (!(waveInfo = waveInfoTable[1]))
								{
									// Must have cycled through all the waves, so move back to the head of the list
									if (!(waveInfo = waveInfoTable[0])) goto out;
								}

								waveInfoTable[1] = waveInfo->Next;

								// If audio thread is currently accessing the voice,
								// wait for that to finish before we rewrite it
#ifdef JG_NOTE_DEBUG
								lockVoice(voiceInfo, threadId,"Bass",noteNum);
#else
								lockVoice(voiceInfo, threadId);
#endif
								voiceInfo->CurrentOffset = flag ? waveInfo->LegatoOffset : 0;
//if (flag) printf("leg %u\n",noteNum);
								// Store the WAVEFORM_INFO, note number, and velocity
								voiceInfo->Waveform = waveInfo;
								voiceInfo->NoteNum = noteNum;
								setupVoice(zone, voiceInfo, noteNum, velocity);

								// Let audio thread play this voice now
								voiceToPlayQueue(voiceInfo, threadId);

								__atomic_and_fetch(&voiceInfo->Lock, ~threadId, __ATOMIC_RELAXED);

								goto out;
							}
						}
					}
					}

					break;
				}

			} while ((zone = zone->Next));
		}
	}
	}
out:
#endif
	return;
}





#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
static void send_bass_ntf(register unsigned char noteNum, register unsigned char threadId)
{
	unsigned char		msg[4];

	msg[0] = 0x90|MidiChans[PLAYER_BASS];
	msg[1] = noteNum;
	LastBassNote = msg[2] = 0;
	sendMidiOut(DevAssigns[PLAYER_BASS], msg, 3|threadId);
}
#endif

/*********************** stopBassNote() **********************
 * Stops the bass waveform assigned to the specified note
 * number from playing. (ie, Turns off the voice playing
 * the waveform mapped to this MIDI note number).
 */

void stopBassNote(register unsigned char noteNum, unsigned char threadId)
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	// If bass is assigned to a MIDI bus, output a MIDI msg
	if (DevAssigns[PLAYER_BASS] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		send_bass_ntf(noteNum, threadId);
		return;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register VOICE_INFO		*voiceInfo;

	if (DevAssigns[PLAYER_BASS] && (voiceInfo = VoiceLists[PLAYER_BASS]))
	{
		register uint32_t			cnt;

		// Find any VOICE_INFO playing this note #
		// NOTE: If we can't find a VOICE_INFO playing this note #, the voice must have been stolen
		voiceInfo += voiceInfo->TriggerTime;
		cnt = 4;
		do
		{
			if (voiceInfo->NoteNum == noteNum)
			{
				voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
				voiceInfo->NoteNum |= 0x80;
				break;
			}
			if (++voiceInfo >= VoiceLists[PLAYER_BASS + 1]) voiceInfo = VoiceLists[PLAYER_BASS];
		} while (--cnt);
	}
	}
#endif
}

/********************** stopAllBassNotes() ********************
 * Stops any playing bass waveform.
 */

void stopAllBassNotes(register unsigned char threadId)
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[PLAYER_BASS] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		if (LastBassNote) send_bass_ntf(LastBassNote, threadId);
		return;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register VOICE_INFO		*voiceInfo;

	if (DevAssigns[PLAYER_BASS] && (voiceInfo = VoiceLists[PLAYER_BASS]))
	{
		register unsigned char	i;

		i = 4;
		do
		{
			voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
			voiceInfo->NoteNum |= 0x80;
			++voiceInfo;
		} while (--i);
	}
	}
#endif
}





/*********************** startSoloNote() *********************
 * Starts playback of the soloist's Instrument waveform assigned
 * to the specified note number.
 */

void startSoloNote(unsigned char noteNum, unsigned char velocity, unsigned char musicianNum)
{
	unsigned char	threadId;

	threadId = musicianNum & 0xE0;
	musicianNum &= 0x1F;

	// External synth?
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[musicianNum] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		unsigned char		msg[4];

		if (!CurrentInstrument[musicianNum] || !((noteNum += CurrentInstrument[musicianNum]->Sub.Patch.Transpose) & 0x80))
		{
			msg[0] = 0x90|MidiChans[musicianNum];
			msg[1] = noteNum;
			msg[2] = velocity;
			sendMidiOut(DevAssigns[musicianNum], msg, 3|threadId);
		}
		return;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register PLAYZONE_INFO *	zone;
	register WAVEFORM_INFO *	waveInfo;
	unsigned char					actualNote;

	// Find the waveform assigned to this note #
	if (CurrentInstrument[musicianNum] && (zone = CurrentInstrument[musicianNum]->Zones) && VoiceListSolo)
	{
		register uint32_t		i;

		actualNote = noteNum + CurrentInstrument[musicianNum]->Sub.Patch.Transpose;

		i = (uint32_t)actualNote;
		do
		{
			// Is this zonetriggered by the MIDI note #
			if (zone->HighNote >= i)
			{
				{
				register int32_t	transpose;

				// Transpose out of range?

				transpose = (int32_t)i - (int32_t)zone->RootNote;
				if (transpose > PCM_TRANSPOSE_LIMIT || transpose < -PCM_TRANSPOSE_LIMIT) goto out;
				}

				// Now we need to look for the matching velocity range
				waveInfo = 0;
				if ((i = zone->RangeCount))
				{
					register unsigned char *	ranges;

					ranges = (unsigned char *)((char *)zone + sizeof(PLAYZONE_INFO) + (i * sizeof(void *) * 2));
					for (i=0; i < zone->RangeCount; i++)
					{
						if (velocity <= ranges[i])
						{
							register WAVEFORM_INFO **	waveInfoTable;

							// Check WaveQueue[] to get next round-robin wave
							waveInfoTable = (WAVEFORM_INFO **)((char *)zone + sizeof(PLAYZONE_INFO) + (i * sizeof(void *) * 2));
							if (!(waveInfo = waveInfoTable[1]))
							{
								// Must have cycled through all the waves, so move back to the head of the list
								if (!(waveInfo = waveInfoTable[0])) goto got_it;
							}

							waveInfoTable[1] = waveInfo->Next;
							goto got_it;
						}
					}
				}

				goto got_it;
			}

			// Check next zone
		} while ((zone = zone->Next));
	}

	goto out;

got_it:
	{
	register VOICE_INFO *	voiceInfo;
	register VOICE_INFO *	earliestVoiceInfo;
	register VOICE_INFO *	sameNoteVoiceInfo;

	earliestVoiceInfo = sameNoteVoiceInfo = 0;

	// ===================================
	// If a MUTE group, make sure that we cutoff any sound in those groups
	// ===================================
	if (zone->MuteGroups)
	{
		voiceInfo = VoiceListSolo;
		do
		{
			--voiceInfo;
			if (voiceInfo < VoiceLists[PLAYER_SOLO])
			{
				voiceInfo = VoiceLists[PLAYER_SOLO + 1];
				--voiceInfo;
			}

			if (voiceInfo->NoteNum < 128 && voiceInfo->Musician == musicianNum)
			{
				if (voiceInfo->Zone->Groups & zone->MuteGroups)
				{
					voiceInfo->NoteNum |= 0x80;
					voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
				}

				// Save the voice with the same note/wave that has been playing the longest
				if (voiceInfo->NoteNum == noteNum && (!sameNoteVoiceInfo || sameNoteVoiceInfo->CurrentOffset < voiceInfo->CurrentOffset)) sameNoteVoiceInfo = voiceInfo;
			}

			if (!voiceInfo->AudioFuncFlags && (!earliestVoiceInfo || earliestVoiceInfo->NoteNum > 127))
				earliestVoiceInfo = voiceInfo;

		} while (voiceInfo != VoiceListSolo);
	}

	// ===================================
	// Find an unused (not playing) VOICE_INFO, and set it to play this wave
	// ===================================
	if (waveInfo)
	{
		register unsigned char	freeFlag;

		// If we found a free voice in our mute processing above, use it
		if ((voiceInfo = earliestVoiceInfo)) goto use2;

		// If we did the mute processing, and didn't find a free voice,
		// then we know we've got to steal. See if we found the same note/wave
		// playing. If so, steal that
		if (sameNoteVoiceInfo) goto steal;

		earliestVoiceInfo = voiceInfo = VoiceListSolo;
		do
		{
			if (++voiceInfo >= VoiceLists[PLAYER_SOLO + 1]) voiceInfo = VoiceLists[PLAYER_SOLO];

			// Is audio thread playing this voice?
			if (!voiceInfo->AudioFuncFlags)
			{
				// It may be finished playing, but we may still be waiting for its
				// Midi note-off. If so, let's keep looking for a free one, but
				// use this one if necessary
				if (voiceInfo->NoteNum > 127)
				{
					// We got a free voice
use2:				freeFlag = 1;
#ifdef JG_NOTE_DEBUG
					printf("new solo note %u\r\n",noteNum);
#endif
use:				voiceInfo->Waveform = waveInfo;
					voiceInfo->Instrument = CurrentInstrument[musicianNum];
					voiceInfo->Musician = musicianNum;
					voiceInfo->NoteNum = noteNum;
					voiceInfo->ActualNote = actualNote;
					voiceInfo->CurrentOffset = (LegatoPedal & (0x01 << musicianNum)) ? waveInfo->LegatoOffset : 0;
					setupVoice(zone, voiceInfo, actualNote, velocity);

					voiceToPlayQueue(voiceInfo, threadId);

					if (!freeFlag) __atomic_and_fetch(&voiceInfo->Lock, ~threadId, __ATOMIC_RELAXED);

					VoiceListSolo = voiceInfo;

					goto out;
				}

				earliestVoiceInfo = voiceInfo;
			}

			else if (voiceInfo->Musician == musicianNum)
			{
				// Save the voice with the same note/wave that has been playing the longest
				if (voiceInfo->NoteNum == noteNum) sameNoteVoiceInfo = voiceInfo;

				// Save the voice that has been playing the longest
				if (!earliestVoiceInfo) earliestVoiceInfo = voiceInfo;
			}
		} while (voiceInfo != VoiceListSolo);

		// ==================================================
		// Polyphony exceeded. We must steal a voice.

		// If the same note/wave is already playing, steal the one playing for the longest time.
		// Otherwise steal the one playing the longest
steal:
		if (!(voiceInfo = sameNoteVoiceInfo) && !(voiceInfo = earliestVoiceInfo)) voiceInfo = VoiceLists[PLAYER_SOLO];

		freeFlag = 0;

		// If audio thread is currently accessing the voice, wait for that to finish before
		// we rewrite it. If some other thread has locked it, then it's already stealing
		// that voice, so we must continue
#ifdef JG_NOTE_DEBUG
		printf("stealing solo note %u\r\n",noteNum);
		lockVoice(voiceInfo, threadId,"Solo",noteNum);
#else
		lockVoice(voiceInfo, threadId);
#endif
		goto use;
	} // if (waveInfo)
	}
	}
out:
	return;
#endif
}





#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

static void doReleaseWave(register VOICE_INFO * voiceInfo, register VOICE_INFO * unused, unsigned char threadId)
{
	register PLAYZONE_INFO *	zone;

	if ((zone = voiceInfo->Instrument->ReleaseZones))
	{
		register uint32_t			i;

		if (!unused) unused = voiceInfo;

		i = (uint32_t)voiceInfo->ActualNote;
		do
		{
			if (zone->HighNote >= i)
			{
				// Same pitch as ntn
				{
				register int32_t	transpose;
				register float		virtualPitch;

				transpose = (int32_t)i - (int32_t)zone->RootNote;
				if (transpose > PCM_TRANSPOSE_LIMIT || transpose < -PCM_TRANSPOSE_LIMIT) break;
				virtualPitch = (float)exp(0.69314718056f * ((float)transpose / 12.0f));
				unused->TransposeIncrement = (uint32_t)(UPSAMPLE_FACTOR * virtualPitch);
				}
				unused->TransposeFracPos = unused->CurrentOffset = 0;

				// Only 1 velocity range and round robin
				{
				register WAVEFORM_INFO **	waveInfoTable;

				waveInfoTable = (WAVEFORM_INFO **)((char *)zone + sizeof(PLAYZONE_INFO));
				unused->Waveform = waveInfoTable[0];
				}

				unused->Zone = zone;
				unused->Instrument = voiceInfo->Instrument;

				// We regard this note as "off"
				unused->NoteNum = voiceInfo->NoteNum | 0x80;

				unused->Musician = voiceInfo->Musician;

				// Set vol based on the ntn
				unused->Velocity = voiceInfo->Velocity;
				unused->ClientFlags = 0;
				set_vol_factor(unused);

				// Don't respond to volume, nor release time, changes
				unused->AudioFuncFlags = AUDIOPLAYFLAG_FINAL_FADE;

				// Add to audio thread's list
				voiceToPlayQueue(unused, threadId);

				if (unused == voiceInfo) goto out;

				break;
			}
		} while ((zone = zone->Next));
	}

	// Fast fade the ntn
	voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
	voiceInfo->NoteNum |= 0x80;
out:
	return;
}

#endif





/*********************** stopSoloNote() **********************
 * Stops the Soloist's waveform assigned to the specified note
 * number from playing. (ie, Turns off the voice playing
 * the waveform mapped to this MIDI note number).
 */

void stopSoloNote(register unsigned char noteNum, unsigned char musicianNum)
{
	// External synth?
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[musicianNum & 0x1F] >= &SoundDev[DEVNUM_MIDIOUT1])
		startSoloNote(noteNum, 0, musicianNum);	// Sends a NTF
	else
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register VOICE_INFO *		voiceInfo;

	if ((voiceInfo = VoiceListSolo))
	{
		register VOICE_INFO *	unused;
		unsigned char				threadId;

		threadId = musicianNum & 0xE0;
		musicianNum &= 0x1F;

		// Find the longest playing instance of this note #, which isn't already marked for fade-out or sustain pedal (NoteNum < 127)
		unused = 0;
		do
		{
			if (++voiceInfo >= VoiceLists[PLAYER_SOLO + 1]) voiceInfo = VoiceLists[PLAYER_SOLO];

			if (!voiceInfo->AudioFuncFlags && (!unused || unused->NoteNum < 128) && voiceInfo->NoteNum > 127) unused = voiceInfo;

			// Correct note #? Still "on"?
			if (voiceInfo->NoteNum == noteNum && voiceInfo->Musician == musicianNum)
			{
				// If voice always fades out at one specific rate, ignore note-off except for marking this voice as "off"
				if (voiceInfo->Zone->Flags & PLAYZONEFLAG_ALWAYS_FADE) goto off;
				// If sustain pedal held, mark this voice to be faded when pedal released, and mark it "off" (even
				// though it's still playing). Don't start its release phase yet. That's done when we get a pedal
				// off event
				if (SustainPedal & (0x01 << musicianNum))
				{
					voiceInfo->ClientFlags |= VOICEFLAG_SUSTAIN_PEDAL;

					// If PLAYER_PAD, then when sustain pedal is held, the release phase is triggered with a
					// longer fade (than the fast fade). If it was infinite sustain loop, clear that so it
					// will fade out
					if (musicianNum != PLAYER_SOLO)
					{
						voiceInfo->FadeOut = 20;
						voiceInfo->ReleaseTime = DecayRate * 20;
						voiceInfo->ClientFlags &= ~VOICEFLAG_SUSTAIN_INFINITE;
					}

off:				voiceInfo->NoteNum |= 0x80;
					break;
				}

				// Otherwise, start its fast fade out now, or trigger any release sample
				doReleaseWave(voiceInfo, unused, threadId);
				break;
			}

		} while (voiceInfo != VoiceListSolo);

		// NOTE: If we can't find a VOICE_INFO playing this note, the voice must have been stolen
	}
	}
#endif
}





#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)

/******************* releaseSustain() *******************
 * Turns off all notes being held only via the sustain pedal.
 */

void releaseSustain(register unsigned char musicianNum, register unsigned char threadId)
{
	register VOICE_INFO *		voiceInfo;
	register VOICE_INFO *		unused;

	if ((voiceInfo = VoiceLists[musicianNum]))
	{
		unused = voiceInfo - 1;
		do
		{
			if (voiceInfo->Musician == musicianNum && (voiceInfo->ClientFlags & VOICEFLAG_SUSTAIN_PEDAL))
			{
				voiceInfo->ClientFlags &= ~VOICEFLAG_SUSTAIN_PEDAL;

				if (voiceInfo->Instrument->ReleaseZones && unused)
				{
					// Find an unused voice to play the release sample
					goto loop;
					do
					{
						if (!unused->AudioFuncFlags) goto off;
loop:					;
					} while (++unused < VoiceLists[musicianNum + 1]);
					unused = 0;
				}

off:			doReleaseWave(voiceInfo, unused, threadId);
			}

		} while (++voiceInfo < VoiceLists[musicianNum + 1]);
	}
}
#endif




/********************** stopAllSoloNotes******************
 * Mutes all currently playing Soloist's voices.
 */

void stopAllSoloNotes(register unsigned char threadId)
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[PLAYER_SOLO] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		send_alloff(0, threadId);
		return;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register VOICE_INFO		*voiceInfo;

	if ((voiceInfo = VoiceLists[PLAYER_SOLO]))
	{
		do
		{
			voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
			voiceInfo->NoteNum |= 0x80;
			// Don't bother with release samples since this function is called only to instantly mute the solo (Panic btn)
		} while (++voiceInfo < VoiceLists[PLAYER_SOLO+1]);
	}
	}
#endif
}




void changeGtrChord(void)
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[PLAYER_GTR] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		stopGtrString(0, BEATTHREADID);
		return;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register VOICE_INFO *	voiceInfo;

	if (DevAssigns[PLAYER_GTR] && (voiceInfo = VoiceLists[PLAYER_GTR]))
	{
		do
		{
			register unsigned char	i, noteNum;

			i = 6;
			noteNum = voiceInfo->NoteNum;
			do
			{
				if (GtrStrings[--i] == noteNum) goto keep;
			} while (i);

			voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
			voiceInfo->NoteNum |= 0x80;
			voiceInfo->GtrNoteSpec = 0;
keep:		;
		} while (++voiceInfo < VoiceLists[PLAYER_GTR+1]);
	}
	}
#endif
}

/********************** stopGtrString() ********************
 * Stops the specified guitar string (1 to 6, or 0 for all
 * strings, or >6 is note spec).
 */

void stopGtrString(register unsigned char string, unsigned char threadId)
{
	register unsigned char	noteNum;

#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[PLAYER_GTR] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		unsigned char			msg[4];

		msg[0] = 0x90|MidiChans[PLAYER_GTR];
		msg[2] = 0;

		if (string > 6)
		{
			noteNum = string;
			string = 6;
			do
			{
				if (noteNum == GtrNotes[--string]) goto off;
			} while (string);
		}
		else if (!string)
		{
			string = 6;
			do
			{
				if ((msg[1] = GtrNotes[--string]))
					sendMidiOut(DevAssigns[PLAYER_GTR], msg, 3|threadId);
			} while (string);
			memset(GtrNotes, 0, sizeof(GtrNotes));
		}
		else
		{
off:		if ((msg[1] = GtrNotes[string]))
			{
				if (CurrentInstrument[PLAYER_GTR] && (!BeatInPlay || (TempFlags & APPFLAG3_NOGTR)))
					msg[1] += CurrentInstrument[PLAYER_GTR]->Sub.Patch.Transpose;
				GtrNotes[string] = 0;
				sendMidiOut(DevAssigns[PLAYER_GTR], msg, 3|threadId);
			}
		}

		return;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register VOICE_INFO		*voiceInfo;

	if (DevAssigns[PLAYER_GTR] && (voiceInfo = VoiceLists[PLAYER_GTR]))
	{
		if (string > 6)
		{
			do
			{
				if (voiceInfo->GtrNoteSpec == string)
				{
					voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
					voiceInfo->NoteNum |= 0x80;
					voiceInfo->GtrNoteSpec = 0;
					break;
				}
			} while (++voiceInfo < VoiceLists[PLAYER_GTR+1]);
		}

		else if (!string)
		{
			do
			{
				voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
				voiceInfo->NoteNum |= 0x80;
				voiceInfo->GtrNoteSpec = 0;
			} while (++voiceInfo < VoiceLists[PLAYER_GTR+1]);
		}
		else
		{
			voiceInfo += (string - 1);
			voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
			voiceInfo->NoteNum |= 0x80;
			voiceInfo->GtrNoteSpec = 0;
			voiceInfo += 6;
			voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
			voiceInfo->NoteNum |= 0x80;
			voiceInfo->GtrNoteSpec = 0;
		}
	}
	}
#endif
}




/********************** startGtrString() ********************
 * Starts the specified note on the specified guitar string.
 *
 * string =	1 to 6 for string num, or "note specification" byte.
 *				If string, then "noteNum" is spec byte. Otherwise,
 *				midi note #.
 */

void startGtrString(register unsigned char string, register unsigned char noteNum, register unsigned char velocity, unsigned char threadId)
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[PLAYER_GTR] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		register unsigned char *	gtrptr;
		register unsigned char		spec;

		if (string > 6)
		{
			gtrptr = &GtrNotes[6];
			while (--gtrptr > &GtrNotes[0] && *gtrptr);
//			spec = string;
		}
		else
		{
			spec = noteNum;
			gtrptr = &GtrNotes[--string];
			noteNum = GtrStrings[string];
			if (spec & GTREVTFLAG_PITCHDOWN) noteNum--;
			if (spec & GTREVTFLAG_PITCHUP) noteNum++;
		}

		{
		unsigned char			msg[4];

		if (CurrentInstrument[PLAYER_GTR] && (!BeatInPlay || (TempFlags & APPFLAG3_NOGTR)))
			noteNum += CurrentInstrument[PLAYER_GTR]->Sub.Patch.Transpose;

		msg[0] = 0x90|MidiChans[PLAYER_GTR];

		if ((msg[1] = *gtrptr))
		{
			msg[2] = 0;
			sendMidiOut(DevAssigns[PLAYER_GTR], msg, 3|threadId);
		}

		*gtrptr = msg[1] = noteNum;
		msg[2] = velocity;
		sendMidiOut(DevAssigns[PLAYER_GTR], msg, 3|threadId);

		PlayFlags |= PLAYFLAG_CHORDSOUND;
		}

		return;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register VOICE_INFO		*voiceInfo;

	if (DevAssigns[PLAYER_GTR] && (voiceInfo = VoiceLists[PLAYER_GTR]))
	{
		register PLAYZONE_INFO *zone;
		register unsigned char	spec;

		// Use non-playing string?
		if (string > 6)
		{
			spec = MAX_GUITAR_POLYPHONY - 1;
			while (voiceInfo->AudioFuncFlags && --spec) voiceInfo++;
			voiceInfo->TriggerTime = 0x01;
			spec = string;
		}
		else
		{
			spec = noteNum;

			// Mute any previously playing note for this voice
			voiceInfo += (string - 1);
			if (!(voiceInfo->TriggerTime ^= 0x01))
			{
				voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
				voiceInfo->NoteNum |= 0x80;
				voiceInfo += 6;
			}
			else
			{
				voiceInfo += 6;
				voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
				voiceInfo->NoteNum |= 0x80;
				voiceInfo -= 6;
			}

			noteNum = GtrStrings[string - 1];

			// Up/down a half step?
			if (spec & GTREVTFLAG_PITCHDOWN) noteNum--;
			if (spec & GTREVTFLAG_PITCHUP) noteNum++;
		}

		// Find the waveform assigned to this note #
		if (CurrentInstrument[PLAYER_GTR])
		{
			zone = CurrentInstrument[PLAYER_GTR]->Zones;

			if (!BeatInPlay || (TempFlags & APPFLAG3_NOGTR))
				noteNum += CurrentInstrument[PLAYER_GTR]->Sub.Patch.Transpose;

			if (zone)
			do
			{
				// Within note range?
				if (zone->HighNote >= noteNum)
				{
					{
					register int32_t	transpose;

					// Transpose out of range?
					transpose = (int32_t)noteNum - (int32_t)zone->RootNote;
					if (transpose > PCM_TRANSPOSE_LIMIT || transpose < -PCM_TRANSPOSE_LIMIT) goto out;
					}

					{
					register uint32_t				i;

					// Yes. Now we need to look for the matching velocity range
					if ((i = zone->RangeCount))
					{
						register unsigned char *	ranges;

						ranges = (unsigned char *)((char *)zone + sizeof(PLAYZONE_INFO) + (i * sizeof(void *) * 2));
						for (i=0; i < zone->RangeCount; i++)
						{
							if (velocity <= ranges[i])
							{
								register WAVEFORM_INFO **	waveInfoTable;
								register WAVEFORM_INFO *	waveInfo;

								// Check WaveQueue[] to get next round-robin wave
								waveInfoTable = (WAVEFORM_INFO **)((char *)zone + sizeof(PLAYZONE_INFO) + (i * sizeof(void *) * 2));
								if (!(waveInfo = waveInfoTable[1]))
								{
									// Must have cycled through all the waves, so move back to the head of the list
									if (!(waveInfo = waveInfoTable[0])) goto out;
								}

								waveInfoTable[1] = waveInfo->Next;

								// If audio thread is currently accessing the voice,
								// wait for that to finish before we rewrite it
#ifdef JG_NOTE_DEBUG
								lockVoice(voiceInfo, BEATTHREADID,"Gtr",string);
#else
								lockVoice(voiceInfo, BEATTHREADID);
#endif
								// Fill in the VOICE_INFO
								voiceInfo->Waveform = waveInfo;

								voiceInfo->GtrNoteSpec = string;
								voiceInfo->NoteNum = noteNum;
								voiceInfo->CurrentOffset = (LegatoPedal & (0x01 << PLAYER_GTR)) ? waveInfo->LegatoOffset : 0;
								voiceInfo->AttackDelay = PickAttack++;

								setupVoice(zone, voiceInfo, noteNum, velocity);

								if (PickAttack < 6) PickAttack++;
								if (PickAttack > 8) PickAttack = 0;

								// Let audio thread play this voice now. Audio thread will zero
								// VOICE_INFO->AudioFuncFlags when voice is done playing
								voiceToPlayQueue(voiceInfo, BEATTHREADID);

								// Unlock the voice so audio thread can access it
								__atomic_and_fetch(&voiceInfo->Lock, ~BEATTHREADID, __ATOMIC_RELAXED);

								// Indicate gtr notes will need to be muted after play stops and
								// user releases keys
								PlayFlags |= PLAYFLAG_CHORDSOUND;

								goto out;
							}
						}
					}
					}

					break;
				}

			} while ((zone = zone->Next));
		}
	}
	}
out:
	return;
#endif
}




void copy_gtrnotes(register const unsigned char * notes)
{
	memcpy(&GtrNotes[0], notes, sizeof(GtrNotes));
}




/********************* lockVoice() *******************
 * Locks access to the specified VOICE_INFO. When locked,
 * the audio thread will ignore the VOICE_INFO, even if
 * it's in the audio thread's list/queue. If the audio
 * thread has locked the VOICE_INFO, then the caller will
 * wait for it to be unlocked.
 *
 * NOTE: The audio thread never calls this.
 */

#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
#ifdef JG_NOTE_DEBUG
static void lockVoice(register VOICE_INFO * voiceInfo, register unsigned char threadId, const char *name, unsigned char noteNum)
#else
static void lockVoice(register VOICE_INFO * voiceInfo, register unsigned char threadId)
#endif
{
#ifdef JG_NOTE_DEBUG
uint32_t cnt;
cnt = 0;
#endif

	while (__atomic_or_fetch(&voiceInfo->Lock, threadId, __ATOMIC_RELAXED) != threadId)
	{
#ifdef JG_NOTE_DEBUG
if (!cnt) printf("%s waiting for note %u\r\n", name,noteNum);
cnt=1;
#endif
		usleep(100);
	}

	// Preserve the flag that indicates whether voice is still in audio thread's list,
	// but clear all others
	voiceInfo->AudioFuncFlags &= AUDIOPLAYFLAG_QUEUED;

#ifdef JG_NOTE_DEBUG
if (cnt) printf("%s woken for note %u\r\n", name,noteNum);
#endif
}
#endif




/********************** stopPadVoices() ********************
 * Mutes all currently playing "Backing Chord Pad" voices.
 */

void stopPadVoices(register unsigned char speed)
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[PLAYER_PAD] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		register uint32_t		string;
		unsigned char			msg[4];

		msg[0] = 0x90|MidiChans[PLAYER_PAD];
		msg[2] = 0;

		for (string = 0; string < 3; string++)
		{
			if ((msg[1] = PadNotes[string]))
			{
				PadNotes[string] = 0;
				sendMidiOut(DevAssigns[PLAYER_PAD], msg, 3|BEATTHREADID);
			}
		}

		return;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	{
	register VOICE_INFO		*voiceInfo;

	if (DevAssigns[PLAYER_PAD] && (voiceInfo = VoiceLists[PLAYER_PAD]))
	{
		register VOICE_INFO *		end;

		end = VoiceLists[((AppFlags4 & APPFLAG4_UPPER_PAD) ? PLAYER_SOLO + 1 : PLAYER_PAD + 1)];
		do
		{
			if (voiceInfo->AudioFuncFlags && !(voiceInfo->AudioFuncFlags & AUDIOPLAYFLAG_FINAL_FADE) && voiceInfo->Musician == PLAYER_PAD)
			{
				if (speed > 40)
				{
					voiceInfo->FadeOut = speed;
					voiceInfo->AudioFuncFlags |= AUDIOPLAYFLAG_SKIP_RELEASE;
					voiceInfo->ReleaseTime = DecayRate * (uint32_t)speed;
					voiceInfo->ClientFlags &= ~VOICEFLAG_SUSTAIN_INFINITE;
				}
				else
					voiceInfo->ClientFlags = VOICEFLAG_FASTRELEASE;
			}
			++voiceInfo;
		} while (voiceInfo < end);
	}
	}
#endif
}







static void cache_pads(void)
{
	register INS_INFO *		patch;
	register unsigned char	padnum, padmatch;

	memset(&CachedPads[0], 0, sizeof(CachedPads));

	if ((patch = InstrumentLists[PLAYER_PAD]))
	{
		padmatch = 0;
		do
		{
			for (padnum=0; padnum < 3; padnum++)
			{
				if (!CachedPads[padnum])
				{
					// Convert GUI button # to GM Program Change
					if (patch->PgmNum == PadPatchNums[padnum])
					{
						CachedPads[padnum] = patch;
						padmatch |= (0x01 << padnum);
						break;
					}
				}
			}
		} while (padmatch != 0x07 && (patch = patch->Next));
	}
}

/*********************** changePadInstrument() *********************
 * Changes the "Backing Chord Pad" Instrument. Called by main (user),
 * midi in, or beat thread (when playing a song sheet with a
 * pad change event).
 */

uint32_t changePadInstrument(register unsigned char padnum)
{
	register unsigned char threadId;

	threadId = padnum & 0xE0;
	padnum &= 0x1F;

	if (DevAssigns[PLAYER_PAD] && padnum < 4)
	{
		if (!padnum)
		{
			if (CurrentInstrument[PLAYER_PAD])
			{
				if (lockInstrument(threadId) == threadId)
				{
					// Turning off
					stopPadVoices(80);

					CurrentInstrument[PLAYER_PAD] = 0;
				}
upd:			unlockInstrument(threadId);
				return CTLMASK_BACKINGPAD;
			}
		}
		else
		{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
			if (DevAssigns[PLAYER_PAD] >= &SoundDev[DEVNUM_MIDIOUT1])
			{
				if (lockInstrument(threadId) == threadId)
				{
					send_patch(PLAYER_PAD, PadPatchNums[padnum - 1], threadId | SETINS_NO_LSB | SETINS_NO_MSB);
					checkStartPadNotes();
				}
				goto upd;
			}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
			{
			register INS_INFO *	patch;

			if ((patch = CachedPads[padnum - 1]) && patch != CurrentInstrument[PLAYER_PAD])
			{
				if (lockInstrument(threadId) == threadId)
				{
					CurrentInstrument[PLAYER_PAD] = patch;
					checkStartPadNotes();
				}
				goto upd;
			}
			}
#endif
		}
	}
	return CTLMASK_NONE;
}



unsigned char getPadPgmNum(void)
{
	register INS_INFO *		patch;
	register unsigned char	padnum;

	if (DevAssigns[PLAYER_PAD] && (patch = CurrentInstrument[PLAYER_PAD]))
	{
		padnum = 4;
		do
		{
			if (patch == CachedPads[--padnum]) return padnum + 1;
		} while (padnum);
	}
	return 0;
}

void * getPadCached(register unsigned char padnum)
{
	register INS_INFO *	patch;

	patch = 0;
	if (DevAssigns[PLAYER_PAD] && padnum && !(patch = CachedPads[--padnum]))
	{
		patch = CachedPads[padnum];
	}
	return patch ? &patch->Name[0] : "<none>";
}

/********************* fadePadVoices() ****************
 * Called when play ends, to fade out the pad Instrument's
 * final chord.
 */

uint32_t fadePadVoices(register unsigned char mutespeed)
{
	// Is pad on, and not yet fading?
	if (!(PlayFlags & PLAYFLAG_END_FADE) && CurrentInstrument[PLAYER_PAD])
	{
		// Fade current playing notes
		PlayFlags |= PLAYFLAG_END_FADE;
		stopPadVoices(mutespeed);

		// If a songsheet changed the pad, reset to "None" now that play has ended
#ifndef NO_SONGSHEET_SUPPORT
		if (mutespeed > 12 && isSongsheetActive() && !(AppFlags2 & APPFLAG2_PATCHCHG_MANUAL))
		{
			CurrentInstrument[PLAYER_PAD] = 0;
			return CTLMASK_BACKINGPAD;
		}
#endif
	}

	return CTLMASK_NONE;
}





/********************** startPadVoices() ********************
 * Starts the pad voices.
 */

void startPadVoices(unsigned char * noteNums, unsigned char release)
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[PLAYER_PAD] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		register uint32_t		string;
		unsigned char			msg[4];

		// Is pad on, and unmuted?
		msg[0] = 0x90|MidiChans[PLAYER_PAD];

		for (string = 0; string < 3; string++)
		{
			if ((msg[1] = PadNotes[string]))
			{
				msg[2] = 0;
				sendMidiOut(DevAssigns[PLAYER_PAD], msg, 3|BEATTHREADID);
			}

			PadNotes[string] = msg[1] = noteNums[string];
			msg[2] = ChordVel;
			sendMidiOut(DevAssigns[PLAYER_PAD], msg, 3|BEATTHREADID);
		}

		PlayFlags |= PLAYFLAG_CHORDSOUND;
		PlayFlags &= ~PLAYFLAG_END_FADE;
		return;
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	if (DevAssigns[PLAYER_PAD] && CurrentInstrument[PLAYER_PAD])
	{
		register PLAYZONE_INFO *	zone;
		register uint32_t				string;

		// Let audio thread fade any currently playing pad
//		stopPadVoices(40);
		PlayFlags &= ~PLAYFLAG_END_FADE;

		for (string = 0; string < 3; string++)
		{
			register WAVEFORM_INFO *	waveInfo;

			if (!(zone = CurrentInstrument[PLAYER_PAD]->Zones)) goto out;
			waveInfo = 0;
			do
			{
				if (zone->HighNote >= noteNums[string])
				{
					{
					register int32_t	transpose;

					// Transpose out of range?
					transpose = (int32_t)noteNums[string] - (int32_t)zone->RootNote;
					if (transpose > PCM_TRANSPOSE_LIMIT || transpose < -PCM_TRANSPOSE_LIMIT) goto next;
					}

					{
					register uint32_t				i;

					if ((i = zone->RangeCount))
					{
						register unsigned char *	ranges;

						ranges = (unsigned char *)((char *)zone + sizeof(PLAYZONE_INFO) + (i * sizeof(void *) * 2));

						for (i=0; i < zone->RangeCount; i++)
						{
							if (64 <= ranges[i])
							{
								register WAVEFORM_INFO **	waveInfoTable;

								waveInfoTable = (WAVEFORM_INFO **)((char *)zone + sizeof(PLAYZONE_INFO) + (i * sizeof(void *) * 2));
								if ((waveInfo = waveInfoTable[1]) || (waveInfo = waveInfoTable[0])) waveInfoTable[1] = waveInfo->Next;
								break;
							}
						}
					}
					}
					break;
				}

			} while ((zone = zone->Next));

			{
			register VOICE_INFO		*voiceInfo;

next:		if (waveInfo && (voiceInfo = VoiceLists[PLAYER_PAD]))
			{
				voiceInfo += string;

				// If audio thread is currently accessing the voice,
				// wait for that to finish before we rewrite it
#ifdef JG_NOTE_DEBUG
				lockVoice(voiceInfo,BEATTHREADID,"Pad",noteNums[string]);
#else
				lockVoice(voiceInfo, BEATTHREADID);
#endif
				voiceInfo->Waveform = waveInfo;
				voiceInfo->NoteNum = noteNums[string];
				voiceInfo->Musician = PLAYER_PAD;
				voiceInfo->CurrentOffset = (LegatoPedal & (0x01 << PLAYER_PAD)) ? waveInfo->LegatoOffset : 0;
				setupVoice(zone, voiceInfo, noteNums[string], ChordVel);

				if (release)
				{
					voiceInfo->FadeOut = release;
					voiceInfo->AudioFuncFlags |= AUDIOPLAYFLAG_SKIP_RELEASE;
					voiceInfo->ReleaseTime = DecayRate * (uint32_t)release;
				}

				voiceToPlayQueue(voiceInfo, BEATTHREADID);

				__atomic_and_fetch(&voiceInfo->Lock, ~BEATTHREADID, __ATOMIC_RELAXED);

				voiceInfo->TriggerTime = 1;

				PlayFlags |= PLAYFLAG_CHORDSOUND;
			}
			}
		}
	}
out:
	return;
#endif
}





/********************** stopAllVoices() ********************
 * Mutes any playing bass/gtr/pad waveforms.
 */

void stopAllVoices(register unsigned char threadId)
{
	fadePadVoices(14);
	stopGtrString(0, threadId);
	stopAllBassNotes(threadId);
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[PLAYER_DRUMS] >= &SoundDev[DEVNUM_MIDIOUT1])
		stopAllDrumNotes(threadId);
#endif
}




/****************** setReverbVol() ******************
 * Sets reverb volume.
 */
#ifndef NO_REVERB_SUPPORT
uint32_t setReverbVol(register char vol)
{
	unsigned int	param;

	if (Reverb)
	{
		ReverbGetParams(Reverb, REVPARAM_REVLEVELMASK, &param);
		if ((unsigned char)vol > 100)
			return param;
		if (param != vol)
		{
			param = vol;
			ReverbSetParams(Reverb, REVPARAM_REVLEVELMASK, &param);
			return CTLMASK_REVERBVOL;
		}
	}

	return CTLMASK_NONE;
}
#endif





/******************* setMasterVol() *********************
 * Sets the overall volume attenuation.
 */

uint32_t setMasterVol(register unsigned char vol)
{
	if (vol <= MAX_VOL_ADJUST && MasterVolAdjust != vol)
	{
//		VoiceChangeFlags = (0x01|0x02|0x04|0x08|0x10);
		VoiceChangeFlags = (0x02|0x04|0x08|0x10);
		MasterVolAdjust = vol;
		return CTLMASK_MASTERVOL;
	}

	return CTLMASK_NONE;
}

unsigned char getMasterVol(void)
{
	return MasterVolAdjust;
}





/******************* setInstrumentVol() *********************
 * Sets the volume attenuation for specified player.
 */

#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
uint32_t setInstrumentVol(register uint32_t robotNum, register unsigned char vol, register unsigned char threadId)
#else
uint32_t setInstrumentVol(register uint32_t robotNum, register unsigned char vol)
#endif
{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	if (DevAssigns[robotNum] >= &SoundDev[DEVNUM_MIDIOUT1])
	{
		unsigned char	msg[3];

		msg[0] = 0xB0|setMidiSendChan(robotNum, -1);
		msg[1] = 0x07;
		msg[2] = vol;
		sendMidiOut(DevAssigns[robotNum], msg, 3|threadId);
		return 0x00000001 << (CTLID_DRUMVOL + robotNum);
	}
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	if (vol <= MAX_VOL_ADJUST && VolAdjust[robotNum] != vol)
	{
		VolAdjust[robotNum] = vol;
		if (robotNum) VoiceChangeFlags = (0x01 << robotNum);

		return 0x00000001 << (CTLID_DRUMVOL + robotNum);
	}
#endif
	return CTLMASK_NONE;
}

unsigned char getInstrumentVol(register uint32_t robotNum)
{
	return VolAdjust[robotNum];
}





/************** toggle_solo_vol() ******************
 * Turns volume boost on/off for right hand
 * patch.
 *
 * flag =	0 turns off boost, 1 turns on, 2 toggles, 0xff queries.
 */

unsigned char toggle_solo_vol(register unsigned char flag, register unsigned char threadId)
{
	if (flag != 0xff)
	{
		if (flag == 2) VolBoost ^= 1;
		else VolBoost = flag;

#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
		if (DevAssigns[PLAYER_SOLO] >= &SoundDev[DEVNUM_MIDIOUT1])
		{
			unsigned char	msg[3];

			msg[0] = 0xB0|setMidiSendChan(4, -1);
			msg[1] = 0x07;
			msg[2] = (VolBoost ? 0x7F : 0x6E);
			sendMidiOut(DevAssigns[PLAYER_SOLO], msg, 3|threadId);
		}
#endif
	}

	return VolBoost;
}












// ======================================================
//  Prefs/Devices file load/save

/********************* saveAudioConfig() *******************
 * Called by saveConfigOther() to save app-specific data to
 * the config file.
 *
 * buffer =		MAX_PATH size buffer to format data in.
 *
 * RETURN: Ptr to end of bytes formatted.
 */

unsigned char * saveAudioConfig(register unsigned char * buffer)
{
	{
	register unsigned char	i;

	for (i=0; i <= PLAYER_SOLO; i++)
	{
#if defined(NO_ALSA_AUDIO_SUPPORT) && defined(NO_JACK_SUPPORT)
		if (DevAssigns[i] != &SoundDev[DEVNUM_MIDIOUT1])
#else
		if (DevAssigns[i] != &SoundDev[DEVNUM_AUDIOOUT])
#endif
		{
			*buffer++ = CONFIGKEY_BUSS + i;
			*buffer++ = DevAssigns[i] - &SoundDev[0];
		}

		if (VolAdjust[i] != MAX_VOL_ADJUST - 15)
		{
			*buffer++ = CONFIGKEY_DRUMSVOL + i;
			*buffer++ = VolAdjust[i];
		}
	}
	}

	if (MasterVolAdjust != MAX_VOL_ADJUST - 15)
	{
		*buffer++ = CONFIGKEY_MASTERVOL;
		*buffer++ = MasterVolAdjust;
	}

#ifndef NO_ALSA_AUDIO_SUPPORT
	if (SampleRateFactor)
	{
		*buffer++ = CONFIGKEY_SAMPLERATE;
		*buffer++ = SampleRateFactor;
	}

	if (setFrameSize(0) != (128/4) - 12)
	{
		*buffer++ = CONFIGKEY_FRAMES;
		*buffer++ = setFrameSize(0);
	}
#endif
#ifndef NO_REVERB_SUPPORT
	*buffer++ = CONFIGKEY_REVVOL;
	*buffer++ = setReverbVol(-1);;
#endif

	return buffer;
}




static unsigned char matchSeqOutHash(register struct SOUNDDEVINFO * dev)
{
	register struct SOUNDDEVINFO * 	sounddev;
	register unsigned char				flag;

	sounddev = &SoundDev[DEVNUM_MIDIOUT1];
	flag = 1;	// Assume all 4 External synths resolved
	do
	{
		if ((sounddev->DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_SEQ && sounddev->DevHash && !sounddev->Dev)
		{
			if (sounddev->DevHash == dev->DevHash)
			{
				sounddev->Card = 0;
				sounddev->Dev = dev->Dev;
				sounddev->SubDev = dev->SubDev;
			}
			else
				// Still at least 1 unresolved
				flag = 0;
		}
	} while (++sounddev <= &SoundDev[DEVNUM_MIDIOUT4]);

	return flag;
}

static unsigned char matchSeqInHash(register struct SOUNDDEVINFO * dev)
{
	if (SoundDev[DEVNUM_MIDIIN].DevHash == dev->DevHash)
	{
		SoundDev[DEVNUM_MIDIIN].Card = 0;
		SoundDev[DEVNUM_MIDIIN].Dev = dev->Dev;
		SoundDev[DEVNUM_MIDIIN].SubDev = dev->SubDev;
		return 1;
	}

	return 0;
}

static unsigned char prompt_retry(register const char * str)
{
	GuiLoop = GuiErrShow(GuiApp, str, (AppFlags2 & APPFLAG2_TIMED_ERR) ? GUIBTN_ESC_SHOW|GUIBTN_TIMEOUT_SHOW|GUIBTN_IGNORE_SHOW|GUIBTN_RETRY_SHOW|GUIBTN_RETRY_DEFAULT :
		 GUIBTN_ESC_SHOW|GUIBTN_IGNORE_SHOW|GUIBTN_RETRY_SHOW|GUIBTN_RETRY_DEFAULT);
	return (GuiLoop == GUIBTN_RETRY);
}

/********************* loadDeviceConfig() ********************
 * Called by the main thread to load the "Devices" config file
 * from the Preferences directory, and verify the existence of
 * those devices. The devices aren't opened yet. They are only
 * verified.
 *
 * If an error, displays an error msg.
 */

#define DEVLOAD_AUDIOIN		(0x01 << DEVNUM_AUDIOIN)
#define DEVLOAD_AUDIOOUT	(0x01 << DEVNUM_AUDIOOUT)
#define DEVLOAD_MIDIOUT1	(0x01 << DEVNUM_MIDIOUT1)
#define DEVLOAD_MIDIOUT2	(0x01 << DEVNUM_MIDIOUT2)
#define DEVLOAD_MIDIOUT3	(0x01 << DEVNUM_MIDIOUT3)
#define DEVLOAD_MIDIOUT4	(0x01 << DEVNUM_MIDIOUT4)
#define DEVLOAD_MIDIIN		(0x01 << DEVNUM_MIDIIN)

static const char DevicesName[] = "Devices";

void loadDeviceConfig(void)
{
	unsigned char					deviceLoadFlags, seqLoadFlags;
	{
	register unsigned char *	ptr;
	register const unsigned char *	endptr;

	// Open the file if it exists
	endptr = TempBuffer + load_config_file(&DevicesName[0]);
	ptr = TempBuffer;
	if (ptr < endptr) WavesLoadedFlag = 1;

	seqLoadFlags = deviceLoadFlags = 0;

	while (ptr < endptr)
	{
		register unsigned char	devnum;

		devnum = ptr[0];
		if (&ptr[8] > endptr || devnum > DEVNUM_MIDIIN || ptr[1] > DEVTYPE_SEQ)
		{
			if (devnum > DEVNUM_MIDIIN)
			{
				endptr = (unsigned char *)"#";
				goto badnum;
			}
			devnum = ptr[1];
			if (devnum > DEVTYPE_SEQ)
			{
				endptr = (unsigned char *)"type";
				ptr++;
badnum:		sprintf((char *)TempBuffer, "Devices preferences file has an illegal device %s %u at %u.", endptr, devnum, (uint32_t)(ptr - TempBuffer));
			}
			else
				sprintf((char *)TempBuffer, "Devices preferences file is missing %u bytes at %u.", 8 - (uint32_t)(endptr - ptr), (uint32_t)(endptr - TempBuffer));
			show_msgbox((char *)TempBuffer);
			break;
		}

		// Store DEVTYPE_NONE/AUDIO/MIDI/SEQ
		SoundDev[devnum].DevFlags = (SoundDev[devnum].DevFlags & ~DEVFLAG_DEVTYPE_MASK) | ptr[1];

		if (ptr[1])
		{
			SoundDev[devnum].DevHash = getLong(&ptr[2]);
			SoundDev[devnum].Dev = ptr[6];
			SoundDev[devnum].SubDev = ptr[7];

			if (devnum >= DEVNUM_MIDIOUT1 && devnum <= DEVNUM_MIDIOUT4)
			{
				if (ptr[1] == DEVTYPE_SEQ)
				{
#ifndef NO_MIDI_IN_SUPPORT
seq:
#endif
#ifndef NO_SEQ_SUPPORT
					// If a client name supplied, we must get its alsa seq client id #
					if (SoundDev[devnum].DevHash) seqLoadFlags |= (0x01 << devnum);
#else
					goto none;
#endif
				}
				else
				{
#ifdef NO_MIDI_OUT_SUPPORT
					goto none;
#endif
load:				deviceLoadFlags |= (0x01 << devnum);
				}

				// Temporarily save the type for below, and also set Card != -1
card:			SoundDev[devnum].Card = ptr[1];
			}

			else switch (devnum)
			{
				case DEVNUM_AUDIOOUT:
				{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
					// Jack hash == 0
					if (SoundDev[DEVNUM_AUDIOOUT].DevHash) goto load;
					goto card;
#else
					break;
#endif
				}

				case DEVNUM_AUDIOIN:
				{
#ifndef NO_ALSA_AUDIO_SUPPORT
					goto load;
#else
					break;
#endif
				}

				case DEVNUM_MIDIIN:
				{
#ifndef NO_MIDI_IN_SUPPORT
					if (ptr[1] == DEVTYPE_SEQ) goto seq;
					goto load;
#else
					break;
#endif
				}
			}
		}
#if defined(NO_SEQ_SUPPORT) || defined(NO_MIDI_OUT_SUPPORT)
none:
#endif
		ptr += 8;
	}
	}

	// ===========================================
	// Now that we've loaded all the previously saved hash #'s for the
	// devices, let's look through all the alsa card/device names for a
	// matching hash, and save the card's alsa card #. We assume that the
	// device/sub-dev #'s never change for pcm/rawmidi (but not seq)
	{
	snd_ctl_card_info_t *		cardInfo;
	int								cardNum;

	// We need to get a snd_ctl_card_info_t. Just alloc it on the stack
	snd_ctl_card_info_alloca(&cardInfo);

retry_card:
	cardNum = -1;
	while (deviceLoadFlags)
	{
		snd_ctl_t				*cardHandle;
		char						str[16];

		// Get next sound card's card number. When "cardNum" == -1, then ALSA
		// fetches the first card
		if (snd_card_next(&cardNum) < 0 ||

			// No more cards? ALSA sets "cardNum" to -1 if so
			cardNum < 0)
		{
			break;
		}

		// Open this card's control interface. We specify only the card number -- not any device nor sub-device too
		sprintf(str, &CardCtlSpec[0], cardNum);
		if (snd_ctl_open(&cardHandle, str, 0) >= 0)
		{
			// Tell ALSA to fill in our snd_ctl_card_info_t with info about this card
			if (snd_ctl_card_info(cardHandle, cardInfo) >= 0)
			{
				register uint32_t		hash, devnum;

				// Get card's ID (name) hash
				hash = hash_string((const unsigned char *)snd_ctl_card_info_get_id(cardInfo));

				devnum = 7;
				while (devnum--)
				{
					if ((deviceLoadFlags & (0x01 << devnum)) && hash == SoundDev[devnum].DevHash)
					{
						SoundDev[devnum].Card = cardNum;
						deviceLoadFlags &= ~(0x01 << devnum);
					}
				}
			}

			snd_ctl_close(cardHandle);
		}
	}
	}

	{
	register unsigned char	devnum;

	devnum = 7;
	while (GuiLoop != GUIBTN_QUIT && deviceLoadFlags && devnum--)
	{
		if (deviceLoadFlags & (0x01 << devnum))
		{
			register const char *	str;

			switch (devnum)
			{
				case DEVNUM_AUDIOIN:
					str = "Can't find your Microphone input. Make sure it's on.";
					break;
				case DEVNUM_AUDIOOUT:
					str = "Can't find your Internal Synth's hardware device. Make sure it's on, and JACK isn't running.";
					break;
				case DEVNUM_MIDIIN:
					str = "Can't find your MIDI controller. Make sure it's on.";
					break;
				default:
					str = (char *)TempBuffer;
					sprintf((char *)str, "Can't find your hardware device for External Synth %c.", devnum + ('1' - DEVNUM_MIDIOUT1));
			}
			if (prompt_retry(str)) goto retry_card;
			if (GuiLoop == GUIBTN_QUIT) goto done;
			deviceLoadFlags &= ~(0x01 << devnum);
			SoundDev[devnum].Card = -1;
		}
	}
	}

#ifndef NO_SEQ_SUPPORT
	if (seqLoadFlags)
	{
retry_in:
		if ((seqLoadFlags & (0x01 << DEVNUM_MIDIIN)) && findSeqClient(&matchSeqInHash, DEVFLAG_INPUTDEV))
		{
			if (prompt_retry("Can't find the MIDI software sending data to BackupBand. Make sure the program is running, then click retry.")) goto retry_in;
			if (GuiLoop == GUIBTN_QUIT) goto done;
			SoundDev[DEVNUM_MIDIIN].Card = -1;
		}
		seqLoadFlags &= ~(0x01 << DEVNUM_MIDIIN);

retry_out:
		if (seqLoadFlags && findSeqClient(&matchSeqOutHash, 0))
		{
			register struct SOUNDDEVINFO *	sounddev;

			sounddev = &SoundDev[DEVNUM_MIDIOUT1];
			do
			{
				if (sounddev->Card == DEVTYPE_SEQ && sounddev->DevHash)
				{
					sprintf((char *)TempBuffer, "Can't find the External Synth %c software synth. Make sure the software is running, then click retry.",
					((int32_t)(sounddev - &SoundDev[DEVNUM_MIDIOUT1])) + '1');
					if (prompt_retry((char *)TempBuffer)) goto retry_out;
					sounddev->Card = -1;
				}
			} while (GuiLoop != GUIBTN_QUIT && ++sounddev <= &SoundDev[DEVNUM_MIDIOUT4]);
		}
	}
#endif
done:
	// ALSA allocates some mem to load its config file when we call some of the
	// above functions. Now that we're done getting the info, let's tell ALSA
	// to unload the info and free up that mem
	snd_config_update_free_global();
}




static unsigned char * store_dev(register unsigned char * buffer, register unsigned char devnum)
{
	register struct SOUNDDEVINFO * sounddev;

	sounddev = &SoundDev[devnum];
	*buffer++ = devnum;
	*buffer++ = sounddev->DevFlags & DEVFLAG_DEVTYPE_MASK;
	storeLong(sounddev->DevHash, buffer);
	buffer += 4;
	*buffer++ = sounddev->Dev;
	*buffer++ = sounddev->SubDev;
	return buffer;
}

void saveDeviceConfig(void)
{
	register unsigned char *	buffer;

	if (SaveConfigFlag & SAVECONFIG_DEVICES)
	{
		buffer = TempBuffer;

#if !defined(NO_JACK_SUPPORT) || !defined(NO_ALSA_AUDIO_SUPPORT)
		// Write audio out dev name
		if	(SoundDev[DEVNUM_AUDIOOUT].DevFlags & DEVFLAG_DEVTYPE_MASK)
			buffer = store_dev(buffer, DEVNUM_AUDIOOUT);
#endif
#ifndef NO_MIDI_IN_SUPPORT
		// Write MIDI In name. off is the default
		if (SoundDev[DEVNUM_MIDIIN].DevFlags & DEVFLAG_DEVTYPE_MASK)
			buffer = store_dev(buffer, DEVNUM_MIDIIN);
#endif
		// Write External Synth names. None is the def
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
		{
		register unsigned char devnum;

		for (devnum = DEVNUM_MIDIOUT1; devnum <= DEVNUM_MIDIOUT4; devnum++)
		{
			if (SoundDev[devnum].DevFlags & DEVFLAG_DEVTYPE_MASK)
				buffer = store_dev(buffer, devnum);
		}
		}
#endif
#ifndef NO_ALSA_AUDIO_SUPPORT
		// Write audio in dev name
		if (SoundDev[DEVNUM_AUDIOIN].DevFlags & DEVFLAG_DEVTYPE_MASK)
			buffer = store_dev(buffer, DEVNUM_AUDIOIN);
#endif
		{
		register int				fh;

		if ((fh = create_config_file(&DevicesName[0])) == -1)
			show_msgbox(&DevicesName[0]);
		else
		{
			register uint32_t		len, written;

			len = (buffer - &TempBuffer[0]);
			written = write(fh, &TempBuffer[0], len);
			close(fh);

			if (written != len)
			{
				// Delete file
				unlink(GuiBuffer);

				show_msgbox("Error saving device configuration");
			}
			else
				SaveConfigFlag &= ~SAVECONFIG_DEVICES;
		}
		}
	}
}





/****************** openConfigDevs() *****************
 * Opens MIDI/audio devices at program start. Called by
 * main thread
 *
 * NOTE: Displays any error msg.
 *
 * May set GuiLoop = GUIBTN_QUIT if user clicks
 * window close box in response to error msg.
 *
 * loadDeviceConfig() should have been called prior.
 */

void openConfigDevs(void)
{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
	// Open specified audio in/out devs
	allocAudio();
#endif

	// Open MIDI In/Out devs
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	{
	register uint32_t devnum;

	devnum = 4;
	while (GuiLoop != GUIBTN_QUIT && devnum)
		openMidiOut(&SoundDev[--devnum + DEVNUM_MIDIOUT1]);
	}
#endif

#ifndef NO_MIDI_IN_SUPPORT
	if (GuiLoop != GUIBTN_QUIT) openMidiIn();
#endif
}





/********************* loadAudioConfig() *******************
 * Called by loadConfigOther() to process audio hardware related
 * data from the config file.
 *
 * ptr =		Data to parse.
 * len =		# of bytes.
 *
 * RETURN: # of bytes parsed, 0 if unknown opcode, -1 if truncated.
 */

int loadAudioConfig(register unsigned char * ptr, register unsigned long len)
{
	if (ptr[0] >= CONFIGKEY_DRUMSVOL && ptr[0] <= CONFIGKEY_DRUMSVOL + PLAYER_SOLO)
	{
		VolAdjust[ptr[0] - CONFIGKEY_DRUMSVOL] = ptr[1];
		goto ret1;
	}

	// Busses for DevAssigns[DEVNUM_AUDIOOUT] to DevAssigns[DEVNUM_MIDIOUT4]
	if (ptr[0] >= CONFIGKEY_BUSS && ptr[0] <= CONFIGKEY_BUSS + PLAYER_SOLO)
	{
		len = ptr[1];
#if defined(NO_ALSA_AUDIO_SUPPORT) && defined(NO_JACK_SUPPORT)
		if (len < DEVNUM_MIDIOUT1) len = DEVNUM_MIDIOUT1;
#elif defined(NO_MIDI_OUT_SUPPORT) && defined(NO_SEQ_SUPPORT)
		if (len >= DEVNUM_MIDIOUT1 && len <= DEVNUM_MIDIOUT4) len = DEVNUM_AUDIOOUT;
#endif
		DevAssigns[ptr[0] - CONFIGKEY_BUSS] = len <= DEVNUM_MIDIOUT4 ? &SoundDev[len] : 0;
		goto ret1;
	}

	switch (*ptr++)
	{
		case CONFIGKEY_FRAMES:
#ifndef NO_ALSA_AUDIO_SUPPORT
			setFrameSize(ptr[0]);
#endif
ret1:		return 1;
		case CONFIGKEY_SAMPLERATE:
#ifndef NO_ALSA_AUDIO_SUPPORT
			setSampleRateFactor(ptr[0]);
#endif
			goto ret1;
		case CONFIGKEY_MASTERVOL:
			MasterVolAdjust = ptr[0];
			goto ret1;
		case CONFIGKEY_REVVOL:
#ifndef NO_REVERB_SUPPORT
			setReverbVol(ptr[0]);
#endif
			goto ret1;
	}

	return 0;
}







// ====================== Instrument enumeration ========================

void * getNextInstrument(register void * pgmPtr, register uint32_t roboNum)
{
	if (!pgmPtr)
	{
		if (roboNum == PLAYER_SOLO && !(InstrumentLists[PLAYER_SOLO] = InstrumentLists[PLAYER_PAD]))
			InstrumentLists[PLAYER_SOLO] = InstrumentLists[PLAYER_GTR];
		pgmPtr = InstrumentLists[roboNum];
	}
	else
	{
		if (!(pgmPtr = ((INS_INFO *)pgmPtr)->Next) && roboNum == PLAYER_SOLO && InstrumentLists[PLAYER_SOLO] != InstrumentLists[PLAYER_GTR])
			pgmPtr = InstrumentLists[PLAYER_SOLO] = InstrumentLists[PLAYER_GTR];
	}
	return pgmPtr;
}

const char * isHiddenPatch(register void * pgmPtr, register uint32_t roboNum)
{
	register const char *	name;

	name = ((INS_INFO *)pgmPtr)->Name;
	if (!roboNum)
	{
		if (!(*name)) goto hide;
	}
	else if (((INS_INFO *)pgmPtr)->Sub.Patch.Flags & INSFLAG_HIDDEN)
	{
hide:	name = 0;
	}
	return name;
}

const char * getInstrumentName(register void * pgmPtr)
{
	return ((INS_INFO *)pgmPtr)->Name;
}

void * getCurrentInstrument(register uint32_t roboNum)
{
	return CurrentInstrument[roboNum];
}





unsigned char getNumOfInstruments(register unsigned char roboNum)
{
	return NumOfInstruments[roboNum];
}




/*********************** setInstrumentByNum() *********************
 * Changes the specified player's instrument to the specified
 * MIDI pgm number, or 0-based index.
 *
 * roboNum =	PLAYER_xxx. OR'ed with SETINS_BYINDEX if "pgmNum"
 * 				is a 0-based index rather than MIDI Pgm Change #.
 * 				For 0-based index, OR'd with SETINS_SHOWHIDDEN if
 * 				hidden Instruments should be displayed. For MIDI #,
 * 				bits 8 to 15 are the bank MSB to match or SETINS_NO_MSB.
 * 				Bits 16 to 23 are the bank LSB, or SETINS_NO_LSB.
 * pgmNum =		Pgm Number/Index.
 * ptr =			Where to return the INS_INFO * (for use with
 * 				getInstrumentName() and other such funcs passed a INS_INFO),
 * 				or 0 if not needed. If needed, then the instrument isn't
 * 				actually changed. Only the INS_INFO is returned.
 */

uint32_t setInstrumentByNum(register uint32_t roboNum, register unsigned char pgmNum, void ** ptr)
{
	register unsigned char	num;
	unsigned char				threadId;

	num = ((unsigned char)roboNum & 0x1F);
	threadId = ((unsigned char)roboNum & 0xE0);

	// Is this robot on?
	if (DevAssigns[num])
	{
		register INS_INFO *	patch;
		register INS_INFO *	best;

		best = 0;

		// Human solo borrows from pad and gtr instrument lists
		patch = InstrumentLists[num == PLAYER_SOLO ? PLAYER_PAD : num];
again:
		if (patch)
		{
			if (roboNum & SETINS_BYINDEX)
			{
				if (roboNum & SETINS_SHOWHIDDEN)
				{
					do
					{
						if (!pgmNum--) goto got_it;
					} while ((patch = patch->Next));
				}
				else if (!num)
				{
					do
					{
						if (patch->Name[0] && !pgmNum--) goto got_it;
					} while ((patch = patch->Next));
				}
				else
				{
					do
					{
						if (!(patch->Sub.Patch.Flags & INSFLAG_HIDDEN) && !pgmNum--) goto got_it;
					} while ((patch = patch->Next));
				}
			}
			else
			{
				register unsigned char bankMSB;

				// Drums ignore bank
				bankMSB = (num == PLAYER_DRUMS) ? 0x80 : ((unsigned char)(roboNum >> 8)) & 0xFF;

				if (num <= PLAYER_SOLO)
				{
					// Use an articulation of whatever the current pgm is?
					if (pgmNum == 0xFF && CurrentInstrument[num])
					{
						pgmNum =  CurrentInstrument[num]->PgmNum;
					}

					// First see if the current or prev selected instrument match. This eliminates the
					// need to search the list
					if (PrevInstrument[num] && PrevInstrument[num]->PgmNum == pgmNum &&
						(((PrevInstrument[num]->Sub.Patch.BankMsb | bankMSB) & 0x80) ||
						PrevInstrument[num]->Sub.Patch.BankMsb == bankMSB))
					{
						patch = PrevInstrument[num];
						goto got_it;
					}
				}

				do
				{
					if (patch->PgmNum == pgmNum)
					{
						// If instrument's bank is unspecified, or caller specified SETINS_NO_MSB, then match only pgm #
						if (((patch->Sub.Patch.BankMsb | bankMSB) & 0x80) ||

							// Otherwise match bank msb
							patch->Sub.Patch.BankMsb == bankMSB)
						{
got_it:					if (!ptr)
							{
								if (lockInstrument(threadId) == threadId)
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
									roboNum = setInstrumentByPtr(patch, roboNum, threadId);
#else
									roboNum = setInstrumentByPtr(patch, roboNum);
#endif
								else
									roboNum = CTLMASK_NONE;
								unlockInstrument(threadId);
								return roboNum;
							}
							*ptr = patch;
							return 1;
						}

						// Pgm # matches, so save this as best match
						if (!best) best = patch;
					}
				} while ((patch = patch->Next));
			}

			if (num == PLAYER_SOLO)
			{
				num = 0xFF;
				patch = InstrumentLists[PLAYER_GTR];
				goto again;
			}

			if ((patch = best)) goto got_it;
		}
	}

	return CTLMASK_NONE;
}





/**************** setInstrumentByPtr() ****************
 * patch =	Ptr to INS_INFO to set as the current
 * 			instrument. If 0, sets the previously
 * 			selected instrument.
 *
 * NOTE: Caller must lockInstrument()/unlock.
 */

#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
uint32_t setInstrumentByPtr(register void * patch, register uint32_t roboNum, register unsigned char threadId)
#else
uint32_t setInstrumentByPtr(register void * patch, register uint32_t roboNum)
#endif
{
	register uint32_t		num;

	num = ((unsigned char)roboNum & 0x1F);

	if (patch || (patch = PrevInstrument[num]))
	{
		if (patch != CurrentInstrument[num])
		{
			PrevInstrument[num] = CurrentInstrument[num];
			CurrentInstrument[num] = patch;
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
			if (DevAssigns[num] >= &SoundDev[DEVNUM_MIDIOUT1])
			{
				send_patch(num, ((INS_INFO *)patch)->PgmNum,
							threadId | (((uint32_t)((INS_INFO *)patch)->Sub.Patch.BankLsb) << 16)
							| (((uint32_t)((INS_INFO *)patch)->Sub.Patch.BankMsb) << 8));
			}
#endif
			if (num == PLAYER_PAD) return CTLMASK_BACKINGPAD;
			return (CTLMASK_DRUMLIST << num);
		}
	}

	return CTLMASK_NONE;
}



/******************** lockInstrument() ********************
 * Arbitrates thread access to starting/stopping play.
 */

static unsigned char InstrumentLock = 0;

unsigned char lockInstrument(register unsigned char threadId)
{
	return __atomic_or_fetch(&InstrumentLock, threadId, __ATOMIC_RELAXED);
}

void unlockInstrument(register unsigned char threadId)
{
	__atomic_and_fetch(&InstrumentLock, ~threadId, __ATOMIC_RELAXED);
}


uint32_t findSoloInstrument(register char match)
{
	register INS_INFO *		patch;
	register unsigned char	roboNum;

	if ((patch = CurrentInstrument[PLAYER_SOLO]))
	{
		while ((patch = patch->Next) && !(patch->Sub.Patch.Flags & INSFLAG_HIDDEN))
		{
			if (patch->Name[0] == match) goto got_it;
		}
	}

	roboNum = PLAYER_PAD;
again:
	if ((patch = InstrumentLists[roboNum]))
	{
		do
		{
			if (patch->Name[0] == match && !(patch->Sub.Patch.Flags & INSFLAG_HIDDEN))
			{
				register uint32_t	mask;

got_it:		mask = CTLMASK_NONE;
				if (lockInstrument(GUITHREADID) == GUITHREADID)
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
					mask = setInstrumentByPtr(patch, PLAYER_SOLO, GUITHREADID);
#else
					mask = setInstrumentByPtr(patch, PLAYER_SOLO);
#endif
				unlockInstrument(GUITHREADID);
				return mask;
			}
		} while ((patch = patch->Next));
	}

	if (roboNum != PLAYER_GTR)
	{
		roboNum = PLAYER_GTR;
		goto again;
	}

	return CTLMASK_NONE;
}
