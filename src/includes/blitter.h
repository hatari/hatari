/**
 * Hatari - Blitter emulation.
 * This file has been taken from STonX.
 *
 * Original information text follows:
 *
 *
 * This file is part of STonX, the Atari ST Emulator for Unix/X
 * ============================================================
 * STonX is free software and comes with NO WARRANTY - read the file
 * COPYING for details
 */

#ifndef BLITTER_H
#define BLITTER_H

extern void Do_Blit(void);

extern Uint16 LOAD_W_ff8a28(void);
extern Uint16 LOAD_W_ff8a2a(void);
extern Uint16 LOAD_W_ff8a2c(void);
extern Uint32 LOAD_L_ff8a32(void);
extern Uint16 LOAD_W_ff8a36(void);
extern Uint16 LOAD_W_ff8a38(void);
extern Sint8 LOAD_B_ff8a3a(void);
extern Sint8 LOAD_B_ff8a3b(void);
extern Sint8 LOAD_B_ff8a3c(void);
extern Sint8 LOAD_B_ff8a3d(void);

extern void STORE_W_ff8a28(Uint16 v);
extern void STORE_W_ff8a2a(Uint16 v);
extern void STORE_W_ff8a2c(Uint16 v);
extern void STORE_L_ff8a32(Uint32 v);
extern void STORE_W_ff8a36(Uint16 v);
extern void STORE_W_ff8a38(Uint16 v);
extern void STORE_B_ff8a3a(Sint8 v);
extern void STORE_B_ff8a3b(Sint8 v);
extern void STORE_B_ff8a3c(Sint8 v);
extern void STORE_B_ff8a3d(Sint8 v);

#endif /* BLITTER_H */
