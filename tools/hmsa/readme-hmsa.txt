HMSA(1)                        Hatari utilities                        HMSA(1)



NAME
       hmsa - Atari MSA / ST disk image creator and converter

SYNOPSIS
       hmsa diskimage [disksize]

DESCRIPTION
       Hmsa  is  little  program  to create compressed Atari MSA (Magic Shadow
       Archiver) and uncompressed ST disk images and to  convert  disk  images
       between these two formats.

       MSA and ST image format saving code is same as in Hatari itself.

USAGE
       If  you give only one parameter, the file name of an existing MSA or ST
       disk image, this image will be converted to the other disk image format
       under a suitable new file name.

       If the file does not exist and you give also a disk size:

       SS     Single Sided (360KB)

       DS     Double Sided (720KB)

       HD     High Density (1.44MB)

       ED     Extended Density (2.88MB)

       An empty disk of the given size will be created.

       Disk  image  format  is  recognized  based  on  the file name extension
       (either .msa or .st).

EXAMPLES
       Create a normal double sided empty ST disk image:
            hmsa blank.st DS

       Convert an MSA format disk image to an ST format one:
            hmsa disk.msa

SEE ALSO
       Hmsa is part of hatari.

AUTHOR
       Written by Thomas Huth <huth at tuxfamily.org>.  This manual  page  and
       empty disk creation was added by Eero Tamminen.

LICENSE
       This program is free software; you can redistribute it and/or modify it
       under the terms of the GNU General Public License as published  by  the
       Free  Software Foundation; either version 2 of the License, or (at your
       option) any later version.

NO WARRANTY
       This program is distributed in the hope that it  will  be  useful,  but
       WITHOUT  ANY  WARRANTY;  without  even  the  implied  warranty  of MER‐
       CHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU  General
       Public License for more details.



Hatari                            2010-05-30                           HMSA(1)
