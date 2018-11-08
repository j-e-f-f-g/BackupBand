// X Windows and cairo for GUI
//#define APPCTLDATATYPE struct _GUICTLDATA
#include "GuiCtls.h"

// ALSA for midi/audio
#include <alsa/asoundlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/param.h>
#include <pthread.h>
#include <sched.h>

#include "Utility.h"

// Builds a special custom modification for 1 particular person. Not meant for others. Don't
// uncomment unless you're you-know-who
//#define GIGGING_DRUMS	1

#pragma pack(1)
typedef struct
{
	void (*DrawFunc)(void);
	void (*MouseFunc)(register GUIMSG *);
	void (*KeyFunc)(register GUIMSG *);
	void (*ResizeFunc)(void);
} GUIFUNCS;

typedef struct
{
	uint32_t 		(*UpdateFunc)(register GUICTL *);
	uint32_t			(*SetFunc)(register GUICTL *);
} GUICTLDATA;

#pragma pack()

// ======================== Functions ==========================
void			fixup_setup_ctls(register GUICTL *);
void			move_window(void);
void		 	display_syserr(register int);
void			show_msgbox(register const char *);
void			clearMainWindow(void);
void			set_color_black(void);
void			set_color_red(void);
void			setMainTitle(register uint32_t);
void			dummy_keypress(register GUIMSG *);
void			dummy_drawing(void);
void			do_msg_loop(void);
void			set_large_text(void);
void			set_small_text(void);
void			update_shown_mask(void);
void			signal_stop(register void *, register uint32_t);
void			doPickItemDlg(register GUILISTFUNC *, register GUIFUNCS *);
GUICTL *		showPickItemList(register GUILISTFUNC *);
GUILISTFUNC * setListDrawFunc(register GUILISTFUNC *);
uint32_t		lightPianoKey(register uint32_t);
void			append_list_ctl(register GUICTL *);
void			handle_panel_mouse(register GUIMSG *);

// ===================== Global variables ======================
extern GUIAPPHANDLE	GuiApp;
extern GUIWIN *		MainWin;
extern unsigned char	GuiLoop;
extern GUIFUNCS *		GuiFuncs;
extern const char		WindowTitle[];
extern const char		TransStr[];
extern const char		BassStr[];
extern const char		ReverbStr[];
extern const char		GuitarStr[];
extern const char		DrumsStr[];
extern const char		VolStr[];
extern const char		AutoStartStr[];
extern GUILIST			List;
extern char *			GuiBuffer;
extern unsigned char	SaveConfigFlag;

#define CTLID_DRUMVOL		0
#define CTLID_BASSVOL		(CTLID_DRUMVOL+1)
#define CTLID_GTRVOL			(CTLID_BASSVOL+1)
#define CTLID_PADVOL			(CTLID_GTRVOL+1)
#define CTLID_PATCHVOL		(CTLID_PADVOL+1)
#define CTLID_DRUMLIST		5
#define CTLID_BASSLIST		(CTLID_DRUMLIST+1)
#define CTLID_GTRLIST		(CTLID_BASSLIST+1)
#define CTLID_BACKINGPAD	(CTLID_GTRLIST+1)
#define CTLID_PATCHLIST		(CTLID_BACKINGPAD+1)
#define CTLID_DRUMMUTE		10
#define CTLID_BASSMUTE		(CTLID_DRUMMUTE+1)
#define CTLID_GTRMUTE		(CTLID_BASSMUTE+1)
#define CTLID_ACCOMPMUTE	(CTLID_GTRMUTE+1)
#define CTLID_MASTERVOL		(CTLID_ACCOMPMUTE+1)
#define CTLID_CHORDS				15
#define CTLID_TEMPO				16
#define CTLID_TAPTEMPO			17
#define CTLID_VARIATION			18
#define CTLID_VOLBOOST			19
#define CTLID_PLAY				20
#define CTLID_STYLES				21
#define CTLID_TRANSPOSE			22
#define CTLID_REVERBMUTE		23
#define CTLID_SONGSHEET			24
#define CTLID_AUTOSTART			25
#define CTLID_REVERBVOL			26
#define CTLID_CYCLEVARIATION	27
#define CTLID_SONGSHEETLIST	28
#define CTLID_CMDNOTEBROWSE	29
#define CTLID_XRUN				30
#define CTLID_BEATSTOP			31
// The following ctls don't need visual updating
// after a secondary thread changes their target
// value
#define CTLID_RITARD				32
#define CTLID_ACCELERANDO		33
#define CTLID_PANIC				34
#define CTLID_CLOCK				35

// The bitmasks for ShownCtlsMask
#define CTLMASK_DRUMVOL		(1 << CTLID_DRUMVOL)
#define CTLMASK_BASSVOL		(1 << CTLID_BASSVOL)
#define CTLMASK_GTRVOL			(1 << CTLID_GTRVOL)
#define CTLMASK_PADVOL			(1 << CTLID_PADVOL)
#define CTLMASK_PATCHVOL		(1 << CTLID_PATCHVOL)
#define CTLMASK_MASTERVOL		(1 << CTLID_MASTERVOL)
#define CTLMASK_DRUMLIST		(1 << CTLID_DRUMLIST)
#define CTLMASK_BASSLIST		(1 << CTLID_BASSLIST)
#define CTLMASK_GTRLIST			(1 << CTLID_GTRLIST)
#define CTLMASK_BACKINGPAD		(1 << CTLID_BACKINGPAD)
#define CTLMASK_PATCHLIST		(1 << CTLID_PATCHLIST)
#define CTLMASK_DRUMMUTE		(1 << CTLID_DRUMMUTE)
#define CTLMASK_BASSMUTE		(1 << CTLID_BASSMUTE)
#define CTLMASK_GTRMUTE			(1 << CTLID_GTRMUTE)
#define CTLMASK_ACCOMPMUTE		(1 << CTLID_ACCOMPMUTE)
#define CTLMASK_TEMPO			(1 << CTLID_TEMPO)
#define CTLMASK_TAPTEMPO		(1 << CTLID_TAPTEMPO)
#define CTLMASK_VARIATION		(1 << CTLID_VARIATION)
#define CTLMASK_VOLBOOST		(1 << CTLID_VOLBOOST)
#define CTLMASK_PLAY				(1 << CTLID_PLAY)
#define CTLMASK_STYLES			(1 << CTLID_STYLES)
#define CTLMASK_CHORDS			(1 << CTLID_CHORDS)
#define CTLMASK_TRANSPOSE		(1 << CTLID_TRANSPOSE)
#define CTLMASK_REVERBMUTE		(1 << CTLID_REVERBMUTE)
#define CTLMASK_SONGSHEET		(1 << CTLID_SONGSHEET)
#define CTLMASK_AUTOSTART		(1 << CTLID_AUTOSTART)
#define CTLMASK_REVERBVOL		(1 << CTLID_REVERBVOL)
#define CTLMASK_CYCLEVARIATION	(1 << CTLID_CYCLEVARIATION)
#define CTLMASK_SONGSHEETLIST	(1 << CTLID_SONGSHEETLIST)
#define CTLMASK_CMDNOTEBROWSE (1 << CTLID_CMDNOTEBROWSE)
#define CTLMASK_XRUN				(1 << CTLID_XRUN)
#define CTLMASK_BEATSTOP		(1 << CTLID_BEATSTOP)
#define CTLMASK_RITARD			0
#define CTLMASK_ACCELERANDO	0
#define CTLMASK_PANIC			0
#define CTLMASK_CLOCK			0

#define CTLMASK_SETCONFIG_OTHER CTLMASK_CMDNOTEBROWSE
#define CTLMASK_SETCONFIGSAVE	CTLMASK_XRUN
#define CTLMASK_NOREDRAW		CTLMASK_BEATSTOP
#define CTLMASK_NONE				0

#define CTLSTR_TEMPO			0x654A4C60
#define CTLSTR_PATCHLIST	0x487EE589
#define CTLSTR_BACKINGPAD	0xDF23BC62
#define CTLSTR_VARIATION	0x83BF1C5C
#define CTLSTR_VOLBOOST		0x656FF4B8
#define CTLSTR_PATCHVOL		0x9069381B
#define CTLSTR_PLAY			0xABDC5960
#define CTLSTR_STYLES		0xA006361D
#define CTLSTR_CHORDS		0x6147F3CC
#define CTLSTR_TRANSPOSE	0x81D5FE06
#define CTLSTR_REVERBMUTE	0xD6AA9F83
#define CTLSTR_DRUMMUTE		0x09A42922
#define CTLSTR_BASSMUTE		0xC2C4C4E2
#define CTLSTR_GTRMUTE		0xCF3E9F40
#define CTLSTR_ACCOMPMUTE	0x2B8489C7
#define CTLSTR_MASTERVOL	0xE3B8F935
#define CTLSTR_SONGSHEET		0xDC21EF6B
#define CTLSTR_SONGSHEETLIST	0x369A2D04
#define CTLSTR_PANIC			0xA05AAF91
#define CTLSTR_CLOCK			0x121DBC01
#define CTLSTR_AUTOSTART		0xDA517681
#define CTLSTR_REVERBVOL		0x87CA32C2
#define CTLSTR_XRUN			0xDD87D1A8
#define CTLSTR_GTR_PATCH		0x819FBCE4
#define CTLSTR_CYCLEVARIATION	0x496EE2D2
#define CTLSTR_TAPTEMPO			0x054534A9
#define CTLSTR_RITARD			0x28FEEFF7
#define CTLSTR_ACCELERANDO	0xE0B03993
#define CTLSTR_DRUMVOL		0x4B00C5B7
#define CTLSTR_BASSVOL		0x3A9256C6
#define CTLSTR_PADVOL		0x6C40D9D7
#define CTLSTR_GTRVOL		0x4798FF03
#define CTLSTR_BASSLIST		0x77D9AA4A
#define CTLSTR_DRUMLIST		0x25ED5CBD
#define CTLSTR_GTRLIST		0x93087E4C
#define CTLSTR_BASSLIST		0x77D9AA4A
#define CTLSTR_DRUMBTN		0xDEFBEA52
#define CTLSTR_GTRBTN		0x3C8FE863
#define CTLSTR_BASSBTN		0x88E881E4
#define CTLSTR_PADBTN		0x899CAF72
#define CTLSTR_PATCHBTN		0xC335C493

#define APPFLAG_BASS_ON			0x01
#define APPFLAG_BASS_LEGATO	0x02
#define APPFLAG_FULLKEY			0x04
#define APPFLAG_1FINGER			0x08
#define APPFLAG_2KEY				0x10
#define APPFLAG_WINDCTL			0x20
#define APPFLAG_GTRCHORD		0x40
#define APPFLAG_DRUMPAD			0x80
extern unsigned char				AppFlags;

#define APPFLAG2_CLOCKMASK		0x03
#define APPFLAG2_CLOCK_NORMAL	 0
#define APPFLAG2_CLOCK_FAST	 1
#define APPFLAG2_CLOCK_FASTEST 2
#define APPFLAG2_CLOCK_MIDI	 3
#define APPFLAG2_TIMED_ERR		0x04
#define APPFLAG2_QWERTYKEYS	0x08
#define APPFLAG2_POLARITY		0x10
#define APPFLAG2_STEREO_IN		0x20
#define APPFLAG2_PATCHCHG_STOPPED	0x40
#define APPFLAG2_PATCHCHG_MANUAL		0x80
extern unsigned char				AppFlags2;

#define APPFLAG3_NODRUMS		0x01
#define APPFLAG3_NOBASS			0x02
#define APPFLAG3_NOGTR			0x04
#define APPFLAG3_NOPAD			0x08
#define APPFLAG3_NOVARCYCLE	0x10
#define AUTOSTART_SHIFT 6
#define APPFLAG3_NOAUTOSTART	0x20
#define TEMPFLAG_AUTOSTART		0x20
#define APPFLAG3_AUTOSTARTARM	0x40
#define APPFLAG3_NOREVERB		0x80
extern unsigned char				AppFlags3;
extern unsigned char				TempFlags;
#define ACCOMP_ON					0x40
#define ACCOMP_TOGGLE			0x80

#define APPFLAG4_NOCHORDHOLD		0x01
#define APPFLAG4_UPPER_PAD		0x02
#define APPFLAG4_NO_TIPS		0x04
#define APPFLAG4_CMD_ALWAYS_ON 0x08
extern unsigned char				AppFlags4;

const char *	play_button_label(GUIAPPHANDLE, GUICTL *, char *);
uint32_t			ctl_update_nothing(register GUICTL *);
unsigned char	setCountOff(register void *, register char);
char *			headingCopyTo(register const char *, register char *);
void				headingShow(register unsigned char);
void 				doLoadScreen(void);
void				showMainScreen(void);
void				adjustTranspose(register GUICTL *);
unsigned char	setAudioDevErrNum(register void *, register unsigned char);
uint32_t			setVariationBlink(void);
void				setShownCtls(register uint32_t);
void 				calcMinBox(register unsigned char);
void				doLoadDevScreen(void);
void				setAppFlags(register unsigned char);
void				testAppFlags(register GUICTL *, register unsigned char);
unsigned char	getNumOfStyles(void);
uint32_t 		drawGuiCtl(register void *, register uint32_t, register unsigned char);
GUILIST *		draw_songsheets(GUIAPPHANDLE, GUICTL *, GUIAREA *);
uint32_t			setTempoLabel(register unsigned char);
GUICTLDATA *	getVolCtlDataPtr(register unsigned char);
int				isMainScreen(void);
void				clearLoadScreen(void);

typedef void (*GUINUMRETFUNC)(register GUINUMPAD *);
void doNumeric(GUINUMRETFUNC, const char *, uint32_t);
typedef void (*VOIDRETFUNC)(void);

#define SIGNALMAIN_NOTE_ERR_FIRST	1
#define SIGNALMAIN_NOTE_ERR_LAST		127
#define SIGNALMAIN_REDRAW		128
#define SIGNALMAIN_AUDIO_ERR	129
#define SIGNALMAIN_LOAD			130
#define SIGNALMAIN_MIDIIN		131
#define SIGNALMAIN_MIDICLOCK	132
#define SIGNALMAIN_MIDIVIEW	133
#define SIGNALMAIN_MIDIVIEW2	134
#define SIGNALMAIN_CMDSWITCHERR 135
#define SIGNALMAIN_CMDMODE_SEL 136
#define SIGNALMAIN_LOADMSG_BASE 0x80000000

#define GUIBTN_EDIT	GUIBTN_ABORT
#define GUIBTN_EDIT_SHOW	GUIBTN_ABORT_SHOW
