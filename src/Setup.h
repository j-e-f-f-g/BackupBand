#include <stdint.h>

void setMidiCmdChordModel(register unsigned char);
uint32_t ctl_add_asn(register GUICTL *);
const char * getChanStr(register unsigned char, register char *);
void init_setup_menu(void);
void handle_setup_menu(register unsigned char);
const char * getGmCtl(unsigned char);
void numToPitch(void *, unsigned char);
void	doPickIntSynthDev(void);
void doPickOutDev(register struct SOUNDDEVINFO *, register VOIDRETFUNC);
void	doSetupScreen(void);
void	showSetupScreen(void);
void	positionSetupGui(void);
const char * getPlayDestDevName(register unsigned char);
const char * getMusicianName(register unsigned char);
void endSplitPoint(void);
void showSetupHelp(void);
void updateMidiViewStop(void);
void doNoteScreen(register const char *, register unsigned char);
void endNotePoint(void);
void ctl_set_default(register GUICTL *);
int isRobotSetup(void);
void play_off_state(void);
extern VOIDRETFUNC 	ReturnFunc;

