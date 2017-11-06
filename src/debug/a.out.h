/* a.out.h - Definitions and declarations for GNU-style a.out
   binaries.
   Written by Guido Flohr (gufl0000@stud.uni-sb.de).

   This file is in the public domain. */

#ifndef __A_OUT_GNU_H__
#define __A_OUT_GNU_H__ 1

struct nlist {
  union {
    const char *n_name;     /* in memory address */
    struct nlist *n_next;
    size_t n_strx;          /* string table offset */
  } n_un;
  unsigned char n_type;
  char n_other;
  short n_desc;
  uint32_t n_value;
};

/* sizeof(struct nlist) on disk */
#define SIZEOF_STRUCT_NLIST 12


#define N_UNDF   0x00       /* undefined */
#define N_ABS    0x02       /* absolute */
#define N_TEXT   0x04       /* text */
#define N_DATA   0x06       /* data */
#define N_BSS    0x08       /* bss */
#define N_SIZE   0x0c       /* pseudo type, defines a symbol's size */
#define N_FN     0x1f       /* File name of a .o file */
#define N_COMM   0x12       /* common (internal to ld) */

#define N_EXT    0x01       /* external bit, or'ed in */
#define N_TYPE   0x1e       /* mask for all the type bits */
#define N_STAB   0xe0       /* if any of these bits set, don't discard */

/* The following type indicates the definition of a symbol as being
   an indirect reference to another symbol.  The other symbol
   appears as an undefined reference, immediately following this symbol.

   Indirection is asymmetrical.  The other symbol's value will be used
   to satisfy requests for the indirect symbol, but not vice versa.
   If the other symbol does not have a definition, libraries will
   be searched to find a definition. */
#define N_INDR 0x0a

/* The following symbols refer to set elements.
   All the N_SET[ATDB] symbols with the same name form one set.
   Space is allocated for the set in the text section, and each set
   element's value is stored into one word of the space.
   The first word of the space is the length of the set (number of elements).

   The address of the set is made into an N_SETV symbol
   whose name is the same as the name of the set.
   This symbol acts like a N_DATA global symbol
   in that it can satisfy undefined external references. */

/* These appear as input to LD, in a .o file. */
#define N_SETA  0x14        /* Absolute set element symbol */
#define N_SETT  0x16        /* Text set element symbol */
#define N_SETD  0x18        /* Data set element symbol */
#define N_SETB  0x1A        /* Bss set element symbol */

/* This is output from LD. */
#define N_SETV  0x1C        /* Pointer to set vector in data area. */

/* Warning symbol. The text gives a warning message, the next symbol
   in the table will be undefined. When the symbol is referenced, the
   message is printed. */

#define N_WARNING 0x1e

/* Weak symbols.  These are a GNU extension to the a.out format.  The
   semantics are those of ELF weak symbols.  Weak symbols are always
   externally visible.  The N_WEAK? values are squeezed into the
   available slots.  The value of a N_WEAKU symbol is 0.  The values
   of the other types are the definitions. */
#define N_WEAKU 0x0d        /* Weak undefined symbol. */
#define N_WEAKA 0x0e        /* Weak absolute symbol. */
#define N_WEAKT 0x0f        /* Weak text symbol. */
#define N_WEAKD 0x10        /* Weak data symbol. */
#define N_WEAKB 0x11        /* Weak bss symbol. */

#endif /* __A_OUT_GNU_H__ */
