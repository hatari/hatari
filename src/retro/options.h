/*
  Hatari - options.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_RETRO_OPTIONS_H
#define HATARI_RETRO_OPTIONS_H

#include <libretro.h>

void libretro_set_core_options(retro_environment_t environ_cb, bool *categories_supported);
void Core_ApplyBootOptions(void);
void Core_ApplyRuntimeOptions(void);

#endif /* ifndef HATARI_RETRO_OPTIONS_H */
