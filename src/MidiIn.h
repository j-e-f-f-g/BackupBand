#include <stdint.h>
extern void *			MidiInSigHandle;
extern unsigned char	HHvalue;
extern unsigned char	CmdSwitchNote;
extern unsigned char	SplitPoint;
extern unsigned char	MasterMidiChan;
extern char				BassOct;
extern unsigned char *	BytePtr;

typedef void * (MIDIINSIPHON)(register void *, register unsigned char *);

// The ID for each category of actions
#define MAPID_MIDICHORD			0
#define MAPID_TEMPO				1
#define MAPID_PLAY				2
#define MAPID_TRANSPOSE			3
#define MAPID_STYLE				4
#define MAPID_VARIATION			5
#define MAPID_SOLOPATCH			6
#define MAPID_SUSTAIN			7
#define MAPID_DRUMS				8
#define MAPID_BASS				9
#define MAPID_GTR					10
#define MAPID_PAD					11
#define MAPID_MUTE				12
#define MAPID_REVERB				13
#define MAPID_SONGSHEET			14
#define MAPID_MISC				15

unsigned char setMouseBtn(register char *, register unsigned char);
void doMouseChordBtn(void);
unsigned char isNoteAssign(void);
uint32_t cancel_midi_taptempo(void);
char * copystr(register char *, register const char *);
void drawActionAssign(register uint32_t, register uint32_t);
void startMidiTest(void);
uint32_t transpose(register unsigned char, register unsigned char);
int makeAsnHelpStr(void);
void saveCmdAssigns(void);
void loadCmdAssigns(char *, uint32_t);
void chooseActionsList(register unsigned char);
void doMidiMsgAsn(void);
void doMidiNoteAsn(void);
void doMouseBtnAsn(void);
void doPcKeyAsn(void);
int	openMidiIn(void);
void	closeMidiIn(void);
void setMidiInSiphon(register MIDIINSIPHON *, register unsigned char);
void updChansInUse(void);
void endCmdMode(void);
void positionCmdAsnGui(void);
unsigned char doMouseorKeyCmd(register uint32_t, register unsigned char);
void initMidiInVars(void);
void resetActionsList(void);
void eraseSelAction(void);
void freeActionAssigns(void);
void midiviewDone(void);
void updateMidiClock(void);
void updateMidiView(void);
int isMidiInSiphon(register MIDIINSIPHON *);
void * isActionSelected(void);
GUILIST * drawActionsList(GUIAPPHANDLE, GUICTL *, GUIAREA *);
const char * getCmdKeyStr(void);
