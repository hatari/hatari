
#ifndef CPUMMU030_H
#define CPUMMU030_H

#include "mmu_common.h"

void mmu_op30_pmove (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra);
void mmu_op30_ptest (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra);
void mmu_op30_pload (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra);
void mmu_op30_pflush (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra);

uae_u32 mmu_op30_helper_get_fc(uae_u16 next);

void mmu030_ptest_atc_search(uaecptr logical_addr, uae_u32 function_code, bool write);
uae_u32 mmu030_ptest_table_search(uaecptr addr, bool super, bool data, bool write, int level);


typedef struct {
    uae_u32 tt_addrmask;
    uae_u32 tt_fcmask;
} TT_info;

extern TT_info mmu030_decode_tt(uae_u32 TT);
void mmu030_decode_tc(uae_u32 TC);
void mmu030_decode_rp(uae_u64 RP);

uae_u8 mmu030_get_fc(bool super, bool data);

void mmu030_create_atc_entry(uaecptr addr, bool super, bool data, bool write);
int mmu030_logical_is_in_atc(uaecptr addr, bool super, bool data, bool write);
void mmu030_atc_handle_history_bit(int entry_num);

void mmu030_put_long_atc(uaecptr addr, uae_u32 val, int l, bool super, bool data);
void mmu030_put_word_atc(uaecptr addr, uae_u16 val, int l, bool super, bool data);
void mmu030_put_byte_atc(uaecptr addr, uae_u8 val, int l, bool super, bool data);
uae_u32 mmu030_get_long_atc(uaecptr addr, int l, bool super, bool data);
uae_u16 mmu030_get_word_atc(uaecptr addr, int l, bool super, bool data);
uae_u8 mmu030_get_byte_atc(uaecptr addr, int l, bool super, bool data);

void mmu030_flush_atc_fc(uae_u8 function_code);
void mmu030_flush_atc_page(uaecptr logical_addr);
void mmu030_flush_atc_page_fc(uaecptr logical_addr, uae_u8 function_code);
void mmu030_flush_atc_all(void);

int mmu030_match_ttr(uaecptr addr, bool super, bool data, bool write);
int mmu030_do_match_ttr(uae_u32 tt, TT_info masks, uaecptr addr, bool super, bool data, bool write);

void mmu030_put_long(uaecptr addr, uae_u32 val, bool data, int size);
void mmu030_put_word(uaecptr addr, uae_u16 val, bool data, int size);
void mmu030_put_byte(uaecptr addr, uae_u8  val, bool data, int size);
uae_u32 mmu030_get_long(uaecptr addr, bool data, int size);
uae_u16 mmu030_get_word(uaecptr addr, bool data, int size);
uae_u8  mmu030_get_byte(uaecptr addr, bool data, int size);


extern uae_u32 REGPARAM3 sfc030_get_long(uaecptr addr) REGPARAM;
extern uae_u16 REGPARAM3 sfc030_get_word(uaecptr addr) REGPARAM;
extern uae_u8 REGPARAM3 sfc030_get_byte(uaecptr addr) REGPARAM;
extern void REGPARAM3 dfc030_put_long(uaecptr addr, uae_u32 val) REGPARAM;
extern void REGPARAM3 dfc030_put_word(uaecptr addr, uae_u16 val) REGPARAM;
extern void REGPARAM3 dfc030_put_byte(uaecptr addr, uae_u8 val) REGPARAM;

extern uae_u16 REGPARAM3 mmu030_get_word_unaligned(uaecptr addr, bool data) REGPARAM;
extern uae_u32 REGPARAM3 mmu030_get_long_unaligned(uaecptr addr, bool data) REGPARAM;
extern void REGPARAM3 mmu030_put_word_unaligned(uaecptr addr, uae_u16 val, bool data) REGPARAM;
extern void REGPARAM3 mmu030_put_long_unaligned(uaecptr addr, uae_u32 val, bool data) REGPARAM;

static ALWAYS_INLINE uae_u32 uae_mmu030_get_ilong(uaecptr addr)
{
	if (unlikely(is_unaligned(addr, 4)))
		return mmu030_get_long_unaligned(addr, false);
	return mmu030_get_long(addr, false, sz_long);
}
static ALWAYS_INLINE uae_u16 uae_mmu030_get_iword(uaecptr addr)
{
	if (unlikely(is_unaligned(addr, 2)))
		return mmu030_get_word_unaligned(addr, false);
	return mmu030_get_word(addr, false, sz_word);
}
static ALWAYS_INLINE uae_u16 uae_mmu030_get_ibyte(uaecptr addr)
{
	return mmu030_get_byte(addr, false, sz_byte);
}
static ALWAYS_INLINE uae_u32 uae_mmu030_get_long(uaecptr addr)
{
	if (unlikely(is_unaligned(addr, 4)))
		return mmu030_get_long_unaligned(addr, true);
	return mmu030_get_long(addr, true, sz_long);
}
static ALWAYS_INLINE uae_u16 uae_mmu030_get_word(uaecptr addr)
{
	if (unlikely(is_unaligned(addr, 2)))
		return mmu030_get_word_unaligned(addr, true);
	return mmu030_get_word(addr, true, sz_word);
}
static ALWAYS_INLINE uae_u8 uae_mmu030_get_byte(uaecptr addr)
{
	return mmu030_get_byte(addr, true, sz_byte);
}
static ALWAYS_INLINE void uae_mmu030_put_long(uaecptr addr, uae_u32 val)
{
	if (unlikely(is_unaligned(addr, 4)))
		mmu030_put_long_unaligned(addr, val, true);
	else
		mmu030_put_long(addr, val, true, sz_long);
}
static ALWAYS_INLINE void uae_mmu030_put_word(uaecptr addr, uae_u16 val)
{
	if (unlikely(is_unaligned(addr, 2)))
		mmu030_put_word_unaligned(addr, val, true);
	else
		mmu030_put_word(addr, val, true, sz_word);
}
static ALWAYS_INLINE void uae_mmu030_put_byte(uaecptr addr, uae_u8 val)
{
	mmu030_put_byte(addr, val, true, sz_byte);
}

STATIC_INLINE void put_byte_mmu030 (uaecptr addr, uae_u32 v)
{
    uae_mmu030_put_byte (addr, v);
}
STATIC_INLINE void put_word_mmu030 (uaecptr addr, uae_u32 v)
{
    uae_mmu030_put_word (addr, v);
}
STATIC_INLINE void put_long_mmu030 (uaecptr addr, uae_u32 v)
{
    uae_mmu030_put_long (addr, v);
}
STATIC_INLINE uae_u32 get_byte_mmu030 (uaecptr addr)
{
    return uae_mmu030_get_byte (addr);
}
STATIC_INLINE uae_u32 get_word_mmu030 (uaecptr addr)
{
    return uae_mmu030_get_word (addr);
}
STATIC_INLINE uae_u32 get_long_mmu030 (uaecptr addr)
{
    return uae_mmu030_get_long (addr);
}
STATIC_INLINE uae_u32 get_ibyte_mmu030 (int o)
{
    uae_u32 pc = m68k_getpc () + o;
    return uae_mmu030_get_iword (pc);
}
STATIC_INLINE uae_u32 get_iword_mmu030 (int o)
{
    uae_u32 pc = m68k_getpc () + o;
    return uae_mmu030_get_iword (pc);
}
STATIC_INLINE uae_u32 get_ilong_mmu030 (int o)
{
    uae_u32 pc = m68k_getpc () + o;
    return uae_mmu030_get_ilong (pc);
}
STATIC_INLINE uae_u32 next_iword_mmu030 (void)
{
    uae_u32 pc = m68k_getpc ();
    m68k_incpci (2);
    return uae_mmu030_get_iword (pc);
}
STATIC_INLINE uae_u32 next_ilong_mmu030 (void)
{
    uae_u32 pc = m68k_getpc ();
    m68k_incpci (4);
    return uae_mmu030_get_ilong (pc);
}

extern void m68k_do_rts_mmu030 (void);
extern void m68k_do_rte_mmu030 (uaecptr a7);
extern void flush_mmu030 (uaecptr, int);
extern void m68k_do_bsr_mmu030 (uaecptr oldpc, uae_s32 offset);

#endif
