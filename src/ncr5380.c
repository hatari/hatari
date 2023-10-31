/*
 * Hatari - NCR 5380 SCSI controller emulation
 *
 * Based on scsi.ccp from WinUAE:
 *
 *  Copyright 2007-2015 Toni Wilen
 *
 * Adaptions to Hatari:
 *
 *  Copyright 2018 Thomas Huth
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */
const char NCR5380_fileid[] = "Hatari ncr5380.c";

#include "main.h"
#include "configuration.h"
#include "cycles.h"
#include "cycInt.h"
#include "file.h"
#include "fdc.h"
#include "hdc.h"
#include "hatari-glue.h"
#include "ioMem.h"
#include "log.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "ncr5380.h"
#include "stMemory.h"
#include "newcpu.h"
#include "tos.h"

#define WITH_NCR5380 1

int nScsiPartitions;
bool bScsiEmuOn;

static SCSI_CTRLR ScsiBus;

#define MAX_TOTAL_SCSI_DEVICES 8

#define RAW_SCSI_DEBUG 2
#define NCR5380_DEBUG 1
#define NCR5380_DEBUG_IRQ 0

#ifndef _T
#define _T(x) x
#endif

// raw scsi

#define SCSI_IO_BUSY 0x80
#define SCSI_IO_ATN 0x40
#define SCSI_IO_SEL 0x20
#define SCSI_IO_REQ 0x10
#define SCSI_IO_DIRECTION 0x01
#define SCSI_IO_COMMAND 0x02
#define SCSI_IO_MESSAGE 0x04

#define SCSI_SIGNAL_PHASE_FREE -1
#define SCSI_SIGNAL_PHASE_ARBIT -2
#define SCSI_SIGNAL_PHASE_SELECT_1 -3
#define SCSI_SIGNAL_PHASE_SELECT_2 -4

#define SCSI_SIGNAL_PHASE_DATA_OUT 0
#define SCSI_SIGNAL_PHASE_DATA_IN 1
#define SCSI_SIGNAL_PHASE_COMMAND 2
#define SCSI_SIGNAL_PHASE_STATUS 3
#define SCSI_SIGNAL_PHASE_MESSAGE_OUT 6
#define SCSI_SIGNAL_PHASE_MESSAGE_IN 7

#define SCSI_STATUS_GOOD                   0x00
#define SCSI_STATUS_CHECK_CONDITION        0x02
#define SCSI_STATUS_CONDITION_MET          0x04
#define SCSI_STATUS_BUSY                   0x08
#define SCSI_STATUS_INTERMEDIATE           0x10
#define SCSI_STATUS_ICM                    0x14 /* intermediate condition met */
#define SCSI_STATUS_RESERVATION_CONFLICT   0x18
#define SCSI_STATUS_COMMAND_TERMINATED     0x22
#define SCSI_STATUS_QUEUE_FULL             0x28
#define SCSI_STATUS_ACA_ACTIVE             0x30

struct raw_scsi
{
	int io;
	int bus_phase;
	bool atn;
	bool ack;
	uae_u8 data_write;
	uae_u8 status;
	bool databusoutput;
	int initiator_id, target_id;
	struct scsi_data *device[MAX_TOTAL_SCSI_DEVICES];
	struct scsi_data *target;
	int msglun;
};

struct soft_scsi
{
	uae_u8 regs[9];
	struct raw_scsi rscsi;
	bool irq;

	int dma_direction;
	bool dma_active;
	bool dma_started;
	bool dma_controller;
	bool dma_drq;

	int dmac_direction;
	int dmac_active;
};

struct soft_scsi ncr_soft_scsi;

static const uae_s16 outcmd[] = { 0x04, 0x0a, 0x0c, 0x11, 0x2a, 0xaa, 0x15, 0x55, 0x0f, -1 };
static const uae_s16 incmd[] = { 0x01, 0x03, 0x08, 0x0e, 0x12, 0x1a, 0x5a, 0x25, 0x28, 0x34, 0x37, 0x42, 0x43, 0xa8, 0x51, 0x52, 0xb9, 0xbd, 0xd8, 0xd9, 0xbe, -1 };
static const uae_s16 nonecmd[] = { 0x00, 0x05, 0x06, 0x07, 0x09, 0x0b, 0x10, 0x16, 0x17, 0x19, 0x1b, 0x1d, 0x1e, 0x2b, 0x35, 0x45, 0x47, 0x48, 0x49, 0x4b, 0x4e, 0xa5, 0xa9, 0xba, 0xbc, 0xe0, 0xe3, 0xe4, -1 };
static const uae_s16 scsicmdsizes[] = { 6, 10, 10, 12, 16, 12, 10, 6 };

static uae_u8 ncr5380_bget(struct soft_scsi *scsi, int reg);
void ncr5380_bput(struct soft_scsi *scsi, int reg, uae_u8 v);
static void ncr5380_set_irq(struct soft_scsi *scsi);

static int scsi_data_dir(struct scsi_data *sd)
{
	int i;
	uae_u8 cmd;

	cmd = sd->cmd[0];
	for (i = 0; outcmd[i] >= 0; i++) {
		if (cmd == outcmd[i]) {
			return 1;
		}
	}
	for (i = 0; incmd[i] >= 0; i++) {
		if (cmd == incmd[i]) {
			return -1;
		}
	}
	for (i = 0; nonecmd[i] >= 0; i++) {
		if (cmd == nonecmd[i]) {
			return 0;
		}
	}
	write_log (_T("SCSI command %02X, no direction specified!\n"), cmd);
	return 0;
}

static void scsi_start_transfer(struct scsi_data *sd)
{
	// sd->offset = 0;
	ScsiBus.offset = 0;
}

static int scsi_send_data(struct scsi_data *sd, uae_u8 b)
{
	if (ScsiBus.offset < 0) {
		write_log(_T("SCSI data offset is negative!\n"));
		return 0;
	}
	if (sd->direction == 1) {
		if (ScsiBus.offset >= ScsiBus.buffer_size) {
			write_log (_T("SCSI data buffer overflow!\n"));
			return 0;
		}
		ScsiBus.buffer[ScsiBus.offset++] = b;
	} else if (sd->direction == 2) {
		if (ScsiBus.offset >= 16) {
			write_log (_T("SCSI command buffer overflow!\n"));
			return 0;
		}
		sd->cmd[ScsiBus.offset++] = b;
		if (ScsiBus.offset == sd->cmd_len)
			return 1;
	} else {
		write_log (_T("scsi_send_data() without direction! (%02X)\n"), b);
		return 0;
	}
	if (ScsiBus.offset == ScsiBus.data_len)
		return 1;
	return 0;
}

static int scsi_receive_data(struct scsi_data *sd, uae_u8 *b, bool next)
{
	if (!ScsiBus.data_len) {
		fprintf(stderr, "scsi_receive_data without length!\n");
		return -1;
	}
	*b = ScsiBus.buffer[ScsiBus.offset];
	// fprintf(stderr,"scsi_receive_data %i <-> %i (%i)\n",
	//         ScsiBus.offset, ScsiBus.data_len, next);
	if (next) {
		ScsiBus.offset++;
		if (ScsiBus.offset == ScsiBus.data_len)
			return 1; // requested length got
	}
	return 0;
}

static void bus_free(struct raw_scsi *rs)
{
	// fprintf(stderr, "BUS FREE!\n");
	rs->bus_phase = SCSI_SIGNAL_PHASE_FREE;
	rs->io = 0;
}

static int getbit(uae_u8 v)
{
	int i;

	for (i = 7; i >= 0; i--) {
		if ((1 << i) & v)
			return i;
	}
	return -1;
}

static int countbits(uae_u8 v)
{
	int i, cnt = 0;

	for (i = 7; i >= 0; i--) {
		if ((1 << i) & v)
			cnt++;
	}
	return cnt;
}

static void raw_scsi_reset_bus(struct soft_scsi *scsi)
{
	//struct raw_scsi *r = &scsi->rscsi;
#if RAW_SCSI_DEBUG
	write_log(_T("SCSI BUS reset\n"));
#endif
#if 0	/* FIXME */
	for (int i = 0; i < 8; i++) {
		scsi_emulate_reset_device(r->device[i]);
	}
#endif
}


static void raw_scsi_set_databus(struct raw_scsi *rs, bool databusoutput)
{
	rs->databusoutput = databusoutput;
}

static void raw_scsi_set_signal_phase(struct raw_scsi *rs, bool busy, bool select, bool atn)
{
	// fprintf(stderr,"raw_scsi_set_signal_phase busy=%i sel=%i atn=%i phase=%i\n",
	//         busy, select, atn, rs->bus_phase);
	switch (rs->bus_phase)
	{
		case SCSI_SIGNAL_PHASE_FREE:
		if (busy && !select && !rs->databusoutput) {
			if (countbits(rs->data_write) != 1) {
#if RAW_SCSI_DEBUG
				write_log(_T("raw_scsi: invalid arbitration scsi id mask! (%02x)\n"), rs->data_write);
#endif
				return;
			}
			rs->bus_phase = SCSI_SIGNAL_PHASE_ARBIT;
			rs->initiator_id = getbit(rs->data_write);
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi: arbitration initiator id %d (%02x)\n"), rs->initiator_id, rs->data_write);
#endif
		} else if (!busy && select) {
			if (countbits(rs->data_write) > 2 || rs->data_write == 0) {
#if RAW_SCSI_DEBUG
				write_log(_T("raw_scsi: invalid scsi id selected mask (%02x)\n"), rs->data_write);
#endif
				return;
			}
			rs->initiator_id = -1;
			rs->bus_phase = SCSI_SIGNAL_PHASE_SELECT_1;
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi: selected scsi id mask (%02x)\n"), rs->data_write);
#endif
			raw_scsi_set_signal_phase(rs, busy, select, atn);
		}
		break;
		case SCSI_SIGNAL_PHASE_ARBIT:
		rs->target_id = -1;
		rs->target = NULL;
		ScsiBus.target = -1;
		if (busy && select) {
			rs->bus_phase = SCSI_SIGNAL_PHASE_SELECT_1;
		}
		break;
		case SCSI_SIGNAL_PHASE_SELECT_1:
		rs->atn = atn;
		rs->msglun = -1;
		rs->target_id = -1;
		ScsiBus.target = -1;
		if (!busy) {
			int i;
			for (i = 0; i < 8; i++) {
				if (i == rs->initiator_id)
					continue;
				if ((rs->data_write & (1 << i)) && rs->device[i]) {
					rs->target_id = i;
					rs->target = rs->device[rs->target_id];
					ScsiBus.target = i;
#if RAW_SCSI_DEBUG
					write_log(_T("raw_scsi: selected id %d\n"), rs->target_id);
#endif
					rs->io |= SCSI_IO_BUSY;
				}
			}
#if RAW_SCSI_DEBUG
			if (rs->target_id < 0) {
				for (i = 0; i < 8; i++) {
					if (i == rs->initiator_id)
						continue;
					if ((rs->data_write & (1 << i)) && !rs->device[i]) {
						write_log(_T("raw_scsi: selected non-existing id %d\n"), i);
					}
				}
			}
#endif
			if (rs->target_id >= 0) {
				rs->bus_phase = SCSI_SIGNAL_PHASE_SELECT_2;
			} else {
				if (!select) {
					rs->bus_phase = SCSI_SIGNAL_PHASE_FREE;
				}
			}
		}
		break;
		case SCSI_SIGNAL_PHASE_SELECT_2:
		if (!select) {
			scsi_start_transfer(rs->target);
			rs->bus_phase = rs->atn ? SCSI_SIGNAL_PHASE_MESSAGE_OUT : SCSI_SIGNAL_PHASE_COMMAND;
			rs->io = SCSI_IO_BUSY | SCSI_IO_REQ;
		}
		break;
	}
}

static uae_u8 raw_scsi_get_signal_phase(struct raw_scsi *rs)
{
	uae_u8 v = rs->io;
	if (rs->bus_phase >= 0)
		v |= rs->bus_phase;
	if (rs->ack)
		v &= ~SCSI_IO_REQ;
	return v;
}

static uae_u8 raw_scsi_get_data_2(struct raw_scsi *rs, bool next, bool nodebug)
{
	struct scsi_data *sd = rs->target;
	uae_u8 v = 0;

	switch (rs->bus_phase)
	{
		case SCSI_SIGNAL_PHASE_FREE:
		v = 0;
		break;
		case SCSI_SIGNAL_PHASE_ARBIT:
#if RAW_SCSI_DEBUG
		write_log(_T("raw_scsi: arbitration\n"));
#endif
		v = rs->data_write;
		break;
		case SCSI_SIGNAL_PHASE_DATA_IN:
#if RAW_SCSI_DEBUG > 2
		scsi_receive_data(sd, &v, false);
		write_log(_T("raw_scsi: read data byte %02x (%d/%d)\n"), v, ScsiBus.offset, ScsiBus.data_len);
#endif
		if (scsi_receive_data(sd, &v, next)) {
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi: data in finished, %d bytes: status phase\n"), ScsiBus.offset);
#endif
			rs->bus_phase = SCSI_SIGNAL_PHASE_STATUS;
		}
		break;
		case SCSI_SIGNAL_PHASE_STATUS:
#if RAW_SCSI_DEBUG
		if (!nodebug || next)
			write_log(_T("raw_scsi: status byte read %02x. Next=%d\n"), ScsiBus.status, next);
#endif
		v = ScsiBus.status; // sd->status;
		if (next) {
			ScsiBus.status = 0; // sd->status = 0;
			rs->bus_phase = SCSI_SIGNAL_PHASE_MESSAGE_IN;
		}
		break;
		case SCSI_SIGNAL_PHASE_MESSAGE_IN:
#if RAW_SCSI_DEBUG
		if (!nodebug || next)
			write_log(_T("raw_scsi: message byte read %02x. Next=%d\n"), ScsiBus.status, next);
#endif
		v = ScsiBus.status; // sd->status;
		rs->status = v;
		if (next) {
			bus_free(rs);
		}
		break;
		default:
#if RAW_SCSI_DEBUG
		write_log(_T("raw_scsi_get_data but bus phase is %d!\n"), rs->bus_phase);
#endif
		break;
	}

	return v;
}

static uae_u8 raw_scsi_get_data(struct raw_scsi *rs, bool next)
{
	return raw_scsi_get_data_2(rs, next, true);
}

static int getmsglen(uae_u8 *msgp, int len)
{
	uae_u8 msg = msgp[0];
	if (msg == 0 || (msg >= 0x02 && msg <= 0x1f) || msg >= 0x80)
		return 1;
	if (msg >= 0x20 && msg <= 0x2f)
		return 2;
	// extended message, at least 3 bytes
	if (len < 2)
		return 3;
	return msgp[1];
}

static bool scsi_emulate_analyze (struct scsi_data *sd)
{
	int cmd_len, data_len;

	data_len = ScsiBus.data_len;
	cmd_len = scsicmdsizes[sd->cmd[0] >> 5];
	sd->cmd_len = cmd_len;
	switch (sd->cmd[0])
	{
	case 0x04: // FORMAT UNIT
		// FmtData set?
		if (sd->cmd[1] & 0x10) {
			// int cl = (sd->cmd[1] & 8) != 0;
			// int dlf = sd->cmd[1] & 7;
			//data_len2 = 4;
		} else {
			sd->direction = 0;
			ScsiBus.data_len = 0;
			return true;
		}
	break;
	case 0x06: // FORMAT TRACK
	case 0x07: // FORMAT BAD TRACK
		sd->direction = 0;
		ScsiBus.data_len = 0;
		return true;
	case 0x0c: // INITIALIZE DRIVE CHARACTERICS (SASI)
		data_len = 8;
	break;
	case 0x08: // READ(6)
	break;
	case 0x11: // ASSIGN ALTERNATE TRACK (SASI)
		data_len = 4;
		break;
	case 0x28: // READ(10)
	break;
	case 0xa8: // READ(12)
	break;
	case 0x0f: // WRITE SECTOR BUFFER
		data_len = 512; //sd->blocksize;
	break;
	case 0x0a: // WRITE(6)
		data_len = (sd->cmd[4] == 0 ? 256 : sd->cmd[4]) * 512; //sd->blocksize;
	break;
	case 0x2a: // WRITE(10)
		data_len = ((sd->cmd[7] << 8) | (sd->cmd[8] << 0)) * 512; //sd->blocksize;
	break;
	/*
	case 0xaa: // WRITE(12)
		data_len = ((sd->cmd[6] << 24) | (sd->cmd[7] << 16) | (sd->cmd[8] << 8) | (sd->cmd[9] << 0)) * 512; //sd->blocksize;
	break;
	*/
	case 0x2f: // VERIFY
		if (sd->cmd[1] & 2) {
			ScsiBus.data_len = ((sd->cmd[7] << 8) | (sd->cmd[8] << 0)) * 512; // sd->blocksize;
			sd->direction = 1;
		} else {
			ScsiBus.data_len = 0;
			sd->direction = 0;
		}
		return true;
	}
	if (data_len < 0) {
		if (cmd_len == 6) {
			ScsiBus.data_len = sd->cmd[4];
		} else {
			ScsiBus.data_len = (sd->cmd[7] << 8) | sd->cmd[8];
		}
	} else {
		ScsiBus.data_len = data_len;
	}
	sd->direction = scsi_data_dir(sd);
	if (sd->direction > 0 && ScsiBus.data_len == 0) {
		sd->direction = 0;
	}
	return true;
}

static void scsi_emulate_cmd(struct scsi_data *sd)
{
	int i;

	// fprintf(stderr,"scsi_emulate_cmd cmdlen=%i offset=%i\n", sd->cmd_len, ScsiBus.offset);
	ScsiBus.byteCount = 0;
	for (i = 0; i < sd->cmd_len; i++)
	{
		HDC_WriteCommandPacket(&ScsiBus, sd->cmd[i]);
	}
}

static void raw_scsi_write_data(struct raw_scsi *rs, uae_u8 data)
{
	struct scsi_data *sd = rs->target;
	int len;

	switch (rs->bus_phase)
	{
		case SCSI_SIGNAL_PHASE_SELECT_1:
		case SCSI_SIGNAL_PHASE_FREE:
		break;
		case SCSI_SIGNAL_PHASE_COMMAND:
		sd->cmd[ScsiBus.offset++] = data;
		len = scsicmdsizes[sd->cmd[0] >> 5];
#if RAW_SCSI_DEBUG > 1
		write_log(_T("raw_scsi: got command byte %02x (%d/%d)\n"), data, ScsiBus.offset, len);
#endif
		if (ScsiBus.offset >= len) {
			if (rs->msglun >= 0) {
				sd->cmd[1] &= ~(0x80 | 0x40 | 0x20);
				sd->cmd[1] |= rs->msglun << 5;
			}
			scsi_emulate_analyze(rs->target);
			if (sd->direction > 0) {
#if RAW_SCSI_DEBUG
				write_log(_T("raw_scsi: data out %d bytes required\n"), ScsiBus.data_len);
#endif
				scsi_emulate_cmd(sd);	/* Hatari only */
				scsi_start_transfer(sd);
				rs->bus_phase = SCSI_SIGNAL_PHASE_DATA_OUT;
			} else if (sd->direction <= 0) {
				scsi_emulate_cmd(sd);
				scsi_start_transfer(sd);
				if (!ScsiBus.status && ScsiBus.data_len > 0) {
#if RAW_SCSI_DEBUG
					write_log(_T("raw_scsi: data in %d bytes waiting\n"), ScsiBus.data_len);
#endif
					rs->bus_phase = SCSI_SIGNAL_PHASE_DATA_IN;
				} else {
#if RAW_SCSI_DEBUG
					write_log(_T("raw_scsi: no data, status = %d\n"), ScsiBus.status);
#endif
					rs->bus_phase = SCSI_SIGNAL_PHASE_STATUS;
				}
			}
		}
		break;
		case SCSI_SIGNAL_PHASE_DATA_OUT:
#if RAW_SCSI_DEBUG > 2
		write_log(_T("raw_scsi: write data byte %02x (%d/%d)\n"), data, ScsiBus.offset, ScsiBus.data_len);
#endif
		if (scsi_send_data(sd, data)) {
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi: data out finished, %d bytes\n"), ScsiBus.data_len);
#endif
			if (ScsiBus.dmawrite_to_fh)
			{
				int r;
				r = fwrite(ScsiBus.buffer, 1, ScsiBus.data_len, ScsiBus.dmawrite_to_fh);
				if (r != ScsiBus.data_len)
				{
					Log_Printf(LOG_ERROR, "Could not write bytes to HD image (%d/%d).\n",
					           r, ScsiBus.data_len);
					ScsiBus.status = HD_STATUS_ERROR;
				}
				ScsiBus.dmawrite_to_fh = NULL;
			}

			rs->bus_phase = SCSI_SIGNAL_PHASE_STATUS;
		}
		break;
		case SCSI_SIGNAL_PHASE_MESSAGE_OUT:
		sd->msgout[ScsiBus.offset++] = data;
		len = getmsglen(sd->msgout, ScsiBus.offset);
#if RAW_SCSI_DEBUG
		write_log(_T("raw_scsi_put_data got message %02x (%d/%d)\n"), data, ScsiBus.offset, len);
#endif
		if (ScsiBus.offset >= len) {
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi_put_data got message %02x (%d bytes)\n"), sd->msgout[0], len);
#endif
			if ((sd->msgout[0] & (0x80 | 0x20)) == 0x80)
				rs->msglun = sd->msgout[0] & 7;
			scsi_start_transfer(sd);
			rs->bus_phase = SCSI_SIGNAL_PHASE_COMMAND;
		}
		break;
		default:
#if RAW_SCSI_DEBUG
		write_log(_T("raw_scsi_put_data but bus phase is %d!\n"), rs->bus_phase);
#endif
		break;
	}
}

static void raw_scsi_put_data(struct raw_scsi *rs, uae_u8 data, bool databusoutput)
{
	rs->data_write = data;
	if (!databusoutput)
		return;
	raw_scsi_write_data(rs, data);
}

static void raw_scsi_set_ack(struct raw_scsi *rs, bool ack)
{
	if (rs->ack != ack) {
		rs->ack = ack;
		if (!ack)
			return;
		if (rs->bus_phase < 0)
			return;
		if (!(rs->bus_phase & SCSI_IO_DIRECTION)) {
			if (rs->databusoutput) {
				raw_scsi_write_data(rs, rs->data_write);
			}
		} else {
			raw_scsi_get_data_2(rs, true, false);
		}
	}
}

static void Ncr5380_UpdateDmaAddrAndLen(uint32_t nDmaAddr, uint32_t nDataLen)
{
	uint32_t nNewAddr = nDmaAddr + nDataLen;
	uint32_t nNewLen;

	if (Config_IsMachineFalcon())
	{
		FDC_WriteDMAAddress(nNewAddr);
	}
	else
	{
		IoMem[0xff8701] = nNewAddr >> 24;
		IoMem[0xff8703] = nNewAddr >> 16;
		IoMem[0xff8705] = nNewAddr >> 8;
		IoMem[0xff8707] = nNewAddr;

		nNewLen = (uint32_t)IoMem[0xff8709] << 24 | IoMem[0xff870b] << 16
		          | IoMem[0xff870d] << 8 | IoMem[0xff870f];
		assert(nDataLen <= nNewLen);
		nNewLen -= nDataLen;
		IoMem[0xff8709] = nNewLen >> 24;
		IoMem[0xff870b] = nNewLen >> 16;
		IoMem[0xff870d] = nNewLen >> 8;
		IoMem[0xff870f] = nNewLen;
	}
}

static void dma_check(struct soft_scsi *ncr)
{
	int i, nDataLen;
	uint32_t nDmaAddr;

	// fprintf(stderr, "dma_check: dma_direction=%i data_len=%i/%i phase=%i %i active=%i \n",
	//         ncr->dma_direction, ScsiBus.offset, ScsiBus.data_len, ncr->rscsi.bus_phase, ncr->regs[3] & 7, ncr->dma_active);

	/* Don't do anything if nothing to transfer */
	if (ScsiBus.data_len - ScsiBus.offset == 0 || !ncr->dma_active || !ncr->dma_direction)
		return;

	if (Config_IsMachineFalcon())
	{
		/* Is DMA really active? */
		if ((FDC_DMA_GetMode() & 0xc0) != 0x00)
			return;
		nDmaAddr = FDC_GetDMAAddress();
		nDataLen = FDC_DMA_GetSectorCount() * 512;
	}
	else
	{
		if ((IoMem[0xff8715] & 2) == 0)
			return;
		nDmaAddr = (uint32_t)IoMem[0xff8701] << 24 | IoMem[0xff8703] << 16
		           | IoMem[0xff8705] << 8 | IoMem[0xff8707];
		nDataLen = (uint32_t)IoMem[0xff8709] << 24 | IoMem[0xff870b] << 16
		           | IoMem[0xff870d] << 8 | IoMem[0xff870f];
	}

	if (nDataLen > ScsiBus.data_len - ScsiBus.offset)
		nDataLen = ScsiBus.data_len - ScsiBus.offset;

	if (ncr_soft_scsi.dma_direction < 0)
	{
		if (STMemory_CheckAreaType(nDmaAddr, nDataLen, ABFLAG_RAM | ABFLAG_ROM))
		{
			for (i = 0; i < nDataLen; i++)
			{
				uint8_t val = ncr5380_bget(ncr, 8);
				STMemory_WriteByte(nDmaAddr + i, val);
			}
			ScsiBus.bDmaError = false;
		}
		else
		{
			ScsiBus.bDmaError = true;
			ScsiBus.status = HD_STATUS_ERROR;
		}

		if (Config_IsMachineFalcon())
		{
			/* Note that the Falcon's DMA chip seems to report an
			 * end address that is 16 bytes too high if the DATA IN
			 * phase was interrupted by a different phase, but the
			 * address is correct if there was no interruption. */
			if (ScsiBus.offset < ScsiBus.data_len)
				Ncr5380_UpdateDmaAddrAndLen(nDmaAddr, nDataLen + 16);
			else
				Ncr5380_UpdateDmaAddrAndLen(nDmaAddr, nDataLen);
		}
		else
		{
			int nRemainingBytes = IoMem[0xff8707] & 3;
			Ncr5380_UpdateDmaAddrAndLen(nDmaAddr, nDataLen);
			for (i = 0; i < nRemainingBytes; i++)
			{
				/* For more precise emulation, we should not
				 * pre-write the bytes to the STRam ... */
				const uint32_t addr = nDmaAddr + nDataLen - nRemainingBytes;
				IoMem[0xff8710 + i] = STMemory_ReadByte(addr + i);
			}
		}
	}
	else if (ncr_soft_scsi.dma_direction > 0 && ScsiBus.dmawrite_to_fh)
	{
		/* write - if allowed */
		if (STMemory_CheckAreaType(nDmaAddr, nDataLen, ABFLAG_RAM | ABFLAG_ROM))
		{
			for (i = 0; i < nDataLen; i++)
			{
				uint8_t val = STMemory_ReadByte(nDmaAddr + i);
				ncr5380_bput(ncr, 8, val);
			}
		}
		else
		{
			Log_Printf(LOG_WARN, "SCSI DMA write uses invalid RAM range 0x%x+%i\n",
				   nDmaAddr, nDataLen);
			ScsiBus.bDmaError = true;
			ScsiBus.status = HD_STATUS_ERROR;
		}
		Ncr5380_UpdateDmaAddrAndLen(nDmaAddr, nDataLen);
	}

	if (Config_IsMachineFalcon())
		FDC_SetDMAStatus(ScsiBus.bDmaError);	/* Set/Unset DMA error */

	ncr5380_set_irq ( ncr );

	if (ScsiBus.offset == ScsiBus.data_len)
	{
		ncr->dmac_active = 0;
		ncr->dma_active = 0;
	}
}

static void ncr5380_set_irq(struct soft_scsi *scsi)
{
	if (scsi->irq)
		return;
	scsi->irq = true;
#if 0	/* FIXME */
	devices_rethink_all(ncr80_rethink);
	if (scsi->delayed_irq)
		x_do_cycles(2 * CYCLE_UNIT);
#endif
#if NCR5380_DEBUG_IRQ
	write_log(_T("IRQ\n"));
#endif

	if (Config_IsMachineFalcon())
		FDC_SetIRQ(FDC_IRQ_SOURCE_HDC);
	else if (Config_IsMachineTT())
		MFP_GPIP_Set_Line_Input ( pMFP_TT , MFP_TT_GPIP_LINE_SCSI_NCR , MFP_GPIP_STATE_HIGH );
}

static void ncr5380_databusoutput(struct soft_scsi *scsi)
{
	bool databusoutput = (scsi->regs[1] & 1) != 0;
	struct raw_scsi *r = &scsi->rscsi;

	if (r->bus_phase >= 0 && (r->bus_phase & SCSI_IO_DIRECTION))
		databusoutput = false;
	raw_scsi_set_databus(r, databusoutput);
}

static void ncr5380_check(struct soft_scsi *scsi)
{
	ncr5380_databusoutput(scsi);
}

static void ncr5380_check_phase(struct soft_scsi *scsi)
{
	if (!(scsi->regs[2] & 2))
		return;
	if (scsi->regs[2] & 0x40)
		return;
	if (scsi->rscsi.bus_phase != (scsi->regs[3] & 7)) {
		if (scsi->dma_controller) {
			scsi->regs[5] |= 0x80; // end of dma
			scsi->regs[3] |= 0x80; // last byte sent
		}
		ncr5380_set_irq(scsi);
	}
}

static void ncr5380_reset(struct soft_scsi *scsi)
{
	memset(scsi->regs, 0, sizeof scsi->regs);
	raw_scsi_reset_bus(scsi);
	scsi->regs[1] = 0x80;
	ncr5380_set_irq(scsi);
}

static uae_u8 ncr5380_bget(struct soft_scsi *scsi, int reg)
{
	if (reg > 8)
		return 0;
	uae_u8 v = scsi->regs[reg];
	struct raw_scsi *r = &scsi->rscsi;
	switch(reg)
	{
		case 1:
		break;
		case 4:
		{
			uae_u8 t = raw_scsi_get_signal_phase(r);
			v = 0;
			if (t & SCSI_IO_BUSY)
				v |= 1 << 6;
			if (t & SCSI_IO_REQ)
				v |= 1 << 5;
			if (t & SCSI_IO_SEL)
				v |= 1 << 1;
			if (r->bus_phase >= 0)
				v |= r->bus_phase << 2;
			if (scsi->regs[1] & 0x80)
				v |= 0x80;
		}
		break;
		case 5:
		{
			uae_u8 t = raw_scsi_get_signal_phase(r);
			v &= (0x80 | 0x40 | 0x20 | 0x04);
			if (t & SCSI_IO_ATN)
				v |= 1 << 1;
			if (r->bus_phase == (scsi->regs[3] & 7)) {
				v |= 1 << 3;
			}
			if (scsi->irq) {
				v |= 1 << 4;
			}
			if (scsi->dma_drq || (scsi->dma_active && !scsi->dma_controller && r->bus_phase == (scsi->regs[3] & 7))) {
				scsi->dma_drq = true;
				v |= 1 << 6;
			}
			if (scsi->regs[2] & 4) {
				// monitor busy
				if (r->bus_phase == SCSI_SIGNAL_PHASE_FREE) {
					// any loss of busy = Busy error
					// not just "unexpected" loss of busy
					v |= 1 << 2;
					scsi->dmac_active = false;
				}
			}
		}
		break;
		case 0:
		v = raw_scsi_get_data(r, false);
		break;
		case 6:
		v = raw_scsi_get_data(r, scsi->dma_active);
		ncr5380_check_phase(scsi);
		break;
		case 7:
		scsi->irq = false;
		if (Config_IsMachineFalcon())
			FDC_ClearIRQ();
		break;
		case 8: // fake dma port
		v = raw_scsi_get_data(r, true);
		ncr5380_check_phase(scsi);
		break;
	}
	ncr5380_check(scsi);
	return v;
}

void ncr5380_bput(struct soft_scsi *scsi, int reg, uae_u8 v)
{
	if (reg > 8)
		return;
	bool dataoutput = (scsi->regs[1] & 1) != 0;
	struct raw_scsi *r = &scsi->rscsi;
	uae_u8 old = scsi->regs[reg];
	scsi->regs[reg] = v;
	switch(reg)
	{
		case 0:
		{
			r->data_write = v;
			// assert data bus can be only active if direction is out
			// and bus phase matches
			if (r->databusoutput) {
				if (((scsi->regs[2] & 2) && scsi->dma_active) || r->bus_phase < 0) {
					raw_scsi_write_data(r, v);
					ncr5380_check_phase(scsi);
				}
			}
		}
		break;
		case 1:
		{
			scsi->regs[reg] &= ~((1 << 5) | (1 << 6));
			scsi->regs[reg] |= old & ((1 << 5) | (1 << 6)); // AIP, LA
			if (!(v & 0x80)) {
				bool init = r->bus_phase < 0;
				ncr5380_databusoutput(scsi);
				if (init && !dataoutput && (v & 1) && (scsi->regs[2] & 1)) {
					r->bus_phase = SCSI_SIGNAL_PHASE_SELECT_1;
				}
				raw_scsi_set_signal_phase(r,
					(v & (1 << 3)) != 0,
					(v & (1 << 2)) != 0,
					(v & (1 << 1)) != 0);
				if (!(scsi->regs[2] & 2))
					raw_scsi_set_ack(r, (v & (1 << 4)) != 0);
			}
			if (v & 0x80) { // RST
				ncr5380_reset(scsi);
			}
		}
		break;
		case 2:
		if ((v & 1) && !(old & 1)) { // Arbitrate
			r->databusoutput = false;
			raw_scsi_set_signal_phase(r, true, false, false);
			scsi->regs[1] |= 1 << 6; // AIP
			scsi->regs[1] &= ~(1 << 5); // LA
		} else if (!(v & 1) && (old & 1)) {
			scsi->regs[1] &= ~(1 << 6);
		}
		if (!(v & 2)) {
			// end of dma and dma request
			scsi->regs[5] &= ~(0x80 | 0x40);
			scsi->dma_direction = 0;
			scsi->dma_active = false;
			scsi->dma_drq = false;
		}
		break;
		case 5:
		scsi->regs[reg] = old;
		if (scsi->regs[2] & 2) {
			scsi->dma_direction = 1;
			scsi->dma_active = true;
			dma_check(scsi);
		}
#if NCR5380_DEBUG
		write_log(_T("DMA send PC=%08x\n"), M68K_GETPC);
#endif
		break;
		case 6:
		if (scsi->regs[2] & 2) {
			scsi->dma_direction = 1;
			scsi->dma_active = true;
			scsi->dma_started = true;
			dma_check(scsi);
		}
#if NCR5380_DEBUG
		write_log(_T("DMA target recv PC=%08x\n"), M68K_GETPC);
#endif
		break;
		case 7:
		if (scsi->regs[2] & 2) {
			scsi->dma_direction = -1;
			scsi->dma_active = true;
			scsi->dma_started = true;
			dma_check(scsi);
		}
#if NCR5380_DEBUG
		write_log(_T("DMA initiator recv PC=%08x\n"), M68K_GETPC);
#endif
		break;
		case 8: // fake dma port
		if (r->bus_phase == (scsi->regs[3] & 7)) {
			raw_scsi_put_data(r, v, true);
		}
		ncr5380_check_phase(scsi);
		break;
	}
	ncr5380_check(scsi);
}

/* ***** Hatari glue code below ***** */

/**
 * Open the disk image file, set partitions count.
 * Return true if there are any.
 */
bool Ncr5380_Init(void)
{
#if WITH_NCR5380
	int i;

	nScsiPartitions = 0;
	bScsiEmuOn = false;

	memset(&ScsiBus, 0, sizeof(ScsiBus));
	ScsiBus.typestr = "SCSI";
	ScsiBus.buffer_size = 512;
	ScsiBus.buffer = malloc(ScsiBus.buffer_size);
	if (!ScsiBus.buffer)
	{
		perror("Ncr5380_Init");
		return 0;
	}
	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		if (!ConfigureParams.Scsi[i].bUseDevice)
			continue;
		if (HDC_InitDevice("SCSI", &ScsiBus.devs[i], ConfigureParams.Scsi[i].sDeviceFile, ConfigureParams.Scsi[i].nBlockSize) == 0)
		{
			nScsiPartitions += HDC_PartitionCount(ScsiBus.devs[i].image_file, TRACE_SCSI_CMD, NULL);
			bScsiEmuOn = true;
		}
		else
			ConfigureParams.Scsi[i].bUseDevice = false;
	}
	nNumDrives += nScsiPartitions;
#endif
	return bScsiEmuOn;
}

/**
 * Ncr5380_UnInit - close image files and free resources
 *
 */
void Ncr5380_UnInit(void)
{
#if WITH_NCR5380
	int i;

	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		if (!ScsiBus.devs[i].enabled)
			continue;
		File_UnLock(ScsiBus.devs[i].image_file);
		fclose(ScsiBus.devs[i].image_file);
		ScsiBus.devs[i].image_file = NULL;
		ScsiBus.devs[i].enabled = false;
	}
	free(ScsiBus.buffer);
	ScsiBus.buffer = NULL;

	nNumDrives -= nScsiPartitions;
	nScsiPartitions = 0;
	bScsiEmuOn = false;
#endif
}

/**
 * Emulate external reset "pin": Clear registers etc.
 */
void Ncr5380_Reset(void)
{
#if WITH_NCR5380
	int i;

	ncr5380_reset(&ncr_soft_scsi);
	bus_free(&ncr_soft_scsi.rscsi);

	for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
		if (ScsiBus.devs[i].enabled) {
			ncr_soft_scsi.rscsi.device[i] = &ScsiBus.devs[i];
			// fprintf(stderr, "SCSI #%i enabled\n", i);
		}
		else
		{
			ncr_soft_scsi.rscsi.device[i] = NULL;
		}
	}
#endif
}

/**
 * Write a command byte to the NCR 5380 SCSI controller
 */
void Ncr5380_WriteByte(int addr, uint8_t byte)
{
#if WITH_NCR5380
	ncr5380_bput(&ncr_soft_scsi, addr, byte);
#endif
}

/**
 * Read a command byte from the NCR 5380 SCSI controller
 */
uint8_t Ncr5380_ReadByte(int addr)
{
#if WITH_NCR5380
	return ncr5380_bget(&ncr_soft_scsi, addr);
#else
	return 0;
#endif
}


void Ncr5380_DmaTransfer_Falcon(void)
{
	dma_check(&ncr_soft_scsi);
}


void Ncr5380_IoMemTT_WriteByte(void)
{
	while (nIoMemAccessSize > 0)
	{
		if (IoAccessBaseAddress & 1)
		{
			int addr = IoAccessBaseAddress / 2 & 0x7;
			Ncr5380_WriteByte(addr, IoMem[IoAccessBaseAddress]);
		}
		IoAccessBaseAddress++;
		nIoMemAccessSize--;
	}
}


void Ncr5380_IoMemTT_ReadByte(void)
{
	while (nIoMemAccessSize > 0)
	{
		if (IoAccessBaseAddress & 1)
		{
			int addr = IoAccessBaseAddress / 2 & 0x7;
			IoMem[IoAccessBaseAddress] = Ncr5380_ReadByte(addr);
		}
		IoAccessBaseAddress++;
		nIoMemAccessSize--;
	}
}


void Ncr5380_TT_DMA_Ctrl_WriteWord(void)
{
	if (IoMem[0xff8715] & 2)
		dma_check(&ncr_soft_scsi);
}


