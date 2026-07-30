// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM selection menu, options, episode etc.
//	Sliders and icons. Kinda widget stuff.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: m_menu.c,v 1.7 1997/02/03 22:45:10 b1 Exp $";

#include <stdio.h>
#include <ctype.h>

#include "m_swap.h"
#include "doomdef.h"
#include "dstrings.h"

#include "d_main.h"

#include "i_system.h"
#include "i_video.h"
#include "z_zone.h"
#include "v_video.h"
#include "w_wad.h"

#include "r_local.h"


#include "hu_stuff.h"

#include "g_game.h"

#include "m_argv.h"

#include "s_sound.h"

#include "doomstat.h"
#include "u_mapinfo.h"	// UMAPINFO "episode" menu entries

extern int key_buddy_stay;	// '-' defers to this buddy bind (G_Responder) instead of screen-size

// Data.
#include "sounds.h"

#include "m_menu.h"
#include "m_controls.h"		// Options -> Controls (in-game key bindings)



extern patch_t*		hu_font[HU_FONTSIZE];
extern boolean		message_dontfuckwithme;

extern boolean		chat_on;		// in heads-up code

void M_SpriteShadow(int choice);
void M_Automap(int choice);

//
// defaulted values
//
int			mouseSensitivity;       // has default

// Show messages has default, 0 = off, 1 = on
int			showMessages;
	

// Blocky mode, has default, 0 = high, 1 = normal
int			detailLevel;		
int			screenblocks;		// has default (fixed; screen-size slider removed)

// -1 = no quicksave slot picked!
int			quickSaveSlot;          

 // 1 = message to be printed
int			messageToPrint;
// ...and here is the message string!
char*			messageString;		

// message x & y
int			messx;			
int			messy;
int			messageLastMenuActive;

// timed message = no input from user
boolean			messageNeedsInput;     

void    (*messageRoutine)(int response);

#define SAVESTRINGSIZE 	24

char gammamsg[5][26] =
{
    GAMMALVL0,
    GAMMALVL1,
    GAMMALVL2,
    GAMMALVL3,
    GAMMALVL4
};

// we are going to be entering a savegame string
int			saveStringEnter;              
int             	saveSlot;	// which slot to save in
int			saveCharIndex;	// which char we're editing
// old save description before edit
char			saveOldString[SAVESTRINGSIZE];  

boolean			inhelpscreens;
boolean			menuactive;

#define SKULLXOFF		-32
#define LINEHEIGHT		16

extern boolean		sendpause;
char			savegamestrings[10][SAVESTRINGSIZE];

char	endstring[160];


//
// MENU TYPEDEFS
//
typedef struct
{
    // 0 = no cursor here, 1 = ok, 2 = arrows ok
    short	status;
    
    char	name[10];
    
    // choice = menu item #.
    // if status = 2,
    //   choice=0:leftarrow,1:rightarrow
    void	(*routine)(int choice);
    
    // hotkey in menu
    char	alphaKey;			
} menuitem_t;



typedef struct menu_s
{
    short		numitems;	// # of menu items
    struct menu_s*	prevMenu;	// previous menu
    menuitem_t*		menuitems;	// menu items
    void		(*routine)();	// draw routine
    short		x;
    short		y;		// x,y of menu
    short		lastOn;		// last item user was on in menu
} menu_t;

short		itemOn;			// menu item skull is on
short		skullAnimCounter;	// skull animation counter
short		whichSkull;		// which skull to draw

// graphic name of skulls
// warning: initializer-string for array of chars is too long
char    skullName[2][/*8*/9] = {"M_SKULL1","M_SKULL2"};

// current menudef
menu_t*	currentMenu;                          

//
// PROTOTYPES
//
void M_NewGame(int choice);
void M_Episode(int choice);
void M_ChooseSkill(int choice);
void M_LoadGame(int choice);
void M_SaveGame(int choice);
void M_Options(int choice);
void M_EndGame(int choice);
void M_ReadThis(int choice);
void M_ReadThis2(int choice);
void M_QuitDOOM(int choice);
void M_QuitResponse(int ch);	// fwd: the quit prompt accepts ANY key (responder special-case)

void M_ChangeMessages(int choice);
void M_ChangeSensitivity(int choice);
void M_SfxVol(int choice);
void M_MusicVol(int choice);
void M_StartGame(int choice);
void M_Sound(int choice);

void M_Video(int choice);
void M_VideoRes(int choice);
void M_VideoFullscreen(int choice);
void M_VideoAspect(int choice);
void M_VideoFilter(int choice);
void M_VideoVSync(int choice);
void M_VideoScale(int choice);
void M_VideoBackend(int choice);
void M_StatusBarStyle(int choice);
void M_LightDither(int choice);
void M_DrawVideo(void);
void M_WriteTextBig(int x, int y, char *string, int sc);

void M_FinishReadThis(int choice);
void M_LoadSelect(int choice);
void M_SaveSelect(int choice);
void M_ReadSaveStrings(void);
void M_QuickSave(void);
void M_QuickLoad(void);

void M_DrawMainMenu(void);
void M_DrawReadThis1(void);
void M_DrawReadThis2(void);
void M_DrawNewGame(void);
void M_DrawEpisode(void);
void M_DrawOptions(void);
void M_DrawSound(void);
void M_DrawLoad(void);
void M_DrawSave(void);

void M_DrawSaveLoadBorder(int x,int y);
void M_SetupNextMenu(menu_t *menudef);
void M_DrawThermo(int x,int y,int thermWidth,int thermDot);
void M_DrawEmptyCell(menu_t *menu,int item);
void M_DrawSelCell(menu_t *menu,int item);
void M_WriteText(int x, int y, char *string);
int  M_StringWidth(char *string);
int  M_StringHeight(char *string);
void M_StartControlPanel(void);
void M_StartMessage(char *string,void *routine,boolean input);
void M_StopMessage(void);
void M_ClearMenus (void);




//
// DOOM MENU
//
enum
{
    newgame = 0,
    options,
    loadgame,
    savegame,
    readthis,
    quitdoom,
    main_end
} main_e;

// BuddyDoom: pick your co-op companion (Hexen-style select screen).  Lives on the
// Options menu now (see options_e/OptionsMenu), not the main menu.
void M_Buddy (int choice);

menuitem_t MainMenu[]=
{
    {1,"M_NGAME",M_NewGame,'n'},
    {1,"M_OPTION",M_Options,'o'},
    {1,"M_LOADG",M_LoadGame,'l'},
    {1,"M_SAVEG",M_SaveGame,'s'},
    // Another hickup with Special edition.
    {1,"M_RDTHIS",M_ReadThis,'r'},
    {1,"M_QUITG",M_QuitDOOM,'q'}
};

menu_t  MainDef =
{
    main_end,
    NULL,
    MainMenu,
    M_DrawMainMenu,
    97,64,
    0
};


//
// EPISODE SELECT
//
enum
{
    ep1,
    ep2,
    ep3,
    ep4,
    ep_end
} episodes_e;

menuitem_t EpisodeMenu[]=
{
    {1,"M_EPI1", M_Episode,'k'},
    {1,"M_EPI2", M_Episode,'t'},
    {1,"M_EPI3", M_Episode,'i'},
    {1,"M_EPI4", M_Episode,'t'}
};

menu_t  EpiDef =
{
    ep_end,		// # of menu items
    &MainDef,		// previous menu
    EpisodeMenu,	// menuitem_t ->
    M_DrawEpisode,	// drawing routine ->
    48,63,              // x,y
    ep1			// lastOn
};

// UMAPINFO can replace the hardcoded episode menu with its own entries (the
// "episode" key).  When u_episodes_defined, EpiDef is repointed at this array by
// M_UMapinfoBuildEpisodes() (called at the end of M_Init).  Max 8 per the spec.
#define UMAPINFO_MAX_EPISODES 8
menuitem_t UMapinfoEpisodeMenu[UMAPINFO_MAX_EPISODES];

//
// NEW GAME
//
enum
{
    killthings,
    toorough,
    hurtme,
    violence,
    nightmare,
    newg_end
} newgame_e;

menuitem_t NewGameMenu[]=
{
    {1,"M_JKILL",	M_ChooseSkill, 'i'},
    {1,"M_ROUGH",	M_ChooseSkill, 'h'},
    {1,"M_HURT",	M_ChooseSkill, 'h'},
    {1,"M_ULTRA",	M_ChooseSkill, 'u'},
    {1,"M_NMARE",	M_ChooseSkill, 'n'}
};

menu_t  NewDef =
{
    newg_end,		// # of menu items
    &EpiDef,		// previous menu
    NewGameMenu,	// menuitem_t ->
    M_DrawNewGame,	// drawing routine ->
    48,63,              // x,y
    hurtme		// lastOn
};



//
// OPTIONS MENU
//
// ("End Game" was removed -- quitting to the title is Quit Game / a new game from the
//  main menu; it only ever sat here in vanilla to abandon a co-op session.)
enum
{
    featuresopt,	// Features submenu (Messages / Footclip / Weapon Autoswitch)
    mousesens,
    option_empty2,
    soundvol,
    vidoption,
    controls,
    buddyopt,		// BuddyDoom: pick your co-op companion (was on the main menu)
    automapopt,		// Automap style: vanilla / boom / textured
    crosshairopt,	// Crosshair type + colour submenu
    opt_end
} options_e;

void M_ControlsMenu(int choice);
void M_Features(int choice);		// Options -> Features submenu
void M_ToggleFootclip(int choice);	// Heretic liquid foot-clip on/off
void M_ToggleAutoswitch(int choice);	// auto-raise a picked-up weapon on/off
void M_RunSpeed(int choice);		// player run-speed 100..300%
void M_WeaponPower(int choice);		// player weapon-damage 50..500%
void M_DrawFeatures(void);
void M_Crosshair(int choice);		// Options -> Crosshair submenu
void M_CrosshairType(int choice);	// cycle Off / Cross / Dot / Big
void M_CrosshairColor(int choice);	// cycle Green / White / Red / Yellow / Blue
void M_DrawCrosshair(void);

menuitem_t OptionsMenu[]=
{
    // All text-drawn at the small (hu_font) size by M_DrawOptions -- empty names
    // so M_Drawer doesn't blit the big graphic lumps.
    // (Vanilla's "Graphic Detail" High/Low was removed -- low-detail mode is a
    //  dead no-op in this hi-res renderer; see the old M_ChangeDetail.)
    {1,"",	M_Features,'f'},	// enters the Features submenu (Messages moved inside)
    {2,"",	M_ChangeSensitivity,'m'},
    {-1,"",0},
    {1,"",	M_Sound,'s'},
    {1,"",	M_Video,'v'},
    {1,"",	M_ControlsMenu,'c'},
    {1,"",	M_Buddy,'b'},
    {2,"",	M_Automap,'a'},		// left/right cycles vanilla / boom / textured
    {1,"",	M_Crosshair,'x'}	// enters the Crosshair submenu
};

menu_t  OptionsDef =
{
    opt_end,
    &MainDef,
    OptionsMenu,
    M_DrawOptions,
    60,37,
    0
};

//
// Video MENU (resolution / fullscreen) -- text drawn, no graphic lumps.
//
enum
{
    vid_res,
    vid_fullscreen,
    vid_widescreen, // Aspect
    vid_filter,     // Scaling Filter
    vid_vsync,      // VSync
    vid_scale,      // Presentation scale mode (Letterbox vs Integer)
    vid_backend,    // GPU Backend
    vid_statusbar,  // Status bar style
    vid_dither,     // Light dithering
    vid_shadow,     // Sprite shadows
    vid_fullcolor,  // Truecolor 3D view (automap moved to Options -> Automap)
    vid_end
} video_e;

menuitem_t VideoMenu[]=
{
    {2,"",	M_VideoRes,'r'},	// left/right changes resolution
    {1,"",	M_VideoFullscreen,'f'},
    {2,"",	M_VideoAspect,'w'},	// left/right cycles 4:3 / 16:9 / 16:10
    {2,"",	M_VideoFilter,'t'},	// Nearest vs Linear
    {1,"",	M_VideoVSync,'y'},	// VSync toggle
    {2,"",	M_VideoScale,'s'},	// Letterbox vs Integer
    {2,"",	M_VideoBackend,'b'},	// GPU Backend
    {2,"",	M_StatusBarStyle,'h'},	// Vanilla / Small / Alt HUD
    {2,"",	M_LightDither,'d'},	// soften light banding
    {1,"",	M_SpriteShadow,'o'}	// soft sprite shadows
};

menu_t  VideoDef =
{
    vid_end,
    &OptionsDef,
    VideoMenu,
    M_DrawVideo,
    60,37,
    0
};

//
// Crosshair MENU (Options -> Crosshair): type + colour, text-drawn.
//
enum
{
    xh_type,		// Off / Cross / Dot / Big Cross
    xh_color,		// Green / White / Red / Yellow / Blue
    xh_end
} crosshair_e;

menuitem_t CrosshairMenu[]=
{
    {2,"",	M_CrosshairType,'t'},	// left/right cycles the shape
    {2,"",	M_CrosshairColor,'c'}	// left/right cycles the colour
};

menu_t  CrosshairDef =
{
    xh_end,
    &OptionsDef,
    CrosshairMenu,
    M_DrawCrosshair,
    60,37,
    0
};

//
// Features MENU (Options -> Features): gameplay toggles, text-drawn.
//
enum
{
    feat_messages,	// in-game pickup/status messages
    feat_footclip,	// (H) sink actors into liquid
    feat_autoswitch,	// auto-raise a newly picked-up weapon
    feat_runspeed,	// player run-speed percentage
    feat_weaponpower,	// player weapon-damage percentage
    feat_end
} features_e;

menuitem_t FeaturesMenu[]=
{
    {1,"",	M_ChangeMessages,'m'},		// select toggles On/Off
    {1,"",	M_ToggleFootclip,'f'},
    {1,"",	M_ToggleAutoswitch,'w'},
    {2,"",	M_RunSpeed,'r'},		// left/right cycles 100..300%
    {2,"",	M_WeaponPower,'p'}		// left/right cycles 50..500%
};

menu_t  FeaturesDef =
{
    feat_end,
    &OptionsDef,
    FeaturesMenu,
    M_DrawFeatures,
    60,37,
    0
};

//
// Read This! MENU 1 & 2
//
enum
{
    rdthsempty1,
    read1_end
} read_e;

menuitem_t ReadMenu1[] =
{
    {1,"",M_ReadThis2,0}
};

menu_t  ReadDef1 =
{
    read1_end,
    &MainDef,
    ReadMenu1,
    M_DrawReadThis1,
    280,185,
    0
};

enum
{
    rdthsempty2,
    read2_end
} read_e2;

menuitem_t ReadMenu2[]=
{
    {1,"",M_FinishReadThis,0}
};

menu_t  ReadDef2 =
{
    read2_end,
    &ReadDef1,
    ReadMenu2,
    M_DrawReadThis2,
    330,175,
    0
};

//
// SOUND VOLUME MENU
//
enum
{
    sfx_vol,
    sfx_empty1,
    music_vol,
    sfx_empty2,
    sound_end
} sound_e;

menuitem_t SoundMenu[]=
{
    {2,"M_SFXVOL",M_SfxVol,'s'},
    {-1,"",0},
    {2,"M_MUSVOL",M_MusicVol,'m'},
    {-1,"",0}
};

menu_t  SoundDef =
{
    sound_end,
    &OptionsDef,
    SoundMenu,
    M_DrawSound,
    80,64,
    0
};

//
// LOAD GAME MENU
//
enum
{
    load1,
    load2,
    load3,
    load4,
    load5,
    load6,
    load_end
} load_e;

menuitem_t LoadMenu[]=
{
    {1,"", M_LoadSelect,'1'},
    {1,"", M_LoadSelect,'2'},
    {1,"", M_LoadSelect,'3'},
    {1,"", M_LoadSelect,'4'},
    {1,"", M_LoadSelect,'5'},
    {1,"", M_LoadSelect,'6'}
};

menu_t  LoadDef =
{
    load_end,
    &MainDef,
    LoadMenu,
    M_DrawLoad,
    80,54,
    0
};

//
// SAVE GAME MENU
//
menuitem_t SaveMenu[]=
{
    {1,"", M_SaveSelect,'1'},
    {1,"", M_SaveSelect,'2'},
    {1,"", M_SaveSelect,'3'},
    {1,"", M_SaveSelect,'4'},
    {1,"", M_SaveSelect,'5'},
    {1,"", M_SaveSelect,'6'}
};

menu_t  SaveDef =
{
    load_end,
    &MainDef,
    SaveMenu,
    M_DrawSave,
    80,54,
    0
};


//
// M_ReadSaveStrings
//  read the strings from the savegame files
//
void M_ReadSaveStrings(void)
{
    FILE           *handle;
    int             count;
    int             i;
    char    name[256];
	
    for (i = 0;i < load_end;i++)
    {
	sprintf(name,SAVEGAMENAME"%d.dsg",i);

	handle = fopen (name, "r");
	if (handle == NULL)
	{
	    strcpy(&savegamestrings[i][0],EMPTYSTRING);
	    LoadMenu[i].status = 0;
	    continue;
	}
	count = fread (&savegamestrings[i], 1, SAVESTRINGSIZE, handle);
	fclose (handle);
	LoadMenu[i].status = 1;
    }
}


// ---------------------------------------------------------------------------
// Heretic / art-less IWAD menu fallback.
//
// Heretic (and other non-DOOM IWADs) ship none of DOOM's menu graphics
// (M_NGAME, M_SKILL, M_LOADG, ...).  Where a menu patch lump is missing we
// render a readable text label instead of fatally caching a nonexistent lump.
// Decorative lumps (slot borders, volume thermo) map to "" and are simply
// skipped.  In an ordinary DOOM IWAD every lump exists, so nothing changes.
// ---------------------------------------------------------------------------
static const char* M_MenuFallbackText (const char* lump)
{
    static const struct { const char* l; const char* t; } tbl[] = {
	{ "M_NGAME","New Game" },  { "M_OPTION","Options" }, { "M_LOADG","Load Game" },
	{ "M_SAVEG","Save Game" }, { "M_RDTHIS","Read This!" }, { "M_QUITG","Quit Game" },
	{ "M_NEWG","New Game" },    { "M_SKILL","Choose Skill Level:" },
	{ "M_EPISOD","Which Episode?" }, { "M_OPTTTL","Options" }, { "M_SVOL","Sound Volume" },
	// Heretic skill + episode names (shown when the DOOM graphics are absent).
	{ "M_JKILL","Thou needeth a wet-nurse" }, { "M_ROUGH","Yellowbellies-r-us" },
	{ "M_HURT","Bringest them oneth" },        { "M_ULTRA","Thou art a smite-meister" },
	{ "M_NMARE","Black plague possesses thee" },
	{ "M_EPI1","City of the Damned" }, { "M_EPI2","Hell's Maw" },
	{ "M_EPI3","The Dome of D'Sparil" }, { "M_EPI4","The Ossuary" },
	// Decorative -> skip.
	{ "M_LSLEFT","" }, { "M_LSCNTR","" }, { "M_LSRGHT","" },
	{ "M_THERML","" }, { "M_THERMM","" }, { "M_THERMR","" }, { "M_THERMO","" },
	{ "M_CELL1","" },  { "M_CELL2","" },
    };
    int i;
    if (!lump || !lump[0]) return "";
    for (i = 0; i < (int)(sizeof tbl / sizeof tbl[0]); i++)
	if (!strcasecmp (lump, tbl[i].l)) return tbl[i].t;
    return lump;			// unknown -> at least show the lump name
}

// Draw a menu graphic, or its text fallback (possibly empty) if the lump is absent.
static void M_DrawMenuGraphic (int x, int y, const char* lump)
{
    if (W_CheckNumForName (lump) >= 0)
	V_DrawPatchDirect (x, y, 0, W_CacheLumpName (lump, PU_CACHE));
    else
    {
	const char* t = M_MenuFallbackText (lump);
	if (t[0]) M_WriteText (x, y, t);
    }
}

//
// M_LoadGame & Cie.
//
void M_DrawLoad(void)
{
    int             i;
	
    M_DrawMenuGraphic (72,28,"M_LOADG");
    for (i = 0;i < load_end; i++)
    {
	M_DrawSaveLoadBorder(LoadDef.x,LoadDef.y+LINEHEIGHT*i);
	M_WriteText(LoadDef.x,LoadDef.y+LINEHEIGHT*i,savegamestrings[i]);
    }
}



//
// Draw border for the savegame description
//
void M_DrawSaveLoadBorder(int x,int y)
{
    int             i;
	
    M_DrawMenuGraphic (x-8,y+7,"M_LSLEFT");
	
    for (i = 0;i < 24;i++)
    {
	M_DrawMenuGraphic (x,y+7,"M_LSCNTR");
	x += 8;
    }

    M_DrawMenuGraphic (x,y+7,"M_LSRGHT");
}



//
// User wants to load this game
//
void M_LoadSelect(int choice)
{
    char    name[256];
	
    sprintf(name,SAVEGAMENAME"%d.dsg",choice);		// always ID0/ -- no c:\doomdata
    G_LoadGame (name);
    M_ClearMenus ();
}

//
// Selected from DOOM menu
//
void M_LoadGame (int choice)
{
    if (netgame)
    {
	M_StartMessage(LOADNET,NULL,false);
	return;
    }
	
    M_SetupNextMenu(&LoadDef);
    M_ReadSaveStrings();
}


//
//  M_SaveGame & Cie.
//
void M_DrawSave(void)
{
    int             i;
	
    M_DrawMenuGraphic (72,28,"M_SAVEG");
    for (i = 0;i < load_end; i++)
    {
	M_DrawSaveLoadBorder(LoadDef.x,LoadDef.y+LINEHEIGHT*i);
	M_WriteText(LoadDef.x,LoadDef.y+LINEHEIGHT*i,savegamestrings[i]);
    }
	
    if (saveStringEnter)
    {
	i = M_StringWidth(savegamestrings[saveSlot]);
	M_WriteText(LoadDef.x + i,LoadDef.y+LINEHEIGHT*saveSlot,"_");
    }
}

//
// M_Responder calls this when user is finished
//
void M_DoSave(int slot)
{
    G_SaveGame (slot,savegamestrings[slot]);
    M_ClearMenus ();

    // PICK QUICKSAVE SLOT YET?
    if (quickSaveSlot == -2)
	quickSaveSlot = slot;
}

//
// User wants to save. Start string input for M_Responder
//
void M_SaveSelect(int choice)
{
    // we are going to be intercepting all chars
    saveStringEnter = 1;
    
    saveSlot = choice;
    strcpy(saveOldString,savegamestrings[choice]);
    if (!strcmp(savegamestrings[choice],EMPTYSTRING))
	savegamestrings[choice][0] = 0;
    saveCharIndex = strlen(savegamestrings[choice]);
}

//
// Selected from DOOM menu
//
void M_SaveGame (int choice)
{
    if (!usergame)
    {
	M_StartMessage(SAVEDEAD,NULL,false);
	return;
    }
	
    if (gamestate != GS_LEVEL)
	return;
	
    M_SetupNextMenu(&SaveDef);
    M_ReadSaveStrings();
}



//
//      M_QuickSave
//
char    tempstring[80];

void M_QuickSaveResponse(int ch)
{
    if (ch == 'y')
    {
	M_DoSave(quickSaveSlot);
	S_StartSound(NULL,sfx_swtchx);
    }
}

void M_QuickSave(void)
{
    if (!usergame)
    {
	S_StartSound(NULL,sfx_oof);
	return;
    }

    if (gamestate != GS_LEVEL)
	return;
	
    if (quickSaveSlot < 0)
    {
	M_StartControlPanel();
	M_ReadSaveStrings();
	M_SetupNextMenu(&SaveDef);
	quickSaveSlot = -2;	// means to pick a slot now
	return;
    }
    sprintf(tempstring,QSPROMPT,savegamestrings[quickSaveSlot]);
    M_StartMessage(tempstring,M_QuickSaveResponse,true);
}



//
// M_QuickLoad
//
void M_QuickLoadResponse(int ch)
{
    if (ch == 'y')
    {
	M_LoadSelect(quickSaveSlot);
	S_StartSound(NULL,sfx_swtchx);
    }
}


void M_QuickLoad(void)
{
    if (netgame)
    {
	M_StartMessage(QLOADNET,NULL,false);
	return;
    }
	
    if (quickSaveSlot < 0)
    {
	M_StartMessage(QSAVESPOT,NULL,false);
	return;
    }
    sprintf(tempstring,QLPROMPT,savegamestrings[quickSaveSlot]);
    M_StartMessage(tempstring,M_QuickLoadResponse,true);
}




//
// Read This Menus
// Had a "quick hack to fix romero bug"
//
void M_DrawReadThis1(void)
{
    inhelpscreens = true;
    // V_DrawFullscreenLumpName handles Heretic's RAW 320x200 help pages (drawing them as
    // a patch showed an empty/garbled screen) and centres the page in widescreen.
    switch ( gamemode )
    {
      case commercial:
	V_DrawFullscreenLumpName ("HELP");
	break;
      case shareware:
      case registered:
      case retail:
	V_DrawFullscreenLumpName ("HELP1");
	break;
      default:
	break;
    }
    return;
}



//
// Read This Menus - optional second page.
//
void M_DrawReadThis2(void)
{
    inhelpscreens = true;
    switch ( gamemode )
    {
      case retail:
      case commercial:
	// This hack keeps us from having to change menus.
	V_DrawFullscreenLumpName ("CREDIT");
	break;
      case shareware:
      case registered:
	V_DrawFullscreenLumpName ("HELP2");
	break;
      default:
	break;
    }
    return;
}


//
// Change Sfx & Music volumes
//
void M_DrawSound(void)
{
    M_DrawMenuGraphic (60,38,"M_SVOL");

    M_DrawThermo(SoundDef.x,SoundDef.y+LINEHEIGHT*(sfx_vol+1),
		 16,snd_SfxVolume);

    M_DrawThermo(SoundDef.x,SoundDef.y+LINEHEIGHT*(music_vol+1),
		 16,snd_MusicVolume);
}

void M_Sound(int choice)
{
    M_SetupNextMenu(&SoundDef);
}

void M_SfxVol(int choice)
{
    switch(choice)
    {
      case 0:
	if (snd_SfxVolume)
	    snd_SfxVolume--;
	break;
      case 1:
	if (snd_SfxVolume < 15)
	    snd_SfxVolume++;
	break;
    }
	
    S_SetSfxVolume(snd_SfxVolume /* *8 */);
}

void M_MusicVol(int choice)
{
    switch(choice)
    {
      case 0:
	if (snd_MusicVolume)
	    snd_MusicVolume--;
	break;
      case 1:
	if (snd_MusicVolume < 15)
	    snd_MusicVolume++;
	break;
    }
	
    S_SetMusicVolume(snd_MusicVolume /* *8 */);
}




//
// M_DrawMainMenu
//
void M_DrawMainMenu(void)
{
    // Heretic has no "M_DOOM" logo -- use its "M_HTIC" title; guard so a wad with
    // neither doesn't crash the main menu.
    {
	const char* logo = (heretic_mode && W_CheckNumForName ("M_HTIC") >= 0) ? "M_HTIC" : "M_DOOM";
	if (W_CheckNumForName (logo) >= 0)
	    V_DrawPatchDirect (heretic_mode ? 88 : 94, 2, 0, W_CacheLumpName (logo, PU_CACHE));
    }
}




//
// M_NewGame
//
void M_DrawNewGame(void)
{
    M_DrawMenuGraphic (96,14,"M_NEWG");
    M_DrawMenuGraphic (54,38,"M_SKILL");
}

void M_NewGame(int choice)
{
    if (netgame && !demoplayback)
    {
	M_StartMessage(NEWGAME,NULL,false);
	return;
    }
	
    // UMAPINFO episodes get their own menu even in Doom 2 (which normally skips
    // straight to the skill menu).
    if ( gamemode == commercial && !(u_episodes_defined && u_num_episodes > 0) )
	M_SetupNextMenu(&NewDef);
    else
	M_SetupNextMenu(&EpiDef);
}


//
//      M_Episode
//
int     epi;

void M_DrawEpisode(void)
{
    // M_EPISOD ("Which Episode?") isn't present in every IWAD (Doom 2 has no
    // episode menu); UMAPINFO can now bring this screen up under Doom 2, so fall
    // back to a text title rather than fatally caching a missing lump.
    if (W_CheckNumForName ("M_EPISOD") >= 0)
	V_DrawPatchDirect (54,38,0,W_CacheLumpName("M_EPISOD",PU_CACHE));
    else
	M_WriteText (54, 38, "Which Episode?");

    // UMAPINFO episodes with no menu graphic are drawn as HUD-font text (the
    // generic item loop skips items whose name lump is empty).
    if (u_episodes_defined && u_num_episodes > 0)
    {
	int i;
	for (i = 0; i < EpiDef.numitems; i++)
	    if (!UMapinfoEpisodeMenu[i].name[0])
		M_WriteText (EpiDef.x, EpiDef.y + i*LINEHEIGHT,
			     u_episodes[i].name ? u_episodes[i].name : "?");
    }
}

// Start the game at the selected episode.  With UMAPINFO episodes the chosen
// entry names its own start map (which may be a MAPxx even in Doom 1); otherwise
// the classic "episode epi+1, map 1".
static void M_StartChosenEpisode (int skill)
{
    if (u_episodes_defined && u_num_episodes > 0 && epi < u_num_episodes)
	G_DeferedInitNew (skill, u_episodes[epi].episode, u_episodes[epi].map);
    else if (strife_mode)
	// (S) A new Strife game starts on map 2, not map 1 -- map01 is the town hub the
	// game only returns to later.  strife-ve M_ChooseSkill: "map = 2".
	G_DeferedInitNew (skill, 1, 2);
    else
	G_DeferedInitNew (skill, epi+1, 1);
}

void M_VerifyNightmare(int ch)
{
    if (ch != 'y')
	return;

    M_StartChosenEpisode (nightmare);
    M_ClearMenus ();
}

void M_ChooseSkill(int choice)
{
    if (choice == nightmare)
    {
	M_StartMessage(NIGHTMARE,M_VerifyNightmare,true);
	return;
    }

    M_StartChosenEpisode (choice);
    M_ClearMenus ();
}

void M_Episode(int choice)
{
    // UMAPINFO episodes replace the IWAD's set, so the shareware/registered
    // gating below (which is about the stock episodes) does not apply.
    if (u_episodes_defined && u_num_episodes > 0)
    {
	epi = choice;
	M_SetupNextMenu(&NewDef);
	return;
    }

    if ( (gamemode == shareware)
	 && choice)
    {
	M_StartMessage(SWSTRING,NULL,false);
	M_SetupNextMenu(&ReadDef1);
	return;
    }

    // Yet another hack...
    if ( (gamemode == registered)
	 && (choice > 2))
    {
      fprintf( stderr,
	       "M_Episode: 4th episode requires UltimateDOOM\n");
      choice = 0;
    }

    epi = choice;
    M_SetupNextMenu(&NewDef);
}



//
// M_Options
//
char    detailNames[2][9]	= {"M_GDHIGH","M_GDLOW"};
char	msgNames[2][9]		= {"M_MSGOFF","M_MSGON"};


void M_DrawOptions(void)
{
    int	x = OptionsDef.x;
    int	y = OptionsDef.y;

    M_DrawMenuGraphic (108,15,"M_OPTTTL");

    // All items drawn at the small hu_font size (same as the Video submenu).
    M_WriteText (x, y+LINEHEIGHT*featuresopt, "Features");

    M_WriteText (x, y+LINEHEIGHT*mousesens, "Mouse Sensitivity");
    M_DrawThermo (x, y+LINEHEIGHT*(mousesens+1), 10, mouseSensitivity);

    M_WriteText (x, y+LINEHEIGHT*soundvol, "Sound Volume");

    M_WriteText (x, y+LINEHEIGHT*vidoption, "Video");

    M_WriteText (x, y+LINEHEIGHT*controls, "Controls");

    M_WriteText (x, y+LINEHEIGHT*buddyopt, "Buddy");

    {
	extern int automap_style;
	static char* nm[3] = { "Vanilla", "Overlay", "Textured" };
	M_WriteText (x, y+LINEHEIGHT*automapopt, "Automap");
	M_WriteText (x+130, y+LINEHEIGHT*automapopt,
		     nm[(automap_style>=0 && automap_style<=2) ? automap_style : 2]);
    }

    M_WriteText (x, y+LINEHEIGHT*crosshairopt, "Crosshair");
}

//
// Options -> Controls: hand off to the SDL-overlay key-bindings screen
// (m_controls.c owns state/input; i_video.c draws it with the TTF atlas).
//
void M_ControlsMenu(int choice)
{
    choice = 0;
    M_Controls_Open ();
}


//
// Video MENU handlers (resolution / fullscreen)
//
extern int	hires;			// doomdef.c
void		V_SetRes (int scale);	// i_video.c
void		I_SetFullscreen (int on);// i_video.c
int		I_GetFullscreen (void);	// i_video.c
extern int	scale_mode;		// i_video.c
extern int	vsync;			// i_video.c
extern int	integer_scale;		// i_video.c
extern int	render_backend;		// i_video.c
extern int	aspect;			// doomdef.c (0=4:3, 1=16:9, 2=16:10)
extern int	SCREENWIDTH, SCREENHEIGHT;
void		I_ApplyVideoFilter (void);
void		I_ApplyLogicalPresentation (void);
void		I_ApplyVSync (void);

static char* M_AspectNames[3] = { "4:3", "16:9", "16:10" };
static char* M_FilterNames[2] = { "Nearest(None)", "Linear" };
static char* M_ScaleNames[2]  = { "Letterbox", "Integer" };
static char* M_BackendNames[7] = { "Auto", "Vulkan", "OpenGL", "D3D12", "D3D11", "Metal", "Software" };
extern int statusbar_style;
static char* M_StatusBarNames[3] = { "Vanilla", "Small (50%)", "Alt HUD" };
extern int dither_lighting;
extern int r_shadows;
extern int automap_style;		// am_map.c -- automap style (Options -> Automap)

void M_DrawVideo(void)
{
    char res[24];

    M_DrawMenuGraphic (108,15,"M_OPTTTL");

    sprintf (res, "%dx%d", SCREENWIDTH, SCREENHEIGHT);	// actual render size
    M_WriteText(VideoDef.x, VideoDef.y + LINEHEIGHT*vid_res, "Resolution");
    M_WriteText(VideoDef.x + 130, VideoDef.y + LINEHEIGHT*vid_res, res);

    M_WriteText(VideoDef.x, VideoDef.y + LINEHEIGHT*vid_fullscreen, "Fullscreen");
    M_WriteText(VideoDef.x + 130, VideoDef.y + LINEHEIGHT*vid_fullscreen,
		I_GetFullscreen() ? "On" : "Off");

    M_WriteText(VideoDef.x, VideoDef.y + LINEHEIGHT*vid_widescreen, "Aspect");
    M_WriteText(VideoDef.x + 130, VideoDef.y + LINEHEIGHT*vid_widescreen,
		M_AspectNames[(aspect>=0 && aspect<=2) ? aspect : 2]);

    M_WriteText(VideoDef.x, VideoDef.y + LINEHEIGHT*vid_filter, "Filter");
    M_WriteText(VideoDef.x + 130, VideoDef.y + LINEHEIGHT*vid_filter,
		M_FilterNames[(scale_mode>=0 && scale_mode<=1) ? scale_mode : 0]);

    M_WriteText(VideoDef.x, VideoDef.y + LINEHEIGHT*vid_vsync, "VSync");
    M_WriteText(VideoDef.x + 130, VideoDef.y + LINEHEIGHT*vid_vsync,
		vsync ? "On" : "Off");

    M_WriteText(VideoDef.x, VideoDef.y + LINEHEIGHT*vid_scale, "Scaling");
    M_WriteText(VideoDef.x + 130, VideoDef.y + LINEHEIGHT*vid_scale,
		M_ScaleNames[(integer_scale>=0 && integer_scale<=1) ? integer_scale : 0]);

    M_WriteText(VideoDef.x, VideoDef.y + LINEHEIGHT*vid_backend, "Backend (restart)");
    M_WriteText(VideoDef.x + 130, VideoDef.y + LINEHEIGHT*vid_backend,
		M_BackendNames[(render_backend>=0 && render_backend<=6) ? render_backend : 0]);

    M_WriteText(VideoDef.x, VideoDef.y + LINEHEIGHT*vid_statusbar, "Status Bar");
    M_WriteText(VideoDef.x + 130, VideoDef.y + LINEHEIGHT*vid_statusbar,
		M_StatusBarNames[(statusbar_style>=0 && statusbar_style<=2) ? statusbar_style : 0]);

    M_WriteText(VideoDef.x, VideoDef.y + LINEHEIGHT*vid_dither, "Light Dither");
    M_WriteText(VideoDef.x + 130, VideoDef.y + LINEHEIGHT*vid_dither,
		dither_lighting ? "On" : "Off");

    M_WriteText(VideoDef.x, VideoDef.y + LINEHEIGHT*vid_shadow, "Sprite Shadows");
    M_WriteText(VideoDef.x + 130, VideoDef.y + LINEHEIGHT*vid_shadow,
		r_shadows ? "On" : "Off");
}

// ---------------------------------------------------------------------------
// Options -> Video, rendered as the same crisp TTF overlay as Controls.
// State + input live here (next to the value tables and the M_Video* handlers);
// i_video.c draws it (I_DrawVideoOverlay) via the accessors below.
// ---------------------------------------------------------------------------
void M_SpriteShadow (int choice);		// (defined below; used in the cycle table)
void M_Automap (int choice);
void M_VideoFullcolor (int choice);
extern int truecolor;				// i_video.c -- truecolor 3D view
extern void M_SaveDefaults (void);
extern int  I_RenderBackendCount (void);	// i_video.c: available SDL render drivers (+ Auto)
extern const char* I_RenderBackendName (int i);

static int	mvid_active, mvid_sel;

static const char* const M_VideoLabels[vid_end] =
{
    "Resolution", "Fullscreen", "Aspect", "Filter", "VSync",
    "Scaling", "Backend (restart)", "Status Bar", "Light Dither",
    "Sprite Shadows", "Fullcolor"
};
static void (* const M_VideoCycle[vid_end])(int) =
{
    M_VideoRes, M_VideoFullscreen, M_VideoAspect, M_VideoFilter, M_VideoVSync,
    M_VideoScale, M_VideoBackend, M_StatusBarStyle, M_LightDither,
    M_SpriteShadow, M_VideoFullcolor
};

void	M_Video_Open (void)    { mvid_active = 1; mvid_sel = 0; }
boolean	M_Video_Active (void)  { return mvid_active; }
int	M_Video_Count (void)   { return vid_end; }
int	M_Video_Sel (void)     { return mvid_sel; }

const char* M_Video_Label (int i)
{
    return (i >= 0 && i < vid_end) ? M_VideoLabels[i] : "";
}

void M_Video_Value (int i, char* b, int n)
{
    if (!n) return;
    switch (i)
    {
      case vid_res:        snprintf (b, n, "%dx%d", SCREENWIDTH, SCREENHEIGHT); break;
      case vid_fullscreen: snprintf (b, n, "%s", I_GetFullscreen() ? "On" : "Off"); break;
      case vid_widescreen: snprintf (b, n, "%s", M_AspectNames[(aspect>=0&&aspect<=2)?aspect:2]); break;
      case vid_filter:     snprintf (b, n, "%s", M_FilterNames[(scale_mode>=0&&scale_mode<=1)?scale_mode:0]); break;
      case vid_vsync:      snprintf (b, n, "%s", vsync ? "On" : "Off"); break;
      case vid_scale:      snprintf (b, n, "%s", M_ScaleNames[(integer_scale>=0&&integer_scale<=1)?integer_scale:0]); break;
      case vid_backend:    snprintf (b, n, "%s", I_RenderBackendName (render_backend)); break;
      case vid_statusbar:  snprintf (b, n, "%s", M_StatusBarNames[(statusbar_style>=0&&statusbar_style<=2)?statusbar_style:0]); break;
      case vid_dither:     snprintf (b, n, "%s", dither_lighting ? "On" : "Off"); break;
      case vid_shadow:     snprintf (b, n, "%s", r_shadows ? "On" : "Off"); break;
      case vid_fullcolor:  snprintf (b, n, "%s", truecolor ? "On" : "Off"); break;
      default:             b[0] = 0;
    }
}

boolean M_Video_Responder (event_t* ev)
{
    if (!mvid_active)
	return false;
    if (ev->type != ev_keydown)
	return true;
    switch (ev->data1)
    {
      case KEY_UPARROW:   mvid_sel = (mvid_sel - 1 + vid_end) % vid_end; break;
      case KEY_DOWNARROW: mvid_sel = (mvid_sel + 1)          % vid_end; break;
      case KEY_LEFTARROW: M_VideoCycle[mvid_sel](0); M_SaveDefaults(); break;
      case KEY_RIGHTARROW:
      case KEY_ENTER:     M_VideoCycle[mvid_sel](1); M_SaveDefaults(); break;
      case KEY_ESCAPE:    mvid_active = 0; break;
      default: break;
    }
    return true;
}

// ===========================================================================
// BUDDY SELECT SCREEN (Hexen player-class-select style)
//
// A full-screen paletted screen (NOT an SDL/TTF overlay -- it draws a real
// sprite through V_DrawPatch, which the post-present overlay can't): tiled
// backdrop, the buddy's front-facing sprite big in the middle, its name in the
// big font and a wrapped description.  Left/Right cycles the roster
// (P_Buddy_* in p_buddydef.c, slot 0 = the built-in Marine), Enter picks it
// (persisted as `buddy_select`) and takes effect from the next level.
// ===========================================================================
int	buddy_select = 0;			// config (m_misc.c): 0 = Marine, 1..N = roster
int	buddy_color  = 0;			// config (m_misc.c): player-colour index (0 = Green)

extern int		P_Buddy_Count (void);
extern const char*	P_Buddy_Name (int);
extern const char*	P_Buddy_Desc (int);
extern int		P_Buddy_Sprite (int);
extern int		P_Buddy_Color (int);		// declared default colour, -1 = none
extern void		V_DrawPatchFlipped (int x, int y, int scrn, patch_t* patch);
extern void		V_DrawPatchTranslated (int x, int y, int scrn, patch_t* patch, const byte* trans);
extern void		V_DrawPatchScaledTranslated (int x, int y, int scrn, patch_t* patch, int sc, const byte* trans);
extern int		V_BuddyColorCount (void);
extern const char*	V_BuddyColorName (int);
extern const byte*	V_BuddyColorTable (int);
extern int		I_GetTime (void);

// Buddy stats for the select screen (mirror of buddystats_t in p_buddydef.h -- keep
// the field list identical, P_Buddy_GetStats writes through this layout).
typedef struct { int health, speed, radius, height, mass, painchance, reactiontime;
		 const char* attack; const char* special; const char* ability; } buddystats_t;
extern void		P_Buddy_GetStats (int slot, buddystats_t* out);

// Styled like the Controls/Video screens: a crisp TTF overlay (drawn in i_video.c
// by I_DrawBuddySelectOverlay) over an animated, recoloured paletted sprite (drawn
// here by M_DrawBuddy).  Two selectable rows -- Buddy and Color -- are cycled with
// Left/Right; Up/Down moves between them (mirrors the Video overlay).
enum { MBROW_BUDDY, MBROW_COLOR, MBROW_COUNT };
static int	mbuddy_active, mbuddy_sel, mbuddy_row, mbuddy_color;

// When switching to a buddy that declares a default colour in its BUDDYDEF, snap
// the colour selector to it (the player can still change it afterwards).
static void M_Buddy_SeedColor (void)
{
    int c = P_Buddy_Color (mbuddy_sel);
    if (c >= 0 && c < V_BuddyColorCount ()) mbuddy_color = c;
}

void	M_Buddy_Open (void)
{
    mbuddy_active = 1;
    mbuddy_row = MBROW_BUDDY;
    mbuddy_sel = buddy_select;
    if (mbuddy_sel < 0 || mbuddy_sel >= P_Buddy_Count()) mbuddy_sel = 0;
    mbuddy_color = buddy_color;
    if (mbuddy_color < 0 || mbuddy_color >= V_BuddyColorCount()) mbuddy_color = 0;
}
boolean	M_Buddy_Active (void) { return mbuddy_active; }

// Read-only accessors for the SDL/TTF overlay drawer (i_video.c).
int	M_Buddy_Sel   (void) { return mbuddy_sel; }
int	M_Buddy_Row   (void) { return mbuddy_row; }
int	M_Buddy_Color (void) { return mbuddy_color; }

// Options -> Buddy entry hook.
void M_Buddy (int choice)
{
    choice = 0;
    M_Buddy_Open ();
}

boolean M_Buddy_Responder (event_t* ev)
{
    int n, nc;
    if (!mbuddy_active)
	return false;
    if (ev->type != ev_keydown)
	return true;
    n  = P_Buddy_Count ();
    nc = V_BuddyColorCount ();
    switch (ev->data1)
    {
      case KEY_UPARROW:
	mbuddy_row = (mbuddy_row - 1 + MBROW_COUNT) % MBROW_COUNT;
	S_StartSound (NULL, sfx_pstop);
	break;
      case KEY_DOWNARROW:
	mbuddy_row = (mbuddy_row + 1) % MBROW_COUNT;
	S_StartSound (NULL, sfx_pstop);
	break;
      case KEY_LEFTARROW:
	if (mbuddy_row == MBROW_COLOR)	mbuddy_color = (mbuddy_color - 1 + nc) % nc;
	else				{ mbuddy_sel = (mbuddy_sel - 1 + n) % n; M_Buddy_SeedColor (); }
	S_StartSound (NULL, sfx_pstop);
	break;
      case KEY_RIGHTARROW:
	if (mbuddy_row == MBROW_COLOR)	mbuddy_color = (mbuddy_color + 1) % nc;
	else				{ mbuddy_sel = (mbuddy_sel + 1) % n; M_Buddy_SeedColor (); }
	S_StartSound (NULL, sfx_pstop);
	break;
      case KEY_ENTER:
	buddy_select = mbuddy_sel;
	buddy_color  = mbuddy_color;
	M_SaveDefaults ();
	S_StartSound (NULL, sfx_swtchx);
	mbuddy_active = 0;
	break;
      case KEY_ESCAPE:
	mbuddy_active = 0;
	break;
      default:
	break;
    }
    return true;
}

// Tile a 64x64 flat across the whole (native-res) frame -- the screen backdrop.
static void M_TileFlat (char* name)
{
    byte* src  = W_CacheLumpName (name, PU_CACHE);
    byte* dest = screens[0];
    int x, y;
    for (y = 0 ; y < SCREENHEIGHT ; y++)
    {
	for (x = 0 ; x < SCREENWIDTH/64 ; x++)
	{
	    memcpy (dest, src + ((y&63)<<6), 64);
	    dest += 64;
	}
	if (SCREENWIDTH & 63)
	{
	    memcpy (dest, src + ((y&63)<<6), SCREENWIDTH & 63);
	    dest += (SCREENWIDTH & 63);
	}
    }
}

// Dim whatever is on screen to ~50% black (through colormap row 16 of 0..31), used as
// the Buddy backdrop when the IWAD has no flat to tile (Heretic: no LAVA*, no FLOOR4_8).
// Works on any IWAD -- colormaps[] is the loaded IWAD's own COLORMAP, so Heretic dims
// correctly too.  No missing-lump error, no opaque flat.
static void M_DimScreen50 (void)
{
    byte*		d  = screens[0];
    const byte*		cm = (const byte*)colormaps + 16*256;
    int			i, n = SCREENWIDTH * SCREENHEIGHT;
    for (i = 0 ; i < n ; i++)
	d[i] = cm[d[i]];
}

// The Buddy screen backdrop: the ANIMATED lava flat behind the buddy sprite.  The
// engine's own flat animation (p_spec.c anims[]: LAVA1..LAVA4 at 8 tics a frame) only
// runs while a level is ticking, and this screen is reachable from the title, so step
// the frame off the wall clock here instead.  When the IWAD has neither a lava set nor
// FLOOR4_8 (Heretic), skip the flat entirely and just dim to 50% black.
static void M_TileLavaBackdrop (void)
{
    static const char*	lava[4] = { "LAVA1", "LAVA2", "LAVA3", "LAVA4" };
    const char*		nm = lava[(I_GetTime() / 8) & 3];

    if (W_CheckNumForName ((char*)nm) < 0)
	nm = "FLOOR4_8";
    if (W_CheckNumForName ((char*)nm) < 0)	// no flat at all (Heretic) -> just dim
    {
	M_DimScreen50 ();
	return;
    }
    M_TileFlat ((char*)nm);
}

// Word-wrap a description with the small font at BASE-coord x/y, width `wrap`.
static void M_DrawWrapped (int x, int y, int wrap, const char* text)
{
    char	word[64], line[128];
    int		li = 0, wi = 0, i = 0;
    line[0] = 0;
    for (;;)
    {
	char c = text[i++];
	boolean brk = (c == ' ' || c == 0 || c == '\n');
	if (!brk && wi < (int)sizeof(word)-1) { word[wi++] = c; continue; }
	word[wi] = 0;
	if (wi)
	{
	    char trial[192];
	    snprintf (trial, sizeof trial, "%s%s%s", line, li ? " " : "", word);
	    if (M_StringWidth (trial) > wrap && li)
	    {
		M_WriteText (x, y, line);
		y += 11;
		strcpy (line, word);
	    }
	    else strcpy (line, trial);
	    li = strlen (line);
	    wi = 0;
	}
	if (c == '\n') { M_WriteText (x, y, line); y += 11; line[0] = 0; li = 0; }
	if (c == 0) break;
    }
    if (li) M_WriteText (x, y, line);
}

// Paletted layer of the Buddy screen: the tiled backdrop and the animated,
// recoloured buddy sprite.  All text (title, name, stats, the Buddy/Color cycler
// rows, hints) is drawn on top in crisp TTF by I_DrawBuddySelectOverlay (i_video.c),
// matching the Controls/Video screens.  Called from M_Drawer before I_FinishUpdate.
void M_DrawBuddy (void)
{
    int		n   = P_Buddy_Count ();
    int		sel = mbuddy_sel;
    int		spr;

    if (sel < 0 || sel >= n) sel = 0;

    M_TileLavaBackdrop ();		// animated lava behind the buddy animation

    // Animated buddy sprite (front view), recoloured to the chosen player colour.
    // Walk cycle over frames A-D; every few seconds it plays the attack frame (E)
    // for a moment so it "sometimes shoots".
    spr = P_Buddy_Sprite (sel);
    if (spr >= 0 && spr < numsprites && sprites[spr].numframes > 0)
    {
	int		nf    = sprites[spr].numframes;
	int		t     = I_GetTime ();			// 35 Hz game tics
	int		cyc   = 105;				// ~3s shoot cycle
	int		ph    = t % cyc;
	unsigned	win   = (unsigned)(t / cyc);
	unsigned	hash  = (win * 2654435761u) >> 26;	// 0..63, per-cycle pseudo-random
	boolean		shoot = (nf > 4) && (ph < 9) && (hash < 30);	// fire ~0.25s, ~half the cycles
	int		fr    = shoot ? 4 : ((nf >= 4) ? ((t/5) & 3) : ((t/5) % nf));
	spriteframe_t*	sf    = &sprites[spr].spriteframes[fr];
	patch_t*	p     = spritepatch[sf->lump[0]]
			      ? (patch_t*) spritepatch[sf->lump[0]]	// converted GZDoom PNG sprite
			      : (patch_t*) W_CacheLumpNum (spritelumps[sf->lump[0]], PU_CACHE);
	const byte*	trans = V_BuddyColorTable (mbuddy_color);	// NULL for Green(0)=identity

	// Sprite goes in the UPPER-LEFT QUARTER at 1x, anchored on its origin (the feet),
	// which leaves the lower-left quarter free for the description panel that
	// I_DrawBuddySelectOverlay draws there.
	int	wb = SCREENWIDTH / hires;		// wide base width = the V_ coord space
	int	qw = (int)(wb * 0.44f);			// left column = everything left of the panel
	int	pw = SHORT (p->width);
	int	phh = SHORT (p->height);
	int	po = SHORT (p->leftoffset);
	int	to = SHORT (p->topoffset);
	int	fx = qw / 2;				// centred in the column...
	int	fy = BASE_HEIGHT/2 - 6;			// ...feet just above the half-way line

	// V_DrawPatchScaledTranslated does NOT clip, so an oversized sprite would write
	// outside screens[0].  Nudge the anchor until the whole patch is on screen.
	if (fy - to < 0)		 fy = to;
	if (fy - to + phh > BASE_HEIGHT) fy = BASE_HEIGHT - phh + to;
	if (fx - po < 0)		fx = po;
	if (fx - po + pw > qw)		fx = qw - pw + po;

	V_DrawPatchScaledTranslated (fx, fy, 0, p, 1, trans);
    }
}

void M_LightDither(int choice)
{
    dither_lighting = !dither_lighting;
    R_SetViewSize (screenblocks, detailLevel);	// recompute r_dither_on
    M_SaveDefaults ();
}

void M_SpriteShadow(int choice)
{
    r_shadows = !r_shadows;
    M_SaveDefaults ();
}

// Options -> Automap: cycle the automap style vanilla -> boom -> textured.
// (choice 0 = Left/decrement, else Right/increment; also used as the Options item.)
void M_Automap(int choice)
{
    if (choice == 0) automap_style = (automap_style + 2) % 3;	// Left
    else             automap_style = (automap_style + 1) % 3;	// Right / select
    M_SaveDefaults ();
}

void M_VideoFullcolor(int choice)
{
    truecolor = !truecolor;			// i_video.c -- smooth truecolor 3D view
    M_SaveDefaults ();
}

void M_StatusBarStyle(int choice)
{
    if (choice) statusbar_style = (statusbar_style + 1) % 3;
    else        statusbar_style = (statusbar_style + 2) % 3;
    R_SetViewSize (screenblocks, detailLevel);	// styles 1/2 need a full-height view
    M_SaveDefaults ();
}

//
// Options -> Features submenu: gameplay toggles.  Messages moved here from the
// Options root; footclip lives in r_things.c, weapon_autoswitch in p_inter.c.
//
void M_ToggleFootclip(int choice)
{
    extern int footclip;
    choice = 0;
    footclip = !footclip;
    M_SaveDefaults ();
}

void M_ToggleAutoswitch(int choice)
{
    extern int weapon_autoswitch;
    choice = 0;
    weapon_autoswitch = !weapon_autoswitch;
    M_SaveDefaults ();
}

void M_RunSpeed(int choice)		// 100 .. 300 in 50% steps
{
    extern int run_speed;
    run_speed += choice ? 50 : -50;
    if (run_speed > 300) run_speed = 100;
    if (run_speed < 100) run_speed = 300;
    M_SaveDefaults ();
}

void M_WeaponPower(int choice)		// 50 .. 500 in 50% steps
{
    extern int weapon_power;
    weapon_power += choice ? 50 : -50;
    if (weapon_power > 500) weapon_power = 50;
    if (weapon_power < 50)  weapon_power = 500;
    M_SaveDefaults ();
}

void M_Features(int choice)
{
    choice = 0;
    M_SetupNextMenu (&FeaturesDef);
}

void M_DrawFeatures(void)
{
    extern int footclip, weapon_autoswitch, run_speed, weapon_power;
    int  x = FeaturesDef.x, y = FeaturesDef.y;
    char buf[16];

    M_DrawMenuGraphic (108,15,"M_OPTTTL");

    M_WriteText (x,     y+LINEHEIGHT*feat_messages,   "Messages");
    M_WriteText (x+130, y+LINEHEIGHT*feat_messages,   showMessages ? "On" : "Off");
    M_WriteText (x,     y+LINEHEIGHT*feat_footclip,   "Liquid Footclip");
    M_WriteText (x+130, y+LINEHEIGHT*feat_footclip,   footclip ? "On" : "Off");
    M_WriteText (x,     y+LINEHEIGHT*feat_autoswitch, "Weapon Autoswitch");
    M_WriteText (x+130, y+LINEHEIGHT*feat_autoswitch, weapon_autoswitch ? "On" : "Off");
    M_WriteText (x,     y+LINEHEIGHT*feat_runspeed,   "Run Speed");
    snprintf (buf, sizeof buf, "%d%%", run_speed);
    M_WriteText (x+130, y+LINEHEIGHT*feat_runspeed,   buf);
    M_WriteText (x,     y+LINEHEIGHT*feat_weaponpower,"Weapon Power");
    snprintf (buf, sizeof buf, "%d%%", weapon_power);
    M_WriteText (x+130, y+LINEHEIGHT*feat_weaponpower,buf);
}

//
// Options -> Crosshair submenu (type + colour).  `crosshair` (0..3) lives in
// r_draw.c and `crosshair_color` (0..4) resolves to the nearest palette entry
// at draw time (R_DrawCrosshair), so it looks right in DOOM/Heretic/Hexen.
//
static char* M_XHairTypeNames[4]  = { "Off", "Cross", "Dot", "Big Cross" };
static char* M_XHairColorNames[5] = { "Green", "White", "Red", "Yellow", "Blue" };

void M_CrosshairType(int choice)
{
    extern int crosshair;
    if (choice) crosshair = (crosshair + 1) % 4;
    else        crosshair = (crosshair + 3) % 4;
    M_SaveDefaults ();
}

void M_CrosshairColor(int choice)
{
    extern int crosshair_color;
    if (choice) crosshair_color = (crosshair_color + 1) % 5;
    else        crosshair_color = (crosshair_color + 4) % 5;
    M_SaveDefaults ();
}

void M_Crosshair(int choice)
{
    choice = 0;
    M_SetupNextMenu (&CrosshairDef);
}

void M_DrawCrosshair(void)
{
    extern int crosshair, crosshair_color;
    int x = CrosshairDef.x, y = CrosshairDef.y;
    int t = (crosshair       >= 0 && crosshair       <= 3) ? crosshair       : 0;
    int c = (crosshair_color >= 0 && crosshair_color <= 4) ? crosshair_color : 0;

    M_DrawMenuGraphic (108,15,"M_OPTTTL");

    M_WriteText (x,     y+LINEHEIGHT*xh_type,  "Crosshair");
    M_WriteText (x+130, y+LINEHEIGHT*xh_type,  M_XHairTypeNames[t]);
    M_WriteText (x,     y+LINEHEIGHT*xh_color, "Color");
    M_WriteText (x+130, y+LINEHEIGHT*xh_color, M_XHairColorNames[c]);
}

void M_VideoRes(int choice)
{
    // choice 0 = left (smaller), 1 = right/enter (larger)
    if (choice)
    {
	if (hires < 7) V_SetRes(hires+1);
    }
    else
    {
	if (hires > 1) V_SetRes(hires-1);
    }
    M_SaveDefaults();		// persist now, not just at quit
}

void M_VideoFullscreen(int choice)
{
    I_SetFullscreen(!I_GetFullscreen());
    M_SaveDefaults();		// persist now, not just at quit
}

void M_VideoAspect(int choice)	// left/right cycles 4:3 -> 16:9 -> 16:10
{
    aspect = choice ? (aspect+1)%3 : (aspect+2)%3;
    V_SetRes(hires);		// rebuild at the new aspect (FOV + buffer + present)
    M_SaveDefaults();
}

void M_VideoFilter(int choice)
{
    scale_mode = !scale_mode;
    I_ApplyVideoFilter();	// re-apply scaling filter live
    M_SaveDefaults();
}

void M_VideoVSync(int choice)
{
    vsync = !vsync;
    I_ApplyVSync();		// re-apply VSync live
    M_SaveDefaults();
}

void M_VideoScale(int choice)
{
    integer_scale = choice ? 1 : 0;
    I_ApplyLogicalPresentation(); // re-apply logical presentation live
    M_SaveDefaults();
}

void M_VideoBackend(int choice)
{
    extern int I_RenderBackendCount (void);	// i_video.c: Auto + the real SDL drivers
    int nb = I_RenderBackendCount ();
    if (nb < 1) nb = 1;
    render_backend = choice ? (render_backend+1)%nb : (render_backend+nb-1)%nb;
    M_SaveDefaults();
}

void M_Video(int choice)
{
    M_Video_Open ();		// TTF-overlay video screen (same style as Controls)
}

void M_Options(int choice)
{
    M_SetupNextMenu(&OptionsDef);
}



//
//      Toggle messages on/off
//
void M_ChangeMessages(int choice)
{
    // warning: unused parameter `int choice'
    choice = 0;
    showMessages = 1 - showMessages;
	
    if (!showMessages)
	players[consoleplayer].message = MSGOFF;
    else
	players[consoleplayer].message = MSGON ;

    message_dontfuckwithme = true;
    M_SaveDefaults ();
}


//
// M_EndGame
//
void M_EndGameResponse(int ch)
{
    if (ch != 'y')
	return;
		
    currentMenu->lastOn = itemOn;
    M_ClearMenus ();
    D_StartTitle ();
}

void M_EndGame(int choice)
{
    choice = 0;
    if (!usergame)
    {
	S_StartSound(NULL,sfx_oof);
	return;
    }
	
    if (netgame)
    {
	M_StartMessage(NETEND,NULL,false);
	return;
    }
	
    M_StartMessage(ENDGAME,M_EndGameResponse,true);
}




//
// M_ReadThis
//
void M_ReadThis(int choice)
{
    choice = 0;
    M_SetupNextMenu(&ReadDef1);
}

void M_ReadThis2(int choice)
{
    choice = 0;
    M_SetupNextMenu(&ReadDef2);
}

void M_FinishReadThis(int choice)
{
    choice = 0;
    M_SetupNextMenu(&MainDef);
}




//
// M_QuitDOOM
//
int     quitsounds[8] =
{
    sfx_pldeth,
    sfx_dmpain,
    sfx_popain,
    sfx_slop,
    sfx_telept,
    sfx_posit1,
    sfx_posit3,
    sfx_sgtatk
};

int     quitsounds2[8] =
{
    sfx_vilact,
    sfx_getpow,
    sfx_boscub,
    sfx_slop,
    sfx_skeswg,
    sfx_kntdth,
    sfx_bspact,
    sfx_sgtatk
};



void M_QuitResponse(int ch)
{
    // Any key quits; Escape (or 'n') backs out -- so a stray keypress on the
    // "really quit?" prompt confirms instead of forcing you to find 'y'.
    if (ch == KEY_ESCAPE || ch == 'n')
	return;
    if (!netgame)
    {
	if (gamemode == commercial)
	    S_StartSound(NULL,quitsounds2[(gametic>>2)&7]);
	else
	    S_StartSound(NULL,quitsounds[(gametic>>2)&7]);
	I_WaitVBL(105);
    }
    I_Quit ();
}




void M_QuitDOOM(int choice)
{
  // (H) Heretic has no random funny quit quotes -- just the single fixed prompt
  // ("ARE YOU SURE YOU WANT TO QUIT?", crispy heretic/mn_menu.c QuitEndMsg[0]).
  { extern int heretic_mode;
    if (heretic_mode)
    {
      sprintf (endstring, "ARE YOU SURE YOU WANT TO QUIT?\n\npress any key.");
      M_StartMessage (endstring, M_QuitResponse, true);
      return;
    }
  }
  // We pick index 0 which is language sensitive,
  //  or one at random, between 1 and maximum number.
  if (language != english )
    sprintf(endstring,"%s\n\n"DOSY, endmsg[0] );
  else
    sprintf(endstring,"%s\n\n"DOSY, endmsg[ (gametic%(NUM_QUITMESSAGES-2))+1 ]);

  M_StartMessage(endstring,M_QuitResponse,true);
}




void M_ChangeSensitivity(int choice)
{
    switch(choice)
    {
      case 0:
	if (mouseSensitivity)
	    mouseSensitivity--;
	break;
      case 1:
	if (mouseSensitivity < 9)
	    mouseSensitivity++;
	break;
    }
}




// M_ChangeDetail removed: vanilla's low-detail (half-horizontal-res) mode is a dead
// no-op in this hi-res renderer -- R_ExecuteSetViewSize forces detailshift=0.  The
// `detailLevel` config value is kept (harmless) so old configs still load.




//
//      Menu Functions
//
void
M_DrawThermo
( int	x,
  int	y,
  int	thermWidth,
  int	thermDot )
{
    int		xx;
    int		i;

    xx = x;
    M_DrawMenuGraphic (xx,y,"M_THERML");
    xx += 8;
    for (i=0;i<thermWidth;i++)
    {
	M_DrawMenuGraphic (xx,y,"M_THERMM");
	xx += 8;
    }
    M_DrawMenuGraphic (xx,y,"M_THERMR");

    M_DrawMenuGraphic ((x+8) + thermDot*8, y, "M_THERMO");
}



void
M_DrawEmptyCell
( menu_t*	menu,
  int		item )
{
    M_DrawMenuGraphic (menu->x - 10, menu->y+item*LINEHEIGHT - 1, "M_CELL1");
}

void
M_DrawSelCell
( menu_t*	menu,
  int		item )
{
    M_DrawMenuGraphic (menu->x - 10, menu->y+item*LINEHEIGHT - 1, "M_CELL2");
}


void
M_StartMessage
( char*		string,
  void*		routine,
  boolean	input )
{
    messageLastMenuActive = menuactive;
    messageToPrint = 1;
    messageString = string;
    messageRoutine = routine;
    messageNeedsInput = input;
    menuactive = true;
    return;
}



void M_StopMessage(void)
{
    menuactive = messageLastMenuActive;
    messageToPrint = 0;
}



//
// Find string width from hu_font chars
//
int M_StringWidth(char* string)
{
    int             i;
    int             w = 0;
    int             c;
	
    for (i = 0;i < strlen(string);i++)
    {
	c = toupper(string[i]) - HU_FONTSTART;
	if (c < 0 || c >= HU_FONTSIZE)
	    w += 4;
	else
	    w += SHORT (hu_font[c]->width);
    }
		
    return w;
}



//
//      Find string height from hu_font chars
//
int M_StringHeight(char* string)
{
    int             i;
    int             h;
    int             height = SHORT(hu_font[0]->height);
	
    h = height;
    for (i = 0;i < strlen(string);i++)
	if (string[i] == '\n')
	    h += height;
		
    return h;
}


//
//      Write a string using the hu_font
//
void
M_WriteText
( int		x,
  int		y,
  char*		string)
{
    int		w;
    char*	ch;
    int		c;
    int		cx;
    int		cy;
		

    ch = string;
    cx = x;
    cy = y;
	
    while(1)
    {
	c = *ch++;
	if (!c)
	    break;
	if (c == '\n')
	{
	    cx = x;
	    cy += 12;
	    continue;
	}
		
	c = toupper(c) - HU_FONTSTART;
	if (c < 0 || c>= HU_FONTSIZE)
	{
	    cx += 4;
	    continue;
	}
		
	w = SHORT (hu_font[c]->width);
	if (cx+w > SCREENWIDTH)
	    break;
	V_DrawPatchDirect(cx, cy, 0, hu_font[c]);
	cx+=w;
    }
}


//
// M_WriteTextBig
// Like M_WriteText, but magnifies each character by `sc` (via V_DrawPatchScaled)
// so text-drawn menu items can match the size of the graphic menu items.
// Coordinates are in 320x200 (BASE) space.
//
void
M_WriteTextBig
( int		x,
  int		y,
  char*		string,
  int		sc )
{
    int		w;
    char*	ch;
    int		c;
    int		cx;
    int		cy;

    ch = string;
    cx = x;
    cy = y;

    while(1)
    {
	c = *ch++;
	if (!c)
	    break;
	if (c == '\n')
	{
	    cx = x;
	    cy += 12*sc;
	    continue;
	}

	c = toupper(c) - HU_FONTSTART;
	if (c < 0 || c>= HU_FONTSIZE)
	{
	    cx += 4*sc;
	    continue;
	}

	w = SHORT (hu_font[c]->width) * sc;
	if (cx+w > BASE_WIDTH)
	    break;
	V_DrawPatchScaled(cx, cy, 0, hu_font[c], sc);
	cx+=w;
    }
}



//
// CONTROL PANEL
//

//
// M_Responder
//
boolean M_Responder (event_t* ev)
{
    int             ch;
    int             i;
    static  int     joywait = 0;
    static  int     mousewait = 0;
    static  int     mousey = 0;
    static  int     lasty = 0;
    static  int     mousex = 0;
    static  int     lastx = 0;

    ch = -1;
	
    if (ev->type == ev_joystick && joywait < I_GetTime())
    {
	if (ev->data3 == -1)
	{
	    ch = KEY_UPARROW;
	    joywait = I_GetTime() + 5;
	}
	else if (ev->data3 == 1)
	{
	    ch = KEY_DOWNARROW;
	    joywait = I_GetTime() + 5;
	}
		
	if (ev->data2 == -1)
	{
	    ch = KEY_LEFTARROW;
	    joywait = I_GetTime() + 2;
	}
	else if (ev->data2 == 1)
	{
	    ch = KEY_RIGHTARROW;
	    joywait = I_GetTime() + 2;
	}
		
	if (ev->data1&1)
	{
	    ch = KEY_ENTER;
	    joywait = I_GetTime() + 5;
	}
	if (ev->data1&2)
	{
	    ch = KEY_BACKSPACE;
	    joywait = I_GetTime() + 5;
	}
    }
    else
    {
	if (ev->type == ev_mouse && mousewait < I_GetTime())
	{
	    mousey += ev->data3;
	    if (mousey < lasty-30)
	    {
		ch = KEY_DOWNARROW;
		mousewait = I_GetTime() + 5;
		mousey = lasty -= 30;
	    }
	    else if (mousey > lasty+30)
	    {
		ch = KEY_UPARROW;
		mousewait = I_GetTime() + 5;
		mousey = lasty += 30;
	    }
		
	    mousex += ev->data2;
	    if (mousex < lastx-30)
	    {
		ch = KEY_LEFTARROW;
		mousewait = I_GetTime() + 5;
		mousex = lastx -= 30;
	    }
	    else if (mousex > lastx+30)
	    {
		ch = KEY_RIGHTARROW;
		mousewait = I_GetTime() + 5;
		mousex = lastx += 30;
	    }
		
	    if (ev->data1&1)
	    {
		ch = KEY_ENTER;
		mousewait = I_GetTime() + 15;
	    }
			
	    if (ev->data1&2)
	    {
		ch = KEY_BACKSPACE;
		mousewait = I_GetTime() + 15;
	    }
	}
	else
	    if (ev->type == ev_keydown)
	    {
		ch = ev->data1;
	    }
    }
    
    if (ch == -1)
	return false;

    
    // Save Game string input
    if (saveStringEnter)
    {
	switch(ch)
	{
	  case KEY_BACKSPACE:
	    if (saveCharIndex > 0)
	    {
		saveCharIndex--;
		savegamestrings[saveSlot][saveCharIndex] = 0;
	    }
	    break;
				
	  case KEY_ESCAPE:
	    saveStringEnter = 0;
	    strcpy(&savegamestrings[saveSlot][0],saveOldString);
	    break;
				
	  case KEY_ENTER:
	    saveStringEnter = 0;
	    if (savegamestrings[saveSlot][0])
		M_DoSave(saveSlot);
	    break;
				
	  default:
	    ch = toupper(ch);
	    if (ch != 32)
		if (ch-HU_FONTSTART < 0 || ch-HU_FONTSTART >= HU_FONTSIZE)
		    break;
	    if (ch >= 32 && ch <= 127 &&
		saveCharIndex < SAVESTRINGSIZE-1 &&
		M_StringWidth(savegamestrings[saveSlot]) <
		(SAVESTRINGSIZE-2)*8)
	    {
		savegamestrings[saveSlot][saveCharIndex++] = ch;
		savegamestrings[saveSlot][saveCharIndex] = 0;
	    }
	    break;
	}
	return true;
    }
    
    // Take care of any messages that need input
    if (messageToPrint)
    {
	if (messageNeedsInput == true
	    && messageRoutine != M_QuitResponse		// quit prompt: ANY key confirms
	    && !(ch == ' ' || ch == 'n' || ch == 'y' || ch == KEY_ESCAPE))
	    return false;
		
	menuactive = messageLastMenuActive;
	messageToPrint = 0;
	if (messageRoutine)
	    messageRoutine(ch);
			
	menuactive = false;
	S_StartSound(NULL,sfx_swtchx);
	return true;
    }
	
    if (devparm && ch == KEY_F1)
    {
	G_ScreenShot ();
	return true;
    }
		
    
    // F-Keys
    if (!menuactive)
	switch(ch)
	{
	  case KEY_F1:            // Help key
	    M_StartControlPanel ();

	    if ( gamemode == retail )
	      currentMenu = &ReadDef2;
	    else
	      currentMenu = &ReadDef1;
	    
	    itemOn = 0;
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
				
	  case KEY_F2:            // Save
	    M_StartControlPanel();
	    S_StartSound(NULL,sfx_swtchn);
	    M_SaveGame(0);
	    return true;
				
	  case KEY_F3:            // Load
	    M_StartControlPanel();
	    S_StartSound(NULL,sfx_swtchn);
	    M_LoadGame(0);
	    return true;
				
	  case KEY_F4:            // Sound Volume
	    M_StartControlPanel ();
	    currentMenu = &SoundDef;
	    itemOn = sfx_vol;
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
				
	    // (KEY_F5 "Detail toggle" removed -- low-detail mode is a dead no-op here.)
				
	  case KEY_F6:            // Quicksave
	    S_StartSound(NULL,sfx_swtchn);
	    M_QuickSave();
	    return true;
				
	  case KEY_F7:            // End game
	    S_StartSound(NULL,sfx_swtchn);
	    M_EndGame(0);
	    return true;
				
	  case KEY_F8:            // Toggle messages
	    M_ChangeMessages(0);
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
				
	  case KEY_F9:            // Quickload
	    S_StartSound(NULL,sfx_swtchn);
	    M_QuickLoad();
	    return true;
				
	  case KEY_F10:           // Quit DOOM
	    S_StartSound(NULL,sfx_swtchn);
	    M_QuitDOOM(0);
	    return true;
				
	  case KEY_F11:           // gamma toggle
	    usegamma++;
	    if (usegamma > 4)
		usegamma = 0;
	    players[consoleplayer].message = gammamsg[usegamma];
	    I_SetPalette (W_CacheLumpName ("PLAYPAL",PU_CACHE));
	    return true;
				
	}

    
    // Pop-up menu?
    if (!menuactive)
    {
	if (ch == KEY_ESCAPE)
	{
	    M_StartControlPanel ();
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
	}
	return false;
    }

    
    // Keys usable within menu
    switch (ch)
    {
      case KEY_DOWNARROW:
	do
	{
	    if (itemOn+1 > currentMenu->numitems-1)
		itemOn = 0;
	    else itemOn++;
	    S_StartSound(NULL,sfx_pstop);
	} while(currentMenu->menuitems[itemOn].status==-1);
	return true;
		
      case KEY_UPARROW:
	do
	{
	    if (!itemOn)
		itemOn = currentMenu->numitems-1;
	    else itemOn--;
	    S_StartSound(NULL,sfx_pstop);
	} while(currentMenu->menuitems[itemOn].status==-1);
	return true;

      case KEY_LEFTARROW:
	if (currentMenu->menuitems[itemOn].routine &&
	    currentMenu->menuitems[itemOn].status == 2)
	{
	    S_StartSound(NULL,sfx_stnmov);
	    currentMenu->menuitems[itemOn].routine(0);
	}
	return true;
		
      case KEY_RIGHTARROW:
	if (currentMenu->menuitems[itemOn].routine &&
	    currentMenu->menuitems[itemOn].status == 2)
	{
	    S_StartSound(NULL,sfx_stnmov);
	    currentMenu->menuitems[itemOn].routine(1);
	}
	return true;

      case KEY_ENTER:
	if (currentMenu->menuitems[itemOn].routine &&
	    currentMenu->menuitems[itemOn].status)
	{
	    currentMenu->lastOn = itemOn;
	    if (currentMenu->menuitems[itemOn].status == 2)
	    {
		currentMenu->menuitems[itemOn].routine(1);      // right arrow
		S_StartSound(NULL,sfx_stnmov);
	    }
	    else
	    {
		currentMenu->menuitems[itemOn].routine(itemOn);
		S_StartSound(NULL,sfx_pistol);
	    }
	}
	return true;
		
      case KEY_ESCAPE:
	currentMenu->lastOn = itemOn;
	M_ClearMenus ();
	S_StartSound(NULL,sfx_swtchx);
	return true;
		
      case KEY_BACKSPACE:
	currentMenu->lastOn = itemOn;
	if (currentMenu->prevMenu)
	{
	    currentMenu = currentMenu->prevMenu;
	    itemOn = currentMenu->lastOn;
	    S_StartSound(NULL,sfx_swtchn);
	}
	return true;
	
      default:
	for (i = itemOn+1;i < currentMenu->numitems;i++)
	    if (currentMenu->menuitems[i].alphaKey == ch)
	    {
		itemOn = i;
		S_StartSound(NULL,sfx_pstop);
		return true;
	    }
	for (i = 0;i <= itemOn;i++)
	    if (currentMenu->menuitems[i].alphaKey == ch)
	    {
		itemOn = i;
		S_StartSound(NULL,sfx_pstop);
		return true;
	    }
	break;
	
    }

    return false;
}



//
// M_StartControlPanel
//
void M_StartControlPanel (void)
{
    // intro might call this repeatedly
    if (menuactive)
	return;
    
    menuactive = 1;
    currentMenu = &MainDef;         // JDC
    itemOn = currentMenu->lastOn;   // JDC
}


//
// M_Drawer
// Called after the view has been rendered,
// but before it has been blitted.
//
void M_Drawer (void)
{
    static short	x;
    static short	y;
    short		i;
    short		max;
    char		string[40];
    int			start;

    // The Buddy select screen is a full paletted screen of its own.
    if (M_Buddy_Active ())
    {
	M_DrawBuddy ();
	return;
    }

    // The Controls / Video screens replace the classic menu with their own crisp
    // SDL/TTF overlay (drawn in i_video.c) -- don't draw the paletted menu under them.
    if (M_Controls_Active () || M_Video_Active ())
	return;

    inhelpscreens = false;

    
    // Horiz. & Vertically center string and print it.
    if (messageToPrint)
    {
	start = 0;
	y = 100 - M_StringHeight(messageString)/2;
	while(*(messageString+start))
	{
	    for (i = 0;i < strlen(messageString+start);i++)
		if (*(messageString+start+i) == '\n')
		{
		    memset(string,0,40);
		    strncpy(string,messageString+start,i);
		    start += i+1;
		    break;
		}
				
	    if (i == strlen(messageString+start))
	    {
		strcpy(string,messageString+start);
		start += i;
	    }
				
	    x = 160 - M_StringWidth(string)/2;
	    M_WriteText(x,y,string);
	    y += SHORT(hu_font[0]->height);
	}
	return;
    }

    if (!menuactive)
	return;

    if (currentMenu->routine)
	currentMenu->routine();         // call Draw routine
    
    // DRAW MENU
    x = currentMenu->x;
    y = currentMenu->y;
    max = currentMenu->numitems;

    for (i=0;i<max;i++)
    {
	if (currentMenu->menuitems[i].name[0])
	    M_DrawMenuGraphic (x, y, currentMenu->menuitems[i].name);
	y += LINEHEIGHT;
    }

    
    // DRAW SKULL -- DOOM's M_SKULL*, or Heretic's M_SLCTR* selector, else a text caret.
    {
	int cy = currentMenu->y - 5 + itemOn*LINEHEIGHT;
	const char* cur = skullName[whichSkull];
	if (W_CheckNumForName ((char*)cur) < 0)
	    cur = whichSkull ? "M_SLCTR2" : "M_SLCTR1";	// Heretic menu cursor
	if (W_CheckNumForName ((char*)cur) >= 0)
	    V_DrawPatchDirect (x + SKULLXOFF, cy, 0, W_CacheLumpName ((char*)cur, PU_CACHE));
	else
	    M_WriteText (x + SKULLXOFF + 20, cy + 6, ">");	// last-resort text cursor
    }

}


//
// M_ClearMenus
//
void M_ClearMenus (void)
{
    menuactive = 0;
    // if (!netgame && usergame && paused)
    //       sendpause = true;
}




//
// M_SetupNextMenu
//
void M_SetupNextMenu(menu_t *menudef)
{
    currentMenu = menudef;
    itemOn = currentMenu->lastOn;
}


//
// M_Ticker
//
void M_Ticker (void)
{
    if (--skullAnimCounter <= 0)
    {
	whichSkull ^= 1;
	skullAnimCounter = 8;
    }
}


//
// M_Init
//
// If UMAPINFO defined an episode menu, replace the hardcoded EpisodeMenu with it.
// Entries with a valid menu graphic use it; the rest are drawn as text by
// M_DrawEpisode.  Called at the end of M_Init (after the gamemode fixups, so it
// wins over the shareware/registered episode-count tweaks).
static void M_UMapinfoBuildEpisodes (void)
{
    int i, n;

    if (!u_episodes_defined || u_num_episodes <= 0)
	return;					// keep the classic episode menu

    n = u_num_episodes;
    if (n > UMAPINFO_MAX_EPISODES) n = UMAPINFO_MAX_EPISODES;

    for (i = 0; i < n; i++)
    {
	UMapinfoEpisodeMenu[i].status = 1;
	if (u_episodes[i].patch[0] && W_CheckNumForName (u_episodes[i].patch) >= 0)
	{
	    strncpy (UMapinfoEpisodeMenu[i].name, u_episodes[i].patch, 8);
	    UMapinfoEpisodeMenu[i].name[8] = 0;
	}
	else
	    UMapinfoEpisodeMenu[i].name[0] = 0;	// no graphic -> M_DrawEpisode draws text
	UMapinfoEpisodeMenu[i].routine  = M_Episode;
	UMapinfoEpisodeMenu[i].alphaKey = u_episodes[i].key;
    }

    EpiDef.menuitems = UMapinfoEpisodeMenu;
    EpiDef.numitems  = n;
    EpiDef.lastOn    = 0;
    NewDef.prevMenu  = &EpiDef;			// so Doom 2 "back" returns to the episode menu
}

void M_Init (void)
{
    currentMenu = &MainDef;
    menuactive = 0;
    itemOn = currentMenu->lastOn;
    whichSkull = 0;
    skullAnimCounter = 10;
    messageToPrint = 0;
    messageString = NULL;
    messageLastMenuActive = menuactive;
    quickSaveSlot = -1;

    // Here we could catch other version dependencies,
    //  like HELP1/2, and four episodes.

  
    switch ( gamemode )
    {
      case commercial:
	// This is used because DOOM 2 had only one HELP
        //  page. I use CREDIT as second page now, but
	//  kept this hack for educational purposes.
	MainMenu[readthis] = MainMenu[quitdoom];
	MainDef.numitems--;
	MainDef.y += 8;
	NewDef.prevMenu = &MainDef;
	ReadDef1.routine = M_DrawReadThis1;
	ReadDef1.x = 330;
	ReadDef1.y = 165;
	ReadMenu1[0].routine = M_FinishReadThis;
	break;
      case shareware:
	// Episode 2 and 3 are handled,
	//  branching to an ad screen.
      case registered:
	// We need to remove the fourth episode.
	EpiDef.numitems--;
	break;
      case retail:
	// We are fine.
      default:
	break;
    }

    M_UMapinfoBuildEpisodes ();		// UMAPINFO "episode" menu (overrides the above)
}

