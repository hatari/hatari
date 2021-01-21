/*
 Linux/m68k OS loader

 Copyright 1992 by Greg Harp (bootinfo definitions)
 ARAnyM (C) 2005-2008 Patrice Mandin
 ARAnyM (C) 2014 Andreas Schwab
 Adaption from ARAnyM (bootos_linux.cpp) to Hatari (C) 2019 Eero Tamminen

 This file is distributed under the GNU General Public License, version 2
 or at your option any later version. Read the file gpl.txt for details.
*/

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "lilo.h"
#include "log.h"
#include "tos.h"	/* TosAddress */
#include "stMemory.h"	/* STRam etc */
#include "symbols.h"
#include <stdint.h>
#include <SDL_endian.h>

bool bUseLilo;

#define LILO_DEBUG 1
#if LILO_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif

/*--- Rip from elf.h ---*/

/* Type for a 16-bit quantity.  */
typedef uint16_t Elf32_Half;

/* Types for signed and unsigned 32-bit quantities.  */
typedef uint32_t Elf32_Word;
typedef	int32_t  Elf32_Sword;

/* Types for signed and unsigned 64-bit quantities.  */
typedef uint64_t Elf32_Xword;
typedef	int64_t  Elf32_Sxword;

/* Type of addresses.  */
typedef uint32_t Elf32_Addr;

/* Type of file offsets.  */
typedef uint32_t Elf32_Off;

/* Type for section indices, which are 16-bit quantities.  */
typedef uint16_t Elf32_Section;

/* Type for version symbol information.  */
typedef Elf32_Half Elf32_Versym;


/* The ELF file header.  This appears at the start of every ELF file.  */

#define EI_NIDENT (16)

typedef struct
{
  unsigned char	e_ident[EI_NIDENT];	/* Magic number and other info */
  Elf32_Half	e_type;			/* Object file type */
  Elf32_Half	e_machine;		/* Architecture */
  Elf32_Word	e_version;		/* Object file version */
  Elf32_Addr	e_entry;		/* Entry point virtual address */
  Elf32_Off	e_phoff;		/* Program header table file offset */
  Elf32_Off	e_shoff;		/* Section header table file offset */
  Elf32_Word	e_flags;		/* Processor-specific flags */
  Elf32_Half	e_ehsize;		/* ELF header size in bytes */
  Elf32_Half	e_phentsize;		/* Program header table entry size */
  Elf32_Half	e_phnum;		/* Program header table entry count */
  Elf32_Half	e_shentsize;		/* Section header table entry size */
  Elf32_Half	e_shnum;		/* Section header table entry count */
  Elf32_Half	e_shstrndx;		/* Section header string table index */
} Elf32_Ehdr;

/* Program segment header.  */

typedef struct
{
  Elf32_Word	p_type;			/* Segment type */
  Elf32_Off	p_offset;		/* Segment file offset */
  Elf32_Addr	p_vaddr;		/* Segment virtual address */
  Elf32_Addr	p_paddr;		/* Segment physical address */
  Elf32_Word	p_filesz;		/* Segment size in file */
  Elf32_Word	p_memsz;		/* Segment size in memory */
  Elf32_Word	p_flags;		/* Segment flags */
  Elf32_Word	p_align;		/* Segment alignment */
} Elf32_Phdr;

#define EI_MAG0		0		/* File identification byte 0 index */
#define	ELFMAG		"\177ELF"
#define	SELFMAG		4
#define ET_EXEC		2		/* Executable file */
#define EM_68K		 4		/* Motorola m68k family */
#define EV_CURRENT	1		/* Current version */

/*
 * Tag Definitions
 *
 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/arch/m68k/include/uapi/asm/bootinfo.h
 *
 * Machine independent tags start counting from 0x0000
 * Machine dependent tags start counting from 0x8000
 */

struct bi_record {
    uint16_t tag;			/* tag ID */
    uint16_t size;			/* size of record (in bytes) */
    uint32_t data[0];			/* data */
};

#define BI_LAST			0x0000	/* last record (sentinel) */
#define BI_MACHTYPE		0x0001	/* machine type (u_long) */
#define BI_CPUTYPE		0x0002	/* cpu type (u_long) */
#define BI_FPUTYPE		0x0003	/* fpu type (u_long) */
#define BI_MMUTYPE		0x0004	/* mmu type (u_long) */
#define BI_MEMCHUNK		0x0005	/* memory chunk address and size */
					/* (struct mem_info) */
#define BI_RAMDISK		0x0006	/* ramdisk address and size */
					/* (struct mem_info) */
#define BI_COMMAND_LINE		0x0007	/* kernel command line parameters */
					/* (string) */
/*
 * Linux/m68k Architectures (BI_MACHTYPE)
 */

#define MACH_ATARI    2

/*
 *  CPU, FPU and MMU types (BI_CPUTYPE, BI_FPUTYPE, BI_MMUTYPE)
 *
 *  Note: we may rely on the following equalities:
 *
 *      CPU_68020 == MMU_68851
 *      CPU_68030 == MMU_68030
 *      CPU_68040 == FPU_68040 == MMU_68040
 *      CPU_68060 == FPU_68060 == MMU_68060
 */

#define CPUB_68020	0
#define CPUB_68030	1
#define CPUB_68040	2
#define CPUB_68060	3

#define BI_CPU_68020	(1 << CPUB_68020)
#define BI_CPU_68030	(1 << CPUB_68030)
#define BI_CPU_68040	(1 << CPUB_68040)
#define BI_CPU_68060	(1 << CPUB_68060)

#define FPUB_68881	0
#define FPUB_68882	1
#define FPUB_68040	2	/* Internal FPU */
#define FPUB_68060	3	/* Internal FPU */

#define BI_FPU_68881	(1 << FPUB_68881)
#define BI_FPU_68882	(1 << FPUB_68882)
#define BI_FPU_68040	(1 << FPUB_68040)
#define BI_FPU_68060	(1 << FPUB_68060)

#define MMUB_68851	0
#define MMUB_68030	1	/* Internal MMU */
#define MMUB_68040	2	/* Internal MMU */
#define MMUB_68060	3	/* Internal MMU */

#define BI_MMU_68851	(1 << MMUB_68851)
#define BI_MMU_68030	(1 << MMUB_68030)
#define BI_MMU_68040	(1 << MMUB_68040)
#define BI_MMU_68060	(1 << MMUB_68060)

/*
 * Stuff for bootinfo interface versioning
 *
 * At the start of kernel code, a 'struct bootversion' is located.
 */
#define BOOTINFOV_MAGIC			0x4249561A	/* 'BIV^Z' */
#define MK_BI_VERSION(major,minor)	(((major)<<16)+(minor))
#define BI_VERSION_MAJOR(v)		(((v) >> 16) & 0xffff)
#define BI_VERSION_MINOR(v)		((v) & 0xffff)

/*
 * Atari-specific tags
 *
 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/arch/m68k/include/uapi/asm/bootinfo-atari.h
 */

#define ATARI_BOOTI_VERSION		MK_BI_VERSION(2, 1)

/* (values are ATARI_MACH_* defines) */
#define BI_ATARI_MCH_COOKIE	0x8000	/* _MCH cookie from TOS (u_long) */
#define BI_ATARI_MCH_TYPE	0x8001	/* special machine type (u_long) */

/* mch_cookie values (upper word) */
#define ATARI_MCH_ST		0
#define ATARI_MCH_STE		1
#define ATARI_MCH_TT		2
#define ATARI_MCH_FALCON	3

/* mch_type values */
#define ATARI_MACH_NORMAL	0	/* no special machine type */
#define ATARI_MACH_MEDUSA	1	/* Medusa 040 */
#define ATARI_MACH_HADES	2	/* Hades 040 or 060 */
#define ATARI_MACH_AB40		3	/* Afterburner040 on Falcon */

/*--- Other defines ---*/

#define NUM_MEMINFO  4
#define CL_SIZE      (256)
#undef PAGE_SIZE
#define PAGE_SIZE 4096

#define M68K_EMUL_RESET 0x7102

/* Start address of kernel in Atari RAM */
#define KERNEL_START		PAGE_SIZE
/* Offset to start of fs in ramdisk file (no microcode on Atari) */
#define RAMDISK_FS_START	0

#define MAX_BI_SIZE     (4096)

#define GRANULARITY (256*1024) /* min unit for memory */

/*--- Structures ---*/

static union {
	struct bi_record record;
	unsigned char fake[MAX_BI_SIZE];
} bi_union;

struct mem_info {
	uint32_t addr;		/* physical address of memory chunk */
	uint32_t size;		/* length of memory chunk (in bytes) */
};

struct atari_bootinfo {
	uint32_t machtype;		/* machine type */
	uint32_t cputype;		/* system CPU */
	uint32_t fputype;		/* system FPU */
	uint32_t mmutype;		/* system MMU */
	int32_t num_memory;		/* # of memory blocks found */
	 /* memory description */
	struct mem_info memory[NUM_MEMINFO];
	/* ramdisk description */
	struct mem_info ramdisk;
	/* kernel command line parameters */
	char command_line[CL_SIZE];
	uint32_t mch_cookie;		/* _MCH cookie from TOS */
	uint32_t mch_type;		/* special machine types */
};

static struct atari_bootinfo bi;
static uint32_t bi_size;

static bool lilo_load(void);
static void *load_file(const char *filename, uint32_t *length);
static bool check_kernel(void *kernel, Elf32_Addr *offset,
			 void *ramdisk, uint32_t ramdisk_len);
static bool create_bootinfo(void);
static bool set_machine_type(void);
static bool add_bi_record(uint16_t tag, uint16_t size, const void *data);
static bool add_bi_string(uint16_t tag, const char *s);


/*	Linux/m68k loader */

bool lilo_init(void)
{
	uint8_t *ROMBaseHost = STRam + TosAddress;

	if (!ConfigureParams.System.bMMU || ConfigureParams.Memory.STRamSize_KB < 8*1024) {
		Log_AlertDlg(LOG_FATAL, "Linux requires MMU and at least 8MB of RAM!");
		return false;
	}
	/* RESET + Linux/m68k boot */
	ROMBaseHost[0x0000] = 0x4e;		/* reset */
	ROMBaseHost[0x0001] = 0x70;
	ROMBaseHost[0x0002] = 0x4e;		/* jmp <abs.addr> */
	ROMBaseHost[0x0003] = 0xf9;

	/* TODO: ROM + 0x30 is Linux reset address on AB40, 0x4 on Falcon/TT */
#if 0
	if (!(ConfigureParams.Log.bNatFeats && ConfigureParams.Lilo.bHaltOnReboot)) {
		/* set up a minimal OS for successful Linux/m68k reboot */
		ROMBaseHost[0x0030] = 0x46;		/* move.w #$2700,sr */
		ROMBaseHost[0x0031] = 0xfc;
		ROMBaseHost[0x0032] = 0x27;
		ROMBaseHost[0x0033] = 0x00;
		ROMBaseHost[0x0034] = 0x4e;		/* reset */
		ROMBaseHost[0x0035] = 0x70;
		ROMBaseHost[0x0036] = M68K_EMUL_RESET >> 8;
		ROMBaseHost[0x0037] = M68K_EMUL_RESET & 0xff;
	} else {
		/* quit Hatari with NatFeats when Linux/m68k tries to reboot */
		ROMBaseHost[0x0030] = 0x48;		/* pea.l NF_SHUTDOWN(pc) */
		ROMBaseHost[0x0031] = 0x7a;
		ROMBaseHost[0x0032] = 0x00;
		ROMBaseHost[0x0033] = 0x0c;
		ROMBaseHost[0x0034] = 0x59;		/* subq.l #4,sp */
		ROMBaseHost[0x0035] = 0x8f;
		ROMBaseHost[0x0036] = 0x73;		/* NF_ID */
		ROMBaseHost[0x0037] = 0x00;
		ROMBaseHost[0x0038] = 0x2f;		/* move.l d0,-(sp) */
		ROMBaseHost[0x0039] = 0x00;
		ROMBaseHost[0x003a] = 0x59;		/* subq.l #4,sp */
		ROMBaseHost[0x003b] = 0x8f;
		ROMBaseHost[0x003c] = 0x73;		/* NF_CALL */
		ROMBaseHost[0x003d] = 0x01;
		ROMBaseHost[0x003e] = 'N';		/* "NF_SHUTDOWN" */
		ROMBaseHost[0x003f] = 'F';
		ROMBaseHost[0x0040] = '_';
		ROMBaseHost[0x0041] = 'S';
		ROMBaseHost[0x0042] = 'H';
		ROMBaseHost[0x0043] = 'U';
		ROMBaseHost[0x0044] = 'T';
		ROMBaseHost[0x0045] = 'D';
		ROMBaseHost[0x0046] = 'O';
		ROMBaseHost[0x0047] = 'W';
		ROMBaseHost[0x0048] = 'N';
		ROMBaseHost[0x0049] = 0;
	}
#endif
	return lilo_load();
}

/*--- Private functions ---*/

static bool lilo_load(void)
{
	const char *kernel_s  = ConfigureParams.Lilo.szKernelFileName;
	const char *ramdisk_s = ConfigureParams.Lilo.szRamdiskFileName;

	char *symbols_s = ConfigureParams.Lilo.szKernelSymbols;
	Elf32_Addr kernel_offset;
	bool loaded;

	void *kernel, *ramdisk = NULL;
	uint32_t kernel_length = 0;
	uint32_t ramdisk_length = 0;

	/* Load the kernel */
	kernel = load_file(kernel_s, &kernel_length);
	if (!kernel) {
		Log_AlertDlg(LOG_FATAL, "LILO: error loading Linux kernel:\n'%s'", kernel_s);
		return false;
	}

	/* Load the ramdisk */
	if (strlen(ramdisk_s) > 0) {
		ramdisk = load_file(ramdisk_s, &ramdisk_length);
		if (!ramdisk) {
			Log_AlertDlg(LOG_ERROR, "LILO: error loading ramdisk:\n'%s'", ramdisk_s);
		}
	}

	/* Check the kernel */
	loaded = check_kernel(kernel, &kernel_offset, ramdisk, ramdisk_length);

	/* Kernel and ramdisk copied in Atari RAM, we can free them */
	if (ramdisk != NULL) {
		free(ramdisk);
	}
	free(kernel);

	if (loaded) {
		if (strlen(symbols_s) > 0) {
			char offstr[12];
			static char symstr[] = "symbols";
			char *cmd[] = { symstr, symbols_s, offstr, NULL };
			sprintf(offstr, "0x%x", kernel_offset);
			Symbols_Command(3, cmd);
		}
	} else {
		Log_AlertDlg(LOG_FATAL, "LILO: error setting up kernel!");
	}
	return true;
}

static void *load_file(const char *filename, uint32_t *length)
{
	void *buffer = NULL;
	long nFileLength = 0;

	if (strlen(filename) == 0) {
		Dprintf(("LILO: empty filename\n"));
		return NULL;
	}

#ifdef HAVE_LIBZ
	buffer = File_ZlibRead(filename, &nFileLength);
#else
	buffer = File_ReadAsIs(filename, &nFileLength);
#endif
	*length = nFileLength;

	if (buffer) {
		Dprintf(("LILO: (uncompressed) '%s' size: %d bytes\n",
			 filename, *length));
	}
	return buffer;
}

/**
 * Add bootinfo chunk
 */
static void add_chunk(uint32_t start, uint32_t size)
{
	size = (size) & ~(GRANULARITY-1);
	if (size > 0) {
		bi.memory[bi.num_memory].addr = SDL_SwapBE32(start);
		bi.memory[bi.num_memory].size = SDL_SwapBE32(size);
		bi.num_memory++;
	}
}

/**
 * Load given kernel code and ramdisk to suitable memory area,
 * and update bootinfo accordingly.
 * Return true for success
 */
static bool check_kernel(void *kernel, Elf32_Addr *kernel_offset,
			 void *ramdisk, uint32_t ramdisk_len)
{
	/* map Hatari variables to Aranym code */
	const uint32_t RAMSize = 1024 * ConfigureParams.Memory.STRamSize_KB;
	uint8_t *hostkbase, *RAMBaseHost = STRam;

	const uint32_t FastRAMBase = 0x01000000;
	uint8_t *FastRAMBaseHost = TTmemory;

	/* TODO: separate FastRAM setting for kernel & ramdisk? */
	const uint32_t FastRAMSize = TTmemory ? 1024 * ConfigureParams.Memory.TTRamSize_KB : 0;
	bool kernel_to_fastram = (ConfigureParams.Lilo.bKernelToFastRam && FastRAMSize > 0);
	bool ramdisk_to_fastram = (ConfigureParams.Lilo.bRamdiskToFastRam && FastRAMSize > 0);

	Elf32_Ehdr *kexec_elf;	/* header of kernel executable */
	Elf32_Phdr *kernel_phdrs;
	Elf32_Addr min_addr = 0xffffffff, max_addr = 0;
	Elf32_Addr kernel_size;
	Elf32_Addr mem_ptr;
	const char *kname, *kernel_name = "vmlinux";
	uint32_t *tmp;
	int i;

	bi_size = 0;
	bi.ramdisk.addr = 0;
	bi.ramdisk.size = 0;

	if (!set_machine_type()) {
		return false;
	}

	kexec_elf = (Elf32_Ehdr *) kernel;
	if (memcmp(&kexec_elf->e_ident[EI_MAG0], ELFMAG, SELFMAG) != 0 ||
	    SDL_SwapBE16(kexec_elf->e_type) != ET_EXEC ||
	    SDL_SwapBE16(kexec_elf->e_machine) != EM_68K ||
	    SDL_SwapBE32(kexec_elf->e_version) != EV_CURRENT) {
		fprintf(stderr, "LILO: Invalid ELF header contents in kernel\n");
		return false;
	}

	/*--- Copy the kernel at start of RAM ---*/

	/* Load the program headers */
	kernel_phdrs = (Elf32_Phdr *) (((char *) kexec_elf) + SDL_SwapBE32(kexec_elf->e_phoff));

	/* calculate the total required amount of memory */
	Dprintf(("LILO: kexec_elf->e_phnum = 0x%08x\n", SDL_SwapBE16(kexec_elf->e_phnum)));

	for (i = 0; i < SDL_SwapBE16(kexec_elf->e_phnum); i++) {
		Dprintf(("LILO: kernel_phdrs[%d].p_vaddr  = 0x%08x\n", i, SDL_SwapBE32(kernel_phdrs[i].p_vaddr)));
		Dprintf(("LILO: kernel_phdrs[%d].p_offset = 0x%08x\n", i, SDL_SwapBE32(kernel_phdrs[i].p_offset)));
		Dprintf(("LILO: kernel_phdrs[%d].p_filesz = 0x%08x\n", i, SDL_SwapBE32(kernel_phdrs[i].p_filesz)));
		Dprintf(("LILO: kernel_phdrs[%d].p_memsz  = 0x%08x\n", i, SDL_SwapBE32(kernel_phdrs[i].p_memsz)));

		if (min_addr > SDL_SwapBE32(kernel_phdrs[i].p_vaddr)) {
			min_addr = SDL_SwapBE32(kernel_phdrs[i].p_vaddr);
		}
		if (max_addr < SDL_SwapBE32(kernel_phdrs[i].p_vaddr) + SDL_SwapBE32(kernel_phdrs[i].p_memsz)) {
			max_addr = SDL_SwapBE32(kernel_phdrs[i].p_vaddr) + SDL_SwapBE32(kernel_phdrs[i].p_memsz);
		}
	}

	/* This is needed for newer linkers that include the header
	 * in the first segment.
	 */
	Dprintf(("LILO: min_addr = 0x%08x\n", min_addr));
	Dprintf(("LILO: max_addr = 0x%08x\n", max_addr));

	if (min_addr == 0) {
		Dprintf(("LILO: new linker:\n"));
		Dprintf(("LILO:  kernel_phdrs[0].p_vaddr  = 0x%08x\n", SDL_SwapBE32(kernel_phdrs[0].p_vaddr)));
		Dprintf(("LILO:  kernel_phdrs[0].p_offset = 0x%08x\n", SDL_SwapBE32(kernel_phdrs[0].p_offset)));
		Dprintf(("LILO:  kernel_phdrs[0].p_filesz = 0x%08x\n", SDL_SwapBE32(kernel_phdrs[0].p_filesz)));
		Dprintf(("LILO:  kernel_phdrs[0].p_memsz  = 0x%08x\n", SDL_SwapBE32(kernel_phdrs[0].p_memsz)));

		min_addr = PAGE_SIZE;
		/*kernel_phdrs[0].p_vaddr += PAGE_SIZE;*/
		kernel_phdrs[0].p_vaddr = SDL_SwapBE32(SDL_SwapBE32(kernel_phdrs[0].p_vaddr) + PAGE_SIZE);
		/*kernel_phdrs[0].p_offset += PAGE_SIZE;*/
		kernel_phdrs[0].p_offset = SDL_SwapBE32(SDL_SwapBE32(kernel_phdrs[0].p_offset) + PAGE_SIZE);
		/*kernel_phdrs[0].p_filesz -= PAGE_SIZE;*/
		kernel_phdrs[0].p_filesz = SDL_SwapBE32(SDL_SwapBE32(kernel_phdrs[0].p_filesz) - PAGE_SIZE);
		/*kernel_phdrs[0].p_memsz -= PAGE_SIZE;*/
		kernel_phdrs[0].p_memsz = SDL_SwapBE32(SDL_SwapBE32(kernel_phdrs[0].p_memsz) - PAGE_SIZE);

		Dprintf(("LILO: modified to:\n"));
		Dprintf(("LILO:  kernel_phdrs[0].p_vaddr  = 0x%08x\n", SDL_SwapBE32(kernel_phdrs[0].p_vaddr)));
		Dprintf(("LILO:  kernel_phdrs[0].p_offset = 0x%08x\n", SDL_SwapBE32(kernel_phdrs[0].p_offset)));
		Dprintf(("LILO:  kernel_phdrs[0].p_filesz = 0x%08x\n", SDL_SwapBE32(kernel_phdrs[0].p_filesz)));
		Dprintf(("LILO:  kernel_phdrs[0].p_memsz  = 0x%08x\n", SDL_SwapBE32(kernel_phdrs[0].p_memsz)));
	}
	kernel_size = max_addr - min_addr;
	Dprintf(("LILO: kernel_size = %u\n", kernel_size));
	Dprintf(("LILO: %d kB ST-RAM, %d kB TT-RAM\n",
		 ConfigureParams.Memory.STRamSize_KB,
		 ConfigureParams.Memory.TTRamSize_KB));

	if (kernel_to_fastram) {
		if (KERNEL_START + kernel_size > FastRAMSize) {
			fprintf(stderr, "LILO: kernel of size %x does not fit in TT-RAM of size %x\n", kernel_size, FastRAMSize);
			kernel_to_fastram = false;
		}
	}
	if (!kernel_to_fastram) {
		if (KERNEL_START + kernel_size > RAMSize) {
			fprintf(stderr, "LILO: kernel of size %x does not fit in RAM of size %x\n", kernel_size, RAMSize);
			return false;
		}
	}
	if (kernel_to_fastram) {
		*kernel_offset = FastRAMBase;
		hostkbase = FastRAMBaseHost;
	} else {
		*kernel_offset = 0;
		hostkbase = RAMBaseHost;
	}

	mem_ptr = KERNEL_START;
	int segments = SDL_SwapBE16(kexec_elf->e_phnum);
	Dprintf(("LILO: copying %d segments to %s...\n", segments,
		 kernel_to_fastram ? "FastRAM" : "ST-RAM"));
	for (i = 0; i < segments; i++) {
		Elf32_Word segment_length;
		Elf32_Addr segment_ptr;
		Elf32_Off segment_offset;

		segment_offset = SDL_SwapBE32(kernel_phdrs[i].p_offset);
		segment_length = SDL_SwapBE32(kernel_phdrs[i].p_filesz);

		if (segment_offset == 0xffffffffu) {
			fprintf(stderr, "LILO: Failed to seek to segment %d\n", i);
			return false;
		}
		segment_ptr = SDL_SwapBE32(kernel_phdrs[i].p_vaddr) - PAGE_SIZE;

		memcpy(hostkbase + mem_ptr + segment_ptr,
		       (char *) kexec_elf + segment_offset, segment_length);

		Dprintf(("LILO: Copied segment %d: 0x%08x + 0x%08x to 0x%08x\n",
			 i, segment_offset, segment_length, *kernel_offset + mem_ptr + segment_ptr));
	}

	/*--- Copy the ramdisk after kernel (and reserved bootinfo) ---*/
	if (ramdisk && ramdisk_len) {
		Elf32_Addr rd_start;
		Elf32_Word rd_len;
		Elf32_Off rd_offset;
		const char *to_ram_s;

		if (kernel_to_fastram && ramdisk_to_fastram) {
			rd_offset = KERNEL_START + kernel_size + MAX_BI_SIZE;
		} else {
			rd_offset = 0;
		}
		rd_len = ramdisk_len - RAMDISK_FS_START;
		if (ramdisk_to_fastram && FastRAMSize > rd_offset + rd_len) {
			/* Load at end of FastRAM */
			rd_start = FastRAMBase + FastRAMSize - rd_len;
			memcpy(FastRAMBaseHost + rd_start - FastRAMBase, (unsigned char *)ramdisk + RAMDISK_FS_START, rd_len);
			to_ram_s = "FastRAM";
		} else {
			/* Load at end of ST-RAM */
			if (kernel_to_fastram) {
				rd_offset = PAGE_SIZE;
			} else {
				rd_offset = KERNEL_START + kernel_size + MAX_BI_SIZE;
			}
			if (RAMSize < rd_offset + rd_len) {
				Log_AlertDlg(LOG_FATAL, "LILO: not enough memory to load ramdisk of size %u\n", rd_len);
				return false;
			}
			rd_start = RAMSize - rd_len;
			memcpy(RAMBaseHost + rd_start, ((unsigned char *)ramdisk) + RAMDISK_FS_START, rd_len);
			to_ram_s = "ST-RAM";
		}
		bi.ramdisk.addr = SDL_SwapBE32(rd_start);
		bi.ramdisk.size = SDL_SwapBE32(rd_len);
		Dprintf(("lilo: Ramdisk at 0x%08x in %s, length=0x%08x\n",
			 rd_start, to_ram_s, rd_len));
	} else {
		bi.ramdisk.addr = 0;
		bi.ramdisk.size = 0;
		Dprintf(("LILO: No ramdisk\n"));
	}

	/*--- Create the bootinfo structure ---*/

	/* Command line */
	kname = kernel_name;
	if (strncmp(kernel_name, "local:", 6) == 0) {
		kname += 6;
	}
	if (strlen(ConfigureParams.Lilo.szCommandLine) > CL_SIZE-1) {
		Log_AlertDlg(LOG_FATAL, "LILO: kernel command line too long\n(max %d chars)\n", CL_SIZE-1);
		return false;
	}
	strcpy(bi.command_line, ConfigureParams.Lilo.szCommandLine);
	if (strlen(bi.command_line) + 1 + strlen(kname) + 12 < CL_SIZE-1) {
		if (*bi.command_line) {
			strcat(bi.command_line, " ");
		}
		strcat(bi.command_line, "BOOT_IMAGE=");
		strcat(bi.command_line, kname);
	} else {
		fprintf(stderr, "LILO: kernel command line too long to include kernel name\n");
	}

	Dprintf(("LILO: config_file command line: %s\n", ConfigureParams.Lilo.szCommandLine));
	Dprintf(("LILO: kernel command line: %s\n", bi.command_line));

	/* Memory banks */
	bi.num_memory = 0;
	/* If kernel is loaded to FastRAM, switch the order of ST-RAM
	 * and FastRAM, otherwise kernel panics.
	 *
	 * However, when ST-RAM comes after FastRAM, kernel tells that
	 * it ignores the area and tells to fix the bootloader...
	 */
	if (!kernel_to_fastram) {
		add_chunk(0, RAMSize);
	}
	if (FastRAMSize > 0) {
		add_chunk(FastRAMBase, FastRAMSize);
	}
	if (kernel_to_fastram) {
		add_chunk(0, RAMSize);
	}
	bi.num_memory = SDL_SwapBE32(bi.num_memory);

	if (!create_bootinfo()) {
	    fprintf(stderr, "LILO: Can not create bootinfo structure\n");
		return false;
	}

	/*--- Copy boot info to RAM after kernel ---*/
	memcpy(hostkbase + KERNEL_START + kernel_size, &bi_union.record, bi_size);
	Dprintf(("LILO: bootinfo at 0x%08x\n", *kernel_offset + KERNEL_START + kernel_size));

#if LILO_DEBUG
	tmp = (uint32_t *)(hostkbase + KERNEL_START + kernel_size);
	for (i = 0; i < 16; i++) {
		Dprintf(("LILO: bi_union.record[%2d] = 0x%08x\n",
			 i, SDL_SwapBE32(tmp[i])));
	}
#endif

	/*--- Init SP & PC ---*/
	tmp = (uint32_t *)RAMBaseHost;
	tmp[0] = SDL_SwapBE32(*kernel_offset + KERNEL_START);	/* SP */
	tmp[1] = SDL_SwapBE32(TosAddress);		/* PC = ROMBase */
	uint8_t *ROMBaseHost = STRam + TosAddress;
	ROMBaseHost[4] = (*kernel_offset + KERNEL_START) >> 24;
	ROMBaseHost[5] = (*kernel_offset + KERNEL_START) >> 16;
	ROMBaseHost[6] = (*kernel_offset + KERNEL_START) >>  8;
	ROMBaseHost[7] = (*kernel_offset + KERNEL_START) & 0xff;

	Dprintf(("LILO: OK\n"));

	return true;
}

/**
 * Set machine type settings to bootinfo based on Hatari configuration
 * Return true for success
 */
static bool set_machine_type(void)
{
	bi.machtype = SDL_SwapBE32(MACH_ATARI);
	bi.mch_type = SDL_SwapBE32(ATARI_MACH_NORMAL);

	switch (ConfigureParams.System.nMachineType) {
	case MACHINE_FALCON:
		bi.mch_cookie = SDL_SwapBE32(ATARI_MCH_FALCON);
		break;
	case MACHINE_TT:
		bi.mch_cookie = SDL_SwapBE32(ATARI_MCH_TT);
		break;
	case MACHINE_STE:
	case MACHINE_MEGA_STE:
		bi.mch_cookie = SDL_SwapBE32(ATARI_MCH_STE);
		break;
	case MACHINE_ST:
	case MACHINE_MEGA_ST:
		bi.mch_cookie = SDL_SwapBE32(ATARI_MCH_ST);
		break;
	}

	switch(ConfigureParams.System.nCpuLevel) {
	case 3:
		bi.cputype = SDL_SwapBE32(BI_CPU_68030);
		bi.mmutype = SDL_SwapBE32(BI_MMU_68030);
		break;
	case 4:
		bi.cputype = SDL_SwapBE32(BI_CPU_68040);
		bi.mmutype = SDL_SwapBE32(BI_MMU_68040);
#if 0
		/*
		 * AB40 has different reset address handling:
		 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/arch/m68k/atari/config.c#n494
		 */
		if (ConfigureParams.System.nMachineType == MACHINE_FALCON) {
			/* let's try claiming it's Falcon AfterBurner like Aranym does */
			bi.mch_cookie = SDL_SwapBE32(ATARI_MCH_AB40);
		}
#endif
		break;
	case 5: /* special case: 060 */
		bi.cputype = SDL_SwapBE32(BI_CPU_68060);
		bi.mmutype = SDL_SwapBE32(BI_MMU_68060);
		break;
	default:
		Log_AlertDlg(LOG_FATAL, "LILO: Linux requires at least 030 CPU (for MMU), not 0%d0!",
			     ConfigureParams.System.nCpuLevel);
		return false;
	}

	switch(ConfigureParams.System.n_FPUType) {
	case FPU_68881:
		bi.fputype = SDL_SwapBE32(BI_FPU_68881);
		break;
	case FPU_68882:
		bi.fputype = SDL_SwapBE32(BI_FPU_68882);
		break;
	case FPU_CPU:
		if (ConfigureParams.System.nCpuLevel == 4) {
			bi.fputype = SDL_SwapBE32(BI_FPU_68040);
		} else if (ConfigureParams.System.nCpuLevel == 5) { /* special case: 060 */
			bi.fputype = SDL_SwapBE32(BI_FPU_68060);
		}
		/* TODO: else -> fail? */
		break;
	case FPU_NONE:
		bi.fputype = 0; /* TODO */
		break;
	}
	return true;
}

/**
 * Create the Bootinfo Structure
 * Return true for success
 */
static bool create_bootinfo(void)
{
	unsigned int i;
	struct bi_record *record;

	/* Initialization */
	bi_size = 0;

	/* Generic tags */
	if (!add_bi_record(BI_MACHTYPE, sizeof(bi.machtype), &bi.machtype)) {
		return false;
	}
	if (!add_bi_record(BI_CPUTYPE, sizeof(bi.cputype), &bi.cputype)) {
		return false;
	}
	if (!add_bi_record(BI_FPUTYPE, sizeof(bi.fputype), &bi.fputype)) {
		return false;
	}
	if (!add_bi_record(BI_MMUTYPE, sizeof(bi.mmutype), &bi.mmutype)) {
		return false;
	}
	for (i = 0; i < SDL_SwapBE32((Uint32)bi.num_memory); i++) {
		if (!add_bi_record(BI_MEMCHUNK, sizeof(bi.memory[i]), &bi.memory[i]))
			return false;
	}
	if (SDL_SwapBE32(bi.ramdisk.size)) {
		if (!add_bi_record(BI_RAMDISK, sizeof(bi.ramdisk), &bi.ramdisk))
			return false;
	}
	if (!add_bi_string(BI_COMMAND_LINE, bi.command_line)) {
		return false;
	}
	/* Atari tags */
	if (!add_bi_record(BI_ATARI_MCH_COOKIE, sizeof(bi.mch_cookie), &bi.mch_cookie)) {
		return false;
	}
	if (!add_bi_record(BI_ATARI_MCH_TYPE, sizeof(bi.mch_type), &bi.mch_type)) {
		return false;
	}
	/* Trailer */
	record = (struct bi_record *)((char *)&bi_union.record + bi_size);
	record->tag = SDL_SwapBE16(BI_LAST);
	bi_size += sizeof(bi_union.record.tag);

	return true;
}

/**
 * Add a Record to the Bootinfo Structure
 * Return true for success
 */
static bool add_bi_record(uint16_t tag, uint16_t size, const void *data)
{
	struct bi_record *record;
	unsigned short size2;

	size2 = (sizeof(struct bi_record) + size + 3) & -4;
	if (bi_size + size2 + sizeof(bi_union.record.tag) > MAX_BI_SIZE) {
		fprintf (stderr, "LILO: can't add bootinfo record. Ask a wizard to enlarge me.\n");
		return false;
	}
	record = (struct bi_record *)((char *)&bi_union.record + bi_size);
	record->tag = SDL_SwapBE16(tag);
	record->size = SDL_SwapBE16(size2);
	memcpy((char *)record + sizeof(struct bi_record), data, size);
	bi_size += size2;

	return true;
}

/**
 * Add a String Record to the Bootinfo Structure
 * return true for success
 */
static bool add_bi_string(uint16_t tag, const char *s)
{
	return add_bi_record(tag, strlen(s) + 1, s);
}
