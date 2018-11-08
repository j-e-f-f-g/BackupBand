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
#include "PickDevice.h"
#include "MidiIn.h"
#include "AudioPlay.h"
#include "AccompSeq.h"
#include "Setup.h"
#include "Prefs.h"
#include "FileLoad.h"
#include "SongSheet.h"
#include "StyleData.h"

//#define DISABLE_CHORDS_ON_PANIC


static void update_chord(void);
static uint32_t do_countoff(register void *);
static uint32_t get_hw_clock(void);
#ifndef NO_MIDICLOCK_IN
static void wait_for_midiclock(void);
#endif

#ifdef MIDIFILE_PLAYBACK
// MIDI File header for custom MIDI files
#pragma pack(1)
typedef struct {
	unsigned char	Tempo;		// Tempo in BPM
	unsigned char	Kit;			// pgm #
	unsigned char	Bass;			// pgm #
	unsigned char	Gtr;			// pgm #
} MIDIFILEHDR;

// For playing back the MIDI file
typedef struct {
	unsigned char	*TrackStart;	// Points to start of trk data, prefaced with MIDIFILEHDR
	uint32_t			SongLen;			// Size of data in bytes
} TRACKING_INFO;

#pragma pack()
#endif

// The Beat Playback thread handle
static pthread_t			PlayThreadHandle = 0;
static pthread_mutex_t	PlayMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	PlayCondition = PTHREAD_COND_INITIALIZER;

#ifdef MIDIFILE_PLAYBACK
// To load a MIDI File
static TRACKING_INFO		Tracking = {0};

// To temp hold a Midi msg
static unsigned char		MidiMsg[3];
#endif

// Used by Midi In and Beat Play threads to arbitrate access to
// variables used to signal the main thread
static volatile int		GuiThreadLock = 0;
static uint32_t			MidiGuiUpdate = 0;

// Set to INPLAY_RUNNING during Beat Playback. Also used to alert the playback thread
// to do things
unsigned char				BeatInPlay;

// Flag bits for beat playback thread. See PLAYFLAG_ #define
unsigned char				PlayFlags;

// Alternating crash cymbal
static unsigned char		PrevCrashCymbal;

// The midi chan upon which we look for incoming midi msgs for Drums/Bass/Gtr/Pad/Solo,
// and upon which we send msgs
unsigned char				MidiChans[5];
static const unsigned char	MidiChansDef[5] = {9,1,2,3,0};

// For timing the playback of events
static uint32_t			MsecsPerTick;	// Milliseconds per seq tick at current tempo
static uint32_t			CurrentClock;
#ifndef NO_MIDICLOCK_IN
static uint32_t			TimeoutClock;
#endif
static unsigned char		Clocks[] = {CLOCK_MONOTONIC, CLOCK_MONOTONIC_RAW, CLOCK_MONOTONIC_COARSE, 0xFE};
static unsigned char		ClockId = CLOCK_MONOTONIC;
static unsigned char		TempoChangeCnt;
static unsigned char		TempoBPM;
static unsigned char		PrevTempoBPM = 120;

// For transposing by half steps
#ifdef GIGGING_DRUMS
static char					Transpose = -2;
static char					ConfigTranspose = -2;
#else
static char					Transpose = 0;
static char					ConfigTranspose = 0;
#endif

#ifndef NO_MIDI_IN_SUPPORT
// Velocity curves
#ifdef GIGGING_DRUMS
unsigned char				VelCurve = 1;
#else
unsigned char				VelCurve = 0;
#endif
const unsigned char	Comp1VelCurve[127] = {32, 34, 36, 38, 40, 42, 44, 46, 48,
49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
59, 60, 61, 62, 63, 64, 64, 65, 65, 66,
66, 67, 67, 68, 68, 69, 69, 70, 70, 71,
71, 72, 72, 73, 73, 74, 74, 75, 75, 76,
76, 77, 77, 78, 78, 79, 79, 80, 80, 81,
81, 82, 82, 83, 83, 84, 84, 85, 85, 86,
86, 87, 87, 88, 88, 88, 89, 89, 89, 90,
90, 90, 91, 91, 91, 92, 92, 92, 93, 93,
93, 94, 94, 94, 95, 95, 95, 96, 96, 96,
97, 97, 97, 98, 98, 98, 99, 99, 99, 100,
100, 100, 101, 101, 101, 102, 102, 102, 103, 103,

103, 104, 104, 104, 105, 105, 106, 106,
};
#endif

// Backing pad velocity
unsigned char			ChordVel = 34;

unsigned char Sensitivity = 1;
unsigned char ChordTrigger = 0x90;

//====================================
// For left-hand accompaniment

// Points to the current scale for bass runs, and chords
static const unsigned char *	Scale;
static const unsigned char *	NextScale;

// Points to current playing event in gtr/bass ptn
const unsigned char *	GtrEvtPtr;
const unsigned char *	BassEvtPtr;

// Flag to add 6th, 7th, or 9th
#define	NOTEADD_NINTH			1
#define	NOTEADD_SEVENTH		2
#define	NOTEADD_SIXTH			4
static unsigned char	NoteAddMask;
static unsigned char	NextNoteAddMask;

// Root note (MIDI note number) of the currently playing chord. This is
// confined to the low notes of bass range
static unsigned char	RootNote, NextRootNote;

// Coordinates the timing of a chord change between MIDI In, and Play
// beat, threads
static unsigned char	ChordChangedCnt;
unsigned char			ChordPlayCnt;
unsigned char			ChordBoundary = 24*2;

// Organizes MIDI note #'s (of the currently playing chord) into an array
// from lowest to highest #
static unsigned char	NextChordNotes[12];
static unsigned char	NextNumNotes;

// midi note # currently assigned to ("fret" on) each of the 6 gtr strings
unsigned char			GtrStrings[6];

// Scale arrays
#define ROOT_TO_DIFF(a) (a << 20)
#define DIFF_TO_ROOT(a) ((a >> 20) & 0x0F)
#define MAJOR_SCALE	(0 << 24)
#define MINOR_SCALE	(1 << 24)
#define DOM7_SCALE	(2 << 24)
#define AUG_SCALE		(3 << 24)
#define DIM_SCALE		(4 << 24)
#define SUS_SCALE		(5 << 24)
#define MINOR2_SCALE	(6 << 24)
#define MAJOR2_SCALE	(7 << 24)
#define DIFF_TO_SCALE(a) (((a >> 24) & 0x0F) * 7)
#define TO_FLAGS(a) ((a >> 28) & 0x0F)
#define ADD_7 (NOTEADD_SEVENTH<<28)
#define ADD_9 (NOTEADD_NINTH<<28)

static const unsigned char Scales[] =	{0, 2, 4, 5, 7, 9, 11,	// The intervals of a Major scale
													 0, 2, 3, 5, 7, 8, 10,	// Minor
													 0, 2, 4, 5, 7, 9, 10,	// Dominent 7th
													 0, 2, 4, 6, 6, 9, 11,	// Augmented
													 0, 2, 3, 6, 6, 9, 11,	// Diminished
													 0, 2, 5, 5, 7, 9, 10,	// Suspended 4th
													 0, 1, 5, 7, 9, 10, 12,
													 0, 1, 6, 7, 9, 10, 12};

#ifndef NO_MIDI_IN_SUPPORT

// Our chord dictionary. Comments show an example chord where a C note is
// the lowest note played. To the right of the equal sign are the actual notes played
// by the user from lowest (C) to highest. To the left of the equal sign is the chord
// our dictionary produces. This is hard-wired. If we wanted to let the user change
// chord definition or add new chords, these uint32 "chords" would need to go into
// into a growable non-const array. See analyze_chord() for how this works

static const uint32_t ChordDictionary[] = {
// C Db = C/D
0x00000001 | ROOT_TO_DIFF(0) /* added to low note to calc root note */ | MAJOR2_SCALE,
// C D  = C/Dm
0x00000002 | ROOT_TO_DIFF(0) | MINOR2_SCALE,
// C Eb = Cm
0x00000003 | ROOT_TO_DIFF(0) | MINOR_SCALE,
// C E = C
0x00000004 | ROOT_TO_DIFF(0) | MAJOR_SCALE,
// C F = F
0x00000005 | ROOT_TO_DIFF(5) | MAJOR_SCALE,
// C F# = C aug
0x00000006 | ROOT_TO_DIFF(0) | AUG_SCALE,
// C G = C
0x00000007 | ROOT_TO_DIFF(0) | MAJOR_SCALE,
// C Ab = Ab
0x00000008 | ROOT_TO_DIFF(8) | MAJOR_SCALE,
// C A = Am
0x00000009 | ROOT_TO_DIFF(9) | MINOR_SCALE,
// Bb C = Bb/Cm
0x0000000A | ROOT_TO_DIFF(10) | MINOR2_SCALE,
// B C = B/C#
0x0000000B | ROOT_TO_DIFF(11) | MAJOR2_SCALE,
// C Db F = DbM7
0x00000015 | ROOT_TO_DIFF(1) | MAJOR_SCALE | ADD_7,
// C E F = FM7
0x00000018 | ROOT_TO_DIFF(1) | MAJOR_SCALE | ADD_7,
// C D Eb = Cm9
0x00000023 | ROOT_TO_DIFF(0) | MINOR_SCALE | ADD_7 | ADD_9,
// C D E = CM9
0x00000024 | ROOT_TO_DIFF(0) | MAJOR_SCALE | ADD_7 | ADD_9,
// C D F = Dm7
0x00000025 | ROOT_TO_DIFF(2) | MINOR_SCALE | ADD_7,
// C D F# = D7
0x00000026 | ROOT_TO_DIFF(2) | DOM7_SCALE | ADD_7,
// C D G = Gsus
0x00000027 | ROOT_TO_DIFF(7) | SUS_SCALE,
// C D A = D7
0x00000029 | ROOT_TO_DIFF(2) | DOM7_SCALE | ADD_7,
// C D Bb = BbM9
0x0000002A | ROOT_TO_DIFF(10) | MAJOR_SCALE | ADD_7 | ADD_9,
// C Eb F = F7
0x00000035 | ROOT_TO_DIFF(5) | DOM7_SCALE | ADD_7,
// C Eb F# = C dim
0x00000036 | ROOT_TO_DIFF(0) | DIM_SCALE,
// C Eb G = Cm
0x00000037 | ROOT_TO_DIFF(0) | MINOR_SCALE,
// C Eb Ab = Ab
0x00000038 | ROOT_TO_DIFF(8) | MAJOR_SCALE,
// C Eb A = A dim
0x00000039 | ROOT_TO_DIFF(9) | DIM_SCALE,
// C Eb Bb = Cm7
0x0000003A | ROOT_TO_DIFF(0) | MINOR_SCALE | ADD_7,
// E G# A = AM7
0x00000045 | ROOT_TO_DIFF(5) | MAJOR_SCALE | ADD_7,
// C E G = C
0x00000047 | ROOT_TO_DIFF(0) | MAJOR_SCALE,
// C E A = Am
0x00000049 | ROOT_TO_DIFF(9) | MINOR_SCALE,
// C E Bb = C7
0x0000004A | ROOT_TO_DIFF(0) | DOM7_SCALE | ADD_7,
// C E B = CM7
0x0000004B | ROOT_TO_DIFF(0) | MAJOR_SCALE | ADD_7,
// C F G = Csus
0x00000057 | ROOT_TO_DIFF(0) | SUS_SCALE,
// C F Ab = Fm
0x00000058 | ROOT_TO_DIFF(5) | MINOR_SCALE,
// C F A = F
0x00000059 | ROOT_TO_DIFF(5) | MAJOR_SCALE,
// C F Bb = Fsus
0x0000005A | ROOT_TO_DIFF(5) | SUS_SCALE,
// C Gb Ab = Ab7
0x00000068 | ROOT_TO_DIFF(8) | DOM7_SCALE | ADD_7,
// C F# A = F# dim
0x00000069 | ROOT_TO_DIFF(6) | DIM_SCALE,
// C G Ab = AbM7
0x00000078 | ROOT_TO_DIFF(8) | MAJOR_SCALE | ADD_7,
// C G A = Am
0x00000079 | ROOT_TO_DIFF(9) | MINOR_SCALE | ADD_7,
// C G Bb = C7
0x0000007A | ROOT_TO_DIFF(0) | DOM7_SCALE | ADD_7,
// C G B = CM7
0x0000007B | ROOT_TO_DIFF(0) | MAJOR_SCALE | ADD_7,
// C D E = CM9
0x0000008A | ROOT_TO_DIFF(8) | MAJOR_SCALE | ADD_7 | ADD_9,
// C Db F Ab = DbM7
0x00000158 | ROOT_TO_DIFF(1) | MAJOR_SCALE | ADD_7,
// C Db F Bb = Bbm9
0x0000015A | ROOT_TO_DIFF(10) | MINOR_SCALE | ADD_7 | ADD_9,
// C D Eb G = Cm9
0x00000237 | ROOT_TO_DIFF(0) | ADD_7 | ADD_9,
// C D E G = CM9
0x00000247 | ROOT_TO_DIFF(0) | MAJOR_SCALE | ADD_7 | ADD_9,
// C D F Ab = D dim
0x00000258 | ROOT_TO_DIFF(2) | DIM_SCALE,
// C D F A = Dm7
0x00000259 | ROOT_TO_DIFF(2) | MINOR_SCALE | ADD_7,
// C D F Bb = BbM9
0x0000025A | ROOT_TO_DIFF(10) | MAJOR_SCALE | ADD_7 | ADD_9,
// C D F# A = D7
0x00000269 | ROOT_TO_DIFF(2) | DOM7_SCALE | ADD_7,
// C Eb F Ab = Fm7
0x00000358 | ROOT_TO_DIFF(5) | MINOR_SCALE | ADD_7,
// C Eb F A = F7
0x00000359 | ROOT_TO_DIFF(5) | DOM7_SCALE | ADD_7,
// C Eb Gb Ab = Ab7
0x00000368 | ROOT_TO_DIFF(8) | DOM7_SCALE | ADD_7,
// C Eb F# A = Eb dim
0x00000369 | ROOT_TO_DIFF(3) | DIM_SCALE,
// C Eb G Ab = AbM7
0x00000378 | ROOT_TO_DIFF(8) | MAJOR_SCALE | ADD_7,
// C Eb G Bb = Cm7
0x0000037A | ROOT_TO_DIFF(0) | MINOR_SCALE | ADD_7,
// C E F A = FM7
0x00000459 | ROOT_TO_DIFF(5) | MAJOR_SCALE | ADD_7,
// C E G A = Am7
0x00000479 | ROOT_TO_DIFF(9) | MINOR_SCALE | ADD_7,
// C E G Bb = C7
0x0000047A | ROOT_TO_DIFF(0) | DOM7_SCALE | ADD_7,
// C E G B = CM7
0x0000047B | ROOT_TO_DIFF(0) | MAJOR_SCALE | ADD_7,
// C F G Ab = Fm9
0x00000578 | ROOT_TO_DIFF(5) | MINOR_SCALE | ADD_7 | ADD_9,
// C F G A = FM9
0x00000579 | ROOT_TO_DIFF(5) | MAJOR_SCALE | ADD_7 | ADD_9,
// C D Eb G Bb = Cm9
0x0000237A | ROOT_TO_DIFF(0) | MINOR_SCALE | ADD_7 | ADD_9,
// C D E G B = CM9
0x0000247B | ROOT_TO_DIFF(0) | MAJOR_SCALE | ADD_7 | ADD_9,
// C Eb F G A = F9
0x00003579 | ROOT_TO_DIFF(5) | DOM7_SCALE | ADD_7 | ADD_9,
0xFFFFFFFF}; /* marks the end */

// C D F Ab = F min6
// C D G A = D sus 7
// C Eb G  A = C min

//====================================

#endif









/******************* songChordChange() ******************
 * Called by Beat Play thread when a chord change in a
 * song sheet.
 *
 */

void songChordChange(register unsigned char chord, register unsigned char flags)
{
	NextRootNote = 28 + 4 - 1 + (chord >> 4) + Transpose;
	NextScale = &Scales[(chord & 0x07) * 7];
	NextNoteAddMask = flags;
	if (NextRootNote != RootNote || NextScale != Scale || NextNoteAddMask != NoteAddMask) ++ChordPlayCnt;
}





/************ getCurrChord() *************
 * Called by main thread to get the root note
 * of the chord currently being played. Note
 * number is 0 to 11 for C to B, or 0xFF if no
 * chord playing.
 */

uint32_t getCurrChord(void)
{
	if (Scale)
		return (((uint32_t)NoteAddMask) << 16) | ((((uint32_t)(Scale - &Scales[0]))/7) << 8) | ((uint32_t)RootNote % 12);
	return 0xFF;
}




/********************* pickChord() ********************
 * Called by MIDI IN thread to change chord.
 */

uint32_t pickChord(register unsigned char rootNote, register unsigned char scale, register unsigned char threadId)
{
	if (lockChord(threadId) == threadId)
	{
		NextNoteAddMask = 0;
		if (scale >= 6)
		{
			scale -= 6;
			NextNoteAddMask = NOTEADD_SEVENTH;
			if (scale > 1)
			{
				NextNoteAddMask = NOTEADD_NINTH;
				scale -= 2;
			}
		}
		NextScale = &Scales[scale * 7];
		// Note: eventChordTrigger unlocks
		return eventChordTrigger(rootNote, 254, threadId);
	}

	unlockChord(threadId);
	return CTLMASK_NONE;
}





#ifdef MIDIFILE_PLAYBACK
/********************** playMidiThread() **********************
 * The thread that handles MIDI file playback. It queues each
 * MIDI event (to be played), counts down its delta time, and
 * tells the audio thread to play it.
 *
 * First byte in file is the BPM tempo. Second byte is the kit pgm #.
 * Third is the bass pgm #. Fourth is the gtr pgm #.
 *
 * Each event starts with a Timing byte referenced in 24 PPQN. If
 * 0xFF, then the next byte is also timing, and is added to it. Etc.
 *
 * The next byte is the note #. If 0, then this is a lyric event up
 * to a 0. If the high bit of note number is set, then this is a bass
 * guitar, or electric guitar note on or off. The note range tells
 * whether it's bass or electric guitar. Note numbers above G3 are
 * guitar. If the high bit is not set, then this is a drum note #.
 *
 * The next byte is the note velocity. Only Note On events are stored
 * for Drums. If the high bit of velocity is set for drums, this means
 * to lessen the initial attack on the note (by skipping beginning
 * samples).  For bass/guitar, the velocity tells whether it's a note
 * on or off (0 velocity is off).
 */

static void * playMidiThread(void * arg)
{
	register unsigned char	*trk;
	register uint32_t			currentEvtTime;

	arg = GuiWinSignal(GuiApp, 0, 0);

	// No L.H.chords
	TempFlags |= (APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NOPAD);
	guimask = mute_playing_chord(BEATTHREADID);

	// Get trk start
	trk = Tracking.TrackStart;

	// Set MsecsPerTick based upon BPM tempo
	set_PPQN(*trk++);

	// Get the kit/bass/gtr
	setInstrumentByNum(PLAYER_DRUMS | SETINS_NO_MSB | SETINS_NO_LSB | BEATTHREADID, *trk++, 0);
	setInstrumentByNum(PLAYER_BASS | SETINS_NO_MSB | SETINS_NO_LSB | BEATTHREADID, *trk++, 0);
	setInstrumentByNum(PLAYER_GTR | SETINS_NO_MSB | SETINS_NO_LSB | BEATTHREADID, *trk++, 0);

	do_countoff();
	if (PlayFlags & PLAYFLAG_STOP) goto stopme;

	// ==========================================================================
	do
	{
		// Get the timing of next evt
		{
		register uint32_t			value;

		if ((value = *trk++) == 0xFF)
		{
			do
			{
				value += *trk;
			} while (*trk++ == 0xFF);
		}
		currentEvtTime = value;
		}

		// Do we need to count off some timing first? See do_countoff()
		if (currentEvtTime)
		{
			if (ClockId >= 0xFE)
				wait_for_midiclock(currentEvtTime);
			else do
			{
				register uint32_t			target_msecs;

				target_msecs = CurrentClock + MsecsPerTick;
				usleep(MsecsPerTick*900);
				while ((CurrentClock = get_hw_clock()) < target_msecs) sched_yield();
			} while (--currentEvtTime);
		}

		// End of song?
		if (trk >= Tracking.TrackStart + Tracking.SongLen)
		{
			// No more evts. The MIDI file is done playing
stopme:	signal_stop(arg);
			break;
		}

		// User wants to silence bass/guitar/pad?
		if (BeatInPlay & (INPLAY_USERMUTEBASS|INPLAY_USERMUTEGTR||INPLAY_USERMUTEPAD))
		{
			if (BeatInPlay & INPLAY_USERMUTEBASS) stopAllBassNotes(BEATTHREADID);
			if (BeatInPlay & INPLAY_USERMUTEGTR) stopGtrString(0, BEATTHREADID);
			if (BeatInPlay & INPLAY_USERMUTEPAD)
			{
				if (BeatInPlay & INPLAY_TURNING_OFF)
					changePadInstrument(BEATTHREADID);
				else
					fadePadVoices(14);
			}

			BeatInPlay &= ~(INPLAY_USERMUTEBASS|INPLAY_USERMUTEGTR|INPLAY_USERMUTEPAD|INPLAY_TURNING_OFF);
		}

		// Lyrics?
		if (!(MidiMsg[1] = *trk++))
		{

		}

		// Gtr/Bass?
		else if (MidiMsg[1] & 0x80)
		{
			MidiMsg[1] &= 0x7F;
			MidiMsg[2] = *trk++;
			if (!(TempFlags & APPFLAG3_NOBASS))
			{
				if (MidiMsg[1] <= 56)
				{
					MidiMsg[1] += Transpose;
					if (MidiMsg[2])
						startBassNote(MidiMsg[1], MidiMsg[2], BEATTHREADID);
					else
						stopBassNote(MidiMsg[1], BEATTHREADID);
				}
				else
				{
					MidiMsg[1] += Transpose;
// TODO: Guitar
				}
			}
		}

		// Drums
		else
			startDrumNote(0, MidiMsg[1], *trk++, BEATTHREADID);

		// Keep going until main thread aborts us
	} while (PlayThreadHandle);

	GuiWinSignal(GuiApp, arg, 0);

	return 0;
}

#endif





/********************** do_countoff() **********************
 * Counts off 3 or 4 beat lead-in, displaying them in main
 * window.
 */

static uint32_t do_countoff(register void * signalHandle)
{
	register uint32_t				refreshGuiMask;
	register unsigned char		countoff;

	// Get initial time, to be used as a start reference, if internal clock. If
	// midi clock, wait for the next incoming F8 to sync TimeoutClock
#ifndef NO_MIDICLOCK_IN
	if (ClockId >= 0xFE)
	{
		while (!(TimeoutClock = CurrentClock)) sched_yield();
	}
	else
#endif
		CurrentClock = get_hw_clock();

	// If playing a songsheet, issue non-note events at time 0
#ifndef NO_SONGSHEET_SUPPORT
	refreshGuiMask = songStart();
#else
	refreshGuiMask = 0;
#endif
	TempoChangeCnt = 0;

	// If "Setup -> Robots -> Autostart -> Auto" enabled, turn off autostart temporarily
	if (AppFlags3 & APPFLAG3_AUTOSTARTARM)
	{
		TempFlags &= ~TEMPFLAG_AUTOSTART;
		refreshGuiMask |= CTLMASK_AUTOSTART;
	}

	// Do a countoff?
//	if (setCountOff(0))
	{
		// Count off 3 or 4 beats
		while (setCountOff(signalHandle, -1))
		{
			// Do something useful while waiting. Queue events if needed
			if (!isStyleQueued()) refreshGuiMask |= update_play_style(BEATTHREADID);

			// Check user changing chord
			update_chord();
			if ((refreshGuiMask |= lightPianoKey(getCurrChord())))
			{
				// Signal main if any gui update
				refreshGuiMask = drawGuiCtl(signalHandle, refreshGuiMask, BEATTHREADID);
			}

			// Wait for a quarter note duration
			countoff = PPQN_VALUE;
			do
			{
#ifndef NO_MIDICLOCK_IN
				if (ClockId >= 0xFE)
					wait_for_midiclock();
				else
#endif
				{
					register uint32_t			target_msecs;

					// See comments in playBeatThread()
					target_msecs = CurrentClock + MsecsPerTick;
					goto chk_clk;
					do
					{
						usleep((target_msecs - CurrentClock) * 900);
chk_clk:			;
					} while ((CurrentClock = get_hw_clock()) < target_msecs);
				}

				if (PlayFlags & PLAYFLAG_STOP) goto out;

			} while (--countoff);
		}
	}

out:
	return refreshGuiMask;
}





/********************** playBeatThread() **********************
 * Thread that handles drum beat and chord accomp playback.
 * Actually, the audio thread in AudioPlay.c does the actual audio
 * mixing and playback. But this thread does the rhythmic timing for
 * each drum, bass, guitar, and background pad note, and hands
 * each of those notes to the audio thread. Think of the audio
 * thread as all the musicians in an orchestra, making/mixing
 * their instrument sounds. And this thread is the conductor
 * telling which musician to play what notes, and when.
 */

static void * playBeatThread(void * arg)
{
	uint32_t				refreshGuiMask;
	unsigned char		bassNtfTime, currentPpqnTime, drumTime;

	// Get the "handle" we use to signal other threads
//	arg = GuiWinSignal(GuiApp, 0, 0);
	arg = (void *)2;

	// Set the priority to slightly less than audio thread, but > GUI thread
	set_thread_priority();

	refreshGuiMask = 0;
wait:
	// Fade out pad notes (if user not manually playing the background pad)
	if (!(TempFlags & APPFLAG3_NOPAD)) refreshGuiMask |= fadePadVoices(100);

	// Select Intro
	resetPlayVariation();

	// Queue playback to start. Potentially eliminates needing to do extra
	// overhead when we wake up below
	update_play_style(BEATTHREADID);

	// Let other threads know we're going back to sleep
	BeatInPlay = INPLAY_STOPPED;

	// Signal main thread informing it the accomp is done playing.
	// Note we MUST wait until we have definitively signaled main,
	// so signal_stop() passes isBeatThread=0
	signal_stop(arg, refreshGuiMask);

	// Wait for main (or midi in) thread to start us in play, or tell us to abort
	pthread_mutex_lock(&PlayMutex);
	pthread_cond_wait(&PlayCondition, &PlayMutex);

	// Ok, we just woke up from being signaled. Let other threads know we're
	// now playing the accomp
#ifndef NO_MIDICLOCK_IN
	CurrentClock =
#endif
	MidiGuiUpdate = 0;
	BeatInPlay = INPLAY_RUNNING;
	pthread_mutex_unlock(&PlayMutex);

	// Main thread wants us to abort? Probably terminating the app
	if (!PlayThreadHandle) goto out;

	xrun_count(-1);

	// Do the countoff if any
	refreshGuiMask = do_countoff(arg);
	if (PlayFlags & PLAYFLAG_STOP) goto wait;

	// Queue events if needed. Hopefully what we did in resetPlayVariation() above
	// is still valid (ie, the user didn't change the style while we were asleep.)
	// Therefore we won't need to repeat setup in update_play_style()
	if (!isStyleQueued()) refreshGuiMask |= update_play_style(BEATTHREADID);

	// Get current chord played by the user
	update_chord();

	// Start pad notes if pad is selected, and robot musician on
	if (Scale && !(TempFlags & APPFLAG3_NOPAD)) startPadVoices(&GtrStrings[0], 0);

	// Highlight the chord on the graphical piano
	refreshGuiMask |= lightPianoKey(getCurrChord());

	// Reset time to meas start. Clear flags
	PlayFlags = currentPpqnTime = 0;

	// Reset bass for chord play
	bassNtfTime = 0xff;

	// Signal main if any gui redrawing needs to be done as a result of any of
	// our setup above
	if (refreshGuiMask) refreshGuiMask = drawGuiCtl(arg, refreshGuiMask, BEATTHREADID);

	// Get time of first drum event
	drumTime = *(DrumEvtPtr)++;

	do
	{
		// Has user played a chord yet? can't do anything with the bass or
		// guitar until the first chord
		if (Scale)
		{
			// =============== Bass ================
			// Is the bass robot on, and assigned to a buss?
			if (DevAssigns[PLAYER_BASS])
			{
				// Cut off any previously played bass note.
				// Our bass is monophonic. That really simplifies and speeds up
				// processing. Plus it's musically typical
				if (currentPpqnTime >= bassNtfTime)
				{
					stopAllBassNotes(BEATTHREADID);
					bassNtfTime = 0xff;
				}

				// Time to play next bass note?
				if (currentPpqnTime >= *BassEvtPtr)
				{
					register unsigned char	spec, note;

					// Skip timing, get note #, and inc to next evt
					++BassEvtPtr;
					spec = *BassEvtPtr++;

					if (!(TempFlags & APPFLAG3_NOBASS))
					{
						// Non-note event?
						if (spec & BASSEVTFLAG_NOT_ON)
						{
							// ntf
							if (spec == BASSEVTFLAG_NOTEOFF) stopAllBassNotes(BEATTHREADID);
						}
						else
						{
							// Get new note # based upon the current scale
							note = Scale[(spec & BASSEVTFLAG_SCALESTEPMASK)] + RootNote;

							// Raise the pitch outside of the scale?
							if (spec & GTREVTFLAG_PITCHUP)
							{
								// Raising the 6th for minor scales raises to a flat 7th
								if ((spec & BASSEVTFLAG_SCALESTEPMASK) == 5 && note - RootNote == 8) note++;
								note++;
							}

							// Ptn wants the note down/up an octave?
							if (spec & BASSEVTFLAG_OCTDOWN)
							{
								if (note >= 27+12) note -= 12;
							}
							else if (spec & BASSEVTFLAG_OCTUP)
							{
								if (note < 27+12) note += 12;
							}

							// Let audio thread play the new bass note using our sampled bass waveforms
							startBassNote(note, *BassEvtPtr++, BEATTHREADID);

							// Indicate bass/gtr notes will need to be muted after play stops and
							// user releases keys
							PlayFlags |= PLAYFLAG_CHORDSOUND;

							// If next is another ntn, insert a ntf between. Smoothes transistion
							if (*BassEvtPtr < getMeasureTime() && !(BassEvtPtr[1] & BASSEVTFLAG_NOT_ON) && BassEvtPtr[1] != spec) bassNtfTime = *BassEvtPtr - 1;
						}
					}
					else if (!(spec & BASSEVTFLAG_NOT_ON))
						BassEvtPtr++;
				}
			}

			// =============== Guitar ================
			// Is guitar robot on?
			if (DevAssigns[PLAYER_GTR])
			{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
				// Reset the pick attack time
				PickAttack = 0;
#endif
				// Time to play next gtr note?
				while (currentPpqnTime >= *GtrEvtPtr)
				{
					register unsigned char	note, spec;

					// Skip timing, get note spec, and inc to velocity byte
					GtrEvtPtr++;
					spec = *GtrEvtPtr++;

					if (!(TempFlags & APPFLAG3_NOGTR))
					{
						// Special evt?
//						if ((spec & GTREVTFLAG_NONNOTEMASK) == GTREVTFLAG_NONNOTEID)
//						{
//
//						}
//						else

						// Scale step?
						if (spec & GTREVTFLAG_SCALESTEP)
						{
							// ntf?
							if (spec & GTREVTFLAG_STEPOFFFLAG)
							{
								stopGtrString(spec & ~GTREVTFLAG_STEPOFFFLAG, BEATTHREADID);
								continue;
							}

							// Transpose rootnote from bass to guitar range.
							// Add step interval (low 3 bits are scale step 0 to 6)
							note = RootNote + 12 + Scale[(spec & GTREVTFLAG_SCALESTEPMASK)];

							if (spec & GTREVTFLAG_PITCHUP)
							{
								// Raising the 6th for minor scales raises to a flat 7th
								if ((spec & GTREVTFLAG_SCALESTEPMASK) == 5 && note - RootNote == 8+12) note++;
								note++;
							}

							// Move to desired octave
							note += (((spec >> 3) & 0x03) * 12);

							// Tell audio thread to play the note with our guitar waves
							startGtrString(spec, note, *GtrEvtPtr, BEATTHREADID);
						}

						// String #

						// ntn?
						else if ((spec & GTREVTFLAG_STRINGOFFMASK) != GTREVTFLAG_STRINGOFF)
						{
							register unsigned char	vel;

							note = spec & GTREVTFLAG_STRINGNUMMASK;
							vel = *GtrEvtPtr;

							// Chord? ie, This 1 event triggers (or mutes) several strings (notes)
							if (note >= 7)
							{
								// 7 = All 6 strings down-stroke
								// 9 = High 4 strings down-stroke
								// 13 = power 5th chord
								// 15 = power 6th chord
								if (note & 0x01)
								{
									if (note >= 13)
									{
										unsigned char	temp[2];
										register unsigned char	root;

										temp[0] = GtrStrings[5];
										temp[1] = GtrStrings[4];
										GtrStrings[5] = root = RootNote + 12;
										if (Scale[5 - 1] + root > 39+12) root -= 12;
										GtrStrings[4] = Scale[(note == 15 ? 6 - 1 : 5 - 1)] + root;
										startGtrString(6, spec, vel, BEATTHREADID);
										startGtrString(5, spec, vel, BEATTHREADID);
										GtrStrings[5] = temp[0];
										GtrStrings[4] = temp[1];
									}
									else
									{
										if (note != 9)
										{
											startGtrString(6, spec, vel, BEATTHREADID);
											startGtrString(5, spec, vel, BEATTHREADID);
										}
										startGtrString(4, spec, vel, BEATTHREADID);
										startGtrString(3, spec, vel, BEATTHREADID);
										startGtrString(2, spec, vel, BEATTHREADID);
										startGtrString(1, spec, vel, BEATTHREADID);
									}
								}
								// 8 = All 6 strings up-stroke
								// 10 = High 4 strings up-stroke
								else
								{
									startGtrString(1, spec, vel, BEATTHREADID);
									startGtrString(2, spec, vel, BEATTHREADID);
									startGtrString(3, spec, vel, BEATTHREADID);
									startGtrString(4, spec, vel, BEATTHREADID);
									if (note != 10)
									{
										startGtrString(5, spec, vel, BEATTHREADID);
										startGtrString(6, spec, vel, BEATTHREADID);
									}
								}
							}

							// Single string. 'note' is the string # (1 to 6)
							else if (note)
								startGtrString(note, spec, *GtrEvtPtr, BEATTHREADID);
						}

						// Muting (stopping) strs
						else
						{
							stopGtrString(spec & 0x07, BEATTHREADID);
							continue;
						}
					}

					GtrEvtPtr++;
				}
			}
		}

		// ====================== Drums ============================
		// Time to play next drum note?
		while (currentPpqnTime >= drumTime)
		{
			if (DevAssigns[PLAYER_DRUMS] && !(TempFlags & APPFLAG3_NODRUMS))
			{
				register unsigned char		noteNum, velocity;

				// Alternate the crash. Less monotonous
				noteNum = *DrumEvtPtr;
				if (noteNum == 49 || noteNum == 57)
				{
					if (PrevCrashCymbal == noteNum) noteNum = (noteNum == 49 ? 57 : 49);
					PrevCrashCymbal = noteNum;
				}

				// Let audio thread play this drum note
				velocity = *(DrumEvtPtr + 1);
				if ((noteNum = startDrumNote(0, noteNum, velocity & 0x7F, BEATTHREADID | (velocity >> 7))))
//					GuiWinSignal(GuiApp, arg, noteNum);
					GuiWinSignal(GuiApp, 0, noteNum);
			}

			// Skip to next evt
			DrumEvtPtr += 2;

			// Get time of next evt
			drumTime = *DrumEvtPtr++;
		}

		// ================= PPQN advance ======================

		// We just advanced within the measure 1 ppqn. Now we need to
		// do the actual delay between ppqn's

		{
		register unsigned char	endTime;

		endTime = getMeasureTime();

		// Are we playing the final measure of the End variation?
		if ((PlayFlags & PLAYFLAG_FINAL_PTN) &&

			// Any more events in final drum meas?
			drumTime >= endTime)
		{
			// We've just played a clean ending, and can now go back to sleep
			goto wait;
		}

		// Inc PPQN clock
		// If we're at the meas end, queue the next meas
		if (++currentPpqnTime >= endTime)
		{
#ifndef NO_SONGSHEET_SUPPORT
			refreshGuiMask |= nextSongBeat(0xFF);
#endif
			refreshGuiMask |= queueNextDrumMeas();

			// Reset currentPpqnTime to 0 because we're starting the next measure
			currentPpqnTime = 0;

			// Get time of first drum evt
			drumTime = *(DrumEvtPtr)++;
		}
#ifndef NO_SONGSHEET_SUPPORT
		// Check for a song sheet event
		else
			refreshGuiMask |= nextSongBeat(currentPpqnTime);
#endif
		// Check for audio underruns
		if (xrun_count(1)) refreshGuiMask |= CTLMASK_XRUN;

		// Check whether some midi in message has required a gui redraw. See
		// comment in signalMainFromMidiIn()
		if (lockGui(BEATTHREADID))
		{
			refreshGuiMask |= MidiGuiUpdate;
			MidiGuiUpdate = 0;
			unlockGui(BEATTHREADID);
		}

		// Check if we need to flash one of the variation buttons (ie, user
		// manually changed variation during play)
		refreshGuiMask |= setVariationBlink();

		if (refreshGuiMask) refreshGuiMask = drawGuiCtl(arg, refreshGuiMask, BEATTHREADID);
		}

		// If user wants to ritard, reduce the tempo each ppqn. Accelerando option too
		if (TempoChangeCnt)
		{
			if (!(currentPpqnTime % ((TempoBPM/(256/4)) * 3)))
			{
				if (PlayFlags & PLAYFLAG_RITARD)
				{
					if (PlayFlags & PLAYFLAG_FINAL_PTN)
					{
						if (currentPpqnTime > PPQN_VALUE*2) MsecsPerTick++;
						if (currentPpqnTime > PPQN_VALUE) MsecsPerTick++;
					}
					if (++MsecsPerTick > 66)
					{
						MsecsPerTick = 66;
						goto zero;
					}
				}
				else if (--MsecsPerTick < 13)
				{
					MsecsPerTick = 13;
zero:				TempoChangeCnt = 0;
				}
				if (TempoChangeCnt) --TempoChangeCnt;
			}
		}

		if (!TempoChangeCnt && (PlayFlags & PLAYFLAG_ACCEL))
		{
			PlayFlags &= ~PLAYFLAG_ACCEL;
			TempoChangeCnt = 256 / (TempoBPM - 40);
		}

		// =====================================================
		// Wait 1 PPQN. Since this loop is all about waiting, we'll throw in
		// something useful to do while waiting -- checking for, and responding
		// to the user changing the chord
		{
		register uint32_t			target_msecs;
		register unsigned char	beat, measQueue;

		// Calc the next ppqn where we allow a chord change
#ifndef NO_SONGSHEET_SUPPORT
		beat = isSongsheetActive() ? currentPpqnTime : (currentPpqnTime / ChordBoundary) * ChordBoundary;
#else
		beat = (currentPpqnTime / ChordBoundary) * ChordBoundary;
#endif
		// If the 1st beat, indicate we need to queue the next meas below
		measQueue = (!currentPpqnTime ? 0x80 : 0xFF);

		// Add the amount of milliseconds in one PPQN, to the current time
		target_msecs = CurrentClock + MsecsPerTick;
		goto chk_clock;

		// (Partially) busy-wait for the remainder of a PPQN clock
		do
		{
			// Sleep for only part of the wait, just in case this takes longer than we request
			usleep((target_msecs - CurrentClock) * 900);
chk_clock:
			// ================ chord update ===============
			// Is this a point where we allow a chord change? (Allow 4 ticks of "wiggle room" in
			// case the user lags the beat)
			if (currentPpqnTime >= beat && currentPpqnTime < beat+5)
			{
				// Has the user changed the chord since the previous time we checked?
				if (ChordChangedCnt != ChordPlayCnt)
				{
					// Yep. We got to change our bass root note and scale, and re-fret
					// our 6 guitar strings
					update_chord();

					if (PlayFlags & PLAYFLAG_CHORDSOUND)
					{
						// Go through currently sounding strings and mute any not a root,
						// third, fifth (and maybe 7th/9th) of new scale
						changeGtrChord();

						// Mute the bass too
						if (!(TempFlags & APPFLAG3_NOBASS)) stopAllBassNotes(BEATTHREADID);
						bassNtfTime = 0xff;
					}

					// We need to resync our bass/gtr ptn to where the chord change ideally
					// should have happened, but maybe lagged. In other words, we may need to
					// backup as much as 4 ticks, and replay any bass/gtr events in that space
					// to ensure we're playing this new chord. This could cause an audible
					// "glitch", but hopefully not. User needs to not lag. Practice!
					if (beat || currentPpqnTime) measQueue = beat;

					if (Scale && !(TempFlags & APPFLAG3_NOPAD)) startPadVoices(&GtrStrings[0], 0);

					refreshGuiMask |= lightPianoKey(getCurrChord());
				}

				// If chord hold is off, check whether the user is holding any chord notes. If
				// not, mute the bass/gtr/pad until the next chord
				else if ((AppFlags4 & APPFLAG4_NOCHORDHOLD) & !NextNumNotes)
					BeatInPlay |= (INPLAY_USERMUTEBASS|INPLAY_USERMUTEGTR|INPLAY_USERMUTEPAD|INPLAY_TURNING_OFF);
			}

			// User wants to silence bass, gtr, and/or pad?
			if (BeatInPlay & (INPLAY_USERMUTEBASS|INPLAY_USERMUTEGTR|INPLAY_USERMUTEPAD))
			{
				// If caller has disabled bass/gtr/pad, then we don't need to clear the
				// current chord since we won't play the instrument(s) above. Otherwise,
				// caller wants to mute instruments only until the user plays another chord,
				// so we need to clear the current chord and indicate the user hasn't yet
				// played a chord
				if (BeatInPlay & INPLAY_TURNING_OFF)
				{
					refreshGuiMask |= clearChord(2|BEATTHREADID);
					PlayFlags &= ~PLAYFLAG_CHORDSOUND;

					// Don't play any more bass/gtr evts above (until user plays another chord)
					Scale = 0;
				}

				// Mute instrument(s) notes in play
				if (BeatInPlay & INPLAY_USERMUTEBASS)
				{
					stopAllBassNotes(BEATTHREADID);
					bassNtfTime = 0xff;
				}
				if (BeatInPlay & INPLAY_USERMUTEGTR) stopGtrString(0, BEATTHREADID);

				if (BeatInPlay & INPLAY_USERMUTEPAD)
				{
					if (BeatInPlay & INPLAY_TURNING_OFF)
						refreshGuiMask |= changePadInstrument(BEATTHREADID);
					else
						fadePadVoices(14);
				}

				if ((BeatInPlay & (INPLAY_USERMUTEBASS|INPLAY_USERMUTEGTR|INPLAY_USERMUTEPAD)) == (INPLAY_USERMUTEBASS|INPLAY_USERMUTEGTR|INPLAY_USERMUTEPAD))
					PlayFlags &= ~PLAYFLAG_CHORDSOUND;

				// Do once only (at user's request)
				BeatInPlay &= ~(INPLAY_USERMUTEBASS|INPLAY_USERMUTEGTR|INPLAY_USERMUTEPAD|INPLAY_TURNING_OFF);
			}

#ifndef NO_MIDICLOCK_IN
			// If we're sync'ed to external MIDI Clock, wait for the MIDI In thread to
			// tell us when it receives the next MIDI Clock
			if (ClockId >= 0xFE)
			{
				wait_for_midiclock();
				break;
			}
#endif
			// Otherwise, check our internal clock
		} while ((CurrentClock = get_hw_clock()) < target_msecs);

		// Queue the next meas, or resync the current? (Wait until user plays the first
		// chord before we queue anything)
		if (measQueue < 0xff && NextScale)
		{
			// Yes. Do the queue or resync
			setAccompEvtPtr(measQueue);

			// Since we just muted the bass, see if there's a bass note coming up soon. If
			// not, we just created a too-noticeable "hole" in the low end. Let's fill it
			// by making the bass play the root note now. Do this only for a chord change
			// on the 2nd or 4th beat. The typical bass ptn usually has the 1st and 3rd covered
			if (*BassEvtPtr > measQueue + 6 && (measQueue == 24 || measQueue == 24*3) && !(TempFlags & APPFLAG3_NOBASS))
			{
				bassNtfTime = 0xff;
				startBassNote(RootNote, 100, BEATTHREADID);
			}
		}
		}

		// Keep going unless main thread wants us to abruptly abort now (without even playing
		// any ending ptn). This typically happens only when terminating app
	} while (PlayThreadHandle);

out:
//	GuiWinSignal(GuiApp, arg, 0);

	return 0;
}



#ifndef NO_MIDICLOCK_IN

/************ advance_midiclock() *************
 * Called by Play Beat thread to wait for the
 * next MIDI Clock received. This is when sync'ed
 * to external MIDI Clock.
 */

static void wait_for_midiclock(void)
{
	// Has the previously set timeout been satisfied?
	if (CurrentClock < TimeoutClock)
	{
		// Tell Midi In thread's advance_midiclock() to wake us
		ClockId = 0xFF;

		// Do a wait (sleep)
		pthread_mutex_lock(&PlayMutex);
		pthread_cond_wait(&PlayCondition, &PlayMutex);

		// No longer waiting
		ClockId = 0xFE;
		pthread_mutex_unlock(&PlayMutex);
	}

	// Inc to next midi clock for the next call
	TimeoutClock = CurrentClock + 1;
}

unsigned char is_midiclock(void)
{
	return (ClockId & 0xF0);
}

/************ advance_midiclock() *************
 * Called by MIDI In thread whenever a MIDI Clock
 * is received.
 */

void advance_midiclock(void)
{
	if (ClockId >= 0xFE && BeatInPlay &&
		++CurrentClock >= TimeoutClock && (ClockId & 0x01))
	{
		// Wakeup beat thread sleeping in wait_for_midiclock()
		pthread_mutex_lock(&PlayMutex);
		pthread_cond_signal(&PlayCondition);
		pthread_mutex_unlock(&PlayMutex);
	}
}
#endif

/* Our internal hardware clock */

static uint32_t get_hw_clock(void)
{
	struct timespec	tv;

	clock_gettime(ClockId, &tv);
	return (uint32_t)((1000 * tv.tv_sec) + (tv.tv_nsec/1000000));
}

uint32_t get_current_clock(void)
{
#ifndef NO_MIDICLOCK_IN
	if (ClockId >= 0xFE)
	{
		struct timespec	tv;

		clock_gettime(CLOCK_MONOTONIC_COARSE, &tv);
		return (uint32_t)((1000 * tv.tv_sec) + (tv.tv_nsec/1000000));
	}
#endif
	if (BeatInPlay) return CurrentClock;
	return get_hw_clock();
}

void set_clock_type(void)
{
	ClockId = Clocks[(AppFlags2 & APPFLAG2_CLOCKMASK)];

#ifndef NO_MIDICLOCK_IN
	// If clock = "MIDI sync", then grab and hold the tempo
	// and play locks with the id CLOCKTHREADID so that the
	// GUI/MIDI/BEAT threads can't change the tempo
	if (ClockId & 0xF0)
	{
		if (BeatInPlay) PlayFlags |= PLAYFLAG_STOP;
		while (lockPlay(CLOCKTHREADID) != CLOCKTHREADID) usleep(100);
		while (lockTempo(CLOCKTHREADID) != CLOCKTHREADID) usleep(100);
	}
	else
	{
		unlockPlay(CLOCKTHREADID);
		unlockTempo(CLOCKTHREADID);
	}
#endif
}





/********************* update_chord() *******************
 * Called by Play Beat thread when user has changed chord.
 *
 * Also called by midi in, and main threads, but only
 * when beat thread isn't playing.
 */

static void update_chord(void)
{
	register unsigned char	rootnote;

	// Chord change now accepted/processed by beat play thread
	ChordChangedCnt = ChordPlayCnt;

	// Sync vars between threads. Should lock access here, but not critical
	NoteAddMask = NextNoteAddMask;
	RootNote = rootnote = NextRootNote;
	Scale = NextScale;

	// Fret a 6 string gtr chord with root, 3rd, and 5th. maybe 7th and 9th. Use
	// "guitar voicing" (ie try to keep intervals of 4 or 5 steps between the notes
	// on the lower strings. Avoid placing more than 2 notes within an octave, except
	// on the highest strs. Don't do things like put the 9th too close to the 3rd. Got
	// to play like a guitarist -- not a pianist)
	if (Scale)
	{
		rootnote += 12;

		if (rootnote + Scale[3 - 1] - 12 > 35)
		{
			GtrStrings[5] = rootnote + Scale[3 - 1] - 12;
			GtrStrings[4] = rootnote;
			GtrStrings[3] = Scale[5 - 1] + rootnote;
			GtrStrings[2] = (NextNoteAddMask & NOTEADD_SEVENTH) ? rootnote + Scale[7 - 1] : rootnote + 12;
			GtrStrings[1] = (NextNoteAddMask & NOTEADD_NINTH) ? rootnote + Scale[2 - 1] + 12 : GtrStrings[5] + 24;
			GtrStrings[0] = GtrStrings[3] + 12;
			goto out;
		}
		if (rootnote + Scale[5 - 1] - 12 > 35)
		{
			GtrStrings[5] = rootnote + Scale[5 - 1] - 12;
			GtrStrings[4] = rootnote;
			GtrStrings[3] = GtrStrings[5] + 12;
			rootnote += 12;
			GtrStrings[2] = rootnote + Scale[(NextNoteAddMask & NOTEADD_NINTH) ? 2 - 1 : 3 - 1];
			GtrStrings[1] = rootnote + Scale[5 - 1];
			GtrStrings[0] = rootnote + ((NextNoteAddMask & NOTEADD_SEVENTH) ? Scale[7 - 1] : 12);
			goto out;
		}

		GtrStrings[5] = rootnote;
		GtrStrings[4] = Scale[5 - 1] + rootnote;
		GtrStrings[3] = rootnote + ((NextNoteAddMask & NOTEADD_SEVENTH) ? Scale[7 - 1] : 12);
		GtrStrings[2] = rootnote + 12 + Scale[3 - 1];
		GtrStrings[1] = GtrStrings[4] + 12;
		GtrStrings[0] = rootnote + 24 + ((NextNoteAddMask & NOTEADD_NINTH) ? Scale[2 - 1] : Scale[3 - 1]);
	}
out:
	return;
}







static unsigned char GuiLock = 0;

unsigned char lockGui(register unsigned char threadId)
{
	register unsigned char	who;

	while ((who = __atomic_or_fetch(&GuiLock, threadId, __ATOMIC_RELAXED)) != threadId)
	{
		// Someone is holding the lock. If the caller is the
		// Beat Play thread, then we'll get out of here now
		// (instead of waiting), and postpone giving the
		// main thread our data until the next PPQN clock.
		if (threadId & (BEATTHREADID | AUDIOTHREADID))
		{
			unlockGui(threadId);
			return 0;
		}

		// Someone is locking access to drawGuiCtl. If it's the beat thread, he's
		// the same priority as midi thread. So if the MIDI thread called here,
		// just relinquish to him. If it's the lower priority gui thread, we must
		// sleep() to allow him to finish
		if ((who & (BEATTHREADID|MIDITHREADID|AUDIOTHREADID)) == (BEATTHREADID|MIDITHREADID)) sched_yield();
		else usleep(100);
	}

	return 1;
}

void unlockGui(register unsigned char threadId)
{
	__atomic_and_fetch(&GuiLock, ~threadId, __ATOMIC_RELAXED);
}






/****************** signalMainFromMidiIn() *******************
 * Called by the MIDI input thread to signal the gui thread
 * that some graphical control's value needs updating/redraw.
 */

#ifndef NO_MIDI_IN_SUPPORT
void signalMainFromMidiIn(register void * signalHandle, register uint32_t mask)
{
	// If beat thread is asleep, then we don't have to worry about 2 threads
	// fighting over signaling the main gui thread, perhaps even about the
	// same event. In this case, the midi in thread can directly pass "mask"
	// to gui thread via drawGuiCtl().
	// Otherwise, midi thread defers to the beat thread by giving "mask" to
	// the beat thread (by storing in the global "MidiGuiUpdate"). Then the
	// beat thread passes mask to gui via drawGuiCtl().
	// Of course, the midi and beat threads can't be simultaneously accessing
	// MidiGuiUpdate, thus the lock/wait below. But this MidiGuiUpdate lock/wait
	// is much shorter than drawGuiCtl's lock/wait. Plus it eliminates the
	// problem of 2 threads giving potentially conflicting or redundant signals.
	if (BeatInPlay)
	{
		lockGui(MIDITHREADID);
		MidiGuiUpdate |= mask;
		unlockGui(MIDITHREADID);
	}
	else
		drawGuiCtl(signalHandle, mask, MIDITHREADID);
}
#endif




void checkStartPadNotes(void)
{
	// Note: Caller checks if Pad robot is enabled (DevAssigns[PLAYER_PAD])

	// If Robot pad player is muted, don't sound notes
	if (!(TempFlags & APPFLAG3_NOPAD))
	{
		// Beat thread will sound the voices if in play. Otherwise
		// if stopped, we sound them now if...
		if (!BeatInPlay)
		{
			// user has played a chord...
			if (NextScale &&

				// Autostart is off
				!(TempFlags & TEMPFLAG_AUTOSTART))
			{
				startPadVoices(&GtrStrings[0], 255);
			}
		}
		else
			// Tell (playing) Beat thread to sound the pad notes
			++ChordPlayCnt;
	}
}




/******************** strum_chord() *********************
 * Called by threads other than the Play Beat thread. When
 * playback is stopped, this allows other threads to "strum"
 * one new chord when the user changes the chord.
 */

static void strum_chord(register unsigned char vel, register unsigned char threadId)
{
	// Sound a single bass note, a guitar chord "strum", and
	// a sustained pad chord... But only if the respective
	// robots are on
	if (!((TempFlags & APPFLAG3_NOBASS) | (AppFlags & (APPFLAG_BASS_ON|APPFLAG_BASS_LEGATO)))) startBassNote(RootNote, vel, threadId);
	if (!(TempFlags & APPFLAG3_NOGTR) && DevAssigns[PLAYER_GTR])
	{
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
		PickAttack = 0;
#endif
		startGtrString(6, 6, ChordVel, BEATTHREADID);
		startGtrString(5, 5, ChordVel, BEATTHREADID);
		startGtrString(4, 4, ChordVel, BEATTHREADID);
		startGtrString(3, 3, ChordVel, BEATTHREADID);
		startGtrString(2, 2, ChordVel, BEATTHREADID);
		startGtrString(1, 1, ChordVel, BEATTHREADID);
	}

	if (!(TempFlags & APPFLAG3_NOPAD))
		startPadVoices(&GtrStrings[0], 255);
}




#if 0
static void print_notes(void)
{
	register const unsigned char *	notePtr;
	register unsigned char				numNotes;
	static const char NoteName[] = {' ','C',0,0,
	' ','C','#',0,
	' ','D',0,0,
	' ','D','#',0,
	' ','E',0,0,
	' ','F',0,0,
	' ','F','#',0,
	' ','G',0,0,
	' ','G','#',0,
	' ','A',0,0,
	' ','A','#',0,
	' ','B',0,0};

	numNotes = NextNumNotes;
	printf("num=%u", numNotes);
	if (numNotes > 1)
	{
		for (;;)
		{
			if (*notePtr++)
			{

				printf(&NoteName[((notePtr - &NextChordNotes[0]) - 1) * 4]);
				if (!(--numNotes)) break;
			}
		}
	}
	printf("\r\n");
}

static const char ScaleNames[] = {'m','a','j',
'm','i','n',
0,0,0,
'a','u','g',
'd','i','m',
's','u','s'};

static const char NoteNames[] = {'C',0,'D',0,'E','F',0,'G',0,'A',0,'B'};


static void format_chordname(register char * namePtr)
{
	register unsigned int	num;

	num = NextRootNote % 12;
	namePtr[0] = NoteNames[num];
	if (!namePtr[0])
	{
		namePtr[0] = NoteNames[num - 1];
		namePtr[1] = '#';
		++namePtr;
	}
	++namePtr;

	num = (NextScale - &Scales[0]) / 7;
	if (num > 5)
	{
	}
	else if (num != 2)
	{
		memcpy(namePtr, &ScaleNames[num * 3], 3);
		namePtr += 3;
	}

	if (NextNoteAddMask & NOTEADD_NINTH)
		*namePtr++ = '9';
	else if (NextNoteAddMask & NOTEADD_SEVENTH)
		*namePtr++ = '7';

	namePtr[0] = 0;
}
#endif





#ifndef NO_MIDI_IN_SUPPORT

unsigned char setChordSensitivity(register unsigned char sens)
{
	if (sens && sens < 6) Sensitivity = sens;
	return Sensitivity;
}





/******************** analyze_chord() *********************
 * For bass/gtr auto-accompaniment. Called by MIDI In
 * thread when user presses a key on the lower half of MIDI
 * controller. Figures out what chord is being played.
 */

static void analyze_chord(void)
{
	register const unsigned char *	notePtr;
	register unsigned char				rootNote, numNotes;

	// Must be at least 2 notes being held down by user (this ain't applicable for APPFLAG_1FINGER/2KEY/MIDICHORD)
	// 4 notes if GTRCHORD
	numNotes = NextNumNotes;
	if (numNotes > Sensitivity)
	{
		register uint32_t			chord;
		register int				low, mid, up;

		// Convert the note numbers to offsets from the lowest note, and combine them into a
		// uint32_t for fast comparison to our chord dictionary
		chord = rootNote = 0;
		notePtr = &NextChordNotes[0];
		for (;;)
		{
			if (*notePtr++)
			{
				if (!rootNote) rootNote = (unsigned char)(notePtr - &NextChordNotes[0]);
				chord = (chord << 4) | ((notePtr - &NextChordNotes[0]) - rootNote);
				if (!(--numNotes)) break;
			}
		}
		--rootNote;

		// Look through our chord dictionary to find an appropriate scale, and determine which is
		// the root note, and whether guitar should play a 7th and/or 9th

		// Begin with the entire table
		low = 0;
//		up = (sizeof(ChordDictionary)/sizeof(uint32_t)) - 1;
		up = (sizeof(ChordDictionary)/sizeof(uint32_t));

		// Do a lookup whereby we divide the list of chords in half and check for a match with
		// the chord at that mid-point. If that chord is > the desired chord
		// to match, then we toss away the top half of the list. (Remember that the list is in
		// numerical order). If the chord is < the desired chord to match, then we toss
		// away the bottom half of the list. In this way, we continously toss away half of the
		// chords with each iteration through this loop, and thereby more quickly locate a match.
		while (low <= up)
		{
			// Get the midpoint in the list so far
			mid = (up+low)/2;

			// Matched all the played notes?
			if (chord == (ChordDictionary[mid] & 0xFFFF))
			{
				// Found the chord. The dictionary entry tells us what scale the chord is based
				// on, which note is root, and whether the 7th and/or 9th of the scale are added
				// to the chord
				chord = ChordDictionary[mid];

				// Bring root note to low bass octave. Lowest bass note is midi note #28 (E)
				rootNote += (unsigned char)(DIFF_TO_ROOT(chord)) + 24;
				if (rootNote < 28) rootNote += 12;

				// Get scale/7th/9th
				numNotes = (unsigned char)(TO_FLAGS(chord));
				notePtr = &Scales[DIFF_TO_SCALE(chord)];

				// Chord is different from currently playing?
				if (NextScale != notePtr || NextRootNote != rootNote || NextNoteAddMask != numNotes)
				{
					// Yes. Update globals
					NextScale = notePtr;
					NextRootNote = rootNote;
					NextNoteAddMask = numNotes;

					// Alert beat thread another chord played
					++ChordPlayCnt;

//{
//char buffer[8];

//format_chordname(buffer);
//printf("%u notes = %s %08X\r\n", NextNumNotes, buffer, chord & 0xFFFF);
//}

				}

				break;
			}

			// Not a match
			else
			{
				// Compare the two non-matching chords so we can determine
				// whether we should toss away the top or bottom half
				if ((ChordDictionary[mid] & 0xFFFF) > chord)
				{
					// Throw away the top half
					up = mid - 1;
				}
				else
				{
					// Throw away the bottom half
					low = mid + 1;
				}
			}
		}
//printf("************* chord %08X not found\r\n", chord);
	}
}




/***************** midiChordTrigger() *******************
 * Called by MIDI In thread when a note on/off
 * received from a Midi Controller.
 */

uint32_t midiChordTrigger(register unsigned char noteNum, register unsigned char velocity)
{
	if (lockChord(MIDITHREADID) == MIDITHREADID)
	{
		// Ntn?
		if (velocity)
		{
			// Set volume for backing pad sound
			ChordVel = Comp1VelCurve[velocity - 1] >> 1;

			// If "Breath/Wind" chord mode, allow 1 note (sets Root note. Scale is set
			// via ChordTrigger)
			if (AppFlags & APPFLAG_WINDCTL) goto oneMidi;

			// If "2 key" chord mode, allow 1 note plus SplitPoint key
			if (AppFlags & APPFLAG_2KEY)
			{
				if (noteNum != SplitPoint)
				{
oneMidi:			if (!NextScale) NextScale = &Scales[0];	// Major
					goto inc1;
				}

				NextScale = &Scales[7];	// Minor
				if (NextNumNotes) goto trig;
				goto out;
			}

			// If 1 finger, allow 2 notes at most
			if (AppFlags & APPFLAG_1FINGER)
			{
				if (NextNumNotes)
				{
					// This check is for controllers that stack note-ons
					// without intervening note-offs
					if (noteNum == NextChordNotes[0])
					{
						++NextNumNotes;
						goto out;
					}

					// If new note is further than 2 steps, it replaces the previous root
					if (noteNum > NextChordNotes[0])
					{
						if (noteNum - NextChordNotes[0] < 3)
						{
							noteNum = NextChordNotes[0];
							goto minor;
						}
					}
					else if (NextChordNotes[0] - noteNum < 3)
					{
minor:				NextScale = &Scales[7];
						goto gotChord;
					}
				}

				NextScale = &Scales[0];	// Major

gotChord:
//				if (msg[1])
				{
inc1:				++NextNumNotes;
					NextChordNotes[0] = noteNum;
					NextRootNote = (noteNum % 12) + 24;
					if (NextRootNote < 28) NextRootNote += 12;
				}

				// Another chord played
trig:			++ChordPlayCnt;

				goto autoOn;
			}

			// ================= Chord modes other than 1 Finger and Breath ==============
			// Save the note # in the array, ordered from lowest to highest,
			// and inc count of notes. Analyze chord
			{
			register unsigned char * ptr;

			// Bring note into octave C0 to B0
			ptr = &NextChordNotes[noteNum % 12];

			// We keep track of duplicate notes, but those don't change
			// the chord
			*ptr += 1;
			if (*ptr == 1)
			{
				// Update cnt of notes in chord. Non-duplicates only
				++NextNumNotes;

				// Update current chord
				analyze_chord();
	//print_notes();
			}
			}

			// Implement "auto-start" if needed
autoOn:
			// If autostart is on, playback is stopped, and user has played a chord, then start play.
			// When beat is stopped, and no auto-start, just sound a bass note and guitar strum once
			if (!BeatInPlay)
			{
				if (!(TempFlags & TEMPFLAG_AUTOSTART))
				{
					if (NextScale)
					{
						if (ChordChangedCnt != ChordPlayCnt) update_chord();
						unlockChord(MIDITHREADID);
						strum_chord(velocity, MIDITHREADID);
						return lightPianoKey(getCurrChord());
					}
				}

				// If we finished play, and some notes are lingering from the last chord,
				// then mute them now (unless sustain pedal held), and don't autostart
				else if (PlayFlags & PLAYFLAG_CHORDSOUND)
				{
					register uint32_t		mask;

					if (!(SustainPedal & (0x01 << PLAYER_SOLO)))
					{
clear:				mask = clearChord(2|MIDITHREADID);
						//unlockChord(MIDITHREADID);
						return mask;
					}
				}

				// If stopped, and a chord has been established, then autostart (if enabled)
				else if (NextScale)
				{
					unlockChord(MIDITHREADID);
					play_beat(1, MIDITHREADID);
					goto ret0;
				}
			}
		}

		// Ntf
		else
		{
			register unsigned char * ptr;

			if (AppFlags & (APPFLAG_2KEY|APPFLAG_1FINGER))
			{
				if (noteNum != SplitPoint)
				{
					if (NextNumNotes) --NextNumNotes;
				}
				else
					NextScale = &Scales[0];	// Major
			}

			// Remove the note # from the array
			else
			{
				ptr = &NextChordNotes[noteNum % 12];
				if (*ptr)
				{
					*ptr -= 1;
					if (!(*ptr) && NextNumNotes) --NextNumNotes;
				}
			}

			// If no longer in play, and an accomp chord may still be sounding,
			// silence any lingering chord and indicate user is no
			// longer holding a chord (assuming sustain pedal isn't on)
			if (!(BeatInPlay|SustainPedal|NextNumNotes) && (PlayFlags & PLAYFLAG_CHORDSOUND)) goto clear;
		}
	}
out:
	unlockChord(MIDITHREADID);
ret0:
	return CTLMASK_NONE;
}

#endif // NO_MIDI_IN_SUPPORT





/******************** lockStyle() ********************
 * Arbitrates thread access to changing chords.
 */

static unsigned char ChordLock = 0;

unsigned char lockChord(register unsigned char threadId)
{
	return __atomic_or_fetch(&ChordLock, threadId, __ATOMIC_RELAXED);
}

void unlockChord(register unsigned char threadId)
{
	__atomic_and_fetch(&ChordLock, ~threadId, __ATOMIC_RELAXED);
}




/***************** eventChordTrigger() *******************
 * Called by main thread to implement "1 finger chords"
 * on the computer QWERTY keyboard, or a click on the
 * gui piano. Also may be called by Midi In thread for a
 * "Chord" or "Note Trigger" command.
 */

uint32_t eventChordTrigger(register unsigned char rootNote, register unsigned char scaleFlag, register unsigned char threadId)
{
	if (lockChord(threadId) == threadId)
	{
		register const unsigned char *	scalePtr;

		ChordVel = 34;

		// If rootNote=0, caller doesn't want the root changed
		if (rootNote) rootNote = (rootNote & 0x7F) + Transpose;
		else rootNote = NextRootNote;

		scalePtr = NextScale;
		// Full selection of scale specified?
		// 255 = toggle major/minor
		// 254 = Don't change the scale. Change only the root note
		// < 254 = A particular scale, where 0=major and 1=minor, etc
		if (scaleFlag < 254)
		{
			NextNoteAddMask = scaleFlag >> 6;
			scalePtr = &Scales[(scaleFlag & 0x07) * 7];
		}
		// Only toggle major/minor
		else if (scaleFlag == 255)
		{
			NextNoteAddMask = 0;
			scalePtr = (NextScale == &Scales[0] ? &Scales[7] : &Scales[0]);
		}

		if (rootNote != NextRootNote || scalePtr != NextScale || NextNoteAddMask != NoteAddMask)
		{
			NextScale = scalePtr;
			if ((NextRootNote = rootNote)) ++ChordPlayCnt;
		}

		if (!BeatInPlay && NextRootNote)
		{
			if (TempFlags & TEMPFLAG_AUTOSTART)
				play_beat(1, threadId);
			else
			{
				update_chord();
				unlockChord(threadId);
				strum_chord(100, threadId);
				return lightPianoKey(getCurrChord());
			}
		}

		// Make sure chord gets triggered even if "chord hold" is off
		NextNumNotes = 1;

		unlockChord(threadId);
	}
	return CTLMASK_NONE;
}



void clear_note(void)
{
	if (AppFlags4 & APPFLAG4_NOCHORDHOLD) NextNumNotes = 0;
}

/******************* mute_playing_chord() *******************
 * Releases any currently sounding chord (includes muting
 * any notes being held by robot musicians).
 */

uint32_t mute_playing_chord(register unsigned char threadId)
{
	if (!BeatInPlay) return clearChord(2 | threadId);

	// The Beat Play thread also accesses the same globals. But rather than
	// using a lock, if that thread is running, we'll tell him to mute the
	// playing accomp notes instead of us doing it here
	BeatInPlay |= (INPLAY_USERMUTEGTR|INPLAY_USERMUTEBASS|INPLAY_USERMUTEPAD|INPLAY_TURNING_OFF);
	return CTLMASK_NONE;
}





/****************** mute_robots() *******************
 * Mutes/unmutes robots.
 */

uint32_t mute_robots(register unsigned char flag, register unsigned char threadId)
{
	register uint32_t			mask;
	register unsigned char	orig;

	mask = CTLMASK_ACCOMPMUTE;
	orig = TempFlags & (APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NODRUMS|APPFLAG3_NOPAD);

	// Set/clear appropriate bits in TempFlags
	if (flag & ACCOMP_TOGGLE)
	{
		// When toggling accompaniment (bass/gtr/pad robots together), if any are off, all are turned on.
		// Only when all are on, they're turned off
		if ((flag & (APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NODRUMS|APPFLAG3_NOPAD)) == (APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NOPAD))
		{
			if (!(orig & (APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NOPAD)))
			{
				TempFlags |= (APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NOPAD);
			}
			else
				TempFlags &= ~(APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NOPAD);
		}

		// Toggling only one robot
		else
			TempFlags ^= (flag & (APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NODRUMS|APPFLAG3_NOPAD));
	}
	else if (flag & ACCOMP_ON)
		TempFlags &= ~(flag & (APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NODRUMS|APPFLAG3_NOPAD));
	else
		TempFlags |= (flag & (APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NODRUMS|APPFLAG3_NOPAD));

	// ============================================
	// Now we need to figure out what ctls to update. Update only those whose value has changed

	if ((orig & APPFLAG3_NODRUMS) != (TempFlags & APPFLAG3_NODRUMS)) mask |= CTLMASK_DRUMMUTE;
	if ((orig & APPFLAG3_NOBASS) != (TempFlags & APPFLAG3_NOBASS)) mask |= CTLMASK_BASSMUTE;
	if ((orig & APPFLAG3_NOGTR) != (TempFlags & APPFLAG3_NOGTR)) mask |= CTLMASK_GTRMUTE;
	if ((orig & APPFLAG3_NOPAD) != (TempFlags & APPFLAG3_NOPAD)) mask |= CTLMASK_BACKINGPAD;

	// Any robots being muted (as opposed to unmuted)?
	if ((flag = (mask & TempFlags & ~(CTLMASK_DRUMMUTE|CTLMASK_ACCOMPMUTE))))
	{
		// If in play, tell beat thread to mute playing notes. Otherwise, we do it here
		if (BeatInPlay) BeatInPlay |= (flag | INPLAY_TURNING_OFF);
		else
		{
			if (flag & APPFLAG3_NOBASS) stopAllBassNotes(threadId);
			if (flag & APPFLAG3_NOGTR) stopGtrString(0, threadId);
			if (flag & APPFLAG3_NOPAD) mask |= changePadInstrument(threadId);
		}
	}

	return mask;
}




/************** allNotesOff() ******************
 * Implements "All notes off" panic button.
 */

uint32_t allNotesOff(register unsigned char threadId)
{
	register uint32_t	guimask;

	SustainPedal = LegatoPedal = 0;

	stopAllSoloNotes(threadId);

#ifdef DISABLE_CHORDS_ON_PANIC
	guimask = mute_robots(APPFLAG3_NOPAD|APPFLAG3_NOBASS|APPFLAG3_NOGTR, threadId);
#else
	guimask = mute_playing_chord(threadId);
#endif

	toggle_solo_vol(0, threadId);
//	setMasterVol(getConfigMasterVol(), threadId);
//	setInstrumentVol(PLAYER_SOLO, getConfigPatchVol(), threadId);

#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	{
	register struct SOUNDDEVINFO *	soundDev;
	unsigned char							msg[6];
	register unsigned char				roboNum;

	msg[5] = msg[2] = 0x00;
	msg[1] = 0x78;
	msg[4] = 0x79;

	roboNum = 5;
	while (roboNum)
	{
		soundDev = DevAssigns[--roboNum];
		if (soundDev >= &SoundDev[DEVNUM_MIDIOUT1])
		{
			msg[3] = msg[0] = 0xB0|MidiChans[roboNum];
			sendMidiOut(soundDev, &msg[0], 6|threadId);
		}
	}
	}
#endif

	clear_banksel();
	return guimask | CTLMASK_VOLBOOST;
}


void close_midi_port(register struct SOUNDDEVINFO * sounddev)
{
	register snd_seq_t *		handle;
	register unsigned char	type;

	// Mark it as explicitly closed by the user
	type = sounddev->DevFlags & DEVFLAG_DEVTYPE_MASK;
	sounddev->DevFlags &= ~DEVFLAG_DEVTYPE_MASK;

	if ((handle = sounddev->Handle))
	{
		sounddev->Handle = 0;

#ifndef NO_SEQ_SUPPORT
		if (type != DEVTYPE_SEQ)
#endif
			snd_rawmidi_close((snd_rawmidi_t *)handle);
#ifndef NO_SEQ_SUPPORT
		else
		{
			// Close the port
			if (sounddev->Card >= 0) snd_seq_delete_simple_port(handle, sounddev->Card);

			// Also close the seq handle if no other MIDI ports are using it
			sounddev = &SoundDev[DEVNUM_MIDIOUT1];
			do
			{
				if (sounddev->Handle == handle) goto out;
			} while (++sounddev <= &SoundDev[DEVNUM_MIDIIN]);

			closeAlsaSeq();
		}
#endif
	}
out:
	return;
}



#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)

void send_alloff(register uint32_t roboNum, register unsigned char threadId)
{
	unsigned char		msg[4];

	msg[0] = 0xB0|MidiChans[roboNum];
	msg[1] = 123;
	msg[2] = 0x00;
	sendMidiOut(DevAssigns[roboNum], &msg[0], 3|threadId);
}






#ifndef NO_SEQ_SUPPORT
static const unsigned char SeqTypes[] = {SND_SEQ_EVENT_NOTEOFF, SND_SEQ_EVENT_NOTEON, SND_SEQ_EVENT_KEYPRESS,
SND_SEQ_EVENT_CONTROLLER, SND_SEQ_EVENT_PGMCHANGE, SND_SEQ_EVENT_CHANPRESS, SND_SEQ_EVENT_PITCHBEND};
#endif

/****************** sendMidiOut() *******************
 * Sends to MIDI Out, if open.
 *
 * sounddev =		One of the four MIDI Out playback devices.
 * msg =				MIDI bytes
 * count =			How many bytes.
 *
 * NOTE: The caller's thread ID must be OR'd with count.
 */

void sendMidiOut(register struct SOUNDDEVINFO * sounddev, register unsigned char * msg, register uint32_t count)
{
	register unsigned char				threadId;

	threadId = count & (MIDITHREADID|BEATTHREADID|GUITHREADID);
	count &= ~(MIDITHREADID|BEATTHREADID|GUITHREADID);

	// Make sure other threads aren't accessing "SoundDev[MIDIOUT_x]" at
	// this moment. If otherwise, let other thread first finish
	while (__atomic_or_fetch(&sounddev->Lock, threadId, __ATOMIC_RELAXED) != threadId) usleep(100);

	// Output open?
	if (sounddev->Handle)
	{
#ifndef NO_SEQ_SUPPORT
		if ((sounddev->DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_SEQ)
		{
			// Use ALSA Seq API
			snd_seq_event_t	ev;

			snd_seq_ev_clear(&ev);
			snd_seq_ev_set_source(&ev, sounddev->Card);
			snd_seq_ev_set_subs(&ev);
			snd_seq_ev_set_direct(&ev);
			do
			{
				ev.type = SeqTypes[(*msg >> 4) - 8];
//				snd_seq_ev_set_fixed(&ev);
				ev.data.note.channel = *msg++ & 0x0F;
				ev.data.note.note = *msg++;
				if (count >= 3 && !(msg[0] & 0x80))
				{
					count--;
					if (!(ev.data.note.velocity = *msg++) && ev.type == SND_SEQ_EVENT_NOTEON)
						ev.type = SND_SEQ_EVENT_NOTEOFF;
				}
				count -= 2;
				snd_seq_event_output(sounddev->Handle, &ev);
			} while (count);
			snd_seq_drain_output(sounddev->Handle);
		}
#ifndef NO_MIDI_OUT_SUPPORT
		else
#endif
#endif
#ifndef NO_MIDI_OUT_SUPPORT
			snd_rawmidi_write((snd_rawmidi_t *)sounddev->Handle, msg, count);
#endif
	}

	// Allow other threads access now
	__atomic_and_fetch(&sounddev->Lock, ~threadId, __ATOMIC_RELAXED);
}





/************** send_patch() ******************
 * Sends a patch change to an external synth.
 *
 * roboNum =	PLAYER_xxx.
 * pgm =			MIDI pgm change #
 * threadId =	Caller's ID. Bits 8 to 15 are the bank MSB to send
 * 				or SETINS_NO_MSB. Bits 16 to 24 are the bank
 * 				LSB to send, or SETINS_NO_LSB.
 */

void send_patch(register unsigned char roboNum, register unsigned char pgm, register uint32_t threadId)
{
	unsigned char						msg[8];
	register unsigned char *		msgPtr;
	register unsigned char			midichan;

	midichan = 0xB0 | MidiChans[roboNum];
	msgPtr = &msg[0];
	if (!(threadId & SETINS_NO_MSB))
	{
		*msgPtr++ = midichan;
		*msgPtr++ = 0x00;
		*msgPtr++ = (threadId >> 8) & 0xff;
	}
	if (!(threadId & SETINS_NO_LSB))
	{
		*msgPtr++ = midichan;
		*msgPtr++ = 0x20;
		*msgPtr++ = (threadId >> 16) & 0xff;
	}

	*msgPtr++ = midichan - 16;
	*msgPtr++ = pgm;
	sendMidiOut(DevAssigns[roboNum], &msg[0], (unsigned char)(msgPtr - &msg[0]) | (threadId & 0xFF));
}




/****************** closeMidiOut() *****************
 * Closes MIDI output if open.
 *
 * Called only by main (GUI) thread.
 */

void closeMidiOut(register struct SOUNDDEVINFO * sounddev)
{
	while (__atomic_or_fetch(&sounddev->Lock, GUITHREADID, __ATOMIC_RELAXED) != GUITHREADID) usleep(100);

#ifndef NO_SEQ_SUPPORT
	// Is it open?
	if (sounddev->Handle)
	{
		// If alsa seq...
		if ((sounddev->DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_SEQ)
		{
			snd_seq_event_t	ev;
			register unsigned char	musicianNum;

			// Send an ALL NOTES OFF
			snd_seq_ev_clear(&ev);
			snd_seq_ev_set_source(&ev, sounddev->Card);
			snd_seq_ev_set_subs(&ev);
			snd_seq_ev_set_direct(&ev);
			ev.type = SND_SEQ_EVENT_CONTROLLER;
         ev.data.control.param = 123;
			musicianNum = PLAYER_SOLO + 1;
			do
			{
				if (DevAssigns[--musicianNum] == sounddev)
				{
					ev.data.control.channel = MidiChans[musicianNum];
					snd_seq_event_output(sounddev->Handle, &ev);
				}
			} while (musicianNum);
			snd_seq_drain_output(sounddev->Handle);
		}
	}
#endif
	close_midi_port(sounddev);

	__atomic_and_fetch(&sounddev->Lock, ~GUITHREADID, __ATOMIC_RELAXED);
}



/******************** openMidiOut() *******************
 * Opens MIDI output device. This is done only if user
 * chooses to play the instruments on an external MIDI module,
 * or software synth, instead of our "Internal Synth"
 * implemented in AudioPlay.c
 *
 * RETURN: 0 if success.
 *
 * NOTE: Displays an error msg.
 *
 * Caller must closeMidiOut() first.
 *
 * Called only by main (GUI) thread.
 */

int openMidiOut(register struct SOUNDDEVINFO * sounddev)
{
	while (__atomic_or_fetch(&sounddev->Lock, GUITHREADID, __ATOMIC_RELAXED) != GUITHREADID) usleep(100);

	// Is a MIDI output dev chosen?
	if (sounddev->Card >= 0 && (sounddev->DevFlags & DEVFLAG_DEVTYPE_MASK))
	{
		register uint32_t	devnum;

		devnum = sounddev - &SoundDev[0];

#ifndef NO_SEQ_SUPPORT
		if ((sounddev->DevFlags & DEVFLAG_DEVTYPE_MASK) == DEVTYPE_SEQ)
		{
			// Use ALSA Seq API
			if (!(sounddev->Handle = openAlsaSeq(&WindowTitle[0]))) goto err;

			if ((sounddev->Card = snd_seq_create_simple_port((snd_seq_t *)sounddev->Handle, getPlayDestDevName(devnum),
					SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_SOFTWARE|SND_SEQ_PORT_TYPE_MIDI_GENERIC|SND_SEQ_PORT_TYPE_APPLICATION)) < 0)
			{
				close_midi_port(sounddev);
#ifndef NO_MIDI_OUT_SUPPORT
				goto err;
#else
err:			sprintf((char *)TempBuffer, "Can't open %s", getPlayDestDevName(devnum));
				__atomic_and_fetch(&sounddev->Lock, ~GUITHREADID, __ATOMIC_RELAXED);
				show_msgbox((char *)TempBuffer);
				return -1;
#endif
			}

			// If a client specified, connect to it
			if (sounddev->DevHash && snd_seq_connect_to((snd_seq_t *)sounddev->Handle, sounddev->Card, sounddev->Dev, sounddev->SubDev) < 0)
			{
				sprintf((char *)TempBuffer, "Can't connect %s to other software", getPlayDestDevName(devnum));
				goto err2;
			}
		}
#ifndef NO_MIDI_OUT_SUPPORT
		else
#endif
#endif
#ifndef NO_MIDI_OUT_SUPPORT
		{
			sprintf(GuiBuffer, &CardSpec[0], sounddev->Card, sounddev->Dev, sounddev->SubDev);
			if (snd_rawmidi_open(0, (snd_rawmidi_t **)&sounddev->Handle, GuiBuffer, 0) < 0)
			{
				sounddev->Handle = 0;
#ifndef NO_SEQ_SUPPORT
err:
#endif
				sprintf((char *)TempBuffer, "Can't open %s", getPlayDestDevName(devnum));
#ifndef NO_SEQ_SUPPORT
err2:
#endif
				__atomic_and_fetch(&sounddev->Lock, ~GUITHREADID, __ATOMIC_RELAXED);
				show_msgbox((char *)TempBuffer);
				return -1;
			}
		}
#endif
	}

	__atomic_and_fetch(&sounddev->Lock, ~GUITHREADID, __ATOMIC_RELAXED);
	return 0;
}

#endif	// !NO_MIDI_OUT_SUPPORT && !NO_SEQ_SUPPORT




/************** set_sustain() ******************
 * Turns sustain pedal on/off for specified robot.
 *
 * musicianNum = PLAYER_DRUM, PLAYER_BASS, etc.
 * flag = 1 for on, 0 for off
 */

uint32_t set_sustain(register unsigned char musicianNum, register unsigned char flag, register unsigned char threadId)
{
	if ((flag << musicianNum) != (SustainPedal & (0x01 << musicianNum)))
	{
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
		// Send to external synth?
		if (DevAssigns[musicianNum] >= &SoundDev[DEVNUM_MIDIOUT1])
		{
			unsigned char	msg[3];

			msg[0] = 0xB0 | MidiChans[musicianNum];
			msg[1] = 64;
			msg[2] = flag;
			sendMidiOut(DevAssigns[musicianNum], &msg[0], 3|threadId);
		}
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
		else
#endif
#endif
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
		if (!flag)
		{
			SustainPedal &= ~(0x01 << musicianNum);
			releaseSustain(musicianNum, threadId);

			// If no longer in play, and an accomp chord may still be sounding,
			// silence any lingering chord and indicate user is no
			// longer holding a chord
			if (!BeatInPlay && (PlayFlags & PLAYFLAG_CHORDSOUND))
				return clearChord(2|threadId);
		}
		else
			SustainPedal |= (0x01 << musicianNum);
	}
#endif
	return CTLMASK_NONE;
}





#ifdef MIDIFILE_PLAYBACK

/******************** unloadMidiFile() ******************
 * Frees loaded MIDI File.
 */

void unloadMidiFile(void)
{
	if (Tracking.TrackStart) free(Tracking.TrackStart);
	memset(&Tracking, 0, sizeof(TRACKING_INFO));
}





/********************* loadMidiFile() *****************
 * Loads a (specially formatted) MIDI File into memory.
 *
 * fn =	Name of MIDI file to load.
 *
 * RETURNS: 0 = success.
 */

unsigned char loadMidiFile(char *fn)
{
	register int			hFile;
	struct stat				buf;

	// Free any previously loaded MIDI file
	unloadMidiFile();

	// Open the MIDI file
	if ((hFile = open(fn, O_RDONLY|O_NOATIME)) == -1)
	{
		format_syserr(fn, PATH_MAX);
bad:	return 1;
	}

	// Get the sizeeventChordTrigger
	fstat(hFile, &buf);
	Tracking.SongLen = buf.st_size;

	// Allocate a buffer to read in the whole file
	if (!(Tracking.TrackStart = (unsigned char *)malloc(Tracking.SongLen)))
	{
		format_syserr(fn, PATH_MAX);
		close(hFile);
		goto bad;
	}

	// Read in the file, and close it
	buf.st_size = read(hFile, Tracking.TrackStart, Tracking.SongLen);
	close(hFile);

	if (buf.st_size != Tracking.SongLen)
	{
		show_msgbox("Failed reading the file");
		unloadMidiFile();
		goto bad;
	}

	return 0;
}

#endif





/******************** lockTempo() ********************
 * Arbitrates thread access to changing tempo.
 */

static unsigned char TempoLock = 0;

unsigned char lockTempo(register unsigned char threadId)
{
	return __atomic_or_fetch(&TempoLock, threadId, __ATOMIC_RELAXED);
}

void unlockTempo(register unsigned char threadId)
{
	__atomic_and_fetch(&TempoLock, ~threadId, __ATOMIC_RELAXED);
}

/******************** set_bpm() *********************
 * Sets micro PPQN from specified BPM. 0 queries the
 * current tempo.
 *
 * Note: Caller should lockTempo()
 */

unsigned char set_bpm(register unsigned char tempo)
{
	if (tempo)
	{
		if (TempoBPM != tempo) PrevTempoBPM = TempoBPM;
		TempoBPM = tempo;

		// Update tempo for beat play thread
		set_PPQN(tempo);

		cancel_midi_taptempo();
	}

	return TempoBPM;
}




/********************* set_PPQN() *********************
 * Sets the "milliseconds per seq tick" based upon tempo
 * in BPM.
 */

void set_PPQN(register unsigned char bpm)
{
	// Cancel any ritard/accel in progress
	PlayFlags &= ~(PLAYFLAG_RITARD|PLAYFLAG_ACCEL);
	TempoChangeCnt = 0;

	MsecsPerTick = 60000000/((uint32_t)bpm * (PPQN_VALUE * 1000));
}





unsigned char get_prev_bpm(void)
{
	return PrevTempoBPM;
}





void set_ritard_or_accel(register unsigned char flag)
{
	register unsigned char	newFlags;

#ifndef NO_MIDICLOCK_IN
	if (ClockId < 0xFE)
#endif
	{
		newFlags = 	PlayFlags & ~(PLAYFLAG_RITARD|PLAYFLAG_ACCEL);
		if (flag)
		{
			flag |= PLAYFLAG_ACCEL;
			if ((PlayFlags & flag) != flag) goto set;
		}
		else if (PlayFlags & PLAYFLAG_ACCEL)
set:		PlayFlags = flag | newFlags;
	}
}





/**************** signalBeatThread() ****************
 * Called by MIDI In or Main thread to signal Beat
 * Playback thread to wake up and start play.
 */

void signalBeatThread(register unsigned char threadId)
{
	if (PlayThreadHandle)
	{
		// Mute any lingering chord accomp, but don't clear pending chord
		if (PlayFlags & PLAYFLAG_CHORDSOUND) stopAllVoices(threadId);

		// Clear flags
		PlayFlags = 0;

		// Signal beat thread to wake up
		pthread_mutex_lock(&PlayMutex);
		pthread_cond_signal(&PlayCondition);
		pthread_mutex_unlock(&PlayMutex);
	}
}





/******************** clearChord() *******************
 * Initializes variables associated with chord play,
 * to indicate user is not playing/holding a chord.
 *
 * flag =	0 means don't cutoff any currently sounding
 * 			chord. Just mark that no chord has yet been
 * 			established. 1 means cutoff any sounding chord,
 * 			and indicate no chord. 2 is the same as 1, but
 * 			also unhighlight any key on the graphical piano.
 *
 * 			OR'd with GUITHREADID, BEATTHREADID, or MIDITHREADID
 */

uint32_t clearChord(register unsigned char flag)
{
	register uint32_t			guimask;
	register unsigned char	i;

	guimask = CTLMASK_NONE;
	if (flag & 0x03)
	{
		i = flag & 0xFC;
		if (lockInstrument(i) == i)
		{
			PlayFlags &= ~PLAYFLAG_CHORDSOUND;
			stopAllVoices(i);
		}
		unlockInstrument(i);

		if (flag & 0x02) guimask = lightPianoKey(0xFF);
	}

	// Init to no chord playing
	memset(&NextChordNotes[0], 0, sizeof(NextChordNotes));
	NextNoteAddMask = NextNumNotes = ChordChangedCnt = ChordPlayCnt = 0;
	NextScale = 0;

	return guimask;
}





/***************** initBeatThread() ******************
 * Launches a separate thread that times out each
 * drum/bass/guitar accompaniment event, and flags the
 * audio thread to play the event. We call this the
 * "Play Beat" thread.
 */

void initBeatThread(void)
{
	BeatInPlay = INPLAY_STOPPED;
	clearChord(0|GUITHREADID);
	ChordVel = 34;

	memcpy(MidiChans, MidiChansDef, sizeof(MidiChans));

	if (!getStyleCategory(0)) goto none;

	// Start up a background thread to play accomp
	if (pthread_create(&PlayThreadHandle, 0, playBeatThread, 0))
	{
		show_msgbox("Can't create beat play thread");
none:	PlayThreadHandle = 0;
	}
	else
		pthread_detach(PlayThreadHandle);
}





/******************** endBeatThread() *********************
 * Terminates the "beat play" thread and frees resources.
 */

void endBeatThread(void)
{
	register pthread_t	handle;

	if ((handle = PlayThreadHandle))
	{
		void *	status;

		// Alert beat thread to abruptly abort
		PlayThreadHandle = 0;
		pthread_mutex_lock(&PlayMutex);
		pthread_cond_signal(&PlayCondition);
		pthread_mutex_unlock(&PlayMutex);

		// Wait for playBeatThread() to terminate. It should do that
		// when it detects PlayThreadHandle = 0
		pthread_join(handle, &status);
	}

	allNotesOff(GUITHREADID);

#ifdef MIDIFILE_PLAYBACK
	unloadMidiFile();
#endif

	pthread_cond_destroy(&PlayCondition);
	pthread_mutex_destroy(&PlayMutex);
}






/***************** setTranspose() ******************
 * Sets transpose amount += 5 half steps where 0 =
 * -5, 5 = 0, 10 = +5.
 *
 * amt = 0xFF queries transpose amt in range 0 to
 * 10. 0xFE queries in range -5 to 5.
 */

unsigned char setTranspose(register unsigned char amt, register unsigned char threadId)
{
	if (amt <= 10)
	{
		allNotesOff(threadId);
		Transpose = (char)amt - 5;
	}

	if (amt == 0xFE) return Transpose;
	return (unsigned char)(Transpose + 5);
}

unsigned char setConfigTranspose(register unsigned char amt)
{
	if (amt <= 10)
		ConfigTranspose = (char)amt - 5;
	return (unsigned char)(ConfigTranspose + 5);
}





/***************** setMidiSendChan() ******************
 * Sets MIDI chan to transmit upon.
 * Called by Main thread.
 *
 * roboNum = 0 for drums, 1 bass, 2 gtr, 3 pad, 4 solo
 */

unsigned char setMidiSendChan(register uint32_t roboNum, register unsigned char chan)
{
	if (chan < 16) MidiChans[roboNum] = chan;
	return MidiChans[roboNum];
}





/********************* saveAccompConfig() *******************
 * Called by saveConfigOther() to save app-specific data to
 * the config file.
 *
 * buffer =		MAX_PATH size buffer to format data in.
 *
 * RETURN: Ptr to end of bytes formatted.
 */

unsigned char * saveAccompConfig(register unsigned char * buffer)
{
#ifndef NO_MIDI_IN_SUPPORT
	if (MasterMidiChan != 16)
	{
		*buffer++ = CONFIGKEY_MASTERCHAN;
		*buffer++ = MasterMidiChan;
	}
	if (SplitPoint != 56)
	{
		*buffer++ = CONFIGKEY_SPLIT;
		*buffer++ = SplitPoint;
	}
	if (CmdSwitchNote < 128)
	{
		*buffer++ = CONFIGKEY_CMDSPLIT;
		*buffer++ = CmdSwitchNote;
	}
	if (BassOct != 28)
	{
		*buffer++ = CONFIGKEY_BASSOCT;
		*buffer++ = BassOct;
	}
	if (VelCurve)
	{
		*buffer++ = CONFIGKEY_VELCURVE;
		*buffer++ = VelCurve;
	}
#endif
	{
	register unsigned char	i;

	for (i=0; i <= PLAYER_SOLO; i++)
	{
		if (MidiChans[i] != MidiChansDef[i])
		{
			*buffer++ = CONFIGKEY_CHAN + i;
			*buffer++ = MidiChans[i];
		}
	}
	}
	if (ConfigTranspose)
	{
		*buffer++ = CONFIGKEY_TRANSPOSE;
		*buffer++ = ConfigTranspose;
	}
	if (ChordBoundary != 24*2)
	{
		*buffer++ = CONFIGKEY_CHORDBOUND;
		*buffer++ = ChordBoundary;
	}

	if (!(AppFlags & (APPFLAG_2KEY|APPFLAG_1FINGER)))
	{
		if (Sensitivity != 1)
		{
			*buffer++ = CONFIGKEY_SENSITIVITY;
			*buffer++ = Sensitivity;
		}
		if (ChordTrigger != 0x90)
		{
			*buffer++ = CONFIGKEY_DRUMTRIGGER;
			*buffer++ = ChordTrigger;
		}
	}
	return buffer;
}

/********************* loadAccompConfig() *******************
 * Called by loadConfigOther() to load app-specific data from the
 * config file.
 *
 * ptr =		Data to parse.
 * len =		# of bytes.
 *
 * RETURN: # of bytes parsed, 0 if unknown opcode, -1 if truncated.
 */

int loadAccompConfig(register unsigned char * ptr, register unsigned long len)
{
	if (ptr[0] >= CONFIGKEY_CHAN && ptr[0] <= CONFIGKEY_CHAN + PLAYER_SOLO)
	{
		MidiChans[ptr[0] - CONFIGKEY_CHAN] = ptr[1];
ret1:	return 1;
	}

	switch (*ptr++)
	{
		case CONFIGKEY_MASTERCHAN:
		{
#ifndef NO_MIDI_IN_SUPPORT
			MasterMidiChan = ptr[0];
#endif
			goto ret1;
		}
		case CONFIGKEY_SPLIT:
#ifndef NO_MIDI_IN_SUPPORT
			SplitPoint = ptr[0];
#endif
			goto ret1;
		case CONFIGKEY_CMDSPLIT:
#ifndef NO_MIDI_IN_SUPPORT
			CmdSwitchNote = ptr[0];
#endif
			goto ret1;
		case CONFIGKEY_VELCURVE:
#ifndef NO_MIDI_IN_SUPPORT
			VelCurve = ptr[0];
#endif
			goto ret1;
		case CONFIGKEY_CHORDBOUND:
			ChordBoundary = ptr[0];
			goto ret1;
		case CONFIGKEY_TRANSPOSE:
			ConfigTranspose = Transpose = ptr[0];
			goto ret1;
		case CONFIGKEY_BASSOCT:
			BassOct = ptr[0];
			goto ret1;
		case CONFIGKEY_SENSITIVITY:
			Sensitivity = ptr[0];
			goto ret1;
		case CONFIGKEY_DRUMTRIGGER:
			ChordTrigger = ptr[0];
			goto ret1;
	}

	return 0;
}
