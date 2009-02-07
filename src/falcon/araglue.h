// defines to get Aranym code running in Hatari... they should be removed later

#ifndef ARAGLUE_H
#define ARAGLUE_H

#if defined(_VCWIN_)
# define bug(args) printf(args);printf("\n")
#else
# define bug(args...) printf(args);printf("\n")
#endif

#if DEBUG
#define D(x) x
#else
#define D(x)
#endif

#endif /* ARAGLUE_H */
