/*
 * Hatari - cfgopts.h
 */

#ifndef HATARI_CFGOPTS_H
#define HATARI_CFGOPTS_H

typedef enum
{
  Error_Tag,
  Bool_Tag,
  Char_Tag,
  Short_Tag,
  Int_Tag,
  Long_Tag,
  Float_Tag,
  Double_Tag,
  String_Tag,
  Key_Tag
} TAG_TYPE;


struct Config_Tag
{
  const char *code;                /* Option switch        */
  TAG_TYPE   type;                 /* Type of option       */
  void       *buf;                 /* Storage location     */
};

int input_config(const char *, const struct Config_Tag *, const char *);
int update_config(const char *, const struct Config_Tag *, const char *);

#endif
