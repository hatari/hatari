/*
  Hatari
*/

extern int MSA_UnCompress(unsigned char *pMSAFile,unsigned char *pBuffer);
extern int MSA_ReadDisc(char *pszFileName,unsigned char *pBuffer);
extern BOOL MSA_WriteDisc(char *pszFileName,unsigned char *pBuffer,int ImageSize);
