/*
  Hatari
*/

typedef void (*ShortCutFunction_t)(void);

enum {
  SHORTCUT_NOTASSIGNED,
  SHORTCUT_FULLSCREEN,
  SHORTCUT_MOUSEMODE,
  SHORTCUT_RECORDSOUND,
  SHORTCUT_RECORDANIM,
  SHORTCUT_CURSOREMU,
  SHORTCUT_SOUND,
  SHORTCUT_MAXSPEED,
  SHORTCUT_COLDRESET,
  SHORTCUT_WARMRESET,
  SHORTCUT_BOSSKEY,

  NUM_SHORTCUTS
};

typedef struct {
  unsigned short Key;
  BOOL bShiftPressed;
  BOOL bCtrlPressed;
} SHORTCUT_KEY;

extern char *pszShortCutTextStrings[NUM_SHORTCUTS+1];
extern char *pszShortCutF11TextString[];
extern char *pszShortCutF12TextString[];
extern SHORTCUT_KEY ShortCutKey;

extern void ShortCut_ClearKeys(void);
extern void ShortCut_CheckKeys(void);
extern void ShortCut_FullScreen(void);
extern void ShortCut_MouseMode(void);
extern void ShortCut_RecordSound(void);
extern void ShortCut_RecordAnimation(void);
extern void ShortCut_JoystickCursorEmulation(void);
extern void ShortCut_SoundOnOff(void);
extern void ShortCut_MaximumSpeed(void);
extern void ShortCut_ColdReset(void);
extern void ShortCut_WarmReset(void);
extern void ShortCut_BossKey(void);
