#include <stdint.h>
#include "PickDevice.h"

extern unsigned char	MidiChans[5];
extern unsigned char	BeatInPlay;
extern unsigned char	PlayFlags;
extern const unsigned char *	GtrEvtPtr;
extern const unsigned char *	BassEvtPtr;
extern unsigned char	ChordPlayCnt;
extern unsigned char	Sustain;
extern unsigned char	GtrStrings[6];
extern unsigned char	ChordVel;
extern unsigned char	VelCurve;
extern unsigned char	ChordBoundary;
extern const unsigned char	Comp1VelCurve[127];
extern unsigned char ChordTrigger;
extern unsigned char Sensitivity;

void				clear_note(void);
unsigned char	lockChord(register unsigned char);
void				unlockChord(register unsigned char);
void				advance_midiclock(void);
unsigned char	is_midiclock(void);
uint32_t			pickChord(register unsigned char, register unsigned char, register unsigned char);
uint32_t			getCurrChord(void);
void				set_clock_type(void);
uint32_t			get_current_clock(void);
void				songChordChange(register unsigned char, register unsigned char);
uint32_t			clearChord(register unsigned char);
unsigned char	set_bpm(register unsigned char);
unsigned char	get_prev_bpm(void);
void				set_PPQN(register unsigned char);
void				set_ritard_or_accel(register unsigned char);
void				initBeatThread(void);
void				endBeatThread(void);
void				signalBeatThread(register unsigned char);
void				closeMidiOut(register struct SOUNDDEVINFO *);
int				openMidiOut(register struct SOUNDDEVINFO *);
uint32_t			mute_robots(register unsigned char, register unsigned char);
uint32_t			mute_playing_chord(register unsigned char);
uint32_t			midiChordTrigger(register unsigned char, register unsigned char);
uint32_t			eventChordTrigger(register unsigned char, register unsigned char, register unsigned char);
void				sendMidiOut(register struct SOUNDDEVINFO *, register unsigned char *, register uint32_t);
uint32_t			allNotesOff(register unsigned char);
void				send_patch(register unsigned char, register unsigned char, register uint32_t);
uint32_t			set_sustain(register unsigned char, register unsigned char, register unsigned char );
unsigned char	setTranspose(register unsigned char, register unsigned char);
unsigned char	setConfigTranspose(register unsigned char);
unsigned char	setMidiSendChan(register uint32_t, register unsigned char);
unsigned char * saveAccompConfig(register unsigned char *);
int				loadAccompConfig(register unsigned char *, register unsigned long);
unsigned char	lockGui(register unsigned char);
void				unlockGui(register unsigned char);
void				signalMainFromMidiIn(register void *, register uint32_t);
void				checkStartPadNotes(void);
void				send_alloff(register uint32_t, register unsigned char);
unsigned char	setChordSensitivity(register unsigned char);
void				close_midi_port(register struct SOUNDDEVINFO *);
unsigned char	lockTempo(register unsigned char);
void				unlockTempo(register unsigned char);

#define PLAYFLAG_STOP			0x01
#define PLAYFLAG_FILLPLAY		0x02
#define PLAYFLAG_FINAL_PTN		0x04
#define PLAYFLAG_CHORDSOUND	0x08
#define PLAYFLAG_STYLEJUMP		0x10
#define PLAYFLAG_ACCEL			0x20
#define PLAYFLAG_RITARD			0x40
#define PLAYFLAG_END_FADE		0x80

// BeatInPlay
#define INPLAY_STOPPED			0x00
#define INPLAY_RUNNING			0x01
#define INPLAY_USERMUTEBASS	0x02
#define INPLAY_USERMUTEGTR		0x04
#define INPLAY_USERMUTEPAD		0x08
#define INPLAY_TURNING_OFF		0x10

#define PPQN_VALUE	24
