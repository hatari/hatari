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


/* Bottom border */

#define  BOTTOM_OFFSET  (-20)

// Medway Menu 67
SYNCSHIFTER_ACCESS BottomBorderAccess_Med_67_1[] = {
  { 0xFF820A,0x00,135204+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135284+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Med_67_2[] = {
  { 0xFF820A,0x00,135208+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135288+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Med_67_3[] = {
  { 0xFF820A,0x00,135212+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135292+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Med_67_4[] = {
  { 0xFF820A,0x00,135216+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135296+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Med_67_5[] = {
  { 0xFF820A,0x00,135220+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135300+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Med_67_6[] = {
  { 0xFF820A,0x00,135224+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135304+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Med_67_7[] = {
  { 0xFF820A,0x00,135228+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135308+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Med_67_8[] = {
  { 0xFF820A,0x00,135232+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135312+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Med_67_9[] = {
  { 0xFF820A,0x00,135236+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135316+BOTTOM_OFFSET }
};
/* D-Bug 136a */
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_1[] = {
  { 0xFF820A,0x00,135200+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135288+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_2[] = {
  { 0xFF820A,0x00,135204+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135292+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_3[] = {
  { 0xFF820A,0x00,135208+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135296+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_4[] = {
  { 0xFF820A,0x00,135212+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135300+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_5[] = {
  { 0xFF820A,0x00,135216+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135304+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_6[] = {
  { 0xFF820A,0x00,135220+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135308+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_7[] = {
  { 0xFF820A,0x00,135224+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135312+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_8[] = {
  { 0xFF820A,0x00,135228+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135316+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_9[] = {
  { 0xFF820A,0x00,135232+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135320+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_10[] = {
  { 0xFF820A,0x00,135236+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135324+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_11[] = {
  { 0xFF820A,0x00,135240+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135328+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_136a_12[] = {
  { 0xFF820A,0x00,135244+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135332+BOTTOM_OFFSET }
};
/* D-Bug 141a */
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_141a_1[] = {
  { 0xFF820A,0x00,135200+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135292+BOTTOM_OFFSET },
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_141a_2[] = {
  { 0xFF820A,0x00,135204+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135296+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_141a_3[] = {
  { 0xFF820A,0x00,135208+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135300+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_141a_4[] = {
  { 0xFF820A,0x00,135212+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135304+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_141a_5[] = {
  { 0xFF820A,0x00,135216+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135308+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_141a_6[] = {
  { 0xFF820A,0x00,135220+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135312+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_141a_7[] = {
  { 0xFF820A,0x00,135224+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135316+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_DBug_141a_8[] = {
  { 0xFF820A,0x00,135228+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135320+BOTTOM_OFFSET }
};

/* Auto 95 */
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_95_1[] = {
  { 0xFF820A,0x00,135168+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135256+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_95_2[] = {
  { 0xFF820A,0x00,135172+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135260+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_95_3[] = {
  { 0xFF820A,0x00,135176+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135264+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_95_4[] = {
  { 0xFF820A,0x00,135180+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135268+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_95_5[] = {
  { 0xFF820A,0x00,135184+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135272+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_95_6[] = {
  { 0xFF820A,0x00,135188+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135276+BOTTOM_OFFSET }
};
/* Auto 106 */
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_106_1[] = {
  { 0xFF820A,0x00,135204+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135260+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_106_2[] = {
  { 0xFF820A,0x00,135208+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135264+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_106_3[] = {
  { 0xFF820A,0x00,135212+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135268+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_106_4[] = {
  { 0xFF820A,0x00,135216+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135272+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_106_5[] = {
  { 0xFF820A,0x00,135220+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135276+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_106_6[] = {
  { 0xFF820A,0x00,135224+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135280+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_106_7[] = {
  { 0xFF820A,0x00,135228+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135284+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_106_8[] = {
  { 0xFF820A,0x00,135232+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135288+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_106_9[] = {
  { 0xFF820A,0x00,135236+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135292+BOTTOM_OFFSET }
};
/* Auto 149 (writes 0x2 twice) */
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_149_1[] = {
  { 0xFF820A,0x02,135252+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135272+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_149_2[] = {
  { 0xFF820A,0x02,135256+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135276+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_149_3[] = {
  { 0xFF820A,0x02,135260+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135280+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_149_4[] = {
  { 0xFF820A,0x02,135264+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135284+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_149_5[] = {
  { 0xFF820A,0x02,135268+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135288+BOTTOM_OFFSET }
};
/* Auto 169 */
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_169_1[] = {
  { 0xFF820A,0x00,135152+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135244+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_169_2[] = {
  { 0xFF820A,0x00,135156+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135248+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_169_3[] = {
  { 0xFF820A,0x00,135160+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135252+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_169_4[] = {
  { 0xFF820A,0x00,135164+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135256+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Auto_169_5[] = {
  { 0xFF820A,0x00,135168+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135260+BOTTOM_OFFSET }
};

/* Pompey 6 */
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_6_1[] = {
  { 0xFF820A,0x00,135196+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135248+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_6_2[] = {
  { 0xFF820A,0x00,135200+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135252+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_6_3[] = {
  { 0xFF820A,0x00,135204+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135256+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_6_4[] = {
  { 0xFF820A,0x00,135208+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135260+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_6_5[] = {
  { 0xFF820A,0x00,135212+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135264+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_6_6[] = {
  { 0xFF820A,0x00,135216+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135268+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_6_7[] = {
  { 0xFF820A,0x00,135220+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135272+BOTTOM_OFFSET }
};
/* Pompey 11 */
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_1[] = {
  { 0xFF820A,0x00,135188+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135236+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_2[] = {
  { 0xFF820A,0x00,135192+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135240+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_3[] = {
  { 0xFF820A,0x00,135196+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135244+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_4[] = {
  { 0xFF820A,0x00,135200+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135248+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_5[] = {
  { 0xFF820A,0x00,135204+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135252+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_6[] = {
  { 0xFF820A,0x00,135208+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135256+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_7[] = {
  { 0xFF820A,0x00,135212+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135260+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_8[] = {
  { 0xFF820A,0x00,135216+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135264+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_9[] = {
  { 0xFF820A,0x00,135220+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135268+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_10[] = {
  { 0xFF820A,0x00,135224+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135272+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_11[] = {
  { 0xFF820A,0x00,135228+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135276+BOTTOM_OFFSET }
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_11_12[] = {
  { 0xFF820A,0x00,135232+BOTTOM_OFFSET },
  { 0xFF820A,0x02,135280+BOTTOM_OFFSET }
};
/* Pompey 27 */
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_27_1[] = {
  0xFF820A,0x00,135208+BOTTOM_OFFSET,
  0xFF820A,0x02,135284+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_27_2[] = {
  0xFF820A,0x00,135212+BOTTOM_OFFSET,
  0xFF820A,0x02,135288+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_27_3[] = {
  0xFF820A,0x00,135216+BOTTOM_OFFSET,
  0xFF820A,0x02,135292+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_27_4[] = {
  0xFF820A,0x00,135220+BOTTOM_OFFSET,
  0xFF820A,0x02,135296+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_27_5[] = {
  0xFF820A,0x00,135224+BOTTOM_OFFSET,
  0xFF820A,0x02,135300+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_27_6[] = {
  0xFF820A,0x00,135228+BOTTOM_OFFSET,
  0xFF820A,0x02,135304+BOTTOM_OFFSET,
};
// Pompey 43
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_43_1[] = {
  0xFF820A,0x00,135236+BOTTOM_OFFSET,
  0xFF820A,0x02,135280+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_43_2[] = {
  0xFF820A,0x00,135240+BOTTOM_OFFSET,
  0xFF820A,0x02,135284+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_43_3[] = {
  0xFF820A,0x00,135244+BOTTOM_OFFSET,
  0xFF820A,0x02,135288+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_43_4[] = {
  0xFF820A,0x00,135248+BOTTOM_OFFSET,
  0xFF820A,0x02,135292+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_43_5[] = {
  0xFF820A,0x00,135252+BOTTOM_OFFSET,
  0xFF820A,0x02,135296+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_43_6[] = {
  0xFF820A,0x00,135256+BOTTOM_OFFSET,
  0xFF820A,0x02,135300+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_43_7[] = {
  0xFF820A,0x00,135260+BOTTOM_OFFSET,
  0xFF820A,0x02,135304+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_43_8[] = {
  0xFF820A,0x00,135264+BOTTOM_OFFSET,
  0xFF820A,0x02,135308+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_43_9[] = {
  0xFF820A,0x00,135268+BOTTOM_OFFSET,
  0xFF820A,0x02,135312+BOTTOM_OFFSET,
};
// Pompey 48
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_48_1[] = {
  0xFF820A,0x00,135204+BOTTOM_OFFSET,
  0xFF820A,0x02,135308+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_48_2[] = {
  0xFF820A,0x00,135208+BOTTOM_OFFSET,
  0xFF820A,0x02,135312+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_48_3[] = {
  0xFF820A,0x00,135212+BOTTOM_OFFSET,
  0xFF820A,0x02,135316+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_48_4[] = {
  0xFF820A,0x00,135216+BOTTOM_OFFSET,
  0xFF820A,0x02,135320+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_48_5[] = {
  0xFF820A,0x00,135220+BOTTOM_OFFSET,
  0xFF820A,0x02,135324+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_48_6[] = {
  0xFF820A,0x00,135224+BOTTOM_OFFSET,
  0xFF820A,0x02,135328+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_48_7[] = {
  0xFF820A,0x00,135228+BOTTOM_OFFSET,
  0xFF820A,0x02,135332+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_48_8[] = {
  0xFF820A,0x00,135232+BOTTOM_OFFSET,
  0xFF820A,0x02,135336+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_48_9[] = {
  0xFF820A,0x00,135236+BOTTOM_OFFSET,
  0xFF820A,0x02,135340+BOTTOM_OFFSET,
};
// Pompey 57 - Seems strange...
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_57_1[] = {
  0xFF820A,0x00,134696+BOTTOM_OFFSET,
  0xFF820A,0x02,134800+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_57_2[] = {
  0xFF820A,0x00,134700+BOTTOM_OFFSET,
  0xFF820A,0x02,134804+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_57_3[] = {
  0xFF820A,0x00,134704+BOTTOM_OFFSET,
  0xFF820A,0x02,134808+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_57_4[] = {
  0xFF820A,0x00,134708+BOTTOM_OFFSET,
  0xFF820A,0x02,134812+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_57_5[] = {
  0xFF820A,0x00,134712+BOTTOM_OFFSET,
  0xFF820A,0x02,134816+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_57_6[] = {
  0xFF820A,0x00,134716+BOTTOM_OFFSET,
  0xFF820A,0x02,134820+BOTTOM_OFFSET,
};
// Pompey 62
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_62_1[] = {
  0xFF820A,0x00,135208+BOTTOM_OFFSET,
  0xFF820A,0x02,135292+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_62_2[] = {
  0xFF820A,0x00,135212+BOTTOM_OFFSET,
  0xFF820A,0x02,135296+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_62_3[] = {
  0xFF820A,0x00,135216+BOTTOM_OFFSET,
  0xFF820A,0x02,135300+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_62_4[] = {
  0xFF820A,0x00,135220+BOTTOM_OFFSET,
  0xFF820A,0x02,135304+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_62_5[] = {
  0xFF820A,0x00,135224+BOTTOM_OFFSET,
  0xFF820A,0x02,135308+BOTTOM_OFFSET,
};
// Pompey 68
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_68_1[] = {
  0xFF820A,0x00,135224+BOTTOM_OFFSET,
  0xFF820A,0x02,135284+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_68_2[] = {
  0xFF820A,0x00,135228+BOTTOM_OFFSET,
  0xFF820A,0x02,135288+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_68_3[] = {
  0xFF820A,0x00,135232+BOTTOM_OFFSET,
  0xFF820A,0x02,135292+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_68_4[] = {
  0xFF820A,0x00,135236+BOTTOM_OFFSET,
  0xFF820A,0x02,135296+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_68_5[] = {
  0xFF820A,0x00,135240+BOTTOM_OFFSET,
  0xFF820A,0x02,135300+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_68_6[] = {
  0xFF820A,0x00,135240+BOTTOM_OFFSET,
  0xFF820A,0x02,135300+BOTTOM_OFFSET,
};
// Pompey 81
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_81_1[] = {
  0xFF820A,0x00,135208+BOTTOM_OFFSET,
  0xFF820A,0x02,135280+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_81_2[] = {
  0xFF820A,0x00,135212+BOTTOM_OFFSET,
  0xFF820A,0x02,135284+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_81_3[] = {
  0xFF820A,0x00,135216+BOTTOM_OFFSET,
  0xFF820A,0x02,135288+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_81_4[] = {
  0xFF820A,0x00,135220+BOTTOM_OFFSET,
  0xFF820A,0x02,135292+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_81_5[] = {
  0xFF820A,0x00,135224+BOTTOM_OFFSET,
  0xFF820A,0x02,135296+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_81_6[] = {
  0xFF820A,0x00,135228+BOTTOM_OFFSET,
  0xFF820A,0x02,135300+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_81_7[] = {
  0xFF820A,0x00,135232+BOTTOM_OFFSET,
  0xFF820A,0x02,135304+BOTTOM_OFFSET,
};
// Pompey 91
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_91_1[] = {
  0xFF820A,0x00,135208+BOTTOM_OFFSET,
  0xFF820A,0x02,135236+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_91_2[] = {
  0xFF820A,0x00,135212+BOTTOM_OFFSET,
  0xFF820A,0x02,135240+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_91_3[] = {
  0xFF820A,0x00,135216+BOTTOM_OFFSET,
  0xFF820A,0x02,135244+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_91_4[] = {
  0xFF820A,0x00,135220+BOTTOM_OFFSET,
  0xFF820A,0x02,135248+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_91_5[] = {
  0xFF820A,0x00,135224+BOTTOM_OFFSET,
  0xFF820A,0x02,135252+BOTTOM_OFFSET,
};
SYNCSHIFTER_ACCESS BottomBorderAccess_Pompey_91_6[] = {
  0xFF820A,0x00,135228+BOTTOM_OFFSET,
  0xFF820A,0x02,135256+BOTTOM_OFFSET,
};

SYNCSHIFTER_ACCESS_TABLE pBottomBorderAccessTable[] = {
  0, 2,BottomBorderAccess_Med_67_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Med_67_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Med_67_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Med_67_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Med_67_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Med_67_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Med_67_7, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Med_67_8, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Med_67_9, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_7, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_8, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_9, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_10, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_11, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_136a_12, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_141a_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_141a_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_141a_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_141a_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_141a_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_141a_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_141a_7, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_DBug_141a_8, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_6_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_6_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_6_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_6_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_6_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_6_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_6_7, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_7, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_8, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_9, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_10, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_11, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_11_12, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_27_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_27_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_27_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_27_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_27_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_27_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_43_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_43_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_43_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_43_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_43_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_43_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_43_7, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_43_8, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_43_9, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_48_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_48_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_48_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_48_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_48_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_48_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_48_7, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_48_8, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_48_9, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_57_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_57_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_57_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_57_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_57_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_57_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_62_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_62_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_62_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_62_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_62_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_68_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_68_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_68_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_68_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_68_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_68_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_81_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_81_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_81_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_81_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_81_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_81_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_81_7, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_91_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_91_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_91_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_91_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_91_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Pompey_91_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_95_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_95_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_95_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_95_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_95_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_95_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_106_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_106_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_106_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_106_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_106_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_106_6, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_106_7, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_106_8, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_106_9, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_149_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_149_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_149_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_149_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_149_5, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_169_1, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_169_2, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_169_3, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_169_4, Video_SyncHandler_SetBottomBorder,0,
  0, 2,BottomBorderAccess_Auto_169_5, Video_SyncHandler_SetBottomBorder,0,

  0, 0  // term
};

//-----------------------------------------------------------------------
// Top border

#define  TOP_OFFSET  (-32)

// Medway Menu 67
SYNCSHIFTER_ACCESS TopBorderAccess_Med_67_1[] = {
  0xFF820A,0x00,17256+TOP_OFFSET,
  0xFF820A,0x02,17352+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Med_67_2[] = {
  0xFF820A,0x00,17260+TOP_OFFSET,
  0xFF820A,0x02,17356+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Med_67_3[] = {
  0xFF820A,0x00,17264+TOP_OFFSET,
  0xFF820A,0x02,17360+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Med_67_4[] = {
  0xFF820A,0x00,17268+TOP_OFFSET,
  0xFF820A,0x02,17364+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Med_67_5[] = {
  0xFF820A,0x00,17272+TOP_OFFSET,
  0xFF820A,0x02,17368+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Med_67_6[] = {
  0xFF820A,0x00,17276+TOP_OFFSET,
  0xFF820A,0x02,17372+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Med_67_7[] = {
  0xFF820A,0x00,17280+TOP_OFFSET,
  0xFF820A,0x02,17376+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Med_67_8[] = {
  0xFF820A,0x00,17284+TOP_OFFSET,
  0xFF820A,0x02,17380+TOP_OFFSET,
};
// DBug 67
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_136a_1[] = {
  0xFF820A,0x00,17944+TOP_OFFSET,
  0xFF820A,0x02,18032+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_136a_2[] = {
  0xFF820A,0x00,17948+TOP_OFFSET,
  0xFF820A,0x02,18036+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_136a_3[] = {
  0xFF820A,0x00,17952+TOP_OFFSET,
  0xFF820A,0x02,18040+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_136a_4[] = {
  0xFF820A,0x00,17956+TOP_OFFSET,
  0xFF820A,0x02,18044+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_136a_5[] = {
  0xFF820A,0x00,17960+TOP_OFFSET,
  0xFF820A,0x02,18048+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_136a_6[] = {
  0xFF820A,0x00,17964+TOP_OFFSET,
  0xFF820A,0x02,18052+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_136a_7[] = {
  0xFF820A,0x00,17968+TOP_OFFSET,
  0xFF820A,0x02,18056+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_136a_8[] = {
  0xFF820A,0x00,17972+TOP_OFFSET,
  0xFF820A,0x02,18060+TOP_OFFSET,
};
// D-Bug 142a
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_142a_1[] = {
  0xFF820A,0x00,17296+TOP_OFFSET,
  0xFF820A,0x02,17344+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_142a_2[] = {
  0xFF820A,0x00,17300+TOP_OFFSET,
  0xFF820A,0x02,17348+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_142a_3[] = {
  0xFF820A,0x00,17304+TOP_OFFSET,
  0xFF820A,0x02,17352+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_142a_4[] = {
  0xFF820A,0x00,17308+TOP_OFFSET,
  0xFF820A,0x02,17356+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_142a_5[] = {
  0xFF820A,0x00,17312+TOP_OFFSET,
  0xFF820A,0x02,17360+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_142a_6[] = {
  0xFF820A,0x00,17316+TOP_OFFSET,
  0xFF820A,0x02,17364+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_142a_7[] = {
  0xFF820A,0x00,17320+TOP_OFFSET,
  0xFF820A,0x02,17368+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_DBug_142a_8[] = {
  0xFF820A,0x00,17324+TOP_OFFSET,
  0xFF820A,0x02,17372+TOP_OFFSET,
};
// Pompey 27
SYNCSHIFTER_ACCESS TopBorderAccess_Pompey_27_1[] = {
  0xFF820A,0x00,17280+TOP_OFFSET,
  0xFF820A,0x02,17360+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Pompey_27_2[] = {
  0xFF820A,0x00,17284+TOP_OFFSET,
  0xFF820A,0x02,17364+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Pompey_27_3[] = {
  0xFF820A,0x00,17288+TOP_OFFSET,
  0xFF820A,0x02,17368+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Pompey_27_4[] = {
  0xFF820A,0x00,17292+TOP_OFFSET,
  0xFF820A,0x02,17372+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Pompey_27_5[] = {
  0xFF820A,0x00,17296+TOP_OFFSET,
  0xFF820A,0x02,17376+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Pompey_27_6[] = {
  0xFF820A,0x00,17300+TOP_OFFSET,
  0xFF820A,0x02,17380+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Pompey_27_7[] = {
  0xFF820A,0x00,17304+TOP_OFFSET,
  0xFF820A,0x02,17384+TOP_OFFSET,
};
// Auto 90
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_90_1[] = {
  0xFF820A,0x00,17316+TOP_OFFSET,
  0xFF820A,0x02,17344+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_90_2[] = {
  0xFF820A,0x00,17320+TOP_OFFSET,
  0xFF820A,0x02,17348+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_90_3[] = {
  0xFF820A,0x00,17324+TOP_OFFSET,
  0xFF820A,0x02,17352+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_90_4[] = {
  0xFF820A,0x00,17328+TOP_OFFSET,
  0xFF820A,0x02,17356+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_90_5[] = {
  0xFF820A,0x00,17332+TOP_OFFSET,
  0xFF820A,0x02,17360+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_90_6[] = {
  0xFF820A,0x00,17336+TOP_OFFSET,
  0xFF820A,0x02,17364+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_90_7[] = {
  0xFF820A,0x00,17340+TOP_OFFSET,
  0xFF820A,0x02,17368+TOP_OFFSET,
};
// Auto 132
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_132_1[] = {
  0xFF820A,0x00,17316+TOP_OFFSET,
  0xFF820A,0x02,17376+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_132_2[] = {
  0xFF820A,0x00,17320+TOP_OFFSET,
  0xFF820A,0x02,17380+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_132_3[] = {
  0xFF820A,0x00,17324+TOP_OFFSET,
  0xFF820A,0x02,17384+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_132_4[] = {
  0xFF820A,0x00,17328+TOP_OFFSET,
  0xFF820A,0x02,17388+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_132_5[] = {
  0xFF820A,0x00,17332+TOP_OFFSET,
  0xFF820A,0x02,17392+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_132_6[] = {
  0xFF820A,0x00,17336+TOP_OFFSET,
  0xFF820A,0x02,17396+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_132_7[] = {
  0xFF820A,0x00,17340+TOP_OFFSET,
  0xFF820A,0x02,17400+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_132_8[] = {
  0xFF820A,0x00,17344+TOP_OFFSET,
  0xFF820A,0x02,17404+TOP_OFFSET,
};
// Auto 275 - Weird
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_275_1[] = {
  0xFF820A,0x00,16672+TOP_OFFSET,
  0xFF820A,0x02,16744+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_275_2[] = {
  0xFF820A,0x00,16676+TOP_OFFSET,
  0xFF820A,0x02,16748+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_275_3[] = {
  0xFF820A,0x00,16680+TOP_OFFSET,
  0xFF820A,0x02,16752+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_275_4[] = {
  0xFF820A,0x00,16684+TOP_OFFSET,
  0xFF820A,0x02,16756+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_275_5[] = {
  0xFF820A,0x00,16688+TOP_OFFSET,
  0xFF820A,0x02,16760+TOP_OFFSET,
};
SYNCSHIFTER_ACCESS TopBorderAccess_Auto_275_6[] = {
  0xFF820A,0x00,16692+TOP_OFFSET,
  0xFF820A,0x02,16764+TOP_OFFSET,
};

SYNCSHIFTER_ACCESS_TABLE pTopBorderAccessTable[] = {
  0, 2,TopBorderAccess_Med_67_1, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Med_67_2, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Med_67_3, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Med_67_4, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Med_67_5, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Med_67_6, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Med_67_7, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Med_67_8, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_136a_1, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_136a_2, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_136a_3, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_136a_4, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_136a_5, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_136a_6, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_136a_7, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_136a_8, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_142a_1, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_142a_2, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_142a_3, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_142a_4, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_142a_5, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_142a_6, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_142a_7, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_DBug_142a_8, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Pompey_27_1, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Pompey_27_2, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Pompey_27_3, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Pompey_27_4, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Pompey_27_5, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Pompey_27_6, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Pompey_27_7, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_90_1, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_90_2, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_90_3, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_90_4, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_90_5, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_90_6, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_90_7, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_132_1, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_132_2, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_132_3, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_132_4, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_132_5, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_132_6, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_132_7, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_132_8, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_275_1, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_275_2, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_275_3, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_275_4, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_275_5, Video_SyncHandler_SetTopBorder,0,
  0, 2,TopBorderAccess_Auto_275_6, Video_SyncHandler_SetTopBorder,0,

  0, 0  // term
};

//-----------------------------------------------------------------------
// Left/Right borders
SYNCSHIFTER_ACCESS LeftRightBorderAccess_Pompey_46[] = {
  0xFF8260,0x02,28,
  0xFF8260,0x00,36,
  0xFF820A,0x00,400,
  0xFF820A,0x02,408,
  0xFF8260,0x02,468,
  0xFF8260,0x00,480,
};

SYNCSHIFTER_ACCESS_TABLE pLeftRightBorderAccessTable[] = {
  0, 6,LeftRightBorderAccess_Pompey_46,Video_SyncHandler_SetLeftRightBorder,BORDERMASK_LEFT|BORDERMASK_RIGHT,

  0, 0  // term
};

//-----------------------------------------------------------------------
// Sync Scrolling

// Syncscr7.s
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_1[] = {  // 0x118de wholeline(+70)
  0xFF8260,0x02,492,
  0xFF8260,0x00,508,
  0xFF820A,0x00,360,  // NOTE This also matches with array below so add (70-44)
  0xFF820A,0x02,376,
  0xFF8260,0x01,424,
  0xFF8260,0x00,440,
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_2[] = {  // 0x119c2 rightonly(+44)
  0xFF820A,0x00,360,
  0xFF820A,0x02,376,
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_3[] = {  // 0x11aae length_2(-2)
  0xFF820A,0x00,352,
  0xFF820A,0x02,368,
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_4[] = {  // 0x11c8a length24(+24)
  0xFF8260,0x02,492,
  0xFF8260,0x00,508,
  0xFF820A,0x00,356,
  0xFF820A,0x02,372,
  0xFF8260,0x01,424,
  0xFF8260,0x00,440,
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_5[] = {  // 0x11d6e length26(+26)
  0xFF8260,0x02,492,
  0xFF8260,0x00,508,
  0xFF8260,0x01,424,
  0xFF8260,0x00,440,
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_SyncScrl_6[] = {  // 0x11e56 length_106(-106)
  0xFF8260,0x02,144,
  0xFF8260,0x00,160,
};

// Hardware.s(ignore L230 as is +26 +44 combined)
SYNCSHIFTER_ACCESS SyncScrollerAccess_Hardware_1[] = {  // 0x121c2 L158(-2)
  0xFF820A,0x00,360,
  0xFF820A,0x02,368,
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_Hardware_2[] = {  // 0x122ae L184(+24)
  0xFF8260,0x02,436,
  0xFF8260,0x00,448,
  0xFF8260,0x02,508,
  0xFF8260,0x00,4,
  0xFF820A,0x00,360,  // NOTE This also matches with array have so add (24+2)
  0xFF820A,0x02,368,
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_Hardware_3[] = {  // 0x12394 L186(+26)
  0xFF8260,0x02,440,
  0xFF8260,0x00,452,
  0xFF8260,0x02,508,
  0xFF8260,0x00,4,
};
SYNCSHIFTER_ACCESS SyncScrollerAccess_Hardware_4[] = {  // 0x1247a L204(+44)
  0xFF820A,0x00,368,
  0xFF820A,0x02,376,
};

SYNCSHIFTER_ACCESS_TABLE pSyncScrollerAccessTable[] = {
  0, 6,SyncScrollerAccess_SyncScrl_1, Video_SyncHandler_SetSyncScrollOffset,+70-44,
  0, 2,SyncScrollerAccess_SyncScrl_2, Video_SyncHandler_SetSyncScrollOffset,+44,
  0, 2,SyncScrollerAccess_SyncScrl_3, Video_SyncHandler_SetSyncScrollOffset,-2,
  0, 6,SyncScrollerAccess_SyncScrl_4, Video_SyncHandler_SetSyncScrollOffset,+24,
  0, 4,SyncScrollerAccess_SyncScrl_5, Video_SyncHandler_SetSyncScrollOffset,+26,
  0, 2,SyncScrollerAccess_SyncScrl_6, Video_SyncHandler_SetSyncScrollOffset,-106,

  0, 2,SyncScrollerAccess_Hardware_1, Video_SyncHandler_SetSyncScrollOffset,-2,
  0, 6,SyncScrollerAccess_Hardware_2, Video_SyncHandler_SetSyncScrollOffset,+24+2,
  0, 4,SyncScrollerAccess_Hardware_3, Video_SyncHandler_SetSyncScrollOffset,+26,
  0, 2,SyncScrollerAccess_Hardware_4, Video_SyncHandler_SetSyncScrollOffset,+44,

  0, 0  // term
};

