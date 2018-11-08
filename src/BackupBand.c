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
#include "Setup.h"
#include "MidiIn.h"
#include "AccompSeq.h"
#include "StyleData.h"
#include "AudioPlay.h"
#include "Prefs.h"
#include "FileLoad.h"
#include "SongSheet.h"
#include "Editor.h"



#pragma pack(1)

struct GUIPANEL {
	struct GUIPANEL * Next;
	GUICTL				Ctls[1];
};

typedef struct
{
	GUICTLDATA		Funcs;
	union {
	const void *	Label;
	const char *	(*BtnLabel)(GUIAPPHANDLE, struct _GUICTL *, char *);
	void				(*AreaDraw)(GUIAPPHANDLE, struct _GUICTL *, GUIAREA *);
	GUILIST *		(*ListDraw)(GUIAPPHANDLE, struct _GUICTL *, GUIAREA *);
	};
	uint32_t			TypeStringCrc;
	unsigned char	TypeId;
	unsigned char	LayoutFlags;
	unsigned char	GuiCtlType;
	unsigned char	DrawFlags;
	unsigned char	PlayerNeeded;
	unsigned char	Unused;
} GUICTLDATA2;

#pragma pack()


//============== CONST DATA ===============

static const char PresetStrs[] = {1, 1, 1, 1, 1,
'A','b','o','r','t',0,	// GUIBTN_LOAD
1, 1, 1,
GUICOLOR_LIGHTGOLD, 'E','x','i','t', ' ', 'S','e','t','u','p', 0,    // GUIBTN_COPY
1, 1, 1, 1, 0};

const char			WindowTitle[] = "Backup Band";

static const char PanicStr[] = "Panic";
static const char AccelStr[] = "Accel.";
static const char RitardStr[] = "Ritard";
static const char CycleStr[] = "Cycle";
const char			BassStr[] = "Bass";
const char			GuitarStr[] = "Guitar";
const char			DrumsStr[] = "Drums";
const char			AutoStartStr[] = "AutoStart";
const char			ReverbStr[] = "Reverb";
const char			VolStr[] = "Volume";
static const char	PlayStr[] = "****\0PLAY\0stop\0ending ...";
static const char	PadStr[] = "None\0Strings\0Brass\0Organ\0Background Pad";
const char			TransStr[] = {'T','r','a','n','s','p','o','s','e',0,'-','5',0,'-','4',0,'-','3',0,'-','2',0,'-','1',0,'0',0,'1',0,'2',0,'3',0,'4',0,'5',0};
static const char AccompStr[] = "Accomp";
static const char VariationStr[] = "Intro\0Verse\0Chorus\0Bridge";
static const char VolBoostStr[] = "Vol Boost";
static const char SongSheetStr[] = "Song\nSheet";
static const char TapStr[] = "Tempo\nTap";

static const unsigned char ChordMap[] = {0,5,1,0x80,0x82,0x81,0x40,0x42,0x41,0xc0,0xc2,0xc1,3,4,6,0x83,0x84,7,0x85,0x45,0xc5};








//============== DATA ===============
GUIAPPHANDLE				GuiApp;
GUIWIN *						MainWin;
GUIFUNCS *					GuiFuncs;
char *						GuiBuffer;
static GUICTL *			TempoCtl;
static GUICTL *			ClockCtl;

static uint32_t			UpdateSigMask = 0;
static uint32_t			ShownCtlsMask;

GUILIST		List;

static GUICTL		ListGuiCtls[] = {
 	{.Type=CTLTYPE_AREA, .Flags.Local=CTLFLAG_AREA_LIST|CTLFLAG_AREA_FULL_SIZE|CTLFLAG_AREA_EVENHEIGHT, .Flags.Global=CTLGLOBAL_APPUPDATE|CTLGLOBAL_AUTO_VAL},
 	{.Type=CTLTYPE_ARROWS, .Flags.Local=CTLFLAG_AREA_HELPER, .Flags.Global=CTLGLOBAL_AUTO_VAL},
	{.Type=CTLTYPE_END},
};

static uint32_t	PatchBoxWidth, PatchMinBoxWidth;

// These must be in the same order as bit definitions of AppFlags/AppFlags2/AppFlags3
static const uint32_t AppFlagsBits[] = {
0x9276A791, // BASSON
0xF076FC81, // BASSLEGATO
0x3F224DDB, // FULLKEY
0xC488D71B, // 1FINGER
0xC00704CA, // 2KEY
0xC1B6F7E0, // WINDCTL
0xA001A4E0, // GTRCHORD
0x1501307D	// DRUMPAD
};

static const uint32_t AppFlags2Bits[] = {
0x1D84DFAA, // CLOCKFAST
0xA8DC8658, // CLOCKFASTEST
0x2304F23F, // TIMEDERR
0x918BD558, // QWERTYKEYS
0xA90A45D3, // POLARITY
0xD13E51A2, // STEREOIN
0x6B812C51, // CHGSTOPPED
0x8674E41D  // CHGMANUAL
};

static const uint32_t AppFlags3Bits[] = {
0xAEB4D549, // NODRUMS
0xA8E8686D, // NOBASS
0x99B876A8, // NOGTR
0x0ABEC700, // NOPAD
0xBC585138, // NOVARCYCLE
0xA331102B, // NOAUTOSTART
0x684E2C36,	// AUTOSTARTARM
0xB4E5E6EC  // NOREVERB
};

static const uint32_t AppFlags4Bits[] = {
0x4CF52356, // NOCHORDHOLD
0xE11AFF2A, // UPPERPAD
0x2C7D7514,	// NOTOOLTIPS
0xD5B4BBD2,	// CMDALWAYSON
0,	// Not used
0,	// Not used
0,	// Not used
0	// Not used
};

static char					TimeBuf[6] = "00:00";
static char					TempoBuf[8];

#define SIZEOFHEADINGBUFFER	120
static char					HeadingBuffer[SIZEOFHEADINGBUFFER];

static unsigned char VariationColor;
static unsigned char VariationSelColor;
static unsigned char VariationNormColor;
static unsigned char VariationBlink = 0;

static void doTempoNumpad(register uint32_t);
static void positionPickItemGui(void);
static void draw_styles(GUIAPPHANDLE, GUICTL *, GUIAREA *);
static void draw_patches(GUIAPPHANDLE, GUICTL *, GUIAREA *);
static void draw_chords(GUIAPPHANDLE, GUICTL *, GUIAREA *);

#ifndef NO_SONGSHEET_SUPPORT
static uint32_t ctl_update_songsheet(register GUICTL *);
static uint32_t ctl_set_songsheet(register GUICTL *);
#endif

static uint32_t ctl_update_vol(register GUICTL *);
static uint32_t ctl_set_vol(register GUICTL *);

static uint32_t ctl_update_accompmute(register GUICTL *);
static uint32_t ctl_set_accompmute(register GUICTL *);
static uint32_t ctl_update_master_vol(register GUICTL *);
static uint32_t ctl_set_master_vol(register GUICTL *);

static const char * patchbtn_label(GUIAPPHANDLE, GUICTL *, char *);
static uint32_t ctl_set_patch_btn(register GUICTL *);
static uint32_t ctl_update_chords(register GUICTL *);
static uint32_t ctl_update_volboost(register GUICTL *);
static uint32_t ctl_set_volboost(register GUICTL *);
static uint32_t ctl_update_time(register GUICTL *);
static uint32_t ctl_set_panic(register GUICTL *);
static uint32_t ctl_update_pad(register GUICTL *);
static uint32_t ctl_set_pad(register GUICTL *);
static uint32_t ctl_update_area(register GUICTL *);
static uint32_t ctl_update_variation(register GUICTL *);
static uint32_t ctl_set_variation(register GUICTL *);
static uint32_t ctl_update_transpose(register GUICTL *);
static uint32_t ctl_set_transpose(register GUICTL *);
static uint32_t ctl_update_autostart(register GUICTL *);
static uint32_t ctl_set_autostart(register GUICTL *);
#if !defined(NO_ALSA_AUDIO_SUPPORT) || !defined(NO_JACK_SUPPORT)
static uint32_t ctl_update_reverb_sw(register GUICTL *);
static uint32_t ctl_set_reverb_sw(register GUICTL *);
static uint32_t ctl_update_rev_vol(register GUICTL *);
static uint32_t ctl_set_rev_vol(register GUICTL *);
#endif
static uint32_t ctl_update_robot_mute(register GUICTL *);
static uint32_t ctl_set_robot_mute(register GUICTL *);
static uint32_t ctl_set_robot_list(register GUICTL *);

static uint32_t ctl_update_cycle(register GUICTL *);
static uint32_t ctl_set_cycle(register GUICTL *);
static uint32_t ctl_update_tempo(register GUICTL *);
static uint32_t ctl_set_tempo(register GUICTL *);
static uint32_t ctl_set_tapTempo(register GUICTL *);
static uint32_t ctl_set_play(register GUICTL *);
static uint32_t ctl_set_ritard(register GUICTL *);
static uint32_t ctl_set_songsheet_list(register GUICTL *);
static uint32_t ctl_set_style(register GUICTL *);

#define LAYOUT_USES_OPTION	0x0001
#define LAYOUT_USES_LABEL	0x0002
#define LAYOUT_USES_COLOR	0x0004
#define LAYOUT_USES_FONT	0x0008
#define LAYOUT_USES_WIDTH	0x0010
#define LAYOUT_USES_HEIGHT	0x0020
//#define LAYOUT_USES_BUTTON	0x0040
//0x80
#define LAYOUT_USES_X		0x0100
#define LAYOUT_USES_Y		0x0200
#define LAYOUT_USES_LINE	0x0400

#define DRAWFLAG_UPDATE			0x80
#define DRAWFLAG_TEMPO_COLOR	0x00
#define DRAWFLAG_VARIA_COLOR	0x01
#define DRAWFLAG_DEF_COLOR		0x02
#define DRAWFLAG_PGM_COLOR		0x03
#define DRAWFLAG_TEXT_COLOR	0x03
#define DRAWFLAG_COLOR_MASK	0x03
#define DRAWFLAG_NOSTRINGS		0x04
#define DRAWFLAG_GET_LABEL		0x10

static const unsigned char DefaultColors[] = {(GUICOLOR_BLACK << 4) | GUICOLOR_VIOLET, (GUICOLOR_BLACK << 4) | GUICOLOR_GRAY, (GUICOLOR_BLACK << 4) | GUICOLOR_GRAY, GUICOLOR_DARKGRAY};

static GUICTLDATA2	UserGui[] = {
{	.TypeStringCrc=CTLSTR_DRUMVOL, .TypeId=CTLID_DRUMVOL, .GuiCtlType=CTLTYPE_ARROWS,
	.Funcs.UpdateFunc=ctl_update_vol,	.Funcs.SetFunc=ctl_set_vol,	.Label=VolStr,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_NOSTRINGS, .PlayerNeeded=PLAYER_DRUMS},
{	.TypeStringCrc=CTLSTR_BASSVOL, .TypeId=CTLID_BASSVOL, .GuiCtlType=CTLTYPE_ARROWS,
	.Funcs.UpdateFunc=ctl_update_vol,	.Funcs.SetFunc=ctl_set_vol,	.Label=VolStr,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_NOSTRINGS, .PlayerNeeded=PLAYER_BASS},
{	.TypeStringCrc=CTLSTR_GTRVOL,	.TypeId=CTLID_GTRVOL, .GuiCtlType=CTLTYPE_ARROWS,
	.Funcs.UpdateFunc=ctl_update_vol,	.Funcs.SetFunc=ctl_set_vol,	.Label=VolStr,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_NOSTRINGS, .PlayerNeeded=PLAYER_GTR},
{	.TypeStringCrc=CTLSTR_PADVOL,	.TypeId=CTLID_PADVOL, .GuiCtlType=CTLTYPE_ARROWS,
	.Funcs.UpdateFunc=ctl_update_vol,	.Funcs.SetFunc=ctl_set_vol,	.Label=VolStr,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_NOSTRINGS, .PlayerNeeded=PLAYER_PAD},
{	.TypeStringCrc=CTLSTR_PATCHVOL,.TypeId=CTLID_PATCHVOL, .GuiCtlType=CTLTYPE_ARROWS,
	.Funcs.UpdateFunc=ctl_update_vol,	.Funcs.SetFunc=ctl_set_vol,	.Label=VolStr,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_NOSTRINGS, .PlayerNeeded=PLAYER_SOLO},

{	.TypeStringCrc=CTLSTR_DRUMLIST,	.TypeId=CTLID_DRUMLIST,
	.Funcs.UpdateFunc=ctl_update_area, .Funcs.SetFunc=ctl_set_robot_list, .AreaDraw=draw_patches,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_HEIGHT|LAYOUT_USES_OPTION,
	.GuiCtlType=CTLTYPE_AREA,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_DRUMS},
{	.TypeStringCrc=CTLSTR_BASSLIST,	.TypeId=CTLID_BASSLIST,
	.Funcs.UpdateFunc=ctl_update_area, .Funcs.SetFunc=ctl_set_robot_list, .AreaDraw=draw_patches,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_HEIGHT|LAYOUT_USES_OPTION,
	.GuiCtlType=CTLTYPE_AREA,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_BASS},
{	.TypeStringCrc=CTLSTR_GTRLIST,	.TypeId=CTLID_GTRLIST,
	.Funcs.UpdateFunc=ctl_update_area, .Funcs.SetFunc=ctl_set_robot_list, .AreaDraw=draw_patches,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_HEIGHT|LAYOUT_USES_OPTION,
	.GuiCtlType=CTLTYPE_AREA,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_GTR},
{	.TypeStringCrc=CTLSTR_BACKINGPAD, .TypeId=CTLID_BACKINGPAD,
	.Funcs.UpdateFunc=ctl_update_pad,.Funcs.SetFunc=ctl_set_pad,	.Label=PadStr,
	.GuiCtlType=CTLTYPE_RADIO,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_PAD},
{	.TypeStringCrc=CTLSTR_PATCHLIST, .TypeId=CTLID_PATCHLIST,
	.Funcs.UpdateFunc=ctl_update_area, .Funcs.SetFunc=ctl_set_robot_list, .AreaDraw=draw_patches,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_HEIGHT|LAYOUT_USES_OPTION,
	.GuiCtlType=CTLTYPE_AREA,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_SOLO},

{	.TypeStringCrc=CTLSTR_DRUMMUTE, .TypeId=CTLID_DRUMMUTE, .GuiCtlType=CTLTYPE_CHECK,
	.Funcs.UpdateFunc=ctl_update_robot_mute, .Funcs.SetFunc=ctl_set_robot_mute, .Label=DrumsStr,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_DRUMS},
{	.TypeStringCrc=CTLSTR_BASSMUTE, .TypeId=CTLID_BASSMUTE, .GuiCtlType=CTLTYPE_CHECK,
	.Funcs.UpdateFunc=ctl_update_robot_mute, .Funcs.SetFunc=ctl_set_robot_mute, .Label=BassStr,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_BASS},
{	.TypeStringCrc=CTLSTR_GTRMUTE, .TypeId=CTLID_GTRMUTE, .GuiCtlType=CTLTYPE_CHECK,
	.Funcs.UpdateFunc=ctl_update_robot_mute, .Funcs.SetFunc=ctl_set_robot_mute, .Label=GuitarStr,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_GTR},
{	.TypeStringCrc=CTLSTR_ACCOMPMUTE, .TypeId=CTLID_ACCOMPMUTE, .GuiCtlType=CTLTYPE_CHECK,
	.Funcs.UpdateFunc=ctl_update_accompmute, .Funcs.SetFunc=ctl_set_accompmute, .Label=AccompStr,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_MASTERVOL, .TypeId=CTLID_MASTERVOL, .GuiCtlType=CTLTYPE_ARROWS,
	.Funcs.UpdateFunc=ctl_update_master_vol, .Funcs.SetFunc=ctl_set_master_vol, .Label=VolStr,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_NOSTRINGS, .PlayerNeeded=0x07},

{	.TypeStringCrc=CTLSTR_TEMPO, .TypeId=CTLID_TEMPO, .GuiCtlType=CTLTYPE_PUSH,
	.Funcs.UpdateFunc=ctl_update_tempo, .Funcs.SetFunc=ctl_set_tempo, .Label=TempoBuf,
	.LayoutFlags=LAYOUT_USES_COLOR, .DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_TEMPO_COLOR, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_TAPTEMPO, .TypeId=CTLID_TAPTEMPO,
	.Funcs.UpdateFunc=ctl_update_nothing,		.Funcs.SetFunc=ctl_set_tapTempo,		.Label=TapStr,
	.LayoutFlags=LAYOUT_USES_COLOR,
	.GuiCtlType=CTLTYPE_PUSH,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_TEMPO_COLOR, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_VARIATION, .TypeId=CTLID_VARIATION,
	.Funcs.UpdateFunc=ctl_update_variation,	.Funcs.SetFunc=ctl_set_variation,	.Label=VariationStr,
	.LayoutFlags=LAYOUT_USES_COLOR,
	.GuiCtlType=CTLTYPE_PUSH,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_VARIA_COLOR, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_VOLBOOST, .TypeId=CTLID_VOLBOOST,
	.Funcs.UpdateFunc=ctl_update_volboost,	.Funcs.SetFunc=ctl_set_volboost,		.Label=VolBoostStr,
	.GuiCtlType=CTLTYPE_CHECK,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_SOLO},
{	.TypeStringCrc=CTLSTR_PLAY, .TypeId=CTLID_PLAY,
	.Funcs.UpdateFunc=ctl_update_area,	.Funcs.SetFunc=ctl_set_play,			.BtnLabel=play_button_label,
	.LayoutFlags=LAYOUT_USES_COLOR,
	.GuiCtlType=CTLTYPE_PUSH,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_GET_LABEL, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_STYLES, .TypeId=CTLID_STYLES,
	.Funcs.UpdateFunc=ctl_update_area, .Funcs.SetFunc=ctl_set_style, .AreaDraw=draw_styles,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_HEIGHT|LAYOUT_USES_OPTION,
	.GuiCtlType=CTLTYPE_AREA,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_CHORDS, .TypeId=CTLID_CHORDS,
	.Funcs.UpdateFunc=ctl_update_chords,		.AreaDraw=draw_chords,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_HEIGHT,
	.GuiCtlType=CTLTYPE_AREA,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_TRANSPOSE, .TypeId=CTLID_TRANSPOSE,
	.Funcs.UpdateFunc=ctl_update_transpose,	.Funcs.SetFunc=ctl_set_transpose,	.Label=TransStr,
	.GuiCtlType=CTLTYPE_ARROWS,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_AUTOSTART, .TypeId=CTLID_AUTOSTART,
	.Funcs.UpdateFunc=ctl_update_autostart,	.Funcs.SetFunc=ctl_set_autostart,	.Label=AutoStartStr,
	.GuiCtlType=CTLTYPE_CHECK,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=0x07},
#ifndef NO_REVERB_SUPPORT
{	.TypeStringCrc=CTLSTR_REVERBMUTE,	.TypeId=CTLID_REVERBMUTE,
	.Funcs.UpdateFunc=ctl_update_reverb_sw,	.Funcs.SetFunc=ctl_set_reverb_sw,	.Label=ReverbStr,
	.GuiCtlType=CTLTYPE_CHECK,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_REVERBVOL,		.TypeId=CTLID_REVERBVOL,
	.Funcs.UpdateFunc=ctl_update_rev_vol,		.Funcs.SetFunc=ctl_set_rev_vol,		.Label=VolStr,
	.GuiCtlType=CTLTYPE_ARROWS,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_NOSTRINGS, .PlayerNeeded=0x07},
#endif
{	.TypeStringCrc=CTLSTR_CYCLEVARIATION, .TypeId=CTLID_CYCLEVARIATION,
	.Funcs.UpdateFunc=ctl_update_cycle,		.Funcs.SetFunc=ctl_set_cycle,			.Label=CycleStr,
	.GuiCtlType=CTLTYPE_CHECK,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=0x07},
#ifndef NO_SONGSHEET_SUPPORT
{	.TypeStringCrc=CTLSTR_SONGSHEET, .TypeId=CTLID_SONGSHEET,
	.Funcs.UpdateFunc=ctl_update_songsheet,	.Funcs.SetFunc=ctl_set_songsheet,	.Label=SongSheetStr,
	.LayoutFlags=LAYOUT_USES_COLOR,
	.GuiCtlType=CTLTYPE_PUSH,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_DEF_COLOR, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_SONGSHEETLIST, .TypeId=CTLID_SONGSHEETLIST,
	.Funcs.UpdateFunc=ctl_update_area, .Funcs.SetFunc=ctl_set_songsheet_list, .ListDraw=draw_songsheets,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_HEIGHT|LAYOUT_USES_OPTION,
	.GuiCtlType=CTLTYPE_AREA,
	.DrawFlags=DRAWFLAG_UPDATE, .PlayerNeeded=0x07},
#endif
{	.TypeStringCrc=CTLSTR_RITARD,			.TypeId=CTLID_RITARD,
	.Funcs.UpdateFunc=ctl_update_nothing,		.Funcs.SetFunc=ctl_set_ritard,		.Label=RitardStr,
	.LayoutFlags=LAYOUT_USES_COLOR,
	.GuiCtlType=CTLTYPE_PUSH,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_TEMPO_COLOR, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_ACCELERANDO, .TypeId=CTLID_ACCELERANDO,
	.Funcs.UpdateFunc=ctl_update_nothing,		.Funcs.SetFunc=ctl_set_ritard,		.Label=AccelStr,
	.LayoutFlags=LAYOUT_USES_COLOR,
	.GuiCtlType=CTLTYPE_PUSH,
	.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_TEMPO_COLOR, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_PANIC,			.TypeId=CTLID_PANIC,
	.Funcs.UpdateFunc=ctl_update_nothing,		.Funcs.SetFunc=ctl_set_panic,			.Label=PanicStr,
	.LayoutFlags=LAYOUT_USES_COLOR,
	.GuiCtlType=CTLTYPE_PUSH,
	.DrawFlags=DRAWFLAG_DEF_COLOR, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_CLOCK, .TypeId=CTLID_CLOCK, .GuiCtlType=CTLTYPE_STATIC,
	.Funcs.UpdateFunc=ctl_update_time, .Label=TimeBuf,
	.LayoutFlags=LAYOUT_USES_COLOR,.DrawFlags=DRAWFLAG_UPDATE|DRAWFLAG_TEXT_COLOR, .PlayerNeeded=0x07},
{	.TypeStringCrc=CTLSTR_DRUMBTN, .TypeId=CTLID_DRUMLIST, .GuiCtlType=CTLTYPE_PUSH,
	.Funcs.UpdateFunc=ctl_update_area, .Funcs.SetFunc=ctl_set_patch_btn,	.BtnLabel=patchbtn_label,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_COLOR, .DrawFlags=DRAWFLAG_DEF_COLOR|DRAWFLAG_GET_LABEL|DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_DRUMS},
{	.TypeStringCrc=CTLSTR_BASSBTN, .TypeId=CTLID_BASSLIST, .GuiCtlType=CTLTYPE_PUSH,
	.Funcs.UpdateFunc=ctl_update_area, .Funcs.SetFunc=ctl_set_patch_btn,	.BtnLabel=patchbtn_label,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_COLOR, .DrawFlags=DRAWFLAG_DEF_COLOR|DRAWFLAG_GET_LABEL|DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_BASS},
{	.TypeStringCrc=CTLSTR_GTRBTN,	.TypeId=CTLID_GTRLIST, .GuiCtlType=CTLTYPE_PUSH,
	.Funcs.UpdateFunc=ctl_update_area, .Funcs.SetFunc=ctl_set_patch_btn, 	.BtnLabel=patchbtn_label,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_COLOR, .DrawFlags=DRAWFLAG_DEF_COLOR|DRAWFLAG_GET_LABEL|DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_GTR},
{	.TypeStringCrc=CTLSTR_PADBTN, .TypeId=CTLID_BACKINGPAD, .GuiCtlType=CTLTYPE_PUSH,
	.Funcs.UpdateFunc=ctl_update_area, .Funcs.SetFunc=ctl_set_patch_btn,	.BtnLabel=patchbtn_label,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_COLOR, .DrawFlags=DRAWFLAG_DEF_COLOR|DRAWFLAG_GET_LABEL|DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_PAD},
{	.TypeStringCrc=CTLSTR_PATCHBTN, .TypeId=CTLID_PATCHLIST, .GuiCtlType=CTLTYPE_PUSH,
	.Funcs.UpdateFunc=ctl_update_area,		.Funcs.SetFunc=ctl_set_patch_btn,	.BtnLabel=patchbtn_label,
	.LayoutFlags=LAYOUT_USES_WIDTH|LAYOUT_USES_COLOR, .DrawFlags=DRAWFLAG_DEF_COLOR|DRAWFLAG_GET_LABEL|DRAWFLAG_UPDATE, .PlayerNeeded=PLAYER_SOLO},
};

#define CTLID_COUNT			(sizeof(UserGui) / sizeof(GUICTLDATA2))

// Flags when to abort Gui loop
unsigned char				GuiLoop;

// Various flag bits for app settings
unsigned char				AppFlags = 0;
unsigned char				AppFlags2 = 0;
unsigned char				AppFlags3 = 0;
unsigned char				AppFlags4 = 0;
unsigned char				TempFlags = 0;

// menu bar
#define NUM_MENU_LABELS 1
#ifndef NO_MIDI_IN_SUPPORT
static const char			CmdModeMenuStr[] = "Command Note Mode";
#endif
static const char			SetupStr[] = "Setup";
static const char	*		MenuLabels[NUM_MENU_LABELS] = {SetupStr};
static unsigned short	MenuWidths[NUM_MENU_LABELS];
static GUIMENU				Menu = {MenuLabels, MenuWidths, 0, 0, 0, NUM_MENU_LABELS | (4 << 4), (GUICOLOR_BLACK << 4)|GUICOLOR_MENU};
#ifndef NO_MIDI_IN_SUPPORT
static const char	*		CmdModeMenuLabels[NUM_MENU_LABELS] = {CmdModeMenuStr};
static unsigned short	CmdModeMenuWidths[NUM_MENU_LABELS];
static GUIMENU				CmdModeMenu = {CmdModeMenuLabels, CmdModeMenuWidths, 0, 0, 0, NUM_MENU_LABELS | (4 << 4), (GUICOLOR_BLUE << 4) | GUICOLOR_ORANGE};
#endif

// Pixel Width/Height of a box into which we display a style/patch name
static uint32_t			StylesBoxWidth;
static uint32_t			StylesMinBoxWidth;
static uint32_t			CatBoxWidth;
static uint32_t			CatMinBoxWidth;
static uint32_t			BoxHeight;
static unsigned char		NumCatLines;
static unsigned char		MostStyles;

// Drum beat countoff
static unsigned char		CountOff, TotalCountOff;

static unsigned char		FontSize;

#define WINFLAGS_RELATIVEXY	0x01
#define WINFLAGS_NOTITLE		0x02
#define WINFLAGS_MENUHIDE		0x04
#define WINFLAGS_WIDTHSET		0x20
#define WINFLAGS_HEIGHTSET		0x40
#define WINFLAGS_POSITION		0x80
static unsigned char		WindowFlags;











// =============================================================
// Window Layout
// =============================================================
static struct GUIPANEL *	PanelList = 0;

#define NUM_OF_EXTRA_CTLIDS	4

#define CTLID_MAIN_TEXT		60
#define CTLID_MAIN_GROUP	61
#define CTLID_MAIN_PANEL	62
#define CTLID_MAIN_WINDOW	63

static const uint32_t ExtraCtlIds[] = {
0x9E524EA8,		// TEXT
0x00000000,		// '{' (GROUP start)
0x6CA6D5EE,		// PANEL
0x58F325B1};	// WINDOW

// Must be in the same order as ExtraCtlIds[]
static const unsigned char ExtraLayoutFlags[] = {
LAYOUT_USES_LABEL|LAYOUT_USES_COLOR,		// TEXT
LAYOUT_USES_LABEL|LAYOUT_USES_OPTION,		// GROUP
LAYOUT_USES_LABEL,								// PANEL
LAYOUT_USES_WIDTH|LAYOUT_USES_HEIGHT|LAYOUT_USES_OPTION|LAYOUT_USES_FONT};	// WINDOW

#define NUM_OF_LAYOUT_PARAMS	11

#define LAYOUT_OPTION	0
#define LAYOUT_LABEL		1
#define LAYOUT_COLOR		2
// Numeric params below here
#define LAYOUT_NUMERICS	3
#define LAYOUT_FONT		3
#define LAYOUT_WIDTH		4
#define LAYOUT_HEIGHT	5
#define LAYOUT_X			8
#define LAYOUT_Y			9
#define LAYOUT_LINE		10
static const uint32_t LayoutParams[] = {
0x90ABF090,		// OPTION
0xCDE39663,		// LABEL
0x33325920,		// COLOR
// Numeric params below here
0x0BCB6C95,		// FONT
0x7D91F192,		// WIDTH
0xA9AD3220,		// HEIGHT
0,					// unused
0,					// unused
0x4AA61533,		// X
0x38CF7186,		// Y
0x6B6FBCEF};	// LINE

#define NUM_OF_LAYOUT_OPTS	2
static const uint32_t LayoutCtlOpts[] = {
0x65036BD3,			// NOPADDING
0xEC0538D9};		// SMALL
static const unsigned char LayoutCtlOptBits[] = {CTLGLOBAL_NOPADDING, CTLGLOBAL_SMALL};
static const uint32_t LayoutPadOpts[] = {
0x65036BD3,			// NOPADDING
0x664B79E2};		// BOX
#define NUM_OF_WINLAYOUT_OPTS 3
static const uint32_t LayoutWinOpts[] = {
0x2A30698D,			// RELATIVE
0x5706581B,			// NOTITLE
0xA66C5A9E};		// MENUHIDE

#define NUM_OF_GUICOLOR_PARAMS	16
static const uint32_t LayoutColors[] = {
0x1BFC86A9, // BACKGROUND
0x4EB3A4BF, // BLACK
0x3748D12F,	// RED
0x4AF9D272, // GREEN
0x38B5595F, // ORANGE
0x4949CA67, // LIGHTBLUE
0x11C9FD78,	// PINK
0x1444525A, // GOLD
0x70577958, // DARKGREEN
0x4503C42C, // PURPLE
0x6D590ECE, // VIOLET
0x070A1EE7, // BLUE
0xB5AB5DED, // DARKGRAY
0x13D07E39, // GRAY
0x41DC0A2C, // LIGHTGOLD
0x6727D0EE, // MENU
};

static const uint32_t Obsolete[] = {0x819FBCE4,	// GUITARPATCH
#ifdef NO_SONGSHEET_SUPPORT
CTLSTR_SONGSHEET,
CTLSTR_SONGSHEETLIST,
#endif
#ifdef NO_REVERB_SUPPORT
CTLSTR_REVERBMUTE,
CTLSTR_REVERBVOL,
#endif
};

#pragma pack(1)

struct BOXSIZE {
	unsigned short	X;
	union {
	unsigned short	Y;
	unsigned short	LineNum;
	};
	unsigned short	Width;
	unsigned short	Height;
};

struct LOADGUICTL {
	union {
	struct BOXSIZE	Box;
	GUICTL *			Ptr;
	};
	uint32_t			LabelOffset;
	unsigned char	CtlFlags;
	unsigned char	GlobalFlags;
	unsigned char	Color;
	unsigned char	Type;
};
#pragma pack()

static const char	WinLayoutDir[] = "Window/";
static const char	WinDefLayout[] = "default.layout";
static const char	GroupBadStr[] = "A group label is";

void fixup_setup_ctls(register GUICTL * ctls)
{
	do
	{
		if (ctls->Ptr && (ctls->Flags.Global & CTLGLOBAL_PRESET))
		{
			register GUICTLDATA2 *	ptr;

			ctls->Flags.Global &= ~CTLGLOBAL_PRESET;
			ptr = &UserGui[0];
			do
			{
				if (((intptr_t)(ctls->Ptr)) == ptr->TypeStringCrc)
				{
					ctls->Ptr = ptr;
					if (ptr->GuiCtlType == CTLTYPE_AREA) ctls->AreaDraw = ptr->AreaDraw;
					goto found;
				}

			} while (++ptr < &UserGui[CTLID_COUNT]);

			ctls->Ptr = 0;
		}
found:
		ctls++;
	} while (ctls->Type);
}

static void loadWindowLayout(void);

static uint32_t create_main_window(void)
{
	register GUIINIT *	init;
	register uint32_t		width;

	// Load the window layout file. If an error, we'll go
	// ahead and use the internal default layout
	loadWindowLayout();

	VariationColor = VariationSelColor;
//	VariationBlink = 0;

	init = GuiAppState(GuiApp, GUISTATE_GET_INIT, 0);
	init->FontSize	= FontSize;

	// Create the app window
	if (GuiWinState(GuiApp, MainWin, GUISTATE_CREATE|GUISTATE_LINK|GUISTATE_OPEN|GUISTATE_FONTSIZE))
	{
		{
		register struct GUIPANEL *	panelList;

		if ((panelList = PanelList))
		{
			do
			{
				if (!(WindowFlags & WINFLAGS_RELATIVEXY))
				{
					register GUICTL *		ctls;

					ctls = &panelList->Ctls[0];
					do
					{
						// Adjust X position of VARIATION buttons if not using relative placement
						if (ctls->Label == VariationStr)
						{
							register unsigned char	i;

							if (ctls->Flags.Global & CTLGLOBAL_SMALL)
								GuiTextSetSmall(MainWin);
							else
								GuiTextSetLarge(MainWin);

							i = 4;
							goto calc;
							do
							{
								width = ctls->Width + ctls->X;
								++ctls;
								ctls->X += width + (GuiApp->GuiFont.CharWidth * ((ctls->Flags.Global & CTLGLOBAL_SMALL) ? 1 : 2));
calc:							ctls->Width = GuiTextWidth(MainWin, ctls->Label) + (GuiApp->GuiFont.CharWidth * ((ctls->Flags.Global & CTLGLOBAL_SMALL) ? 1 : 2));
							} while (--i);

							break;
						}

						++ctls;
					} while (ctls->Type);

					GuiCtlAbsScale(GuiApp, MainWin, &panelList->Ctls[0]);
				}
				else
					GuiCtlScale(GuiApp, MainWin, &panelList->Ctls[0], (uint32_t)-1);
			} while ((panelList = panelList->Next));
		}
		}

		// Scale GUI ctls
		positionPickDevGui();
		positionSetupGui();
		positionPickItemGui();
		positionCmdAsnGui();
		positionEditorGui();

#ifndef NO_SONGSHEET_SUPPORT
		positionSongGui();
#endif
		// Size the menus and set window width to accomodate
		init_setup_menu();
		MainWin->Menu = &Menu;
		GuiWinState(GuiApp, 0, GUISTATE_MENU);
		init->Width = (WindowFlags & WINFLAGS_WIDTHSET) ? MainWin->WinPos.Width : MainWin->MinWidth;
		init->Height = (WindowFlags & WINFLAGS_HEIGHTSET) ? MainWin->WinPos.Height : MainWin->MinHeight;

		// Position/size/show the window
		init->Title = (WindowFlags & WINFLAGS_NOTITLE) ? 0 : &WindowTitle[0];
		GuiWinState(GuiApp, 0, (WindowFlags & (WINFLAGS_HEIGHTSET|WINFLAGS_WIDTHSET)) == (WINFLAGS_HEIGHTSET|WINFLAGS_WIDTHSET)
			? GUISTATE_SHOW|GUISTATE_TITLE : GUISTATE_SHOW|GUISTATE_TITLE|GUISTATE_SIZE);

		// If an error msg. display it
		if (getErrorStr()) show_msgbox(getErrorStr());

		// Indicate we have a window open, so the program can proceed
		return 1;
	}

	printf("Can't create window\r\n");
	return 0;
}





static void free_panels(void)
{
	register struct GUIPANEL *	panelList;
	register struct GUIPANEL *	temp;

	if ((temp = PanelList))
	{
		while ((panelList = temp))
		{
			temp = panelList->Next;
			free(panelList);
		}
	}
}





static void loadWindowLayout(void)
{
	unsigned char *				ptr;
	register uint32_t				size;

	if (alloc_temp_buffer(PATH_MAX))
	{
		NamePtr = WinDefLayout;

		VariationSelColor = GUICOLOR_LIGHTBLUE | (GUICOLOR_BLACK << 4);
		VariationNormColor = GUICOLOR_GRAY | (GUICOLOR_BLACK << 4);

		// Assume layouts are in config dir
		DataPathFlag = FontSize = WindowFlags = 0;
again:
		// Get path and try to load file
		ptr = TempBuffer;
		size = get_style_datapath((char *)ptr);
		strcpy((char *)&ptr[size], WinLayoutDir);
		size += sizeof(WinLayoutDir) - 1;
		strcpy((char *)&ptr[size], NamePtr);
		if (!load_text_file((char *)ptr, 3|FILEFLAG_NOEXIST))
		{
			if (!DataPathFlag)
			{
				DataPathFlag = 1;
				goto again;
			}

			if (NamePtr == WinDefLayout)
			{
				register GUIINIT *	init;

				init = GuiAppState(GuiApp, GUISTATE_GET_INIT, 0);
				NamePtr = ((char *)init) + 256;
				sprintf((char *)NamePtr, "%u%s", init->FontSize, &WinDefLayout[7]);
				goto again;
			}
		}
		else
		{
			uint32_t								lineNum, quietTypes;
			register uint32_t					includedTypes;
			register struct LOADGUICTL *	dest;
			char *								panelName;
			char *								labels;
			uint32_t								grpLabelOffset;
			unsigned char						flags;

			ptr = TempBuffer;		// Must reget TempBuffer after load_text_file()
			lineNum = 1;
			panelName = 0;
			grpLabelOffset = includedTypes = NumCatLines = flags = 0;
			dest = (struct LOADGUICTL *)ptr;
			labels = GuiBuffer;

			for (;;)
			{
				register unsigned char	*field;
				unsigned char				id;

				// Skip to start of next line
nextline:	lineNum += skip_lines(&ptr);
				if (!*ptr) goto final;

				// Get the line "Type ID", such as WINDOW, PANEL, TEMPO, STYLES, etc
				field = ptr;
				if (*ptr == '{')	// A GROUP start?
				{
					if (grpLabelOffset)
					{
						format_text_err("A group is not allowed inside of another group", lineNum, 0);
						goto bad;
					}
					id = CTLID_MAIN_GROUP;
					flags = CTLGLOBAL_GROUPSTART;
					goto group;
				}
				if (*ptr == '}')	// A GROUP end?
				{
					memset(dest, 0, sizeof(struct LOADGUICTL));
					dest->Type = CTLID_MAIN_GROUP;
					dest->LabelOffset = grpLabelOffset;
					dest++;
					grpLabelOffset = flags = 0;
group:			if (!panelName)
					{
						goto missp;
					}
					ptr = skip_spaces(++ptr);
					if (!flags) goto nextline;
				}
				else
				{
					id = 0x80 | CTLID_COUNT;
					ptr = get_field_id(field, &UserGui[0].TypeStringCrc, &id, sizeof(GUICTLDATA2));
					if (id >= CTLID_COUNT)
					{
						id = 0x80 | NUM_OF_EXTRA_CTLIDS;
						get_field_id(field, &ExtraCtlIds[0], &id, 0);
						if (id >= NUM_OF_EXTRA_CTLIDS)
						{
							id = 0x80 | (sizeof(Obsolete) / sizeof(uint32_t));
							get_field_id(field, &Obsolete[0], &id, 0);
							if (id < (sizeof(Obsolete) / sizeof(uint32_t)))
							{
								while (ptr[0] && ptr[0] != '\r' && ptr[0] != '\n') ptr++;
								while (ptr[0] && (ptr[0] == '\r' || ptr[0] == '\n')) ptr++;
								goto nextline;
							}
							*ptr = 0;
badtype:					format_text_err("unknown control %s", lineNum, field);
bad:						return;
						}

						id += CTLID_MAIN_TEXT;
					}
				}

				// If this is a PANEL, we must finish processing the current panel before
				// starting a new one
				if (id == CTLID_MAIN_PANEL)
				{
					// Do we have a PANEL to process?
final:			if (panelName)
					{
						uint32_t					count;
						register GUICTL *		ctls;

						// Make sure any group is closed
						if (grpLabelOffset)
						{
							format_text_err("missing } bracket", lineNum, 0);
							goto bad;
						}

						// How many GUICTLS do we have? (Did user put any ctls in this panel?)
						if (!(count = ((unsigned char *)dest - TempBuffer) / sizeof(struct LOADGUICTL)))
						{
							*ptr = 0;
							format_text_err("%s panel has no controls", lineNum, (unsigned char *)panelName);
							goto bad;
						}

						// User specified VARIATION? It consists of 4 buttons
						if (CTLMASK_VARIATION & includedTypes) count += 3;

						// SongSheet List consists of an AREA and ARROWS
						if (CTLMASK_SONGSHEETLIST & includedTypes) count++;

						// Get a GUIPANEL, plus room for the PANEL label and labels of any GROUPBOX and STATIC
						field = (unsigned char *)malloc(sizeof(struct GUIPANEL) + (count * sizeof(GUICTL)) + ((char *)panelName - labels));

						// Link it into the list
						((struct GUIPANEL *)field)->Next = PanelList;
						PanelList = (struct GUIPANEL *)field;
						ctls = &(((struct GUIPANEL *)field)->Ctls[0]);

						// Store the panel name and ctl labels
						field = (unsigned char *)(&ctls[count + 1]);
						memcpy(field, labels, (char *)panelName - labels);

						// Fill it in
						dest = (struct LOADGUICTL *)TempBuffer;
						do
						{
							memset(ctls, 0, sizeof(GUICTL));

							// Copy any GUICTL params not configured by layout
							if (dest->Type < CTLID_MAIN_TEXT)
							{
								register GUICTLDATA2 *	appdata;

								appdata = &UserGui[dest->Type];
								if (appdata->TypeId == CTLID_SONGSHEETLIST)
								{
									memcpy(ctls, &ListGuiCtls[0], sizeof(GUICTL) * 2);
									ctls->Flags.Local = CTLFLAG_AREA_LIST|CTLFLAG_AREA_EVENHEIGHT;
								}
								ctls->Ptr = appdata;
								ctls->Label = appdata->Label;
								ctls->Type = appdata->GuiCtlType;
								if (ctls->Type <= CTLTYPE_CHECK || ctls->Type == CTLTYPE_STATIC) ctls->Attrib.NumOfLabels = 1;
								if (appdata->TypeId == CTLID_BACKINGPAD && appdata->GuiCtlType == CTLTYPE_RADIO) ctls->Attrib.NumOfLabels = 4;
								if (appdata->TypeId == CTLID_TRANSPOSE) ctls->Attrib.NumOfLabels = 10;
								if (appdata->Label == VolStr) ctls->Attrib.NumOfLabels = 100+1;
								if (appdata->TypeId != CTLID_SONGSHEETLIST) ctls->Flags.Global = (appdata->DrawFlags & DRAWFLAG_GET_LABEL) ? CTLGLOBAL_GET_LABEL|CTLGLOBAL_APPUPDATE : CTLGLOBAL_APPUPDATE;
								if (appdata->DrawFlags & DRAWFLAG_NOSTRINGS) ctls->Flags.Local = CTLFLAG_NOSTRINGS;
								includedTypes = appdata->LayoutFlags;
							}

							// CTLTYPE_GROUP or CTLTYPE_STATIC
							else
							{
								if (dest->Type == CTLID_MAIN_GROUP)
									ctls->Type = CTLTYPE_GROUPBOX;
								else
								{
									ctls->Type = CTLTYPE_STATIC;
									ctls->Attrib.NumOfLabels = 1;
								}

								includedTypes = ExtraLayoutFlags[dest->Type - CTLID_MAIN_TEXT];
								ctls->Label = (char *)(field + dest->LabelOffset);
							}

							// Overlay the usr's params
							if (includedTypes & LAYOUT_USES_COLOR)
							{
								if (!(dest->Color & 0xF0) && ctls->Type != CTLTYPE_STATIC) dest->Color |= (GUICOLOR_BLACK << 4);
								ctls->Attrib.Value = dest->Color;
							}

							ctls->X = dest->Box.X;
							ctls->Y = dest->Box.Y;	// LineNum
							if (dest->GlobalFlags & CTLGLOBAL_X_ALIGN)
								ctls->X = ((struct LOADGUICTL *)(TempBuffer + ctls->X))->Ptr - &PanelList->Ctls[0];

							if (ctls->BtnLabel == &patchbtn_label)
							{
								if (!(ctls->Width = dest->Box.Width)) ctls->Width = ((WindowFlags & WINFLAGS_RELATIVEXY) ? 16 : 16 * GuiApp->GuiFont.CharWidth);
							}
							else if (includedTypes & LAYOUT_USES_WIDTH)
							{
								ctls->BottomY = dest->Box.Height;
								if ((ctls->Width = dest->Box.Width) && ctls->Type == CTLTYPE_AREA)
								{
									ctls->Flags.Local |= CTLFLAG_AREA_STATIC;
									ctls->BottomY += dest->Box.Y;
								}
							}

							ctls->Flags.Global |= dest->GlobalFlags;
							ctls->Flags.Local |= dest->CtlFlags;
							dest->Ptr = ctls;

							if (ctls->Label == VariationStr)
	 						{
								VariationNormColor = dest->Color;
								includedTypes = 3;
								do
								{
									memcpy(&ctls[1], ctls, sizeof(GUICTL));
									++ctls;
									ctls->X = 0;
									ctls->Label += strlen(ctls->Label) + 1;
									ctls->Flags.Global |= CTLGLOBAL_NOPADDING;
								} while (--includedTypes);
								count -= 3;
							}

#ifndef NO_SONGSHEET_SUPPORT
							if (ctls->ListDraw == draw_songsheets)
							{
								ctls++;
								memset(ctls, 0, sizeof(GUICTL));
								ctls->Type = CTLTYPE_ARROWS;
								ctls->Flags.Local = CTLFLAG_AREA_HELPER;
								count--;
							}
#endif
							dest++;
							ctls++;
						} while (--count);

						// Fill in the END GUICTL
						memset(ctls, 0, sizeof(GUICTL));
					}

					// End of layout file?
					if (!*ptr) break;

					// Reset to overwrite at buffer head
					dest = (struct LOADGUICTL *)TempBuffer;
					labels = GuiBuffer;

					// Start a new panel
					panelName = 0;
					quietTypes = includedTypes = 0;
				}

				// If a WINDOW, make sure there wasn't another one
				else if (id == CTLID_MAIN_WINDOW)
				{
					if (NumCatLines)
					{
						*ptr = 0;
						format_text_err("can't have more than 1 %s", lineNum, field);
						goto bad;
					}
					NumCatLines = 1;
				}

				else
				{
					// Make sure we had a preceding PANEL line
					if (!panelName)
					{
missp:				format_text_err("missing PANEL line", lineNum, 0);
						goto bad;
					}

					// Only TEXT and GROUP can appear more than 1 per panel
//					if (id < CTLID_MAIN_TEXT)
					if (id < CTLID_COUNT)
					{
						if (UserGui[id].TypeId > 31)
						{
							if ((1 << (UserGui[id].TypeId - 32)) & quietTypes) goto dupetype;
							quietTypes |= (1 << id);
						}
						else
						{
							if ((1 << UserGui[id].TypeId) & includedTypes)
							{
dupetype:					*ptr = 0;
								format_text_err("can't have more than 1 %s per panel", lineNum, field);
								goto bad;
							}

							includedTypes |= (1 << UserGui[id].TypeId);
						}
					}
				}

				{
				register uint32_t		allow, includedParams;
				struct LOADGUICTL		temp;

				// Set default values
				memset(&temp, 0, sizeof(struct LOADGUICTL));
				if ((unsigned char *)dest > TempBuffer) temp.Box.Y = dest[-1].Box.Y;
				temp.Type = id;
				// Default color
				temp.Color = DefaultColors[DRAWFLAG_TEXT_COLOR];
				if (id < CTLID_MAIN_TEXT)
				{
					if (UserGui[id].TypeId == CTLID_CHORDS) temp.Box.Height = GuiApp->GuiFont.Height * 3;
					temp.Color = UserGui[id].TypeId == CTLID_PLAY ? (GUICOLOR_BLACK << 4) | GUICOLOR_GREEN : DefaultColors[UserGui[id].DrawFlags & DRAWFLAG_COLOR_MASK];

					// Get what params are valid for this type, such as X, LINE, FONT, etc
					allow = UserGui[id].LayoutFlags | LAYOUT_USES_OPTION;	// All except PANEL support at least one OPTION
				}
				else
				{
					allow = ExtraLayoutFlags[id - CTLID_MAIN_TEXT];
					if (id == CTLID_MAIN_WINDOW) allow |= (LAYOUT_USES_X|LAYOUT_USES_Y);
					if (id != CTLID_MAIN_PANEL) allow |= LAYOUT_USES_OPTION;
				}
				if (id < CTLID_MAIN_GROUP) allow |= (LAYOUT_USES_X|LAYOUT_USES_Y|LAYOUT_USES_LINE);

				// Parse each param until the eol
				includedParams = 0;
				while (ptr[0] && ptr[0] != '\n')
				{
					// panel has only label
					if (temp.Type == CTLID_MAIN_PANEL) goto label;

					if (!includedParams && temp.Type == CTLID_MAIN_GROUP)
						id = LAYOUT_LABEL;
					else
					{
						field = ptr;
						id = NUM_OF_LAYOUT_PARAMS;
						ptr = get_field_id(field, &LayoutParams[0], &id, 0);
						if (id >= NUM_OF_LAYOUT_PARAMS)
						{
							*ptr = 0;
unk:						format_text_err("unknown parameter %s", lineNum, field);
							goto bad;
						}
						if (!((1 << id) & allow))
						{
							*ptr = 0;
							format_text_err("%s not allowed for this control", lineNum, field);
							goto bad;
						}
						if (id != LAYOUT_OPTION && ((1 << id) & includedParams))
						{
							*ptr = 0;
dupl:						format_text_err("%s specified twice on this line", lineNum, field);
							goto bad;
						}
					}

					includedParams |= (1 << id);

					if (id >= LAYOUT_NUMERICS)
					{
						register uint32_t		val;

						ptr[-1] = 0;

						// X can be a reference to some preceding ctl
						if (id == LAYOUT_X && (*ptr < '0' || *ptr > '9'))
						{
							register struct LOADGUICTL *	tmp;

							unsigned char					ref;
							ref = 0x80 | CTLID_COUNT;
							ptr = get_field_id((field = ptr), &UserGui[0].TypeStringCrc, &ref, sizeof(GUICTLDATA2));
							if (id >= CTLID_COUNT)
							{
								ref = 0x80 | 2;
								get_field_id(field, &ExtraCtlIds[0], &ref, 0);
								if (id >= 2) goto badtype;
								id += CTLID_MAIN_TEXT;
							}

							tmp = (struct LOADGUICTL *)TempBuffer;
							while (tmp->Type != ref && ++tmp < dest)
							{
							}
							if (tmp >= dest) goto badval;
							if (WindowFlags & WINFLAGS_RELATIVEXY)
								temp.Box.X = ((struct LOADGUICTL *)tmp)->Box.X;
							else
							{
								temp.Box.X = ((unsigned char *)tmp - TempBuffer);
								temp.GlobalFlags |= CTLGLOBAL_X_ALIGN;
							}
						}
						else
						{
							val = asciiToNum(&ptr);
							if (val > (id == LAYOUT_FONT ? 255 : 0xffff) || !ptr)
							{
badval:						format_text_err("bad %s value", lineNum, field);
								goto bad;
							}

							switch (id)
							{
								case LAYOUT_HEIGHT:
									temp.Box.Height = (unsigned short)val;
									if (temp.Type == CTLID_MAIN_WINDOW)
										WindowFlags |= WINFLAGS_HEIGHTSET;
									break;

								case LAYOUT_WIDTH:
									temp.Box.Width = (unsigned short)val;
									if (temp.Type == CTLID_MAIN_WINDOW)
										WindowFlags |= WINFLAGS_WIDTHSET;
									break;

								case LAYOUT_X:
									temp.Box.X = (unsigned short)val;
									if (temp.Type == CTLID_MAIN_WINDOW)
										WindowFlags |= WINFLAGS_POSITION;
									break;

								case LAYOUT_FONT:
									FontSize = (unsigned char)val;
									break;

	//							case LAYOUT_Y:
	//							case LAYOUT_LINE:
								default:
									temp.Box.Y = (unsigned short)val;

									if (temp.Type == CTLID_MAIN_WINDOW)
										WindowFlags |= WINFLAGS_POSITION;

									// Line numbers are referenced from 1
									else if (temp.Box.Y && id == LAYOUT_LINE) temp.Box.Y--;
							}
						}

						goto skip;
					}

					switch (id)
					{
						case LAYOUT_LABEL:
						{
							register uint32_t		len;

							// Isolate the string
label:					field = ptr;
							if (*ptr == '"')
							{
								ptr++;
								field++;
								while (*ptr != '"' && *ptr >= ' ') ptr++;
							}
							else
							{
								while (*ptr > ' ') ptr++;
							}

							// A panel?
							if (!panelName) panelName = &labels[0];

							temp.LabelOffset = (panelName - &labels[0]);

							if (temp.Type == CTLID_MAIN_GROUP)
							{
								if (grpLabelOffset)
								{
									field = (unsigned char *)&GroupBadStr[0];
									goto dupl;
								}

								grpLabelOffset = temp.LabelOffset;
							}

							// Copy to labels[]
							len = ptr - field;
							memcpy(panelName, field, len);
							panelName += len;
							*panelName++ = 0;
							if (*ptr == '"') ptr++;
skip:						ptr = skip_spaces(ptr);
							break;
						}

						case LAYOUT_OPTION:
						{
							register unsigned char		cnt, ctlid;
							unsigned char					ref;

							for (;;)
							{
								field = ptr;
								if (temp.Type > CTLID_MAIN_GROUP)
								{
									cnt = NUM_OF_WINLAYOUT_OPTS;
									goto winopt;
								}

								ctlid = UserGui[temp.Type].TypeId;
								if (temp.Type > CTLID_COUNT) goto two;

								// All ctls support NOPADDING option
								cnt = 1;

								// These also support the SMALL option, except CTLID_BACKINGPAD which supports the BOX option
								if (ctlid == CTLID_VARIATION || ctlid == CTLID_PANIC || ctlid == CTLID_PLAY || ctlid == CTLID_BACKINGPAD ||
									(ctlid >= CTLID_RITARD && ctlid <= CTLID_ACCELERANDO))
								{
two:								cnt = NUM_OF_LAYOUT_OPTS;
								}
winopt:
								ref = cnt | 0x40;
								ptr = get_field_id(field, (temp.Type > CTLID_MAIN_GROUP ? &LayoutWinOpts[0] : (ctlid == CTLID_BACKINGPAD ? &LayoutPadOpts[0] : &LayoutCtlOpts[0])), &ref, 0);
								if (ref >= cnt)
								{
									*ptr = 0;
									goto unk;
								}
								if (temp.Type > CTLID_MAIN_GROUP)
									WindowFlags |= (0x01 << ref);
								else if (ctlid == CTLID_BACKINGPAD && ref == 1)
									temp.CtlFlags |= CTLFLAG_LABELBOX;
								else
									temp.GlobalFlags |= LayoutCtlOptBits[ref];
								if (*ptr != ',') break;
								ptr = skip_spaces(++ptr);
							}
							break;
						}

						case LAYOUT_COLOR:
						{
							unsigned char						ref, cnt;

							cnt = 0;
							if (*ptr == ',') goto skipc;
							for (;;)
							{
								field = ptr;
								ref = NUM_OF_GUICOLOR_PARAMS | 0x40;
								ptr = get_field_id(field, &LayoutColors[0], &ref, 0);
								if (ref >= NUM_OF_GUICOLOR_PARAMS) goto unk;
								if (!cnt) temp.Color = ref;
								else if (temp.Type == CTLID_MAIN_TEXT || cnt > 2)
								{
too_many:						format_text_err("Too many colors", lineNum, 0);
									goto bad;
								}
								if (cnt == 1)
								{
									if (!ref) ref++;
									temp.Color |= (ref << 4);
								}
								if (cnt == 2)
								{
  									if (temp.Type >= CTLID_MAIN_TEXT || UserGui[temp.Type].TypeId != CTLID_VARIATION) goto too_many;
									VariationSelColor = ref;
								}
								if (*ptr != ',') break;
								do
								{
skipc:							cnt++;
									ptr = skip_spaces(++ptr);
								} while (*ptr == ',');
							}

							break;
						}
					}
				}

				// A type that allows LABEL= requires it
				if (temp.Type >= CTLID_MAIN_TEXT && ExtraLayoutFlags[temp.Type - CTLID_MAIN_TEXT] & LAYOUT_USES_LABEL)
				{
					if (!temp.LabelOffset && temp.Type != CTLID_MAIN_PANEL)
					{
						format_text_err("requires a LABEL", lineNum, 0);
						goto bad;
					}
				}

				if (temp.Type >= CTLID_MAIN_GROUP)
				{
					if (temp.Type == CTLID_MAIN_WINDOW)
					{
						// For a WINDOW line, transfer the settings to the GuiApp
						if (WindowFlags & WINFLAGS_POSITION)
						{
							MainWin->WinPos.X = temp.Box.X;
							MainWin->WinPos.Y = temp.Box.Y;
						}
						if (WindowFlags & WINFLAGS_WIDTHSET)
							MainWin->WinPos.Width = temp.Box.Width;
						if (WindowFlags & WINFLAGS_HEIGHTSET)
							MainWin->WinPos.Height = temp.Box.Height;
					}
				}
				else
				{
					temp.GlobalFlags |= flags;
					flags = 0;

					// Copy to temp buffer and move to next array element
					memcpy(dest, &temp, sizeof(struct LOADGUICTL));
					dest++;
				}
				}
			}

			{
			register struct GUIPANEL *	panelList;
			register uint32_t				count;

			if ((panelList = PanelList))
			{
				register const char **	mem;

				count = 2;
				while ((panelList = panelList->Next)) count++;

				mem = (const char **)malloc((count * sizeof(char *)) + (count * sizeof(short)));

				Menu.LabelCnt = count | (4 << 4);
				Menu.Labels = mem;
				*mem++ = SetupStr;
				panelList = PanelList;
				do
				{
					register GUICTL *	ctl;

					ctl = &panelList->Ctls[0];
					do
					{
					} while ((++ctl)->Type);
					*mem++ = (const char *)(++ctl);

				} while ((panelList = panelList->Next));

				Menu.Widths = (unsigned short *)mem;
			}
			}
		}
	}
}

// ============================================


char * headingCopyTo(register const char * msg, register char * headingPtr)
{
	register char * endptr;

	if (!headingPtr) headingPtr = &HeadingBuffer[0];
	endptr = &HeadingBuffer[SIZEOFHEADINGBUFFER - 1];
	if (headingPtr < &HeadingBuffer[0] || headingPtr > endptr)
	{
		headingPtr = &HeadingBuffer[0];
		while (headingPtr < endptr && *headingPtr++);
		--headingPtr;
	}
	while (headingPtr < endptr && (*headingPtr++ = *msg++));
	*(--headingPtr) = 0;
	return headingPtr;
}

void headingShow(register unsigned char color)
{
	GuiWinSetHeading(GuiApp, MainWin, color ? HeadingBuffer : 0, 0, color);
}


// ===========================================
// The "Loading data..." screen.

static uint32_t MsgBottom;
static const char LoadStageStrs[] = "Pad\0Guitar\0Bass\0Drumkit\0Command assignments\0Songs\0Styles\0preferences";
static const char LoadingStr[] = {'L','o','a','d','i','n','g',' '};
static const char WaitStr[] = "Please wait.";
static const char DeviceStr[] = "Setting up audio/midi hardware";
static unsigned char WhatToLoadFlagCopy;

static void handle_wait_mouse(register GUIMSG * msg)
{
	abort_load();
	clearMainWindow();
}

void clearLoadScreen(void)
{
	GUIBOX					box;

	if ((box.Height = MsgBottom))
	{
		box.X = GuiWinGetBound(GuiApp, MainWin, GUIBOUND_XSPACE);
		box.Y = GuiWinGetBound(GuiApp, MainWin, GUIBOUND_YSPACE);
		box.Width = MainWin->WinPos.Width - (box.X << 1);
		GuiWinAreaUpdate(GuiApp, MainWin, &box);
	}
	else
		clearMainWindow();
}


static void draw_loadscreen(void)
{
	GUIBOX					box;
	register uint32_t		lines;
	register char *		buffer;

	buffer = GuiBuffer;
	if (WhatToLoadFlagCopy & LOADFLAG_INPROGRESS)
	{
		register const char *	from;

		memcpy(buffer, &LoadingStr[0], sizeof(LoadingStr));
		buffer += sizeof(LoadingStr);
		if (WhatToLoadFlagCopy & LOADFLAG_INSTRUMENTS)
		{
			from = HeadingBuffer;
			while ((*buffer++ = *from++) && from < &HeadingBuffer[SIZEOFHEADINGBUFFER - 1]);
			buffer[-1] = ' ';
		}

		lines = LOADFLAG_PAD_AND_SOLO;
		from = &LoadStageStrs[0];
		while (!(lines & WhatToLoadFlagCopy))
		{
			while (*from++);
			lines >>= 1;
		}
		strcpy(buffer, from);
	}
	else
		strcpy(buffer, &DeviceStr[0]);

	memcpy(&box, &MainWin->WinPos, sizeof(GUIBOX));
	lines = GuiWinGetBound(GuiApp, MainWin, GUIBOUND_XSPACE);
	if (box.Width > (lines << 2))
	{
		box.X = lines;
		box.Width -= (lines << 1);
		lines = GuiWinGetBound(GuiApp, MainWin, GUIBOUND_YSPACE);
		if (box.Height > (lines << 2))
		{
			box.Y = lines;
			box.Height -= (lines << 1);

			MsgBottom = box.Y = GuiTextDrawMsg(GuiApp, GuiBuffer, &box, GUICOLOR_PURPLE << 24);
			box.Height = MainWin->WinPos.Height - box.Y;
			box.Y = GuiTextDrawMsg(GuiApp, &WaitStr[0], &box, GUICOLOR_VIOLET << 24);
		}
	}
}

static GUIFUNCS LoadGuiFuncs = {draw_loadscreen,
handle_wait_mouse,
dummy_keypress,
0};

void doLoadScreen(void)
{
	GuiWinSetHeading(GuiApp, MainWin, 0, 0, 0);
	GuiFuncs = &LoadGuiFuncs;
	MainWin->Flags = GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY;
	MainWin->LowerPresetBtns = GUIBTN_ABORT_SHOW|GUIBTN_CENTER;
	MainWin->Menu = 0;
	MainWin->Ctls = 0;
	MsgBottom = 0;
	clearMainWindow();
}

void doLoadDevScreen(void)
{
	MsgBottom = WhatToLoadFlagCopy = 0;
	doLoadScreen();
}

static void doFinishPickDev(register struct SOUNDDEVINFO * sounddev, register unsigned char op)
{
	GuiLoop = GUIBTN_ERROR;
	doLoadDevScreen();
}






/******************** mainWindowResize() *******************
 * Called by GUI thread to process MainWindow resize.
 */

//static void mainWindowResize(void)
//{
//}





/******************* show_msgbox() ********************
 * Displays text message, and waits for user response.
 *
 * Sets "GuiLoop" to user's GUIBTN_xxx response.
 */

void show_msgbox(register const char * msg)
{
	register uint32_t		temp;

	// Temp disable handling GUI updates for secondary threads
	temp = ShownCtlsMask;
	ShownCtlsMask = 0;

	// Show msg and wait for user response
	GuiLoop = (unsigned char)GuiErrShow(GuiApp, msg, (AppFlags2 & APPFLAG2_TIMED_ERR) ? GUIBTN_ESC_SHOW|GUIBTN_TIMEOUT_SHOW|GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT : GUIBTN_ESC_SHOW|GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);

	ShownCtlsMask = temp;
}





/******************* display_syserr() ********************
 * Display system API error message.
 */

void display_syserr(register int errNum)
{
	show_msgbox(strerror(errNum));
}




#if 0
void move_window(void)
{
	register GUIINIT *	init;

	init = GuiAppState(GuiApp, GUISTATE_GET_INIT, 0);
	init->X = MainWin->WinPos.X;
	init->Y = MainWin->WinPos.Y;
	init->Width = MainWin->WinPos.Width;
	init->Height = MainWin->WinPos.Height;
	GuiWinState(GuiApp, 0, GUISTATE_MOVE|GUISTATE_SIZE);
}
#endif




void redraw_area(register uint32_t x, register uint32_t y, register uint32_t width, register uint32_t height)
{
	GUIBOX	box;

	box.X = x;
	box.Y = y;
	box.Width = width;
	box.Height = height;
	GuiWinAreaUpdate(GuiApp, MainWin, &box);
}





/******************** setMainTitle() *********************
 * Sets the Main window title.
 */

void setMainTitle(register uint32_t reopen)
{
	register GUIINIT *	init;

	init = GuiAppState(GuiApp, GUISTATE_GET_INIT, 0);
	init->Title = (WindowFlags & WINFLAGS_NOTITLE) ? 0 : &WindowTitle[0];
	GuiWinState(GuiApp, 0, reopen ? GUISTATE_TITLE|GUISTATE_SHOW : GUISTATE_TITLE);
}





void set_color_black(void)
{
	GuiTextSetColor(MainWin, GUICOLOR_BLACK);
}

void set_color_red(void)
{
	GuiTextSetColor(MainWin, GUICOLOR_RED);
}





/********************* clearMainWindow() ********************
 * Invalidates area of main window, thereby causing a Expose
 * message to be sent to it. Does not do actual drawing here.
 */

void clearMainWindow(void)
{
	GuiWinUpdate(GuiApp, MainWin);
}








// XKeys USB Button panel
#ifdef XKEYS_PANEL

static libusb_device_handle *	ButtonsHandle = 0;
static unsigned char XKeyThreadState = 0;

/*********************** buttonsThread() ********************
 * Background thread to handle P.I. Engineering's XKeys USB
 * buttonpanel.
 *
 * Handle in 'ButtonsHandle'.
 */

static void * buttonsThread(void *arg)
{
	do
	{
		int					bytesRead;
		unsigned char		chr[18];

		// Wait for some button input
		if (!libusb_bulk_transfer(ButtonsHandle, 1, &chr[0], sizeof(chr), &bytesRead, 0) && bytesRead == 18)
		{
			register unsigned char	i, buttonNo;

			// Flywheel?
			if ((i = chr[5]))
			{
				if (i & 0x08) play_beat(0, GUITHREADID);
			}
			else
			{
				if ((i = chr[0])) buttonNo = 0;
				else if ((i = chr[1])) buttonNo = 6;
				else if ((i = chr[2])) buttonNo = 12;
				else if ((i = chr[3])) buttonNo = 18;
				else
				{
				 	i = chr[4];
				 	buttonNo = 24;
				}
				if (i)
				{
					while ((i >>= 1)) ++buttonNo;

					// Rocker button up/down chooses volume boost/cut
					if (buttonNo >= 28)
					{
						toggle_solo_vol((buttonNo == 28 ? 0 : 1, GUITHREADID));
						drawGuiCtl(guiHandle, CTLMASK_VOLBOOST, 0);
						continue;
					}

					// Choose patch
					if (setInstrumentByNum(PLAYER_SOLO | SETINS_BYINDEX | GUITHREADID, buttonNo, 0))
						drawGuiCtl(guiHandle, CTLMASK_PATCHLIST, 0);
				}
			}
		}
	} while (!XKeyThreadState);

	return 0;
}





/******************** init_button_panel() *******************
 * Locates XKeys USB button panel, and puts the handle in
 * 'ButtonsHandle'. Also creates the button input thread.
 */

static void init_button_panel(void)
{
	libusb_init(0);

	if (!(ButtonsHandle = libusb_open_device_with_vid_pid(0, 0x05f3, 0x304)))
		show_msgbox("Can't find XKeys panel");
	else
	{
		register int	ret;

		libusb_detach_kernel_driver(ButtonsHandle, 0);
		if ((ret = libusb_claim_interface(ButtonsHandle, 0)))
		{
			if (ret == LIBUSB_ERROR_BUSY)
				show_msgbox("Some other software is using the XKeys panel");
			else
				show_msgbox("Can't access XKeys panel");

bad:		libusb_close(ButtonsHandle);
			ButtonsHandle = 0;
		}
		else
		{
			pthread_t		threadHandle;

			// Start up a background thread to handle button input
			if (pthread_create(&threadHandle, 0, buttonsThread, 0))
			{
				libusb_release_interface(ButtonsHandle, 0);
				show_msgbox("Can't start XKeys panel thread");
				goto bad;
			}

			pthread_detach(threadHandle);
			goto out;
		}
	}

	libusb_exit(0);
out:
	return;
}

static void quit_button_panel(void)
{
	// Close buttons panel
	if (ButtonsHandle)
	{
		// Tell XKey buttons thread to terminate
		XKeyThreadState |= 0x80;

		libusb_release_interface(ButtonsHandle, 0);
		libusb_close(ButtonsHandle);
		libusb_exit(0);
	}
}
#endif






/******************** setCountOff() *******************
 * Sets/decrements/Queries the current countoff. Called
 * by beat play and main threads.
 *
 * Positive value sets the countoff to that value - 1. So
 * 1 = no CountOff, and 5 = 4 quarter notes.
 *
 * 0 queries CountOff without changing it.
 *
 * Negative value decs CountOff and redraws window. Done
 * only by beat play thread.
 */

unsigned char setCountOff(register void * signalHandle, register char dec)
{
	if (dec < 0)
	{
		CountOff -= dec;
		if (CountOff > TotalCountOff) CountOff = 0;
		drawGuiCtl(signalHandle, CTLMASK_PLAY, 0);
		return CountOff;
	}

	if (dec > 0)
	{
		TotalCountOff = dec - 1;
		CountOff = 0;
	}
	return TotalCountOff - CountOff;
}






// Chord chart
static unsigned char	PitchesW[] = {36, 38, 28, 29, 31, 33, 35, 0};
static unsigned char	PitchesUp[] = {1,  1,  0,  1,  1,  1,  0};
static unsigned char	PitchesDn[] = {0,  1,  1,  0,  1,  1,  1};

static uint32_t CurrentChord = 0xFF;

static const char ScaleNames[] = {'M','a','j','o','r',0,'M','i','n','o','r',0,'7','t','h',0,'A','u','g',0,'D','i','m',0,'S','u','s',0,0,0};


uint32_t lightPianoKey(register uint32_t chord)
{
	if (CurrentChord != chord)
	{
		CurrentChord = chord;
		return CTLMASK_CHORDS;
	}

	return CTLMASK_NONE;
}

static uint32_t ctl_update_chords(register GUICTL * ctl)
{
	CurrentChord = getCurrChord();
	return 1;
}

/******************* draw_chords() ******************
 * Draws the chord chart screen.
 */

static void draw_chords(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	register unsigned short	y, x;
	GUIBOX						box;
	register uint32_t			width, tempwidth;
	char							buf[22];
	unsigned char				keyNum, currentPianoKey;

//	if (ctl->Flags.Local & CTLFLAG_AREA_REQ_SIZE)
	{
		area->Bottom = area->Top + (4 * BoxHeight);
//		return;
	}

	if ((currentPianoKey = CurrentChord & 0xff) < 12)
	{
		register const char *	str;

		str = &ScaleNames[0];
		keyNum = (CurrentChord >> 8) & 0x0f;
		while (keyNum--) str += strlen(str) + 1;
		strcpy(&buf[4], str);
		if (CurrentChord & (1 << 16))
		{
			if (buf[4] == '7') buf[4] = '9';
			else strcat(&buf[4], "9");
		}
		else if (CurrentChord & (2 << 16) && buf[4] != '7')
			strcat(&buf[4], "7");
	}
//	GuiTextSetLarge(MainWin);
//	set_color_black();
	y = area->Top;
	width = (area->Right - area->Left) / 7;
	buf[3] = buf[2] = 0;
	x = area->Left;
	box.Width = width;
	box.Height = BoxHeight * 2;
	buf[0] = 'C';
	keyNum = 0;
	goto scale;
	do
	{
		box.X = x - (width / 2);
		box.Y = y;
		if (buf[0] != 'F')
		{
			buf[1] = 'b';
			GuiWinRect(GuiApp, &box, currentPianoKey == keyNum ? GUICOLOR_RED : GUICOLOR_DARKGRAY);
			GuiTextSetColor(MainWin, GUICOLOR_GREEN);
			box.Y += 1 + GuiApp->GuiFont.Height;
			tempwidth = (width - GuiTextWidth(MainWin, &buf[0])) / 2;
			GuiTextDraw(GuiApp, x + tempwidth - (width / 2), box.Y, &buf[0]);
			if (currentPianoKey == keyNum++)
			{
				box.Y += 1 + GuiApp->GuiFont.Height;
				tempwidth = (width - GuiTextWidth(MainWin, &buf[4])) / 2;
				GuiTextDraw(GuiApp, x + tempwidth - (width / 2), box.Y, &buf[4]);
			}
			set_color_black();
		}
		else
			GuiTextDrawMsg(GuiApp, "Click here to mute", &box, (GUICOLOR_GRAY << 24) | GUIDRAW_Y_CENTER);

scale:
		buf[1] = buf[0];
		box.X = x;
		box.Y = y + (BoxHeight * 2);
		GuiWinRect(GuiApp, &box, currentPianoKey == keyNum ? GUICOLOR_RED : 0);
		GuiTextSetColor(MainWin, GUICOLOR_GREEN);
		box.Y += 1 + GuiApp->GuiFont.Height;
		tempwidth = (width - GuiTextWidth(MainWin, &buf[1])) / 2;
		GuiTextDraw(GuiApp, x + tempwidth, box.Y, &buf[1]);
		if (currentPianoKey == keyNum++)
		{
			box.Y += 1 + GuiApp->GuiFont.Height;
			tempwidth = (width - GuiTextWidth(MainWin, &buf[4])) / 2;
			GuiTextDraw(GuiApp, x + tempwidth, box.Y, &buf[4]);
		}

		if (++buf[0] > 'G') buf[0] = 'A';
		x += width;
	} while (buf[0] != 'C');
}












static unsigned char	RoboNum;

static const char * patchbtn_label(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	register void *	ptr;
	register char *	to;

	to = buffer;
	if (app)
	{
		if ((ptr = getCurrentInstrument(((GUICTLDATA2 *)(ctl->Ptr))->PlayerNeeded)))
		{
			register const char *	from;
			register char				chr;

			from = getInstrumentName(ptr);
			do
			{
				chr = *from++;
				if (chr == '_') chr = '\n';
			} while (to < &buffer[63] && (*to++ = chr));
		}
	}
	*to = 0;

	return buffer;
}

static void patchbtn_mouse(register GUIMSG * msg)
{
	register GUICTL *	ctl;

	ctl = msg->Mouse.SelectedCtl;
	if (ctl)
	{
		if (ctl->Flags.Global & CTLGLOBAL_PRESET)
		{
			if (ctl->PresetId == GUIBTN_OK) goto ok;
			goto cancel;
		}

		if (ctl->Type == CTLTYPE_AREA)
		{
			switch (msg->Mouse.ListAction)
			{
				case GUILIST_SELECTION:
ok:				if (List.CurrItemNum != -1)
						setInstrumentByNum(RoboNum | ((!RoboNum || RoboNum >= PLAYER_PAD) ?
						 (SETINS_BYINDEX|GUITHREADID): (SETINS_BYINDEX|SETINS_SHOWHIDDEN|GUITHREADID)), List.CurrItemNum, 0);

				case GUILIST_ABORT:
cancel:			showMainScreen();
			}
		}
	}
}

static GUIFUNCS	PatchBtnFuncs = {dummy_drawing, patchbtn_mouse, dummy_keypress};

/********************* draw_patchbtns() *********************
 * Called by GUI thread to draw patch list for a Patch Button.
 */;

static GUILIST * draw_patchbtns(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app && area)
	{
		register void *		ptr;

		if (area->Right - area->Left >= PatchMinBoxWidth)
		{
			if ((ptr = getNextInstrument(0, RoboNum)))
			{
				do
				{
					register const char *	from;

					if ((from = (!RoboNum || RoboNum > PLAYER_PAD) ? isHiddenPatch(ptr, RoboNum) : getInstrumentName(ptr)))
					{
						register char *	to;
						register char		chr;

						to = GuiBuffer;
						do
						{
							chr = *from++;
							if (chr == '_') chr = ' ';
						} while ((*to++ = chr));

						if (GuiListDrawItem(app, ctl, area, GuiBuffer)) break;
					}
				} while ((ptr = getNextInstrument(ptr, RoboNum)));
			}
		}
	}

	return &List;
}

/***************** ctl_set_patch_btn() ****************
 *
 */

static uint32_t ctl_set_patch_btn(register GUICTL * ctl)
{
	RoboNum = ((GUICTLDATA2 *)(ctl->Ptr))->PlayerNeeded;
	doPickItemDlg(draw_patchbtns, &PatchBtnFuncs);
	List.NumOfItems = getNumOfInstruments(RoboNum);
	List.ColumnWidth = ctl->Width;

	headingCopyTo(getMusicianName(RoboNum), headingCopyTo("Choose instrument for ", 0));
	headingShow(GUICOLOR_PURPLE);

	// doPickItemDlg will do redraw later
	return CTLMASK_NONE;
}

/********************* drawGridName() *********************
 * Called by GUI thread to draw a Patch/Style name.
 */

static void drawGridName(register GUIBOX * box, register const char * name)
{
	register char *	name2;
	register char		chr;
	register uint32_t	width;

	box->Height = BoxHeight;

	// Draw a box around the name
	GuiWinRect(GuiApp, box, 0);

	// Display the name on 1 or 2 lines. An underscore is a line break
	name2 = (char *)name;
	while ((chr = *name2) && chr != '_') ++name2;
	*name2 = 0;
	width = (box->Width - GuiTextWidth(MainWin, name)) / 2;
	GuiTextDraw(GuiApp, box->X + width, box->Y + 1 + (chr ? 0: GuiApp->GuiFont.Height >> 1), name);
	if (chr)
	{
		*name2++ = '_';
		width = (box->Width - GuiTextWidth(MainWin, name2)) / 2;
		GuiTextDraw(GuiApp, box->X + width, box->Y + 1 + GuiApp->GuiFont.Height, name2);
	}
}

/********************* draw_patches() *********************
 * Called by GUI thread to draw the instruments (patches) for
 * the Drums, Bass, Guitar, Pad, Human Soloist. This is used
 * when an AREA GUICTL is used, rather than a PUSHBUTTON ctl.
 */

static void draw_patches(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	GUIBOX						box;
	register unsigned char	roboNum;

	roboNum = ((GUICTLDATA2 *)(ctl->Ptr))->PlayerNeeded;
	box.Y = area->Top;

	{
	register uint32_t		numBoxes, width;

	width = area->Right - area->Left;
	if (width < PatchMinBoxWidth) goto out;
	numBoxes = (width / PatchMinBoxWidth);
	PatchBoxWidth = ((width - (numBoxes * PatchMinBoxWidth)) / numBoxes) + PatchMinBoxWidth;
	}

	{
	register void *			ptr;

	box.Width = PatchBoxWidth;
	box.Height = BoxHeight;
	ptr = 0;
	goto start;
	do
	{
		register const char *	name;

		name = (!roboNum || roboNum > PLAYER_PAD) ? isHiddenPatch(ptr, roboNum) : getInstrumentName(ptr);
		if (name)
		{
			if (ptr == getCurrentInstrument(roboNum))
			{
				GuiWinRect(GuiApp, &box, GUICOLOR_GOLD - roboNum);
				set_color_black();
			}

			// Display the patch name
			drawGridName(&box, name);

			box.X += PatchBoxWidth;
			if (box.X + PatchBoxWidth > area->Right)
			{
				box.Y += BoxHeight;
start:		box.X = area->Left;
				if (box.Y + BoxHeight > area->Bottom) break;
			}
		}
	} while ((ptr = getNextInstrument(ptr, roboNum)));

	if (box.X != area->Left) box.Y += BoxHeight;
	}

out:
	area->Bottom = box.Y;
}




uint32_t ctl_update_nothing(register GUICTL * ctl)
{
	return 0;
}




uint32_t ctl_update_area(register GUICTL * ctl)
{
	return 1;
}





/********************* draw_styles() *********************
 * Called by GUI thread to draw Styles grid.
 */

static void draw_styles(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	// Calc size of a grid box
	if ((ctl->Flags.Local & CTLFLAG_AREA_REQ_SIZE) || !StylesBoxWidth)
	{
		register uint32_t		numBoxes, width;

		width = area->Right - area->Left;
		if (width < StylesMinBoxWidth) width = StylesMinBoxWidth;
		numBoxes = (width / StylesMinBoxWidth);
		StylesBoxWidth = ((width - (numBoxes * StylesMinBoxWidth)) / numBoxes) + StylesMinBoxWidth;
		numBoxes = (width / CatMinBoxWidth);
		CatBoxWidth = ((width - (numBoxes * CatMinBoxWidth)) / numBoxes) + CatMinBoxWidth;
		if (CatBoxWidth < CatMinBoxWidth) CatBoxWidth = CatMinBoxWidth;
	}
	{
	register void *		ptr;
	GUIBOX					box;
	register uint32_t		y, x;

	GuiTextSetSmall(MainWin);

	// =========================== Draw the style Category names
	{
	register const char *	name;

	ptr = 0;
	x = area->Left;
	y = area->Top;
	NumCatLines = 1;
	box.Width = CatBoxWidth;
	while ((ptr = getStyleCategory(ptr)))
	{
		if (y + BoxHeight > area->Bottom) break;
		if (x + CatBoxWidth > area->Right)
		{
			x = area->Left;
			y += BoxHeight;
			NumCatLines++;
		}

		//if (!(ctl->Flags.Local & CTLFLAG_AREA_REQ_SIZE))
		{
			name = getStyleCategoryName(ptr);

			// Highlight if the currently selected style Category
			box.X = x + 1;
			box.Y = y + 1;
			box.Width = CatBoxWidth - 1;
			box.Height = BoxHeight - 1;
			GuiWinRect(GuiApp, &box, getCurrentStyleCategory() == ptr ? GUICOLOR_RED : GUICOLOR_PINK);

			set_color_black();

			// Display the style Category name
			box.X++;
			box.Y++;
			drawGridName(&box, name);
		}

		// Next box position
		x += CatBoxWidth;
	}
	}

	// ======================= Draw the style names in the current Category
	GuiTextSetLarge(MainWin);
	ptr = 0;
	box.Width = StylesBoxWidth;
	goto start;
	while ((ptr = getStyle(ptr)))
	{
//		if (!(ctl->Flags.Local & CTLFLAG_AREA_REQ_SIZE))
		{
			// Highlight if the currently selected style
			if (getCurrentStyle() == ptr)
			{
				box.X = x + 1;
				box.Y = y + 1;
				box.Width = StylesBoxWidth - 1;
				box.Height = BoxHeight - 1;
				GuiWinRect(app, &box, GUICOLOR_GOLD);
				set_color_black();
			}

			// Display the style name
			box.X = x;
			drawGridName(&box, getStyleName(ptr));
		}

		// Next box position
		x += StylesBoxWidth;

		// Go to next row if we're at the right area edge
		if (x + StylesBoxWidth > area->Right)
		{
start:	y += BoxHeight;
			if (y + BoxHeight > area->Bottom) goto out;
			x = area->Left;
			box.Y = y;
		}
	}

out:
	if (ctl->Flags.Local & CTLFLAG_AREA_REQ_SIZE)
	{
		if (!(x = (area->Right - area->Left) / StylesBoxWidth)) x = 1;
		area->Bottom = area->Top + (BoxHeight * (
			(MostStyles / x) +
			((MostStyles % x) ? 1 : 0) +
			NumCatLines));
	}
	}
}





static GUICTL * findCtlById(register uint32_t id)
{
	register GUICTL *		ctl;

	if ((id > 31 || (ShownCtlsMask & (1 << id))) && (ctl = MainWin->Ctls))
	{
		do
		{
			while (ctl->Type)
			{
				if (ctl->Ptr && ((GUICTLDATA2 *)(ctl->Ptr))->TypeId == id) return ctl;
				ctl++;
			}
		} while ((ctl = ctl->Next));
	}

	return 0;
}





/******************* ctl_set_tapTempo() ******************
 * Operates the "Tempo tap" button.
 */

static uint32_t ctl_set_tapTempo(register GUICTL * ctl)
{
	register GUIMSG *			msg;
	register uint32_t			tapTempo;
	register unsigned char	numTaps;

	if (lockTempo(GUITHREADID) == GUITHREADID)
	{
		// Get current time
		numTaps = 0;
		tapTempo = GuiApp->CurrTime;

		// Ignore signals from other threads
		ShownCtlsMask = 0;

		// Do our msg loop here, with raw input
		MainWin->Flags = GUIWIN_RAW_KEY|GUIWIN_RAW_MOUSE;

		goto nexttime;
again:
		while ((msg = GuiWinGetTimedMsg(GuiApp, 5000)))
		{
			switch (msg->Type)
			{
				case GUI_MOUSE_CLICK:
				{
					register uint32_t	amt;

					if (msg->Mouse.ButtonNum != 1) goto out;

					if (!ctl) goto nexttime;

					// If a click on our tap button, figure in
					// the current time
					amt = GuiWinGetBound(GuiApp, MainWin, GUIBOUND_UPPER);
					if (msg->Mouse.Y > amt)
					{
						msg->Mouse.Y -= amt;
						if (ctl && msg->Mouse.Y >= ctl->Y && msg->Mouse.Y < ctl->Y + GuiCtlGetHeight(GuiApp, ctl) &&
							msg->Mouse.X >= ctl->X &&  msg->Mouse.X < ctl->X + ctl->Width);
						{
							goto nexttime;
						}
					}
					goto out;
				}

				case GUI_KEY_PRESS:
				{
					switch (msg->Key.Code)
					{
						case XK_space:
						{
							register uint32_t	amt;

nexttime:				if (++numTaps >= 4)
							{
								amt = (60000 * (4-1)) / (GuiApp->CurrTime - tapTempo);
								if (amt > 10 && amt <= 255)
									set_bpm(amt);

								goto out;
							}

							// Display the next tap
							// Display taps in tempo button if found, otherwise in tap button
							if (TempoCtl)
							{
								setTempoLabel(numTaps);
								GuiCtlUpdate(GuiApp, 0, TempoCtl, 0, 0);
							}

							goto again;
						}

						case XK_Escape:
							goto out;
					}

					break;
				}

				case GUI_WINDOW_CLOSE:
					GuiLoop = GUIBTN_QUIT;
				case GUI_TIMEOUT:
				{
//					headingShow(0);
					goto out;
				}
			}
		}

out:	setTempoLabel(0);
		unlockTempo(GUITHREADID);
		showMainScreen();
	}
	else
		unlockTempo(GUITHREADID);
	return CTLMASK_NONE;
}

uint32_t setTempoLabel(register unsigned char flag)
{
	register GUICTL * ctl;

	if ((ctl = TempoCtl))
	{
		if (flag)
			ctl->Label = &PlayStr[4 - flag];
		else
			ctl->Label = (((GUICTLDATA2 *)(ctl->Ptr))->TypeId == CTLID_TEMPO ? TempoBuf : TapStr);
		return (1 << (((GUICTLDATA2 *)(ctl->Ptr))->TypeId));
	}

	return CTLMASK_NONE;
}


static void get_menu_area(register GUIBOX *  box)
{
	box->Width = MainWin->WinPos.Width - (((WindowFlags & WINFLAGS_NOTITLE) ? 8 : 0) + 3) * GuiApp->GuiFont.QuarterWidth;
	box->Y = box->X = 0;
	box->Height = GuiApp->GuiFont.Height;
}

static void redraw_menu(void)
{
	GUIBOX	box;

	get_menu_area(&box);
	GuiWinAreaUpdate(GuiApp, MainWin, &box);
}


#if !defined(NO_ALSA_AUDIO_SUPPORT)

static uint32_t		XRuns;
static unsigned char XRunColor;


static void draw_xrun(register unsigned char drawFlag)
{
	uint32_t		count;

	// Draw XRun indicator if any XRuns
	if (!drawFlag || (count = xrun_count(0)))
	{
		GUIBOX	box;

//		get_menu_area(&box);
		box.X = MainWin->WinPos.Width - (((WindowFlags & WINFLAGS_NOTITLE) ? 8 : 0) + 3) * GuiApp->GuiFont.QuarterWidth;
		box.Y = GuiApp->GuiFont.Height / 4;
		box.Width = GuiApp->GuiFont.QuarterWidth * 2;
		box.Height = GuiApp->GuiFont.Height / 2;
		if (!drawFlag)
			GuiWinAreaUpdate(GuiApp, MainWin, &box);
		else
		{
			if (count != XRuns) XRunColor = (XRunColor == GUICOLOR_BLACK ? GUICOLOR_RED : GUICOLOR_BLACK);
			GuiWinRect(GuiApp, &box, XRunColor);
			XRuns = count;
		}
	}
}
#endif




/******************* play_button_label() ********************
 * Updates the play button label (Stop, Play, Ending,
 * or CountOff).
 *
 * app = 0 if caller wants longest label.
 */

const char * play_button_label(GUIAPPHANDLE app, GUICTL * ctl, char * buffer)
{
	register const char *	ptr;

	ptr = BeatInPlay ? &PlayStr[10] : &PlayStr[5];

	// Display count off
	if (CountOff) ptr = &PlayStr[4 - CountOff];

	if (!app || getCurrVariationNum(VARIATION_INPLAY) == 4) ptr = &PlayStr[15];

	return ptr;
}

static uint32_t ctl_set_play(register GUICTL * ctl)
{
	play_beat(0, GUITHREADID);

	// beat play thread signals when to update, so tell
	// main thread caller not to do it now
	return CTLMASK_NONE;
}




/******************* ctl_update_vol_boost() *******************
 * Updates the "Vol Boost" checkbox.
 */

static uint32_t ctl_update_volboost(register GUICTL * ctl)
{
	ctl->Attrib.Value = toggle_solo_vol(0xFF, GUITHREADID) != 0;
	return 1;
}

static uint32_t ctl_set_volboost(register GUICTL * ctl)
{
	ctl->Attrib.Value = toggle_solo_vol(2, GUITHREADID) != 0;
	return CTLMASK_VOLBOOST;
}

void setAppFlags(register unsigned char flag)
{
	// If Setup screen, then alter AppFlags3, which gets permanently
	// saved to the config file
	if (!Menu.Select)
	{
		AppFlags3 ^= flag;
		TempFlags = (TempFlags & ~flag) | (AppFlags3 & flag);
	}
	// Otherwise just alter the temporary flags that last only
	// while the program runs
	else
		TempFlags ^= flag;
}

void testAppFlags(register GUICTL * ctl, register unsigned char flag)
{
	flag &= (!Menu.Select ? AppFlags3 : TempFlags);
	ctl->Attrib.Value = (flag ? 1 : 0);
}

static uint32_t ctl_set_cycle(register GUICTL * ctl)
{
	setAppFlags(APPFLAG3_NOVARCYCLE);

	// Done setting this parameter. Let caller redraw the ctl
	return CTLMASK_CYCLEVARIATION;
}

static uint32_t ctl_update_cycle(register GUICTL * ctl)
{
	testAppFlags(ctl, APPFLAG3_NOVARCYCLE);
	ctl->Attrib.Value ^= 0x01;
	return 1;
}





/********************** ctl_update_pad() **********************
 * Updates the current background pad (None, Strings,
 * Brass, or Organ).
 */

static uint32_t ctl_update_pad(register GUICTL * ctl)
{
	ctl->Attrib.Value = getPadPgmNum() + 1;
	return 1;
}

static uint32_t ctl_set_pad(register GUICTL * ctl)
{
	register unsigned char	boxNum;

	ctl->Attrib.Value = boxNum = ctl->Select;
	if (--boxNum > 2) boxNum = 3;
	return changePadInstrument(boxNum | GUITHREADID);
}


static uint32_t ctl_set_panic(register GUICTL * ctl)
{
	return allNotesOff(GUITHREADID);
}



/***************** ctl_update_accompmute() *****************
 * Updates the "Bass/Gtr" checkbox.
 */

static uint32_t ctl_update_accompmute(register GUICTL * ctl)
{
	if ((TempFlags & (APPFLAG3_NOGTR|APPFLAG3_NOBASS)) == (APPFLAG3_NOGTR|APPFLAG3_NOBASS))
		ctl->Attrib.Value = 0;
	else if (!(TempFlags & (APPFLAG3_NOGTR|APPFLAG3_NOBASS)))
		ctl->Attrib.Value = 1;
	return 1;
}
static uint32_t ctl_set_accompmute(register GUICTL * ctl)
{
	return mute_robots(ACCOMP_TOGGLE|APPFLAG3_NOPAD|APPFLAG3_NOBASS|APPFLAG3_NOGTR, GUITHREADID);
}

static uint32_t ctl_update_master_vol(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, getMasterVol());
	return 1;
}

static void store_master_vol(register GUINUMPAD * numpad)
{
	if (numpad && numpad->Value <= 100)
		setMasterVol((unsigned char)numpad->Value);
	if (!Menu.Select)
		doSetupScreen();
	else
		showMainScreen();
}

static uint32_t ctl_set_master_vol(register GUICTL * ctl)
{
	if (GuiApp->DClickFlag)
	{
		doNumeric(store_master_vol, "Enter master mix volume", 0);
		return 0;
	}
	GuiCtlArrowsValue(GuiApp, ctl);
	return setMasterVol(ctl->Attrib.Value);
}

static void store_vol(register GUINUMPAD * numpad)
{
	if (numpad && numpad->Value <= 100)
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
		setInstrumentVol(RoboNum, (unsigned char)numpad->Value, GUITHREADID);
#else
		setInstrumentVol(RoboNum, (unsigned char)numpad->Value);
#endif
	if (!Menu.Select)
		doSetupScreen();
	else
		showMainScreen();
}

static uint32_t ctl_update_vol(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, getInstrumentVol(((GUICTLDATA2 *)(ctl->Ptr))->PlayerNeeded));
	return 1;
}

static uint32_t ctl_set_vol(register GUICTL * ctl)
{
	if (GuiApp->DClickFlag)
	{
		RoboNum = ((GUICTLDATA2 *)(ctl->Ptr))->PlayerNeeded;
		sprintf((char *)TempBuffer, "Enter mix volume for %s", getMusicianName(RoboNum));
		doNumeric(store_vol, (char *)TempBuffer, 0);
		return 0;
	}
	GuiCtlArrowsValue(GuiApp, ctl);
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
	return setInstrumentVol(((GUICTLDATA2 *)(ctl->Ptr))->PlayerNeeded, ctl->Attrib.Value, GUITHREADID);
#else
	return setInstrumentVol(((GUICTLDATA2 *)(ctl->Ptr))->PlayerNeeded, ctl->Attrib.Value);
#endif
}

GUICTLDATA * getVolCtlDataPtr(register unsigned char musicianNum)
{
	register GUICTLDATA2 *	ptr;

	ptr = &UserGui[0];
	while (musicianNum--) ptr++;
	return (GUICTLDATA *)ptr;
}





// ============ Reverb Volume Ctl

#ifndef NO_REVERB_SUPPORT
static uint32_t ctl_update_rev_vol(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, setReverbVol(-1));
	return 1;
}

static void store_reverb_vol(register GUINUMPAD * numpad)
{
	if (numpad && numpad->Value <= 100)
		setReverbVol((unsigned char)numpad->Value);
	if (!Menu.Select)
		doSetupScreen();
	else
		showMainScreen();
}

static uint32_t ctl_set_rev_vol(register GUICTL * ctl)
{
	if (GuiApp->DClickFlag)
	{
		doNumeric(store_reverb_vol, "Enter mix volume for reverb", 0);
		return 0;
	}
	GuiCtlArrowsValue(GuiApp, ctl);
	return setReverbVol(ctl->Attrib.Value);
}

// ============ Reverb Mute Ctl

static uint32_t ctl_update_reverb_sw(register GUICTL * ctl)
{
	testAppFlags(ctl, APPFLAG3_NOREVERB);
	ctl->Attrib.Value ^= 0x01;
	return 1;
}

static uint32_t ctl_set_reverb_sw(register GUICTL * ctl)
{
	setAppFlags(APPFLAG3_NOREVERB);
	return CTLMASK_REVERBMUTE;
}
#endif







// ============ Drum/Bass/Guitar/Pad/Accomp Mute Ctls

static uint32_t ctl_update_robot_mute(register GUICTL * ctl)
{
	ctl->Attrib.Value = ((TempFlags & (APPFLAG3_NODRUMS << ((GUICTLDATA2 *)(ctl->Ptr))->PlayerNeeded)) ? 0 : 1);
	return 1;
}

static uint32_t ctl_set_robot_mute(register GUICTL * ctl)
{
	register uint32_t				mask;
	register unsigned char		roboNum;

	roboNum = ((GUICTLDATA2 *)(ctl->Ptr))->PlayerNeeded;
	mask = mute_robots(ACCOMP_TOGGLE | (APPFLAG3_NODRUMS << roboNum), GUITHREADID);
	if (isRobotSetup())
	{
		AppFlags3 &= ~(APPFLAG3_NOPAD|APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NODRUMS);
		AppFlags3 |= (TempFlags & (APPFLAG3_NOPAD|APPFLAG3_NOBASS|APPFLAG3_NOGTR|APPFLAG3_NODRUMS));
	}
	return mask;
}











static uint32_t ctl_update_autostart(register GUICTL * ctl)
{
	ctl->Attrib.Value = (TempFlags & TEMPFLAG_AUTOSTART) ? 1 : 0;
	return 1;
}

static uint32_t ctl_set_autostart(register GUICTL * ctl)
{
	TempFlags ^= TEMPFLAG_AUTOSTART;
	return CTLMASK_AUTOSTART;
}





/***************** ctl_update_transpose() ******************
 * Updates the current transposition amount.
 */

static uint32_t ctl_update_transpose(register GUICTL * ctl)
{
	GuiCtlArrowsInit(ctl, (!Menu.Select ? setConfigTranspose(0xFF) : setTranspose(0xFF, GUITHREADID)));
	return 1;
}

/******************** ctl_set_transpose() *******************
 * Handles the transpose spin button.
 */

static uint32_t ctl_set_transpose(register GUICTL * ctl)
{
	if (GuiCtlArrowsValue(GuiApp, ctl))
	{
		setTranspose(ctl->Attrib.Value, GUITHREADID);
		if (!Menu.Select) setConfigTranspose(ctl->Attrib.Value);
		return CTLMASK_TRANSPOSE;
	}

	return CTLMASK_NONE;
}





/******************* setVariationBlink() *******************
 * Called by Beat Play thread to determine whether to blink
 * the variation button).
 */

static uint32_t blink_variation(void)
{
	register unsigned char	orig;

	orig = VariationColor;

	if (VariationBlink)
	{
		// Flip the background color
		VariationColor = ((VariationColor & 0x0f) == GUICOLOR_RED ? VariationSelColor : (GUICOLOR_RED | (VariationSelColor & 0xF0)));
	}
	else
		VariationColor = VariationSelColor;

	return (orig != VariationColor);
}

uint32_t setVariationBlink(void)
{
	register unsigned char	blink, orig;
	register uint32_t			mask;

	orig = VariationBlink;
	mask = CTLMASK_VARIATION;
	blink = 0;
	if (BeatInPlay && (ShownCtlsMask & mask) && getCurrVariationNum(VARIATION_USER) != getCurrVariationNum(VARIATION_INPLAY))
		blink = 1;
	VariationBlink = blink;
	return (orig != blink && blink_variation()) ? mask : 0;
}

/******************* ctl_update_variation() *******************
 * Updates the current variation (Intro, Verse, Chorus,
 * or Bridge).
 */

static uint32_t ctl_update_variation(register GUICTL * ctl)
{
	register unsigned char	test, variNum, color, hide;

	variNum = getCurrVariationNum(VARIATION_USER);
	test = 3;	// intro
	do
	{
		hide = (isVariation(test) ? 0 : CTLTYPE_HIDE);
		color = (test == variNum ? VariationColor : VariationNormColor);
		if (ctl->Attrib.Color != color && !(ctl->Type & CTLTYPE_HIDE))
			GuiCtlUpdate(GuiApp,	MainWin, ctl, 0, 0);
		ctl->Attrib.Color = color;
		GuiCtlShow(GuiApp, MainWin, ctl, hide);
		ctl++;
		if (++test > 3) test = 0;
	} while (test < 3);

	return 0;
}

static uint32_t ctl_set_variation(register GUICTL * ctl)
{
	switch (ctl->Label[0])
	{
		register unsigned char variNum, oldVariNum, index;

		case 'B':
		{
			variNum = 2;
			index = 3;
			goto vari;
		}

		case 'C':
		{
			variNum = 1;
			index = 2;
			goto vari;
		}

		case 'I':
		{
			variNum = 3;
			index = 0;
			goto vari;
		}

		case 'V':
		{
			variNum = 0;
			index = 1;
vari:		while (lockVariation(GUITHREADID) != GUITHREADID) usleep(500);
			oldVariNum = getCurrVariationNum(VARIATION_USER);
			ctl -= index;
			VariationColor = VariationSelColor;
			// note selectStyleVariation unlocks
			if (selectStyleVariation(variNum, GUITHREADID) && oldVariNum != variNum) //return CTLMASK_VARIATION;
			{
				ctl_update_variation(ctl);
				return CTLMASK_NOREDRAW;
			}
		}
	}

	return CTLMASK_NONE;
}








static void tempo_numpad_done(register GUINUMPAD * numpad)
{
	if (lockTempo(GUITHREADID))
	{
		if (numpad && numpad->Value >= 20 && numpad->Value < 256)
		{
			set_bpm((unsigned char)numpad->Value);
			drawGuiCtl(0, CTLMASK_TEMPO, 0);
		}
	}

	unlockTempo(GUITHREADID);
	showMainScreen();
}

/******************* doTempoNumpad() ******************
 * Shows/operates the "Enter tempo" number pad screen.
 */

static void doTempoNumpad(register uint32_t initValue)
{
	doNumeric(tempo_numpad_done, "Enter tempo BPM", initValue);
}

/******************** ctl_update_tempo() ********************
 * Updates the current tempo.
 */

static uint32_t ctl_update_tempo(register GUICTL * ctl)
{
	register unsigned char tempo;

	if (is_midiclock())
		strcpy(TempoBuf, "MIDI\nClock");
	else
	{
		tempo = set_bpm(0);
		TempoBuf[0] = TempoBuf[1] = ' ';
		if (tempo >= 100)
		{
			TempoBuf[0] = (tempo / 100) + '0';
			TempoBuf[1] = '0';
			tempo %= 100;
		}
		if (tempo >= 10)
		{
			TempoBuf[1] = (tempo / 10) + '0';
			tempo %= 10;
		}
		TempoBuf[2] = tempo + '0';
		strcpy(&TempoBuf[3], "\nBPM");
	}

	return 1;
}

static uint32_t ctl_set_tempo(register GUICTL * ctl)
{
	doTempoNumpad(0);
	return CTLMASK_NONE;
}





#ifndef NO_SONGSHEET_SUPPORT

/****************** ctl_update_songsheet() *****************
 * Updates the "Song Sheet" selection button.
 */

static uint32_t ctl_update_songsheet(register GUICTL * ctl)
{
	ctl->Attrib.Value = (isSongsheetActive() ? 1 : 0);
	return 1;
}

static uint32_t ctl_set_songsheet(register GUICTL * ctl)
{
	// If there's also a SongSheet list ctl, then regard this click as
	// selecting the current sheet
	if (ShownCtlsMask & CTLMASK_SONGSHEETLIST)
	{
		if (SongsheetBox.CurrItemNum != -1)
			return selectSongSheet(SongsheetBox.CurrItemNum + 1, GUITHREADID);
		return CTLMASK_NONE;
	}

	return selectSongSheet(0xff, GUITHREADID);
}

/********************* draw_songsheets() *********************
 * Called by GUI thread to draw SongSheets grid.
 */

GUILIST * draw_songsheets(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	if (app)
	{
		register void *		ptr;

		if ((ptr = getNextSongSheet(0)))
		{
			do
			{
				if (GuiListDrawItem(app, ctl, area, getSongSheetName(ptr))) break;
			} while ((ptr = getNextSongSheet(ptr)));
		}
	}

	return &SongsheetBox;
}

#endif








static uint32_t ctl_set_ritard(register GUICTL * ctl)
{
	set_ritard_or_accel(ctl->Label == RitardStr ? PLAYFLAG_RITARD : PLAYFLAG_ACCEL);
	return CTLMASK_NONE;
}





/******************** ctl_update_time() *******************
 * Updates the current time.
 */

static uint32_t ctl_update_time(register GUICTL * ctl)
{
	struct tm *		time_ptr;
	time_t			mytime1;
	register unsigned char	CurrentHour, CurrentMin;

	mytime1 = time(0);
	time_ptr = localtime(&mytime1);

	CurrentHour = time_ptr->tm_hour;
	if (CurrentHour >= 12) CurrentHour -= 12;
	if (!CurrentHour) CurrentHour = 12;
	CurrentMin = time_ptr->tm_min;

	sprintf(&TimeBuf[0], "%2d:%02d", CurrentHour, CurrentMin);
	return 1;
}




static uint32_t ctl_set_robot_list(register GUICTL * ctl)
{
	register GUIMSG *			msg;
	register unsigned char	boxNum, roboNum;

	msg = (GUIMSG *)MainWin->Ptr;
	roboNum = ((GUICTLDATA2 *)(ctl->Ptr))->PlayerNeeded;
	boxNum = ((ctl->Width / PatchBoxWidth) * ((msg->Mouse.Y - ctl->Y) / BoxHeight)) + ((msg->Mouse.X - ctl->X) / PatchBoxWidth);
	return setInstrumentByNum(roboNum | ((!roboNum || roboNum >= PLAYER_PAD) ? (SETINS_BYINDEX|GUITHREADID) : (SETINS_BYINDEX|SETINS_SHOWHIDDEN|GUITHREADID)), boxNum, 0);
}




static uint32_t ctl_set_style(register GUICTL * ctl)
{
	register GUIMSG *			msg;
	register unsigned char	boxNum, line;

	msg = (GUIMSG *)MainWin->Ptr;
	msg->Mouse.X -= ctl->X;
	line = ((msg->Mouse.Y - ctl->Y) / BoxHeight);

	// First row(s) is the style Categories
	if (line < NumCatLines)
	{
		boxNum = ((ctl->Width / CatBoxWidth) * line) + (msg->Mouse.X / CatBoxWidth);
		return selectStyleCategory(boxNum);
	}

	// User clicked on a style name

	// Figure out which box user clicked upon
	boxNum = ((ctl->Width / StylesBoxWidth) * (line - NumCatLines)) + (msg->Mouse.X / StylesBoxWidth);
	return selectStyle(boxNum, GUITHREADID);
}





#ifndef NO_SONGSHEET_SUPPORT
static uint32_t ctl_set_songsheet_list(register GUICTL * ctl)
{
	register GUIMSG *			msg;

	msg = (GUIMSG *)MainWin->Ptr;

	if (!msg->Mouse.ListAction && SongsheetBox.CurrItemNum != -1)
		return selectSongSheet(SongsheetBox.CurrItemNum + 1, GUITHREADID);
	return CTLMASK_NONE;
}
#endif





/**************** handle_panel_mouse() ***************
 * Handles mouse click.
 */

void handle_panel_mouse(register GUIMSG * msg)
{
	register GUICTL *		ctl;

	if ((ctl = msg->Mouse.SelectedCtl))
	{
		register GUICTLDATA *	data;

		if ((data = (GUICTLDATA *)ctl->Ptr) && data->SetFunc)
		{
			register uint32_t		mask;

			// Extra processing needed after the set() function?
			if ((mask = data->SetFunc(ctl)))
			{
				// Caller want SaveConfigFlag set?
				if (mask & CTLMASK_SETCONFIGSAVE) SaveConfigFlag |= SAVECONFIG_OTHER;
				mask &= ~(CTLMASK_SETCONFIGSAVE|CTLMASK_SETCONFIG_OTHER);

				// Redraw the ctl?
				if (!(mask & CTLMASK_NOREDRAW))
				{
//					if (data >= (GUICTLDATA *)&UserGui[0] && data < (GUICTLDATA *)&UserGui[CTLID_COUNT])
//						drawGuiCtl(0, mask, 0);
//					else
					if (!data->UpdateFunc || data->UpdateFunc(ctl))
						GuiCtlUpdate(GuiApp, 0, ctl, 0, 0);
				}
			}
		}
	}
}

static const char MainHelpStr[] = "The 12 \7function keys \1select the \2first 12 styles \1under whichever category is shown. While holding the \7Ctrl \1key, the 12 f keys select the \2next 12 styles. \1While \
holding the \7Shift \1key, the 12 f keys select the \2first 12 categories. \1While holding the \7Ctrl and Shift \1keys, the 12 f keys select the \2next 12 categories.\n \1The number \7keys 0 \
to 9 \1let you directly \2enter a tempo. \1Press the Enter key when done. The \7up and down \1arrows \2raise/lower \1the tempo. Holding the Ctrl key makes the arrows move faster. Alternately, you \
can hold the \7Shift key, and tap the Space bar 4 times, \1at the desired speed for the tempo.\n\
Click the \7Left mouse \1button on the one octave piano to play a \2major chord. \1Click the \7Right mouse \1to play a \2minor chord. \7Middle mouse \1plays a \2suspended 4th. \1While holding the \
\7Ctrl \1key, the 3 mouse buttons play a \2major 7, minor 7, and dominant 7, \1respectively. While holding the \7Shift \1key, the buttons \2play 9th chords. \1While holding \
the \7Windows \1key, the left and middle buttons play \2augmented and diminished \1chords. Click in the \7empty space \1between \
the black piano keys, or press Esc, \2to mute the chord. \1Tip: You can simultaneously hold Shift and Ctrl for a 7th with 9th.\n\
Pressing the \7+ (plus) \1key \2transposes up. \7- (minus) \1key \2transposes down.\n\
The \7End \1key plays a \2fill. \1Pressing \7Ctrl + End \1plays a fill, then \2advances to the next variation. \1Holding\
 the Ctrl key while pressing number keys 1 to 4 select the verse, chorus, bridge, or intro variation.\n\
Pressing the \7Space bar \2starts/stops \1play. Ctrl + Space keys skip the count-off.\n\
Pressing \7Tab \1 jumps to the \2 Setup \1 screen. Holding the \7 Shift \1 key while pressing \7Tab \1 moves between the other \
screens.\n\7Alt + R \1turns reverb \2on/off \1.";

#ifndef NO_QWERTY_PIANO
static const unsigned char Qwerty[] = {'A','W','S','E','D','F','T','G','Y','H','U','J'};
#endif

static void showMainHelp(void)
{
	register const char **	ptr;

	ptr = (const char **)GuiBuffer;
	*ptr = MainHelpStr;
	GuiHelpShow(GuiApp, MainWin, 0);
}





/******************** handle_keypress() ********************
 * Handles user keyboard input in Styles/Patches/Chord-chart
 * screens.
 */

static void handle_keypress(register GUIMSG * msg)
{
	register uint32_t keycode;

	keycode = msg->Key.Code;

	// ======================= Alphabetic key
	if ((keycode >= 'A' && keycode <= 'Z') || (keycode >= 'a' && keycode <= 'z'))
	{
		keycode &= 0x5F;

		if (AltMask & msg->Key.Flags)
		{
			switch (keycode)
			{
				// Alt + A = autostart on/off
				case 'A':
					TempFlags ^= TEMPFLAG_AUTOSTART;
					keycode = CTLMASK_AUTOSTART;
					goto gui;
				// Alt + H = help screen
				case 'H':
					showMainHelp();
					break;
				// Alt + R = reverb on/off
				case 'R':
					TempFlags ^= APPFLAG3_NOREVERB;
					keycode = CTLMASK_REVERBMUTE;
					goto gui;
			}
		}

#ifndef NO_QWERTY_PIANO
		// The computer keyboard functions as a piano keyboard in "1 finger" chord mode
		else if (AppFlags2 & APPFLAG2_QWERTYKEYS)
		{
			register unsigned char note;

			note = keycode;
			for (keycode=0; keycode < 12; keycode++)
			{
				if (note == Qwerty[keycode])
				{
					keycode += 24;
					if (keycode < 28) keycode += 12;
					// Ctrl key selects minor
					keycode = eventChordTrigger((unsigned char)keycode, (ControlMask & msg->Key.Flags) ? 1 : 0, GUITHREADID);
					goto gui;
				}
			}
		}
#endif

		// If the patch or song list control is being shown, keycode changes the patch/song by matching its first letter
		else if (ShownCtlsMask & CTLMASK_PATCHLIST)
		{
			keycode = findSoloInstrument(keycode);
			goto gui;
		}
//		else if (ShownCtlsMask & CTLMASK_SONGSHEETLIST)
//		{
//			keycode = findSongsheet(keycode);
//			goto gui;
//		}
	}

	// If a number, then use this to set the tempo or variation
	else if (keycode >= '0' && keycode <= '9')
	{
		register unsigned char	old, new;

		new = keycode - '1';

		// If CTL'ed, then select variation
		if (ControlMask & msg->Key.Flags)
		{
			old = getCurrVariationNum(VARIATION_USER);
			keycode = selectStyleVariation(new, GUITHREADID);
		}

		// If Shift'ed, then select pad
		else if (ShiftMask & msg->Key.Flags)
		{
			old = getPadPgmNum();
			keycode = changePadInstrument(new | GUITHREADID);
			if (keycode && old != new) goto gui;
		}
		else
			doTempoNumpad(keycode - '0');
	}

	// If a function key, then use this to set the style
	else if (keycode >= XK_F1 && keycode <= XK_F12)
	{
		keycode -= XK_F1;
		if (ControlMask & msg->Key.Flags)
		{
			keycode += 12;
			if (AltMask & msg->Key.Flags) keycode += 12;
		}

		// If SHIFT'ed, then select the category
		if (ShiftMask & msg->Key.Flags)
			keycode = selectStyleCategory((unsigned char)keycode);
		else
			keycode = selectStyle((unsigned char)keycode, GUITHREADID);
gui:	if (keycode) drawGuiCtl(0, keycode, 0);
	}

	else switch (keycode)
	{
		case XK_End:
		{
			if (ShiftMask & msg->Key.Flags)
			{
				keycode = playFillAndAdvance(GUITHREADID);
				goto gui;
			}
			PlayFlags |= PLAYFLAG_STYLEJUMP;
			break;
		}

		// ======= UP arrow key =======
		case XK_Up:
		// ======= DOWN arrow key =======
		case XK_Down:
		{
			if (lockTempo(GUITHREADID) == GUITHREADID)
			{
				register int32_t	bpm;

				// inc/dec tempo by 1 or 10
				bpm = ((ControlMask & msg->Key.Flags) ? 10 : 1);
				if (keycode != XK_Up) bpm = -bpm;

				keycode = set_bpm(0);
				bpm += keycode;
				if (bpm < 20) bpm = 20;
				if (bpm > 255) bpm = 255;
				if (bpm != keycode)
				{
					set_bpm((unsigned char)bpm);
					unlockTempo(GUITHREADID);
					drawGuiCtl(0, CTLMASK_TEMPO, 0);
					break;
				}
			}
			unlockTempo(GUITHREADID);
			break;
		}

		// ======= ESC =======
		case XK_Escape:
		{
			// Tell Beat play thread stop playing bass/gtr/pad...
			if (ShiftMask & msg->Key.Flags)
			{
				// ... until user reenables accomp
				keycode = mute_robots(ACCOMP_TOGGLE|APPFLAG3_NOPAD|APPFLAG3_NOBASS|APPFLAG3_NOGTR, GUITHREADID);
			}
			// ... until user plays next chord
			else
				keycode = mute_playing_chord(GUITHREADID);
			goto gui;
		}

		// ======= SPACE =======
		case XK_space:
		{
			if (ShiftMask & msg->Key.Flags)
				ctl_set_tapTempo(findCtlById(CTLID_TAPTEMPO));
			// Play beat with/without count-off, or stop play
			else
				play_beat(((ControlMask & msg->Key.Flags) ? 1 : 0), GUITHREADID);
			break;
		}
	}
}




static GUIFUNCS MainGuiFuncs = {dummy_drawing,
handle_panel_mouse,
handle_keypress,
0};


static void attach_menu(void)
{
	MainWin->Menu = 0;
	if (!(WindowFlags & WINFLAGS_MENUHIDE))
	{
		MainWin->Menu = &Menu;
		GuiWinState(GuiApp, MainWin, GUISTATE_MENU);
	}
}

/********************* showMainScreen() ***********************
 * Shows/operates the main screen.
 */

void showMainScreen(void)
{
	// Unselect Setup menu
	if (!Menu.Select) Menu.Select++;

	{
	register struct GUIPANEL *	panelList;

	MainWin->Ctls = 0;
	if ((panelList = PanelList))
	{
		register unsigned char	count;

		count = Menu.Select;
		while (--count) panelList = panelList->Next;
		MainWin->Ctls = &panelList->Ctls[0];
	}
	}

	GuiFuncs = &MainGuiFuncs;
	MainWin->Flags = GUIWIN_BACKGND_KEY|GUIWIN_WANT_KEYUP|GUIWIN_WANT_MOUSEUP|GUIWIN_ALL_MOUSEBTN|GUIWIN_BACKGND_CLICK;
	MainWin->LowerPresetBtns = 0;
	clearMainWindow();
	updateStyleCategory();

	attach_menu();

	// Update all ctl values and set ShownCtlsMask
	update_shown_mask();
}




/****************** update_shown_mask() ******************
 * Called by main thread to update the values of all GUI
 * ctls, and set "ShownCtlsMask" to indicate which ctls
 * are currently displayed. Does not do actual drawing.
 */

void update_shown_mask(void)
{
	register GUICTL *			ctl;
	register GUICTLDATA2 *	data;

	lockGui(GUITHREADID);

	UpdateSigMask = 0;
	TempoCtl = ClockCtl = 0;

	// Always respond to beat play thread signaling stop, XRUN, and command note browse
	ShownCtlsMask = CTLMASK_BEATSTOP|CTLMASK_XRUN|CTLMASK_CMDNOTEBROWSE;

	if ((ctl = MainWin->Ctls))
	{
		GUICTL *		tap;

		tap = 0;
		do
		{
			while (ctl->Type)
			{
				if ((data = (GUICTLDATA2 *)ctl->Ptr))
				{
					if (data >= &UserGui[0] && data < &UserGui[CTLID_COUNT])
					{
						register uint32_t		mask;

						mask = (data->PlayerNeeded <= PLAYER_SOLO && !DevAssigns[data->PlayerNeeded]) ? CTLTYPE_HIDE : 0;
						GuiCtlShow(GuiApp, MainWin, ctl, mask);
						if (!mask)
						{
							if (data->TypeId < 32) ShownCtlsMask |= (1 << data->TypeId);

							if (data->Funcs.UpdateFunc && data->Funcs.UpdateFunc(ctl)) GuiCtlUpdate(GuiApp, 0, ctl, 0, 0);
						}
					}

					if (data->TypeId == CTLID_TEMPO) TempoCtl = ctl;
					if (data->TypeId == CTLID_TAPTEMPO) tap = ctl;
					if (data->TypeId == CTLID_CLOCK) ClockCtl = ctl;

					// 4 variation buttons processed together. 2 for SongSheet grid
					if (ctl->Label == VariationStr) ctl += 3;
#ifndef NO_SONGSHEET_SUPPORT
					else if (ctl->ListDraw == draw_songsheets) ctl++;
#endif
				}

				ctl++;
			}
		} while ((ctl = ctl->Next));

		if (!TempoCtl) TempoCtl = tap;
	}

	unlockGui(GUITHREADID);
}




/******************* handle_menu *******************
 * Handles a mouse click on the menubar.
 */

static void handle_menu(register unsigned char select)
{
	if (select != Menu.Select)
	{
		if (!(select &= 0x7f))
		{
			Menu.Select = select;
			showSetupScreen();
		}
		else
		{
			register struct GUIPANEL *	panelList;

			if ((panelList = PanelList))
			{
				register unsigned char	count;

				count = select;
				while (--count)
				{
					if (!(panelList = panelList->Next))
					{
						// Clicked on xrun
#ifndef NO_ALSA_AUDIO_SUPPORT
						if (xrun_count(0)) doPickOutDev(&SoundDev[DEVNUM_AUDIOOUT], showMainScreen);
#endif
						goto out;
					}
				}

				Menu.Select = select;
				showMainScreen();
			}
		}
	}
out:
	return;
}






/********************* calcMinBox() ********************
 * Calculates the width required to display the longest
 * style name.
 */

static uint32_t cmpStyleNameLen(const char * name, uint32_t width)
{
	register char *	name2;
	register int32_t	tempwidth;
	register char		chr;

	name2 = (char *)name;
	while ((chr = *name2) && chr != '_') ++name2;
	*name2 = 0;
again:
	tempwidth = GuiTextWidth(MainWin, name);
	if (tempwidth > width) width = tempwidth;
	if (chr)
	{
		*name2++ = '_';
		name = name2;
		chr = 0;
		goto again;
	}

	return width;
}

void calcMinBox(register unsigned char allFlag)
{
	register uint32_t			boxWidth;

	if (allFlag)
	{
		register void *			catptr;
		register uint32_t			catboxWidth;

		boxWidth = catboxWidth = MostStyles = 0;
		catptr = 0;
		while ((catptr = getStyleCategory(catptr)))
		{
			register void *		styleptr;

			GuiTextSetSmall(MainWin);
			catboxWidth = cmpStyleNameLen(getStyleCategoryName(catptr), catboxWidth);

			if ((styleptr = getFirstStyle(catptr)))
			{
				register unsigned char	mostStyles;

				mostStyles = 0;
				do
				{
					catboxWidth = cmpStyleNameLen(getStyleName(styleptr), catboxWidth);
				} while ((styleptr = getStyle(styleptr)));

				GuiTextSetLarge(MainWin);
				styleptr = getFirstStyle(catptr);
				do
				{
					mostStyles++;

					boxWidth = cmpStyleNameLen(getStyleName(styleptr), boxWidth);
				} while ((styleptr = getStyle(styleptr)));
				if (MostStyles < mostStyles) MostStyles = mostStyles;
			}
		}

		CatMinBoxWidth = catboxWidth + (GuiApp->GuiFont.CharWidth / 2);
		StylesMinBoxWidth = boxWidth + (GuiApp->GuiFont.CharWidth / 2);

		BoxHeight = (GuiApp->GuiFont.Height * 2) + 2;
	}

	{
	register void *			pgmPtr;
	register uint32_t			boxWidth;

	GuiTextSetLarge(MainWin);

	pgmPtr = 0;
	boxWidth = 0;
	while ((pgmPtr = getNextInstrument(pgmPtr, PLAYER_SOLO)))
		boxWidth = cmpStyleNameLen(getInstrumentName(pgmPtr), boxWidth);

	PatchMinBoxWidth = boxWidth + (GuiApp->GuiFont.CharWidth / 2);
	}
}





/********************* handleClientMsg() ********************
 * Called by GUI thread to process an XWindows ClientMessage
 * (a signal sent from another thread).
 */

static void updateAllCtls(register uint32_t);

static void handleClientMsg(register GUIMSG * msg)
{
	if (msg->Signal.Cmd > SIGNALMAIN_LOADMSG_BASE)
	{
		WhatToLoadFlagCopy = (unsigned char)msg->Signal.Cmd;
		clearLoadScreen();
	}
	else switch (msg->Signal.Cmd)
	{
		// startDrumNote() reporting an error
		default:
		{
			if (msg->Signal.Cmd >= SIGNALMAIN_NOTE_ERR_FIRST && msg->Signal.Cmd <= SIGNALMAIN_NOTE_ERR_LAST)
			{
				char	buffer[32];

				// Can't use GUIAPP'S TEMPBUF for GuiErrShow msg
				sprintf(buffer, "Drum note %ld not assigned", msg->Signal.Cmd);
				if (GuiErrShow(GuiApp, buffer, GUIBTN_ESC_SHOW|GUIBTN_TIMEOUT_SHOW|GUIBTN_IGNORE_SHOW|GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT) != GUIBTN_OK)
					ignoreErrors();
			}
			break;
		}

		// Another thread changed a param that requires a screen update
		case SIGNALMAIN_REDRAW:
		{
			// lock
			lockGui(GUITHREADID);

			// Update/redraw ctls
			updateAllCtls(UpdateSigMask & ShownCtlsMask);
			break;
		}

		// ALSA audio thread error
		case SIGNALMAIN_AUDIO_ERR:
		{
#if !defined(NO_ALSA_AUDIO_SUPPORT)
			register unsigned char	errNum;

			errNum = setAudioDevErrNum(0, (unsigned char)-1);
			setAudioDevErrNum(0, 0);

			show_audio_error(errNum);
#endif
			break;
		}

		case SIGNALMAIN_LOAD:
		{
			break;
		}

#ifndef NO_MIDI_IN_SUPPORT
		// User finished assigning a midi msg to a cmd, via his controller
		case SIGNALMAIN_MIDIIN:
		{
			endNotePoint();
			break;
		}

		// Incoming midi clock needs to be displayed
		case SIGNALMAIN_MIDICLOCK:
		{
			updateMidiClock();
			break;
		}

		// Incoming midi msg needs to be displayed
		case SIGNALMAIN_MIDIVIEW:
		{
			updateMidiView();
			break;
		}

		case SIGNALMAIN_MIDIVIEW2:
			updateMidiViewStop();
			break;

		case SIGNALMAIN_CMDSWITCHERR:
		{
			show_msgbox("You can't assign an action to the note that switches command mode on/off.");
//			break;
		}
#endif
	}
}





/*************** updateAllCtls() ***************
 * Called by main thread to update all gui ctls
 * indicated by "mask".
 */

static void updateAllCtls(register uint32_t mask)
{
	register GUICTL *			ctl;
	register GUICTLDATA2 *	data;

	UpdateSigMask = 0;

	// Unlock (caller has locked)
	unlockGui(GUITHREADID);

	// Check for BEATSTOP first
	// NOTE: This is called by main thread _after_  the
	// Beat Play thread finishes play in response to PLAYFLAG_STOP
	// set. In this regard, it's for handshaking/cleanup at the
	// end of play (ie after the accomp has played the "ending variation"
	// and cleanly stopped)
	if (mask & CTLMASK_BEATSTOP)
	{
		// Select Intro
		selectStyleVariation(3, GUITHREADID);

		if (ClockCtl) GuiCtlUpdate(GuiApp, 0, ClockCtl, 0, 0);
	}

#if !defined(NO_ALSA_AUDIO_SUPPORT)
	// If audio thread is signaling underrun, mark our indicator for redraw
	if (mask & CTLMASK_XRUN) draw_xrun(0);
#endif

	if (mask & CTLMASK_CMDNOTEBROWSE)
	{
		register const char *	ptr;

		CmdModeMenu.Select = 0xff;
		ptr = CmdModeMenuLabels[0];
		if (ptr[0])
		{
			if (MainWin->Menu != &CmdModeMenu)
			{
				MainWin->Menu = &CmdModeMenu;
				GuiWinState(GuiApp, MainWin, GUISTATE_MENU);
			}
		}
		else
			attach_menu();
		redraw_menu();
	}

	mask &= ~(CTLMASK_BEATSTOP|CTLMASK_XRUN|CTLMASK_CMDNOTEBROWSE);

	if (mask & CTLMASK_SONGSHEET)
	{
		clearMainWindow();
		update_shown_mask();
		mask &= ~CTLMASK_SONGSHEET;
	}

	if (mask && (ctl = MainWin->Ctls))
	{
		do
		{
			while (ctl->Type)
			{
				if ((data = ctl->Ptr) >= &UserGui[0] && data < &UserGui[CTLID_COUNT] && data->TypeId < 32)
				{
					register uint32_t			match;

					match = 1 << data->TypeId;
					if (match & mask)
					{
						if (data->Funcs.UpdateFunc && data->Funcs.UpdateFunc(ctl)) GuiCtlUpdate(GuiApp, 0, ctl, 0, 0);

						mask &= ~match;
						if (!mask) goto out;
					}
				}

				ctl++;
			}
		} while ((ctl = ctl->Next));
	}
out:
	return;
}





/***************** setAudioDevErrNum() ******************
 * Called by audio thread to alert main thread about some
 * fatal audio error (other than xrun). Also called by
 * main thread to retrieve the (non-zero) error number.
 *
 * errNum =		The error number. If -1, then this queries
 *					the number. If 0 (and handle=0), then clears it.
 *
 * signalHandle =	Some IPC handle gotten from GuiWinSignal().
 *				0 = main thread is the caller.
 *
 * RETURNS: The error number.
 */

#if !defined(NO_ALSA_AUDIO_SUPPORT)

static unsigned char AudioDevErrNum;

unsigned char setAudioDevErrNum(register void * signalHandle, register unsigned char errNum)
{
	if (errNum == (unsigned char)-1) errNum = AudioDevErrNum;

	if ((AudioDevErrNum = errNum) && signalHandle)
//		GuiWinSignal(GuiApp, signalHandle, SIGNALMAIN_AUDIO_ERR);
		GuiWinSignal(GuiApp, 0, SIGNALMAIN_AUDIO_ERR);
	return errNum;
}

#endif




#if 0
static const char TypeStrs[] = "DRUMVOL\0\
BASSVOL\0\
GTRVOL\0\
PADVOL\0\
PATCHVOL\0\
DRUMLIST\0\
BASSLIST\0\
GTRLIST\0\
BACKINGPAD\0\
PATCHLIST\0\
DRUMMUTE\0\
BASSMUTE\0\
GTRMUTE\0\
ACCOMPMUTE\0\
MASTERVOL\0\
CHORDS\0\
TEMPO\0\
TAPTEMPO\0\
VARIATION\0\
VOLBOOST\0\
PLAY\0\
STYLES\0\
TRANSPOSE\0\
REVERBMUTE\0\
SONGSHEET\0\
AUTOSTART\0\
REVERBVOL\0\
CYCLEVARIATION\0\
SONGSHEETLIST\0\
CMDNOTEBROWSE\0\
XRUN\0\
BEATSTOP\0"};

static void print_types(register uint32_t mask)
{
	register const char * ptr;

	ptr = TypeStrs;
	while (ptr[0] && mask)
	{
		if (mask & 1) printf(" %s", ptr);
		mask >>= 1;
		while (*ptr++);
	}
	printf("\r\n");
}
#endif





/******************* drawGuiCtl() *********************
 * Sets which GUI ctls the main thread should redraw,
 * and signals the main thread. Called by all threads,
 * including the main thread.
 *
 * signalHandle =	Some IPC handle gotten from GuiWinSignal().
 *				0 = main thread is the caller.
 =	A bitmask of what ctls to redraw. See the
 *				CTLID_ symbols in Main.h.
 */

uint32_t drawGuiCtl(register void * signalHandle, register uint32_t mask, register unsigned char threadId)
{
	register uint32_t		orig;
#if 0
	if (!signalHandle) printf("Main needs");
	else printf("%p needs", signalHandle);
	print_types(mask);
#endif

	// Lock thread access. This may be called by multiple threads, including ones
	// with real-time priority
	if (!lockGui(threadId))
		return mask;

	orig = UpdateSigMask;

	// Discard update sigs for ctls not shown in current win
	mask &= ShownCtlsMask;

	// Any ctls to signal? Are all the desired ctls already signaled and waiting
	// to be processed by the main thread?
	if (mask && (orig & mask) != mask)
	{
		// Some ctl(s) need to be signaled/updated.
		//
		// If caller is main thread (signalHandle=0), process all sigs
		// now, and clear them
		if (!signalHandle)
		{
			updateAllCtls(orig | mask);
			// updateAllCtls() unlocks
		}

		// Not main thread. If UpdateSigMask = 0, then main thread hasn't yet
		// been informed of pending sigs. Send a proprietary ClientMessage to
		// inform main. Otherwise just set the sigs and nothing more
		else
		{
			UpdateSigMask |= mask;

			// Unlock
			unlockGui(threadId);

			if (!orig)
			{
#if 0
				printf("Signaling");
				print_types(UpdateSigMask);
#endif
//				GuiWinSignal(GuiApp, signalHandle, SIGNALMAIN_REDRAW); // plugin would need to pass signalHandle
				GuiWinSignal(GuiApp, 0, SIGNALMAIN_REDRAW);
			}
#if 0
			else
			{
				printf("Adding");
				print_types(UpdateSigMask);
			}
#endif
		}
	}

	// Unlock
	else unlockGui(threadId);

	return 0;
}





/******************** signal_stop() ********************
 * Called by Play beat thread to signal beat has ended.
 */

void signal_stop(register void * signalHandle, register uint32_t mask)
{
	// Clear any countoff
	CountOff = 0;

	if (GuiFuncs == &MainGuiFuncs)
		drawGuiCtl(signalHandle, CTLMASK_BEATSTOP | CTLMASK_PLAY | CTLMASK_VARIATION | CTLMASK_CLOCK | mask, 0);
	else
		GuiWinSignal(GuiApp, 0, SIGNALMAIN_MIDIVIEW2);
}





int isMainScreen(void)
{
	return (GuiFuncs == &MainGuiFuncs);
}




void setShownCtls(register uint32_t mask)
{
	ShownCtlsMask = mask;
}




/***************** gui_piano_click() ***************
 * Handles the user clicking the mouse on the graphical
 * "chords" piano.
 */

static void gui_piano_click(register GUIMSG * msg, register GUICTL * ctl)
{
	register unsigned short	x;
	register unsigned char	boxNum;

	{
	register unsigned short	ypos;

	ypos = (msg->Mouse.Y - ctl->Y) / (BoxHeight * 2);
	x = msg->Mouse.X / (ctl->Width / 7);
	boxNum = 0;
	if (!(ypos & 0x01))
	{
		if (msg->Mouse.X > (x * (ctl->Width / 7)) + ((ctl->Width / 7) / 2))
			boxNum = PitchesUp[x];
		else
			boxNum = -PitchesDn[x];
		if (!boxNum) goto mute;
	}
	}

	if ((boxNum += PitchesW[x]))
	{
		if (!msg->Mouse.Direction)
		{
			register unsigned char	chord;

			msg->Mouse.ButtonNum = (msg->Mouse.ButtonNum & 0x03) - 1;
			switch (msg->Mouse.Flags)
			{
				case ControlMask:
					chord = 3;
					break;
				case ShiftMask:
					chord = 6;
					break;
				case (ShiftMask|ControlMask):
					chord = 9;
					break;
				case 0x40:
					chord = 12;
					break;
				case (ControlMask|0x40):
					chord = 15;
					break;
				case (ShiftMask|0x40):
					chord = 18;
					break;
				default:
					chord = 0;
			}

			if (!BeatInPlay) boxNum |= 0x80;	// |0x80 to force a retrigger if same chord as prev

			if (eventChordTrigger(boxNum, ChordMap[msg->Mouse.ButtonNum + chord], GUITHREADID)) goto updchord;
		}
		else
			clear_note();
	}
	else
	{
mute:
		// Mute bass/gtr accomp until user plays another chord
		if (mute_playing_chord(GUITHREADID))
updchord:
			drawGuiCtl(0, CTLMASK_CHORDS, 0);
	}
}





/******************** do_msg_loop() *********************
 * Does a window message loop, calling the current GUIFUNCS
 * callbacks when needed.
 */

void do_msg_loop(void)
{
	register GUIMSG *		msg;

	GuiLoop = GUIBTN_RETRY;

	if ((msg = GuiWinGetTimedMsg(GuiApp, VariationBlink * 500)))
	{
		switch (msg->Type)
		{
			case GUI_MENU_SELECT:
			{
#ifndef NO_MIDI_IN_SUPPORT
				if (MainWin->Menu == &CmdModeMenu)
				{
					endCmdMode();
					attach_menu();
					redraw_menu();
				}
				else
#endif
menu_done:		handle_menu(msg->MenuNum);
				break;
			}

			case GUI_WINDOW_SIZE:
			{
//				mainWindowResize();
				if (GuiFuncs->ResizeFunc) GuiFuncs->ResizeFunc();
				break;
			}

			case GUI_WINDOW_DRAW:
			{
				GuiFuncs->DrawFunc();

#if !defined(NO_ALSA_AUDIO_SUPPORT)
				// Check if we need to flash our underrun indicator
				if (GuiFuncs == &MainGuiFuncs) draw_xrun(1);
#endif
				break;
			}

			case GUI_MOUSE_CLICK:
			{
				// Just in case one of our GUICTLDATA funcs needs it, let's store it
				MainWin->Ptr = msg;

				// One of the user-defined panels (from window layout)?
				if (GuiFuncs == &MainGuiFuncs)
				{
					register GUICTL *		ctl;

#if !defined(NO_WINDCTL_MODEL)
					if ((AppFlags & APPFLAG_WINDCTL) && ChordTrigger > 127 && (ChordTrigger & 0x0f) == msg->Mouse.ButtonNum)
					{
						eventChordTrigger(0, msg->Mouse.Direction ^ 1, GUITHREADID);
						break;
					}
#endif
					// Is this a click on the gui piano?
					if ((ctl = msg->Mouse.SelectedCtl) && ctl->Type == CTLTYPE_AREA && ctl->AreaDraw == draw_chords)
						gui_piano_click(msg, ctl);
					else
					{
						register uint32_t		button;

						// If this is the left mouse btn clicking upon some ctl, then we
						// don't allow it to be siphoned off by a user command assign.
						// Otherwise, we check if this mouse button assigned a cmd
						if (((button = msg->Mouse.ButtonNum - 1) || !ctl) && doMouseorKeyCmd(button, msg->Mouse.Direction ^ 1))
							break;

						// If not a left-click on a ctl, nor assigned a cmd, then mute any
						// lingering notes held over after play is stopped
						if (!BeatInPlay && !ctl)
						{
							if ((button = clearChord(2|GUITHREADID))) drawGuiCtl(0, button, 0);
							break;
						}
					}
				}
				else if (MainWin->Flags & GUIWIN_ALL_MOUSEBTN) goto allbtns;

				// If we got here, then let the current GUIFUNCS mouse handler process a left-click down.
				// For user-defined panels, that is handle_panel_mouse()
				if (msg->Mouse.ButtonNum == 1 && !msg->Mouse.Direction)
allbtns:			GuiFuncs->MouseFunc(msg);

				break;
			}

			case GUI_KEY_PRESS:
			{
				register uint32_t		keycode;

				keycode = msg->Key.Code;
				if (keycode < XK_Shift_L)
				{
					if (GuiFuncs == &MainGuiFuncs)
					{
						if (keycode == XK_Tab)
						{
							// Shift-Tab rotates the menubar selection
							if (msg->Key.Direction
#ifndef NO_MIDI_IN_SUPPORT
								|| MainWin->Menu == &CmdModeMenu
#endif
								) break;

							if (ShiftMask & msg->Key.Flags)
							{
								msg->MenuNum = Menu.Select + 1;
								if (msg->MenuNum >= (Menu.LabelCnt & 0x0F)) msg->MenuNum = 1;
							}
							else
								msg->MenuNum = 0;

							goto menu_done;
						}

						// Look up and do cmd
						if (keycode >= 'a' && keycode <= 'z') keycode &= 0x5F;
						if (doMouseorKeyCmd(keycode | (msg->Key.Flags << 16), msg->Key.Direction ^ 1)) break;

						// The MENU key shows/hides the menu bar
						if (keycode == XK_Menu && !msg->Key.Direction)
						{
							WindowFlags ^= WINFLAGS_MENUHIDE;
							attach_menu();
							clearMainWindow();
							break;
						}
					}

					if (!msg->Key.Direction) GuiFuncs->KeyFunc(msg);
				}
				break;
			}

			case GUI_WINDOW_CLOSE:
			{
#ifndef NO_MIDI_IN_SUPPORT
				midiviewDone();
#endif
				GuiLoop = GUIBTN_QUIT;
				break;
			}

			case GUI_HELP:
			{
				if (GuiFuncs != &MainGuiFuncs)
					showSetupHelp();
				break;
			}

			case GUI_SIGNAL:
				handleClientMsg(msg);
				break;

			case GUI_TIMEOUT:
				if (blink_variation()) drawGuiCtl(0, CTLMASK_VARIATION, 0);
		}
	}
}

#if 0
static const char GuiApiNames[] = "Init\0Done\0\
\1DrawMsg\0\
\1SetColor\0\
\1SetLarge\0\
\1SetSmall\0\
\1Width\0\
\1DrawSmall\0\
\1Draw\0\
\2SetWidth\0\
\2ArrowsValue\0\
\2ArrowsInit\0\
\2AbsScale\0\
\2Scale\0\
\2SetSelect\0\
\2GetHeight\0\
\2GetTypeHeight\0\
\2Show\0\
\2Update\0\
\2DrawCheck\0\
\2Draw\0\
MenuCalcLabelWidths\0\
\3SetHeading\0\
\3Update\0\
\3AreaUpdate\0\
\3Rect\0\
\3Signal\0\
ExeGetPath\0\
\3State\0\
\3Resize\0\
\3GetBound\0\
\3GetMsg\0\
HelpShow\0\
ErrShow\0\
\3Modal\0\
AppState\0\
ListItemWidth\0\
ListDrawItem\0\
ListMouse\0\
ListKey\0\
NumpadInit\0\
NumpadEnd\0";
#endif


/************************ main() *************************
 * Main entry point.
 */

int main(int argc, char *argv[])
{
	// Init gui subsystem. Get default window and font sizes
	if (!(GuiApp = (GUIAPPHANDLE)GuiInit(0)))
		printf("Can't start XWindows\r\n");
	else
	{
		GuiBuffer = (char *)GuiAppState(GuiApp, GUISTATE_GET_TEMPBUF, 0);
		MainWin = &GuiApp->MainWin;
		GuiApp->PresetBtnStrs = PresetStrs;

		// Init global variables (mostly for non-0 values that need to be set dynamically)
		TempBuffer = 0;
		setErrorStr(0);
		PatchMinBoxWidth = StylesMinBoxWidth = CatMinBoxWidth = 1;
		initAudioVars();
		initMidiInVars();
		CmdModeMenuLabels[0] = getCmdKeyStr();

		// Get temp mem to load files. load_text_file() will
		// test/enlarge as needed, or report err
		alloc_temp_buffer(2048);

		set_bpm(100);
		ctl_update_tempo(0);

		// Load the window layout file and create the window
		if (create_main_window() && GuiLoop != GUIBTN_QUIT)
		{
			WhatToLoadFlagCopy = LOADFLAG_INPROGRESS;
			doLoadScreen();

			// Load user prefs
			loadConfig();
#ifdef GIGGING_DRUMS
			AppFlags |= APPFLAG_1FINGER;
			AppFlags2 = (AppFlags2 & ~APPFLAG2_CLOCKMASK) | (APPFLAG2_TIMED_ERR|APPFLAG2_CLOCK_FASTEST);
			AppFlags3 &= ~(APPFLAG3_NOAUTOSTART);
			AppFlags3 |= APPFLAG3_AUTOSTARTARM;
#endif
			updChansInUse();

			// Locate the ALSA rawmidi/alsa devices the enduser saved to config file. We
			// just verify their status, and get alsa card, dev, sub-dev numbers. We don't
			// open them yet. We wait until after we load the sampled instruments. Why?
			// The sample rate of the device affects which sanples we load, Also,
			// we don't want our internal audio engine started until after the
			// instruments are ready to play. On the other hand, whether we load any
			// sampled instruments depends upon whether the enduser disabled the internal
			// engine. So we first gather all of the info about the device used with our
			// internal engine, up to point where we're ready to open that device
			loadDeviceConfig();
			if (GuiLoop != GUIBTN_QUIT)
			{
#if !defined(NO_JACK_SUPPORT) || !defined(NO_ALSA_AUDIO_SUPPORT)
				// If there wasn't a saved device config file, we assume the user is running this
				// app for the first time. Let's prompt him for device choice
				if (!WavesLoadedFlag)
				{
					SoundDev[DEVNUM_AUDIOOUT].DevFlags |= DEVTYPE_AUDIO;
					SaveConfigFlag = SAVECONFIG_DEVICES;
#ifndef NO_JACK_SUPPORT
					if (GuiErrShow(GuiApp, "BackupBand needs to know which audio output to use for its sound. Do you want to \
use JACK? (Answer NO if you want to see a list of devices from which you can pick one.)", GUIBTN_NO_SHOW|GUIBTN_YES_SHOW|GUIBTN_YES_DEFAULT) == GUIBTN_NO)
					goto choose;

#else
					GuiLoop = GuiErrShow(GuiApp, "BackupBand needs to know which audio output to use for its sound. I'll show you \
a list of devices from which you can pick one.", GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
					headingCopyTo(getPlayDestDevName(DEVNUM_AUDIOOUT), 0);
					headingShow(GUICOLOR_GOLD);

					userSampleRate(setSampleRateFactor(0xff));
					doPickSoundDevDlg(&SoundDev[DEVNUM_AUDIOOUT], doFinishPickDev);
					do
					{
						do_msg_loop();
						if (GuiLoop == GUIBTN_QUIT) goto bail_out;
					} while (GuiLoop != GUIBTN_ERROR);
#endif
				}

				// If using jack, we need to get the sample rate before loading waveforms
#ifndef NO_JACK_SUPPORT
				{
				register const char *	err;

				WavesLoadedFlag = 0;
jack_good:	if (!SoundDev[DEVNUM_AUDIOOUT].DevHash && (err = open_libjack()))
				{
					sprintf((char *)TempBuffer, "%s. Would you like to choose another audio output?", err);
					if (GuiErrShow(GuiApp, (char *)TempBuffer, GUIBTN_NO_SHOW|GUIBTN_YES_SHOW|GUIBTN_YES_DEFAULT) == GUIBTN_YES)
					{
choose:				headingCopyTo(getPlayDestDevName(DEVNUM_AUDIOOUT), 0);
						headingShow(GUICOLOR_GOLD);

						userSampleRate(setSampleRateFactor(0xff));
						doPickSoundDevDlg(&SoundDev[DEVNUM_AUDIOOUT], doFinishPickDev);
						do
						{
							do_msg_loop();
							if (GuiLoop == GUIBTN_QUIT) goto bail_out;
						} while (GuiLoop != GUIBTN_ERROR);
						goto jack_good;
					}

					// Turn off "Internal synth"
					SoundDev[DEVNUM_AUDIOOUT].DevFlags &= ~DEVFLAG_DEVTYPE_MASK;
				}
				}
#endif
#endif
				// Load BackupBand style/instrument/songsheet/command-assigns data
				loadDataSets(LOADFLAG_INSTRUMENTS|LOADFLAG_SONGS|LOADFLAG_STYLES|LOADFLAG_ASSIGNS);
				if (GuiLoop == GUIBTN_QUIT) goto bail_out2;

				// Open the user-chosen (config file's) MIDI/audio devices. This
				// includes JACK and/or software connects via ALSA's Sequencer API.
				// NOTE: If error, a msg box is displayed
				openConfigDevs();
				if (GuiLoop != GUIBTN_QUIT)
				{
#ifdef XKEYS_PANEL
					// Open XKeys button panel
					init_button_panel();
#endif
					// Done with load buffer. Reduce it back to 2K
					free_temp_buffer();
					alloc_temp_buffer(2048);

					// Select the first style with its default tempo, Intro variation
					selectStyleCategory(0);
					selectStyle(0, GUITHREADID);
					selectStyle(0, GUITHREADID);
					selectStyleVariation(3, GUITHREADID);

					// Show panel after "Setup"
					Menu.Select = 1;
					showMainScreen();

					// Setup thread to play accompaniment
					initBeatThread();

					// Update any TIME ctl
					if (ClockCtl) ctl_update_time(ClockCtl);

					// Do GUI loop until user quits
					do
					{
						do_msg_loop();
					} while (GuiLoop != GUIBTN_QUIT);

					endBeatThread();

					// Save settings if any changed
					saveConfig();
#ifdef XKEYS_PANEL
					// Close buttons panel
					quit_button_panel();
#endif
				}
			}
bail_out2:
			freeActionAssigns();

#ifndef NO_MIDI_IN_SUPPORT
			// Close any open MIDI Handles
			closeMidiIn();
#endif
#if !defined(NO_MIDI_OUT_SUPPORT) || !defined(NO_SEQ_SUPPORT)
			{
			register uint32_t devnum;

			devnum = 4;
			do
			{
			closeMidiOut(&SoundDev[--devnum + DEVNUM_MIDIOUT1]);
			} while (devnum);
			}
#endif
			// Free audio stuff
			freeAudio(1);
		}

		// Free accompaniment stuff
		freeAccompStyles();
bail_out:
		// Free the window layouts
		free_panels();

		// Safe if already freed or never alloc
		free_temp_buffer();

		// Free GUI
		GuiDone(GuiApp, 0);
	}

	return 0;
}






// =================================================
// Config file save/load
// =================================================

// Non-zero if config settings have changed and need to be saved
unsigned char			SaveConfigFlag = 0;
static unsigned char FoundConfigPath = 0;
static const char		ConfigName[] = "Settings";
// Subdir and filename for config file
const char		ConfigDirName[] = "Prefs/";

/********************* saveConfigOther() *******************
 * Called by saveConfig() to save app-specific data to the
 * config file.
 *
 * buffer =		MAX_PATH size buffer to format data in.
 *
 * RETURN: Ptr to end of bytes formatted.
 */

static unsigned char * saveConfigFlags(register unsigned char * buffer, register const uint32_t * bits, register unsigned char flag)
{
	while (flag)
	{
		if (flag & 0x01)
		{
			*buffer++ = CONFIGKEY_FLAG;
			storeLong(*bits, buffer);
			buffer += 4;
		}

		flag >>= 1;
		bits++;
	}
	return buffer;
}

unsigned char * saveConfigOther(register unsigned char * buffer)
{
	buffer = saveConfigFlags(buffer, &AppFlagsBits[0], AppFlags);
	buffer = saveConfigFlags(buffer, &AppFlags2Bits[0], AppFlags2);
	buffer = saveConfigFlags(buffer, &AppFlags3Bits[0], AppFlags3);
	buffer = saveConfigFlags(buffer, &AppFlags4Bits[0], AppFlags4);
	if (GuiApp->ClickSpeed != 688/16)
	{
		*buffer++ = CONFIGKEY_DCLICKSPEED;
		*buffer++ = GuiApp->ClickSpeed;
	}

	buffer = saveAudioConfig(buffer);
	buffer = saveAccompConfig(buffer);
	return buffer;
}

static unsigned char findConfigFlags(register const uint32_t match, register const uint32_t * bits, register unsigned char * flag)
{
	register unsigned char	i;
	for (i=0; i<8; i++)
	{
		if (match == *bits++)
		{
			*flag |= (0x01 << i);
			return 4;
		}
	}
	return 0;
}

/********************* loadConfigOther() *******************
 * Called by loadConfig() to load app-specific data from the
 * config file.
 *
 * ptr =		Data to parse.
 * len =		# of bytes.
 *
 * RETURN: # of bytes parsed, 0 if unknown opcode, -1 if truncated.
 */

int loadConfigOther(register unsigned char * ptr, register unsigned long len)
{
	register int	read;

	if (!len)
trun:	read = -1;

	else if (ptr[0] == CONFIGKEY_FLAG)
	{
		register uint32_t val;

		if (len < 4) goto trun;
		val = getLong(&ptr[1]);
		if (findConfigFlags(val, &AppFlagsBits[0], &AppFlags) ||
			findConfigFlags(val, &AppFlags2Bits[0], &AppFlags2) ||
			findConfigFlags(val, &AppFlags3Bits[0], &AppFlags3) ||
			findConfigFlags(val, &AppFlags4Bits[0], &AppFlags4))
		{
			TempFlags = AppFlags3;
			TempFlags ^= TEMPFLAG_AUTOSTART;
			return 4;
		}

		read = 0;
	}

	else switch (ptr[0])
	{
		case CONFIGKEY_DCLICKSPEED:
		{
			GuiApp->ClickSpeed = ptr[1];
			return 1;
		}
		default:
		{
			if (!(read = loadAudioConfig(ptr, len)) &&
				!(read = loadAccompConfig(ptr, len)))
			{
			}
		}
	}

	return read;
}

/********************** load_config_file() *********************
 * Loads the binary config file into TempBuffer[].
 *
 * RETURNS: Size of data, or 0 if config file not found.
 */

uint32_t load_config_file(const char * filename)
{
	char						buffer[PATH_MAX];
	register uint32_t		len;

	// If we're reading a config file, first check if FoundConfigPath != 0. That
	// indicates that get_config_path was previously called, and successfully
	// discovered the path. In that case, FoundConfigPath=1 for Exe's dir, or 2
	// for user's Home dir. If FoundConfigPath=0, then try to open the file (for
	// reading) in the EXE's dir. If that succeeds, set FoundConfigPath to 1. If
	// that fails, try to open in the user's Home dir. If that succeeds, set
	// FoundConfigPath to 2. If that fails, set FoundConfigPath=0, and assume no
	// config file.
	if (FoundConfigPath == 2)
again:
		len = get_home_path(&buffer[0]);
	else
		len = get_exe_path(&buffer[0]);
	strcpy(&buffer[len], &ConfigDirName[0]);
	len += strlen(&buffer[len]);
	strcpy(&buffer[len], filename);
	if (!(len = load_text_file(buffer, 2|FILEFLAG_NOEXIST|FILEFLAG_NO_NUL)))
	{
		if (!FoundConfigPath)
		{
			FoundConfigPath = 0x82;
			goto again;
		}

		if (FoundConfigPath & 0x80) FoundConfigPath = 0;
	}
	else
		if (!(FoundConfigPath &= 0x7F)) FoundConfigPath++;

	return len;
}

/***************** create_config_file() ******************
 * Opens/creates the binary config file.
 *
 * RETURNS: File handle or -1 if an error.
 *
 * Note: Formats the full path name in GuiBuffer[].
 */

int create_config_file(const char * filename)
{
	register uint32_t		len, rootlen;
	register int			fh;

	// If we're writing the config file, then we should already have called
	// load_config_file() to read the file (at program start), so FoundConfigPath
	// is 1 for saving to EXE dir, 2 for user's Home, or 0 if we have yet to
	// decide. For 0, try exe dir first.
	if (FoundConfigPath == 2)
again:
		len = get_home_path(GuiBuffer);
	else
		len = get_exe_path(GuiBuffer);
	rootlen = len;
	strcpy(&GuiBuffer[len], &ConfigDirName[0]);
	len += strlen(&GuiBuffer[len]);
	strcpy(&GuiBuffer[len], filename);
retry:
	if ((fh = open(GuiBuffer, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1)
	{
		if (FoundConfigPath == 0x80)
		{
			FoundConfigPath = 0x82;
			goto again;
		}
		if (!FoundConfigPath || (FoundConfigPath & 0x80))
		{
			// Check that the dir exists
			len = rootlen - 1;
			while (GuiBuffer[len] && GuiBuffer[len] != '/') len++;
			GuiBuffer[len] = 0;
			mkdir(GuiBuffer, 0700);
			GuiBuffer[len++] = '/';
			while (GuiBuffer[len] && GuiBuffer[len] != '/') len++;
			GuiBuffer[len] = 0;
			mkdir(GuiBuffer, 0700);
			GuiBuffer[len] = '/';
			if (!FoundConfigPath)
				FoundConfigPath = 0x80;
			else
				FoundConfigPath = 2;
			goto retry;
		}
	}

	if (!(FoundConfigPath &= 0x7F) && len) FoundConfigPath++;
	return fh;
}

/*********************** saveConfig() *******************
 * Saves the config file.
 *
 * NOTE: Displays an error msg.
 */

void saveConfig(void)
{
	if (SaveConfigFlag)
	{
		// If app is ending, ask user if he wants to save settings
		if (GuiLoop == GUIBTN_QUIT)
		{
			GuiLoop = GuiErrShow(GuiApp, "Save your changed settings?", GUIBTN_ESC_SHOW|GUIBTN_NO_SHOW|GUIBTN_YES_SHOW|GUIBTN_YES_DEFAULT);
			if (GuiLoop != GUIBTN_YES) SaveConfigFlag = 0;
		}

		if (SaveConfigFlag & SAVECONFIG_OTHER)
		{
			register int				fh;

			if ((fh = create_config_file(&ConfigName[0])) == -1)
				show_msgbox(&ConfigName[0]);
			else
			{
				register unsigned char *	ptr;
				register unsigned long		len;

				// Write out keys
				ptr = saveConfigOther(TempBuffer);
				len = (ptr - &TempBuffer[0]);
				if (write(fh, &TempBuffer[0], len) != len)
				{
					format_syserr((char *)&TempBuffer[0], PATH_MAX);

					close(fh);

					// Delete file
//					unlink(GuiBuffer);
				}
				else
				{
					close(fh);

					// Clear flag indicating that settings need to be saved
					SaveConfigFlag &= ~SAVECONFIG_OTHER;
				}
			}
		}

		// Save midi/mouse/pckey cmd assigns if changed
		saveCmdAssigns();

		// Save device config
		saveDeviceConfig();
	}
}

/*********************** loadConfig() *******************
 * Loads the config file.
 *
 * NOTE: Displays an error msg.
 */

void loadConfig(void)
{
	register uint32_t				size;
	register unsigned char *	ptr;
	uint32_t							len;

	// Load the file
	if ((len = load_config_file(&ConfigName[0])))
	{
		// Transfer settings to globals. Also check file integrity
		// since it's a binary format
		size = 0;
		ptr = TempBuffer;
		while (size < len)
		{
			register int	read;

			if ((read = loadConfigOther(&ptr[size], len - size - 1)) < 0)
			{
				sprintf((char *)TempBuffer, "Truncated %s file at offset %u opcode 0x%02X\n", &ConfigName[0], size, ptr[size]);
				goto err;
			}

			if (!read)
			{
				sprintf((char *)TempBuffer, "Bad opcode 0x%02X at offset %u in %s file\n", ptr[size], size, &ConfigName[0]);
err:			show_msgbox((char *)TempBuffer);
				break;
			}

			size++;
			if (len < size + read) break;
			size += read;
		}
	}
}













// =================================================
// List enumeration/display
// =================================================

/******************** doPickItemDlg() ******************
 * Shows/operates the "Pick Item..." screen. Allows
 * user to pick an Item from a list.
 *
 * Caller must set List.ColumnWidth and List.NumOfItems,
 * usually by calling GuiListItemWidth().
 */

void doPickItemDlg(register GUILISTFUNC * drawfunc, register GUIFUNCS * funcs)
{
	showPickItemList(drawfunc);

	MainWin->Ctls = &ListGuiCtls[0];
	MainWin->Menu = 0;
	MainWin->LowerPresetBtns = GUIBTN_OK_SHOW|GUIBTN_CANCEL_SHOW|GUIBTN_CENTER;
	MainWin->Flags |= GUIWIN_ESC_KEY;
	GuiFuncs = funcs;

	clearMainWindow();
	GuiCtlSetSelect(GuiApp, 0, 0, (GUICTL *)GUIBTN_OK);
}

/**************** showPickItemList() ******************
 * Initializes the AREA LIST.
 *
 * Caller must call append_list_ctl() to attach this
 * AREA to its other ctls.
 */

GUICTL * showPickItemList(register GUILISTFUNC * drawfunc)
{
	// Initially no Item selected
	GuiListItemWidth(GuiApp, MainWin, &List, 0);

	ListGuiCtls[0].ListDraw = drawfunc;

	MainWin->Flags = GUIWIN_SHIFTTAB_KEY|GUIWIN_TAB_KEY|GUIWIN_ENTER_KEY|GUIWIN_LIST_KEY|GUIWIN_ESC_KEY;

	// The main screen's ctls are temporarily hidden while the list is operated
	ShownCtlsMask = 0;

	ListGuiCtls[0].Flags.Local = CTLFLAG_AREA_LIST|CTLFLAG_AREA_EVENHEIGHT;
	return &ListGuiCtls[0];
}

GUILISTFUNC * setListDrawFunc(register GUILISTFUNC * drawfunc)
{
	if (drawfunc) ListGuiCtls[0].ListDraw = drawfunc;
	drawfunc = ListGuiCtls[0].ListDraw;
	return drawfunc;
}

/*************** positionPickItemGui() ****************
 * Sets initial position/scaling of GUI ctls based
 * upon font size.
 */

static void positionPickItemGui(void)
{
	GuiCtlScale(GuiApp, MainWin, &ListGuiCtls[0], -1);
}

void append_list_ctl(register GUICTL * ctl)
{
	goto start;
	do
	{
		ctl = ctl->Next;
start:
		while (ctl->Type)
		{
			if (ctl == &ListGuiCtls[0]) return;
			ctl++;
		}
	} while (ctl->Next);

	ctl->Next = &ListGuiCtls[0];
}







void dummy_keypress(register GUIMSG * msg)
{
}

void dummy_drawing(void)
{
}










// ============================== Number Pad Screen ============================

static GUINUMRETFUNC	RetFunc;

/******************* numpad_mouse() ******************
 * Called by GUI thread to process user mouse input in
 * a number pad screen.
 */

static void numpad_mouse(register GUIMSG * msg)
{
	register GUICTL *		ctl;

	if ((ctl = msg->Mouse.SelectedCtl) && (ctl->Flags.Global & CTLGLOBAL_PRESET))
	{
		RetFunc((ctl->PresetId == GUIBTN_NUMPAD ? (GUINUMPAD *)&msg->Numpad : 0));
		GuiNumpadEnd(GuiApp, 0);
	}
}

static GUIFUNCS NumGuiFuncs = {dummy_drawing,
numpad_mouse,
dummy_keypress,
0};

void doNumeric(register GUINUMRETFUNC retFunc, const char * heading, uint32_t initValue)
{
	GuiNumpadInit(GuiApp, MainWin, heading, initValue, 3);
	MainWin->Flags = GUIWIN_ENTER_KEY|GUIWIN_ESC_KEY|GUIWIN_TAB_KEY;
	MainWin->Ctls = 0;
	MainWin->Menu = 0;
	MainWin->LowerPresetBtns = 0;
	setShownCtls(0);
	GuiFuncs = &NumGuiFuncs;
	clearMainWindow();
	RetFunc = retFunc;
}
