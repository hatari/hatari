/*
  Hatari

  Tables for cycles when allow top/bottom borders.
  Also left/right and Sync Scroll
*/

/*------------------------------------------------------------------------*/
/* Top/Bottom Border tables - NOTE due to natural interrupt inaccuracies  */
/* (ie interrupt due while currently processing instruction) these values */
/* have a 'range' - We have a single table entry for each possible +      */
/* overlap, just in-case!                                                 */

/*-----------------------------------------------------------------------*/
/* Left/Right borders */
SYNCSHIFTER_ACCESS LeftRightBorderAccess_Pompey_46[] = {
  { 0xFF8260,0x02,28 },
  { 0xFF8260,0x00,36 },
  { 0xFF820A,0x00,400 },
  { 0xFF820A,0x02,408 },
  { 0xFF8260,0x02,468 },
  { 0xFF8260,0x00,480 }
};

SYNCSHIFTER_ACCESS_TABLE pLeftRightBorderAccessTable[] = {
  { 0, 6,LeftRightBorderAccess_Pompey_46,Video_SyncHandler_SetLeftRightBorder,BORDERMASK_LEFT|BORDERMASK_RIGHT },

  { 0, 0, 0, 0, 0 }  /* term */
};


/*-----------------------------------------------------------------------*/
/* Sync Scrolling */

/* Syncscr7.s */
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_1[] = {  /* 0x118de wholeline(+70) */
  { 0xFF8260,0x02,492 },
  { 0xFF8260,0x00,508 },
  { 0xFF820A,0x00,360 },  /* NOTE This also matches with array below so add (70-44) */
  { 0xFF820A,0x02,376 },
  { 0xFF8260,0x01,424 },
  { 0xFF8260,0x00,440 }
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_2[] = {  /* 0x119c2 rightonly(+44) */
  { 0xFF820A,0x00,360 },
  { 0xFF820A,0x02,376 }
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_3[] = {  /* 0x11aae length_2(-2) */
  { 0xFF820A,0x00,352 },
  { 0xFF820A,0x02,368 }
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_4[] = {  /* 0x11c8a length24(+24) */
  { 0xFF8260,0x02,492 },
  { 0xFF8260,0x00,508 },
  { 0xFF820A,0x00,356 },
  { 0xFF820A,0x02,372 },
  { 0xFF8260,0x01,424 },
  { 0xFF8260,0x00,440 }
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_5[] = {  /* 0x11d6e length26(+26) */
  { 0xFF8260,0x02,492 },
  { 0xFF8260,0x00,508 },
  { 0xFF8260,0x01,424 },
  { 0xFF8260,0x00,440 }
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_6[] = {  /* 0x11e56 length_106(-106) */
  { 0xFF8260,0x02,144 },
  { 0xFF8260,0x00,160 }
};

/* Hardware.s (ignore L230 as is +26 +44 combined) */
SYNCSHIFTER_ACCESS SyncScrollerAccess_Hardware_1[] = {  /* 0x121c2 L158(-2) */
  { 0xFF820A,0x00,360 },
  { 0xFF820A,0x02,368 }
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_Hardware_2[] = {  /* 0x122ae L184(+24) */
  { 0xFF8260,0x02,436 },
  { 0xFF8260,0x00,448 },
  { 0xFF8260,0x02,508 },
  { 0xFF8260,0x00,4 },
  { 0xFF820A,0x00,360 },  /* NOTE This also matches with array have so add (24+2) */
  { 0xFF820A,0x02,368 }
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_Hardware_3[] = {  /* 0x12394 L186(+26) */
  { 0xFF8260,0x02,440 },
  { 0xFF8260,0x00,452 },
  { 0xFF8260,0x02,508 },
  { 0xFF8260,0x00,4 }
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_Hardware_4[] = {  /* 0x1247a L204(+44) */
  { 0xFF820A,0x00,368 },
  { 0xFF820A,0x02,376 }
};

SYNCSHIFTER_ACCESS_TABLE pSyncScrollerAccessTable[] = {
  { 0, 6,SyncScrollerAccess_SyncScrl_1, Video_SyncHandler_SetSyncScrollOffset,+70-44 },
  { 0, 2,SyncScrollerAccess_SyncScrl_2, Video_SyncHandler_SetSyncScrollOffset,+44 },
  { 0, 2,SyncScrollerAccess_SyncScrl_3, Video_SyncHandler_SetSyncScrollOffset,-2 },
  { 0, 6,SyncScrollerAccess_SyncScrl_4, Video_SyncHandler_SetSyncScrollOffset,+24 },
  { 0, 4,SyncScrollerAccess_SyncScrl_5, Video_SyncHandler_SetSyncScrollOffset,+26 },
  { 0, 2,SyncScrollerAccess_SyncScrl_6, Video_SyncHandler_SetSyncScrollOffset,-106 },

  { 0, 2,SyncScrollerAccess_Hardware_1, Video_SyncHandler_SetSyncScrollOffset,-2 },
  { 0, 6,SyncScrollerAccess_Hardware_2, Video_SyncHandler_SetSyncScrollOffset,+24+2 },
  { 0, 4,SyncScrollerAccess_Hardware_3, Video_SyncHandler_SetSyncScrollOffset,+26 },
  { 0, 2,SyncScrollerAccess_Hardware_4, Video_SyncHandler_SetSyncScrollOffset,+44 },

  { 0, 0, 0, 0, 0 }  // term
};

