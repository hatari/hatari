// license:BSD-3-Clause
// copyright-holders:Olivier Galibert

#include <string.h>

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "fdc.h"
#include "floppies/ipf.h"
#include "log.h"
#include "memorySnapShot.h"
#include "utils.h"


/* Magnetic cell encoding used while building a track (MAME flopimg levels) */
#define MG_SHIFT	28
#define MG_MASK		0xf0000000u
#define TIME_MASK	0x0fffffffu
#define MG_0		(4u << MG_SHIFT)
#define MG_1		(5u << MG_SHIFT)
#define MG_N		(1u << MG_SHIFT)

#define IPF_MAX_CYLINDER	84
#define IPF_MAX_HEAD		2
#define IPF_DEFAULT_TCOUNT	(IPF_MAX_CYLINDER * IPF_MAX_HEAD + 1)


typedef struct track_info {
	uint32_t cylinder, head, type;
	uint32_t sigtype, process, reserved[3];
	uint32_t size_bytes, size_cells;
	uint32_t index_bytes, index_cells;
	uint32_t datasize_cells, gapsize_cells;
	uint32_t block_count, weak_bits;

	uint32_t data_size_bits;

	bool info_set;

	const uint8_t *data;
	uint32_t data_size;
} track_info;

typedef struct ipf_decode {
	track_info *tinfos;
	uint32_t tcount;

	uint32_t type, release, revision;
	uint32_t encoder_type, encoder_revision, origin;
	uint32_t min_cylinder, max_cylinder, min_head, max_head;
	uint32_t credit_day, credit_time;
	uint32_t platform[4], extra[5];
} ipf_decode;


#define get_u32be(x) Mem_ReadU32_BE((const uint8_t *)(x))
static bool ipf_generate_track(ipf_decode *dec, track_info *t);
static bool ipf_generate_block(const track_info *t, uint32_t idx, uint32_t ipos,
		uint32_t *track, uint32_t *pos, uint32_t *dpos, uint32_t *gpos,
		uint32_t *spos, bool *context);
static uint32_t block_compute_real_size(const track_info *t);


static int ipf_identify(const uint8_t *h, size_t size)
{
	static const uint8_t refh[12] = { 0x43, 0x41, 0x50, 0x53, 0x00, 0x00, 0x00, 0x0c, 0x1c, 0xd5, 0x73, 0xba };

	if (size < sizeof(refh))
		return 0;

	if(!memcmp(h, refh, 12))
		return 1;

	return 0;
}

static uint32_t ipf_rb(const uint8_t **p, int count)
{
	uint32_t v = 0;
	for(int i=0; i<count; i++)
		v = (v << 8) | *(*p)++;
	return v;
}

static uint32_t crc32r(const uint8_t *data, uint32_t size)
{
	// Reversed crc32
	uint32_t crc = 0xffffffff;
	for(uint32_t i=0; i != size; i++) {
		crc = crc ^ data[i];
		for(int j=0; j<8; j++)
			if(crc & 1)
				crc = (crc >> 1) ^ 0xedb88320;
			else
				crc = crc >> 1;
	}
	return ~crc;
}

static bool ipf_parse_info(ipf_decode *dec, const uint8_t *info)
{
	dec->type = get_u32be(info+12);
	if (dec->type != 1)
		return false;
	dec->encoder_type = get_u32be(info+16); // 1 for CAPS, 2 for SPS
	dec->encoder_revision = get_u32be(info+20); // 1 always
	dec->release = get_u32be(info+24);
	dec->revision = get_u32be(info+28);
	dec->origin = get_u32be(info+32); // Original source reference
	dec->min_cylinder = get_u32be(info+36);
	dec->max_cylinder = get_u32be(info+40);
	dec->min_head = get_u32be(info+44);
	dec->max_head = get_u32be(info+48);
	dec->credit_day = get_u32be(info+52);  // year*1e4 + month*1e2 + day
	dec->credit_time = get_u32be(info+56); // hour*1e7 + min*1e5 + sec*1e3 + msec
	for(int i=0; i<4; i++)
		dec->platform[i] = get_u32be(info+60+4*i);
	for(int i=0; i<5; i++)
		dec->extra[i] = get_u32be(info+76+4*i);
	return true;
}

static track_info *ipf_get_index(ipf_decode *dec, uint32_t idx)
{
	track_info *ni;

	if(idx > 1000)
		return NULL;
	if (idx >= dec->tcount)
	{
		ni = realloc(dec->tinfos, (idx + 1) * sizeof(track_info));
		if (!ni)
			return NULL;
		memset(ni + dec->tcount, 0, (idx + 1 - dec->tcount) * sizeof(track_info));
		dec->tinfos = ni;
		dec->tcount = idx + 1;
	}

	return &dec->tinfos[idx];
}

static bool ipf_parse_imge(ipf_decode *dec, const uint8_t *imge)
{
	track_info *t = ipf_get_index(dec, get_u32be(imge+64));
	if(!t)
		return false;

	t->info_set = true;

	t->cylinder = get_u32be(imge+12);
	if(t->cylinder < dec->min_cylinder || t->cylinder > dec->max_cylinder)
		return false;

	t->head = get_u32be(imge+16);
	if (t->head < dec->min_head || t->head > dec->max_head)
		return false;

	t->type = get_u32be(imge+20);
	t->sigtype = get_u32be(imge+24); // 1 for 2us cells, no other value valid
	t->size_bytes = get_u32be(imge+28);
	t->index_bytes = get_u32be(imge+32);
	t->index_cells = get_u32be(imge+36);
	t->datasize_cells = get_u32be(imge+40);
	t->gapsize_cells = get_u32be(imge+44);
	t->size_cells = get_u32be(imge+48);
	t->block_count = get_u32be(imge+52);
	t->process = get_u32be(imge+56); // encoder process, always 0
	t->weak_bits = get_u32be(imge+60);
	t->reserved[0] = get_u32be(imge+68);
	t->reserved[1] = get_u32be(imge+72);
	t->reserved[2] = get_u32be(imge+76);

	return true;
}

static bool ipf_parse_data(ipf_decode *dec, const uint8_t *data, uint32_t *pos,
                           uint32_t max_extra_size)
{
	track_info *t = ipf_get_index(dec, get_u32be(data+24));
	if(!t)
		return false;

	t->data_size_bits = get_u32be(data+16);
	t->data = data+28;
	t->data_size = get_u32be(data+12);
	if(t->data_size > max_extra_size)
		return false;
	if(crc32r(t->data, t->data_size) != get_u32be(data+20))
		return false;
	*pos += t->data_size;
	return true;
}

static bool ipf_scan_one_tag(uint8_t *data, size_t size, uint32_t *pos,
                             uint8_t **tag, uint32_t *tsize)
{
	if (size - *pos < 12)
		return false;
	*tag = &data[*pos];
	*tsize = get_u32be(*tag + 4);
	if (size - *pos < *tsize)
		return false;
	uint32_t crc = get_u32be(*tag+8);
	(*tag)[8] = (*tag)[9] = (*tag)[10] = (*tag)[11] = 0;
	if (crc32r(*tag, *tsize) != crc)
		return false;
	*pos += *tsize;
	return true;
}

static bool ipf_scan_all_tags(ipf_decode *dec, uint8_t *data, size_t size)
{
	uint32_t pos = 0;
	while(pos != size) {
		uint8_t *tag;
		uint32_t tsize;

		if (!ipf_scan_one_tag(data, size, &pos, &tag, &tsize))
			return false;

		switch(get_u32be(tag)) {
		case 0x43415053: // CAPS
			if(tsize != 12)
				return false;
			break;

		case 0x494e464f: // INFO
			if(tsize != 96)
				return false;
			if (!ipf_parse_info(dec, tag))
				return false;
			break;

		case 0x494d4745: // IMGE
			if(tsize != 80)
				return false;
			if (!ipf_parse_imge(dec, tag))
				return false;
			break;

		case 0x44415441: // DATA
			if(tsize != 28)
				return false;
			if (!ipf_parse_data(dec, tag, &pos, size - pos))
				return false;
			break;

		default:
			return false;
		}
	}
	return true;
}

static bool ipf_generate_tracks(ipf_decode *dec /*, floppy_image &image */)
{
	for (uint32_t i = 0; i != dec->tcount; i++)
	{
		track_info *t = &dec->tinfos[i];
		if (t->info_set && t->data)
		{
			if (!ipf_generate_track(dec, t/*, image*/))
				return false;

		}
		else if (t->info_set || t->data)
			return false;
	}
	return true;
}

static void ipf_rotate(uint32_t *track, uint32_t offset, uint32_t size)
{
	uint32_t done = 0;
	for(uint32_t bpos=0; done < size; bpos++) {
		uint32_t pos = bpos;
		uint32_t hold = track[pos];
		for(;;) {
			uint32_t npos = pos+offset;
			if(npos >= size)
				npos -= size;
			if(npos == bpos)
				break;
			track[pos] = track[npos];
			pos = npos;
			done++;
		}
		track[pos] = hold;
		done++;
	}
}

static void ipf_mark_track_splice(uint32_t *track, uint32_t offset, uint32_t size)
{
	for(int i=0; i<3; i++) {
		uint32_t pos = (offset + i) % size;
		uint32_t v = track[pos];
		if ((v & MG_MASK) == MG_0)
			v = (v & TIME_MASK) | MG_1;
		else if ((v & MG_MASK) == MG_1)
			v = (v & TIME_MASK) | MG_0;
		track[pos] = v;
	}
}

static void timing_set(uint32_t *track, uint32_t start, uint32_t end,
                       uint32_t time)
{
	for(uint32_t i=start; i != end; i++)
		track[i] = (track[i] & MG_MASK) | time;
}

static bool generate_timings(const track_info *t, uint32_t *track,
                             const uint32_t *data_pos, const uint32_t *gap_pos)
{
	timing_set(track, 0, t->size_cells, 2000);

	switch(t->type) {
	case 2: break;

	case 3:
		if (t->block_count >= 4)
			timing_set(track, gap_pos[3], data_pos[4], 1890);
		if (t->block_count >= 5) {
			timing_set(track, data_pos[4], gap_pos[4], 1890);
			timing_set(track, gap_pos[4], data_pos[5], 1990);
		}
		if (t->block_count >= 6) {
			timing_set(track, data_pos[5], gap_pos[5], 1990);
			timing_set(track, gap_pos[5], data_pos[6], 2090);
		}
		if (t->block_count >= 7)
			timing_set(track, data_pos[6], gap_pos[6], 2090);
		break;

	case 4:
		timing_set(track, gap_pos[t->block_count-1], data_pos[0], 1890);
		timing_set(track, data_pos[0], gap_pos[0], 1890);
		timing_set(track, gap_pos[0], data_pos[1], 1990);
		if (t->block_count >= 2) {
			timing_set(track, data_pos[1], gap_pos[1], 1990);
			timing_set(track, gap_pos[1], data_pos[2], 2090);
		}
		if (t->block_count >= 3)
			timing_set(track, data_pos[2], gap_pos[2], 2090);
		break;

	case 5:
		if (t->block_count >= 6)
			timing_set(track, data_pos[5], gap_pos[5], 2100);
		break;

	case 6:
		if (t->block_count >= 2)
			timing_set(track, data_pos[1], gap_pos[1], 2200);
		if (t->block_count >= 3)
			timing_set(track, data_pos[2], gap_pos[2], 1800);
		break;

	case 7:
		if (t->block_count >= 2)
			timing_set(track, data_pos[1], gap_pos[1], 2100);
		break;

	case 8:
		if (t->block_count >= 2)
			timing_set(track, data_pos[1], gap_pos[1], 2200);
		if (t->block_count >= 3)
			timing_set(track, data_pos[2], gap_pos[2], 2100);
		if (t->block_count >= 5)
			timing_set(track, data_pos[4], gap_pos[4], 1900);
		if (t->block_count >= 6)
			timing_set(track, data_pos[5], gap_pos[5], 1800);
		if (t->block_count >= 7)
			timing_set(track, data_pos[6], gap_pos[6], 1700);
		break;

	case 9: {
		uint32_t mask = get_u32be(t->data + 32 * t->block_count + 12);
		for (uint32_t i = 1; i < t->block_count; i++)
			timing_set(track, data_pos[i], gap_pos[i], mask & (1 << (i-1)) ? 1900 : 2100);
		break;
	}

	default:
		return false;
	}

	return true;
}

static bool ipf_generate_track(ipf_decode *dec, track_info *t /*, floppy_image &image*/)
{
	uint32_t *track = NULL;
	uint32_t *data_pos = NULL;
	uint32_t *gap_pos = NULL;
	uint32_t *splice_pos = NULL;
	bool context = false;
	uint32_t pos = 0;
	bool ok = false;

	if (!t->size_cells)
		return true;

	if (t->data_size < 32 * t->block_count)
		return false;

	// Annoyingly enough, too small gaps are ignored, changing the
	// total track size.  Artifact stemming from the byte-only support
	// of old times?
	t->size_cells = block_compute_real_size(t);

	if (t->index_cells >= t->size_cells)
		return false;

	track = calloc(t->size_cells, sizeof(uint32_t));
	data_pos = calloc(t->block_count + 1, sizeof(uint32_t));
	gap_pos = calloc(t->block_count, sizeof(uint32_t));
	splice_pos = calloc(t->block_count, sizeof(uint32_t));
	if (!track || !data_pos || !gap_pos || !splice_pos)
		goto done;

	for (uint32_t i = 0; i != t->block_count; i++) {
		if (!ipf_generate_block(t, i,
				i == t->block_count-1 ? t->size_cells - t->index_cells : 0xffffffff,
				track, &pos, &data_pos[i], &gap_pos[i], &splice_pos[i], &context))
		{
			goto done;
		}
	}
	if (pos != t->size_cells)
		goto done;

	data_pos[t->block_count] = pos;

	ipf_mark_track_splice(track, splice_pos[t->block_count-1], t->size_cells);

	if(!generate_timings(t, track, data_pos, gap_pos)) {
		goto done;
	}

	if (t->index_cells)
		ipf_rotate(track, t->size_cells - t->index_cells, t->size_cells);

#if 0
	generate_track_from_levels(t->cylinder, t->head, track,
	                           splice_pos[t->block_count-1] + t->index_cells,
	                           image);
#else
	fprintf(stderr,"FIXME: Add generate_track_from_levels()!!!\n");
#endif

	ok = true;
done:
	free(track);
	free(data_pos);
	free(gap_pos);
	free(splice_pos);

	return ok;
}

static void ipf_track_write_raw(uint32_t **tpos, const uint8_t *data, uint32_t cells, bool *context)
{
	for(uint32_t i=0; i != cells; i++)
		*(*tpos)++ = data[i >> 3] & (0x80 >> (i & 7)) ? MG_1 : MG_0;
	if(cells)
		*context = (*tpos)[-1] == MG_1;
}

static void ipf_track_write_mfm(uint32_t **tpos, const uint8_t *data,
                                uint32_t start_offset, uint32_t patlen,
                                uint32_t cells, bool *context)
{
	patlen *= 2;
	for(uint32_t i=0; i != cells; i++) {
		uint32_t pos = (i + start_offset) % patlen;
		bool bit = data[pos>>4] & (0x80 >> ((pos >> 1) & 7));
		if(pos & 1) {
			*(*tpos)++ = bit ? MG_1 : MG_0;
			*context = bit;
		} else
			*(*tpos)++ = *context || bit ? MG_0 : MG_1;
	}
}

static void ipf_track_write_weak(uint32_t **tpos, uint32_t cells)
{
	for(uint32_t i=0; i != cells; i++)
		*(*tpos)++ = MG_N;
}

static bool ipf_generate_block_data(const uint8_t *data, const uint8_t *dlimit,
                                    uint32_t *tpos, uint32_t *tlimit, bool *context)
{
	for(;;) {
		if(data >= dlimit)
			return false;
		uint8_t val = *data++;
		if((val >> 5) > dlimit-data)
			return false;
		uint32_t param = ipf_rb(&data, val >> 5);
		uint32_t tleft = tlimit - tpos;
		switch(val & 0x1f) {
		case 0: // End of description
			return !tleft;

		case 1: // Raw bytes
			if(8*param > tleft)
				return false;
			ipf_track_write_raw(&tpos, data, 8 * param, context);
			data += param;
			break;

		case 2: // MFM-decoded data bytes
		case 3: // MFM-decoded gap bytes
			if(16*param > tleft)
				return false;
			ipf_track_write_mfm(&tpos, data, 0, 8 * param, 16*param,
			                    context);
			data += param;
			break;

		case 5: // Weak bytes
			if(16*param > tleft)
				return false;
			ipf_track_write_weak(&tpos, 16 * param);
			*context = 0;
			break;

		default:
			return false;
		}
	}
}

static bool ipf_generate_block_gap_0(uint32_t gap_cells, uint8_t pattern,
                                     uint32_t *spos, uint32_t ipos,
                                     uint32_t **tpos, bool *context)
{
	uint32_t delta = 0;

	*spos = ipos >= 16 && ipos+16 <= gap_cells ? ipos : gap_cells >> 1;
	ipf_track_write_mfm(tpos, &pattern, 0, 8, *spos, context);
	if(gap_cells & 1) {
		*(*tpos)++ = MG_0;
		delta++;
	}
	ipf_track_write_mfm(tpos, &pattern, *spos + delta - gap_cells, 8,
	                    gap_cells - *spos - delta, context);
	return true;
}

static bool gap_description_to_reserved_size(const uint8_t **data,
		const uint8_t *dlimit, uint32_t *res_size)
{
	*res_size = 0;
	for(;;) {
		if (*data >= dlimit)
			return false;
		uint8_t val = *(*data)++;
		if ((val >> 5) > dlimit - *data)
			return false;
		uint32_t param = ipf_rb(data, val >> 5);
		switch(val & 0x1f) {
		case 0:
			return true;
		case 1:
			*res_size += param * 2;
			break;
		case 2:
			*data += (param+7)/8;
			break;
		default:
			return false;
		}
	}
}

static bool ipf_generate_gap_from_description(const uint8_t **data,
		const uint8_t *dlimit, uint32_t *tpos, uint32_t size,
		bool pre, bool *context)
{
	const uint8_t *data1 = *data;
	uint32_t res_size;
	if (!gap_description_to_reserved_size(&data1, dlimit, &res_size))
		return false;

	if(res_size > size)
		return false;
	uint8_t pattern[16];
	memset(pattern, 0, sizeof(pattern));
	uint32_t pattern_size = 0;

	uint32_t pos = 0, block_size = 0;
	for(;;) {
		uint8_t val = *(*data)++;
		uint32_t param = ipf_rb(data, val >> 5);
		switch(val & 0x1f) {
		case 0:
			return size == pos;

		case 1:
			if(block_size)
				return false;
			block_size = param*2;
			pattern_size = 0;
			break;

		case 2:
			// You can't have a pattern at the start of a pre-slice
			// gap if there's a size afterwards
			if(pre && res_size && !block_size)
				return false;
			// You can't have two consecutive patterns
			if(pattern_size)
				return false;
			pattern_size = param;
			if(pattern_size > sizeof(pattern)*8)
				return false;

			memcpy(pattern, *data, (pattern_size+7)/8);
			*data += (pattern_size+7)/8;
			if(pre) {
				if(!block_size)
					block_size = size;
				else if(pos + block_size == res_size)
					block_size = size - pos;
				if(pos + block_size > size)
					return false;
				//              printf("pat=%02x size=%d pre\n", pattern[0], block_size);
				ipf_track_write_mfm(&tpos, pattern, 0, pattern_size, block_size, context);
				pos += block_size;
			} else {
				if(pos == 0 && block_size && res_size != size)
					block_size = size - (res_size-block_size);
				if(!block_size)
					block_size = size - res_size;
				if(pos + block_size > size)
					return false;
				//              printf("pat=%02x block_size=%d size=%d res_size=%d post\n", pattern[0], block_size, size, res_size);
				ipf_track_write_mfm(&tpos, pattern, -block_size, pattern_size, block_size, context);
				pos += block_size;
			}
			block_size = 0;
			break;
		}
	}
}


static bool ipf_generate_block_gap_1(uint32_t gap_cells, uint32_t *spos, uint32_t ipos,
		const uint8_t *data, const uint8_t *dlimit, uint32_t **tpos, bool *context)
{
	if(ipos >= 16 && ipos < gap_cells-16)
		*spos = ipos;
	else
		*spos = 0;
	return ipf_generate_gap_from_description(&data, dlimit, *tpos, gap_cells, true, context);
}

static bool ipf_generate_block_gap_2(uint32_t gap_cells, uint32_t *spos, uint32_t ipos,
		const uint8_t *data, const uint8_t *dlimit, uint32_t **tpos, bool *context)
{
	if(ipos >= 16 && ipos < gap_cells-16)
		*spos = ipos;
	else
		*spos = gap_cells;
	return ipf_generate_gap_from_description(&data, dlimit, *tpos, gap_cells, false, context);
}

static bool ipf_generate_block_gap_3(uint32_t gap_cells, uint32_t *spos, uint32_t ipos,
		const uint8_t *data, const uint8_t *dlimit, uint32_t **tpos, bool *context)
{
	if(ipos >= 16 && ipos < gap_cells-16)
		*spos = ipos;
	else {
		uint32_t presize, postsize;
		const uint8_t *data1 = data;
		if (!gap_description_to_reserved_size(&data1, dlimit, &presize))
			return false;
		if (!gap_description_to_reserved_size(&data1, dlimit, &postsize))
			return false;
		if(presize+postsize > gap_cells)
			return false;

		*spos = presize + (gap_cells - presize - postsize)/2;
	}
	if (!ipf_generate_gap_from_description(&data, dlimit, *tpos, *spos, true, context))
		return false;
	uint32_t delta = 0;
	if(gap_cells & 1) {
		(*tpos)[*spos] = MG_0;
		delta++;
	}

	return ipf_generate_gap_from_description(&data, dlimit, *tpos + *spos + delta,
	                                         gap_cells - *spos - delta, false, context);
}

static bool ipf_generate_block_gap(uint32_t gap_type, uint32_t gap_cells, uint8_t pattern,
		uint32_t *spos, uint32_t ipos, const uint8_t *data, const uint8_t *dlimit,
		uint32_t *tpos, bool *context)
{
	switch(gap_type) {
	case 0:
		return ipf_generate_block_gap_0(gap_cells, pattern, spos, ipos, &tpos, context);
	case 1:
		return ipf_generate_block_gap_1(gap_cells, spos, ipos, data, dlimit, &tpos, context);
	case 2:
		return ipf_generate_block_gap_2(gap_cells, spos, ipos, data, dlimit, &tpos, context);
	case 3:
		return ipf_generate_block_gap_3(gap_cells, spos, ipos, data, dlimit, &tpos, context);
	default:
		return false;
	}
}

static bool ipf_generate_block(const track_info *t, uint32_t idx, uint32_t ipos,
		uint32_t *track, uint32_t *pos, uint32_t *dpos, uint32_t *gpos,
		uint32_t *spos, bool *context)
{
	const uint8_t *data = t->data;
	const uint8_t *data_end = t->data + t->data_size;
	const uint8_t *thead = data + 32*idx;
	uint32_t data_cells = get_u32be(thead);
	uint32_t gap_cells = get_u32be(thead+4);

	if(gap_cells < 8)
		gap_cells = 0;

	// +8  = gap description offset / datasize in bytes (when gap type = 0)
	// +12 =                      1 / gap size in bytes (when gap type = 0)
	// +16 = 1
	// +20 = gap type
	// +24 = type 0 gap pattern (8 bits) / speed mask for sector 0 track type 9
	// +28 = data description offset

	*dpos = *pos;
	*gpos = *dpos + data_cells;
	*pos = *gpos + gap_cells;
	if (*pos > t->size_cells)
		return false;
	if (!ipf_generate_block_data(data + get_u32be(thead+28), data_end,
	                             track + *dpos, track + *gpos, context))
		return false;
	if (!ipf_generate_block_gap(get_u32be(thead+20), gap_cells, get_u32be(thead+24),
	                            spos, ipos > *gpos ? ipos - *gpos : 0,
	                            data + get_u32be(thead+8), data_end,
	                            track + *gpos, context))
		return false;
	*spos += *gpos;

	return true;
}

static uint32_t block_compute_real_size(const track_info *t)
{
	uint32_t size = 0;
	const uint8_t *thead = t->data;
	for (unsigned int i=0; i != t->block_count; i++) {
		uint32_t data_cells = get_u32be(thead);
		uint32_t gap_cells = get_u32be(thead+4);
		if(gap_cells < 8)
			gap_cells = 0;

		size += data_cells + gap_cells;
		thead += 32;
	}
	return size;
}
