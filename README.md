# RPK - Rapid Picture Kompressor
Fast, lossless image compression format. Conversion to and from PNG. Encodes and decodes up to twice as fast as QOI.

## DEPENDS ON
libspng <https://github.com/randy408/libspng/> (tested on version 0.7.1) using miniz (https://github.com/richgel999/miniz)

## COMPILES LIKE
I use `gcc -O3 rpkconv.c -o rpkconv spng.o miniz.o -lm` where spng was compiled with the miniz compiler option, modified to let them live in the same source folder rather than installing miniz as a library. If you have miniz installed as library, this would look more like `gcc -O3 rpkconv.c -o rpkconv spng.o -lminiz -lm` (but don't quote me on the latter). I'm not providing a makefile because it's beyond the scope of this project to make it easy to compile with your preferred settings.

## GOALS
- Fast streaming converter supporting large file sizes. (I don't know how large this can do, but it should theoretically be able to handle images many gigabytes in size.)
- Faster than QOI
- Pretty good compression (compared with QOI).

## DETAILS
See qoig.h

## PERFORMANCE ANALYSIS
Coming soon

## FUTURE STUFF?
- animated rpk?
