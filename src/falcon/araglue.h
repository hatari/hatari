// defines to get Aranym code running in Hatari... they should be removed later

#ifndef ARAGLUE_H
#define ARAGLUE_H

#define bug(args...) printf(args);printf("\n")
#define DUNUSED(x)

#if DEBUG
#define D(x) x
#else
#define D(x)
#endif

#endif /* ARAGLUE_H */
