#include <stdint.h>

#define MIDITHREADID		0x80
#define BEATTHREADID		0x40
#define GUITHREADID		0x20
#define CLOCKTHREADID	0x10
#define AUDIOTHREADID	0x01

// For PLAYZONE_INFO's Flags
#define PLAYZONEFLAG_HHOPEN			0x01
#define PLAYZONEFLAG_HHPEDALOPEN		0x02
#define PLAYZONEFLAG_HHPEDALCLOSE	0x04
#define PLAYZONEFLAG_HHCLOSED			0x08
#define PLAYZONEFLAG_HHHALFOPEN		0x10
#define PLAYZONEFLAG_TOUCH_TRIGGER	0x40
#define PLAYZONEFLAG_CC_TRIGGER		0x80
#define PLAYZONEFLAG_SUSLOOP			0x10
#define PLAYZONEFLAG_ALWAYS_FADE		0x20

void				changeGtrChord(void);
void				setReverb(GUICTL *, register uint32_t);
uint32_t			getReverb(register uint32_t);
void				copy_gtrnotes(register const unsigned char *);
const char *	open_libjack(void);
void				ignoreErrors(void);
uint32_t			xrun_count(register int32_t);
void				show_audio_error(register unsigned char);
void				initAudioVars(void);
void				loadInstrument(char *, uint32_t, unsigned char);
void				loadDataSets(register unsigned char);
void				freeAudio(register unsigned char);
void				toggleReverb(register char);
uint32_t			setReverbVol(register char);
unsigned char	setSampleRateFactor(register unsigned char);
unsigned char	allocAudio(void);
uint32_t			setMasterVol(register unsigned char);
unsigned char	getMasterVol(void);
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
uint32_t			setInstrumentVol(register uint32_t, register unsigned char, register unsigned char);
#else
uint32_t			setInstrumentVol(register uint32_t, register unsigned char);
#endif
unsigned char	getInstrumentVol(register uint32_t);
unsigned char	startDrumNote(unsigned char, unsigned char, unsigned char, unsigned char);
void				startBassNote(unsigned char, unsigned char, unsigned char);
void				stopBassNote(register unsigned char, register unsigned char);
unsigned char	getNumOfInstruments(register unsigned char);
void				stopAllBassNotes(register unsigned char);
#define SETINS_BYINDEX 		0x80000000
#define SETINS_SHOWHIDDEN	0x40000000
#define SETINS_NO_MSB		0x00008000
#define SETINS_NO_LSB		0x00800000
uint32_t		 	setInstrumentByNum(register uint32_t, register unsigned char, void **);
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
uint32_t			setInstrumentByPtr(register void *, register uint32_t, register unsigned char);
#else
uint32_t			setInstrumentByPtr(register void *, register uint32_t);
#endif
uint32_t			findSoloInstrument(register char);
uint32_t		 	changePadInstrument(register unsigned char);
void *			getPadCached(register unsigned char);
unsigned char	getPadPgmNum(void);
void				startPadVoices(unsigned char *, unsigned char);
void				stopPadVoices(register unsigned char);
uint32_t			fadePadVoices(register unsigned char);
void				startSoloNote(unsigned char, unsigned char, unsigned char);
void				stopSoloNote(register unsigned char, unsigned char);
void				stopAllSoloNotes(register unsigned char);
void				releaseSustain(register unsigned char, register unsigned char);
void				stopGtrString(register unsigned char, unsigned char);
void				startGtrString(register unsigned char, register unsigned char, register unsigned char, unsigned char);
void				stopAllVoices(register unsigned char);
unsigned char	toggle_solo_vol(register unsigned char, register unsigned char);
void *			getCurrentInstrument(register uint32_t);
void *			getNextInstrument(register void *, register uint32_t);
const char *	getInstrumentName(register void *);
const char *	isHiddenPatch(register void *, register uint32_t);
unsigned char * saveAudioConfig(register unsigned char *);
int 				loadAudioConfig(register unsigned char *, register unsigned long);
void				openConfigDevs(void);
void				hashToAlsaDevs(void);
void				clear_banksel(void);
void				loadDeviceConfig(void);
void				saveDeviceConfig(void);
void				set_thread_priority(void);
unsigned char	lockInstrument(register unsigned char);
void				unlockInstrument(register unsigned char);

extern const char			CardCtlSpec[];
extern unsigned char		SustainPedal;
extern unsigned char		LegatoPedal;
extern unsigned char		BankNums[5*2];
extern const char			CardSpec[];
extern unsigned char		PickAttack;
extern const char *		NamePtr;
extern unsigned char		WavesLoadedFlag;

#include "PickDevice.h"

extern struct SOUNDDEVINFO			SoundDev[7];
extern struct SOUNDDEVINFO *		DevAssigns[5];

// DevAssigns[]
#define PLAYER_DRUMS	0
#define PLAYER_BASS	1
#define PLAYER_GTR	2
#define PLAYER_PAD	3
#define PLAYER_SOLO	4

// SoundDev[]
#define DEVNUM_AUDIOIN		0
#define DEVNUM_AUDIOOUT		1
#define DEVNUM_MIDIOUT1		2
#define DEVNUM_MIDIOUT2		3
#define DEVNUM_MIDIOUT3		4
#define DEVNUM_MIDIOUT4		5
#define DEVNUM_MIDIIN		6
