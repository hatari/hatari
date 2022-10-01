#ifndef HATARI_SCSIDRV_H
#define HATARI_SCSIDRV_H

bool nf_scsidrv(uint32_t stack, uint32_t subid, uint32_t *retval);
void nf_scsidrv_reset(void);

#endif /* HATARI_SCSIDRV_H */
