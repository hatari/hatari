
#ifndef CPUMMU030_H
#define CPUMMU030_H

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

#endif
