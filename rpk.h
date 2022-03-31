/* Rapid Picture Kompressor
 * Fast, lossless image comprehension inspired by work on QOI/QOIG.
 * The goal here is to be faster and simpler than QOI while still
 * doing "okay" at compressing.
 * 
 * The header is identical to that of QOI/QOIG, with the sole change
 * that the magic string is "RPK" instead of "QOIF".
 * 
 * The footer is also identical to that of QOI. (7 0 bytes and a 1 byte)
 * 
 * However, the compression algorithm is entirely different. On the highest
 * level there are only two kinds of operations: INDEX and RUN. The MSB of
 * the op byte encodes which of these two ops is being used.
 * 
 * 1. INDEX
 * 
 * ┌─ OP_INDEX ─────────────┐
 * │         Byte[0]        │
 * │ 7  6  5  4  3  2  1  0 │
 * │───┼────────────────────│
 * │ 0 │        index       │
 * └───┴────────────────────┘
 * 
 * There is a 128-color cache. If the op byte is less than 128, it is used
 * as an index into this cache and the color in that slot is inserted directly
 * into the output.
 * 
 * Every pixel encoded/decoded has its color inserted into the cache at the
 * position given by the hash function:
 *       ((((88 ^ red)*13 ^ green)*13 ^ blue)*13 ^ alpha) & 127
 * (This is essentially FNV-1a adapted to have decent dispersion over the range
 * 0-127.)
 * 
 * 2. RUN
 * 
 * ┌─ OP_RUN ───────────────┐
 * │         Byte[0]        │
 * │ 7  6  5  4  3  2  1  0 │
 * │───┼─────┼──────────────│
 * │ 1 │ type│   length     │
 * └───┴─────┴──────────────┘
 * 
 * One could think of RUN as being four different operations, but they all start
 * with the same structure as above. They all indicate that a modification of the
 * previous color should be inserted some number of times in a row. The "type" 
 * argument can be thought of as "how many bytes to read per color." But we'll
 * look at the four types separately and individually here:
 * 
 * ┌─ OP_RUN_TYPE_0 ────────┬────────────────────────┬────────────────────────┐
 * │         Byte[0]        │     Byte[1] (maybe)    │     Byte[2] (maybe)    │
 * │ 7  6  5  4  3  2  1  0 │ 7  6  5  4  3  2  1  0 │ 7  6  5  4  3  2  1  0 │
 * │─────────┼──────────────┼────────────────────────┼────────────────────────│
 * │ 1  0  0 │                             length                             │
 * └─────────┴──────────────┴────────────────────────┴────────────────────────┘
 * 
 * Run type 0 encodes a run of identical pixels, using up to 2 additional bytes
 * to encode the length of the run. To be precise, here's how the allowed lengths
 * are encoded:
 * 
 *               Length range          Code range         Total bytes
 * ───────────────────────────┼───────────────────┼──────────────────
 *                    1 to 16 │         0x80-0x8F │                 1
 *             17 to 2**11+16 │     0x9000-0x97FF │                 2
 * 2**11+17 to 2**19+2**11+16 │ 0x980000-0x9FFFFF │                 3
 * 
 * Bits 3 and 4 of the first byte signal whether additional bytes should be read.
 * 
 * Run types 1, 2, and 3 do not use any additional bytes to encode a longer run length.
 * They use only the length indicated by the op byte with a bias of 1 to indicate
 * a run of length 1 to 32.
 * 
 * Run type 1 will read an additional (length) bytes, each encoding an operation to
 * be applied to the previous color to compute the next. These bytes are parsed thusly:
 * 
 * ┌─ OP_RUN_TYPE_1_ARG ────┐
 * │         Byte[0]        │
 * │ 7  6  5  4  3  2  1  0 │
 * │──────┼─────┼─────┼─────│
 * │  dr  │ dg  │ db  │ da  │
 * └──────┴─────┴─────┴─────┘
 * 
 * The new color is computed by xoring each pair of bits with the least significant
 * two bits of the corresponding component. E.g. newred = oldred ^ dr
 * 
 * For a 3-channel image, the da bits should always be zeroed.
 * 
 * Run type 2 will similarly read an additional (2*length) bytes, 2 bytes per new color.
 * 
 * ┌─ OP_RUN_TYPE_2_ARG ────┬────────────────────────┐
 * │         Byte[0]        │         Byte[1]        │
 * │ 7  6  5  4  3  2  1  0 │ 7  6  5  4  3  2  1  0 │
 * │───────────────┼────────┼─────────┼──────────────│
 * │       dr      │        dg        │      db      │
 * └───────────────┴──────────────────┴──────────────┘
 * 
 * The new color is computed by xoring each group of bits with the least significant
 * corresponding bits of the corresponding component, exactly as per run type 1.
 * 
 * Run type 3 will similarly read an additional (channels*length) bytes, (channels) per new color.
 * 
 * ┌─ OP_RUN_TYPE_3_ARG ────┬────────────────────────┬────────────────────────┬────────────────────────┐
 * │         Byte[0]        │         Byte[1]        │         Byte[2]        │Byte[3] (iff channels=4)│
 * │ 7  6  5  4  3  2  1  0 │ 7  6  5  4  3  2  1  0 │ 7  6  5  4  3  2  1  0 │ 7  6  5  4  3  2  1  0 │
 * │────────────────────────┼────────────────────────┼────────────────────────┼────────────────────────│
 * │          red           │         green          │          blue          │         alpha          │
 * └────────────────────────┴────────────────────────┴────────────────────────┴────────────────────────┘
 * 
 * To save an operation when computing the next color, the color encoded by these bytes is precisely the
 * next color.
 * 
 * For a 3-channel image, the fourth byte *should be omitted entirely*.
 * 
 * Compression proceeds exactly one would expect: If the next color matches the previous, a run type 0 is set up.
 * Otherwise, if the next color is in the cache, an INDEX op is output. If not, the next and previous colors are
 * xored. If the result has only the least significant bits set, runs of type 1 or 2 is set up. Otherwise, a run 
 * of type 3 is set up. If a run has already been set up and the result of the comparison indicates the same
 * type of run again, the length of the run is extended and any arguments it will need are added to the buffer. If
 * the run type changes or an index becomes possible, the RUN op is output followed by all the buffered arguments.
 * 
 * There are two small exceptions to this scheme: Runs of type 1 will NOT be interrupted to output an INDEX. Likewise,
 * type two runs are NOT interrupted to begin a type 1 run. (Type 1 runs tend to be very short, so this is more likely
 * to cost an extra byte than not.)
 */
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

//Uses libspng with miniz
#define SPNG_STATIC
#define SPNG_USE_MINIZ
#include "spng.h"

#define RPK_SRBG 0
#define LRS(a,b) ((unsigned)(a)>>(b))
#define RPK_READ(a,b,c,d) if (fread(a,b,c,d)!=c) return -1
#define HASH(C) (((((88^C.red)*13^C.green)*13^C.blue)*13^C.alpha)&127)
#define EQCOLOR(a,b) (a.rgba == b.rgba)
#define PACKRUN(type,length) (128+((type)<<5)+(length))
#define MIN(a,b) ((a)>(b)?(b):(a))
#define RPK_PRINT(b)  if (run) {\
                          if (runtype) {\
                              fprintf(outfile,"%c",PACKRUN(runtype,run-1));\
                              fwrite(buffer,1,MIN(1<<(runtype-1),channels)*run,outfile);\
                              ct+=MIN(1<<(runtype-1),channels)*run+1;\
                              runtype = -1;\
                          } else {\
                              if (run<=16) {\
                                  fprintf(outfile,"%c",PACKRUN(runtype,run-1));\
                                  ct++;\
                              } else {\
                                  run-=17;\
                                  if (run<1<<11) {\
                                      fprintf(outfile,"%c%c",PACKRUN(runtype,16+LRS(run,8)),run&0xFF);\
                                      ct+=2;\
                                  } else {\
                                      run-=1<<11;\
                                      fprintf(outfile,"%c%c%c",PACKRUN(runtype,24+LRS(run,16)),LRS(run,8)&0xFF,run&0xFF);\
                                      ct+=3;\
                                  }\
                              }\
                          }\
                      }\
                      if (b<128) {\
                        fprintf(outfile,"%c",b);\
                        ct++;\
                      }\
                      run = 0

typedef union {
    uint32_t rgba;
    struct {
        unsigned char red;
        unsigned char green;
        unsigned char blue;
        unsigned char alpha;
    };
} color;

typedef struct {
	uint32_t width;
	uint32_t height;
	uint8_t channels;
	uint8_t colorspace;
} rpk_desc;



int rpk_encode(spng_ctx *ctx, size_t width, FILE *outfile, unsigned long *outlen, uint8_t channels) {
    color cache[128] = {0};
    color row[width];
    color last,diff;
    color current = (color){.alpha=255};
    color type2mask = (color){.red = 0xE0,.green = 0xC0,.blue = 0xE0,.alpha=0xFF};
    uint8_t buffer[128];
    unsigned int i;
    uint8_t runtype = -1;
    uint8_t lastrow = 0;
    uint8_t ret,done;
    uint32_t run = 0;
    unsigned long ct = 0;
    //int rows_read = 0;
    
    
    /*spng_decode_row is a bad API. a sane API would return 0 after every successful read*/
    while (!(ret = spng_decode_row(ctx, row, 4*width)) || lastrow && ret == SPNG_EOI) {
        done = ret == SPNG_EOI && !lastrow;
        lastrow = !ret;
        for (i=0;i<width;i+=1) {
            
            last = current;
            
            //Get next pixel

            current=row[i];
            
            if (EQCOLOR(current,last)) {
                if (!runtype && run<526352) {
                    run++;
                } else {
                    RPK_PRINT(128);
                    run=1;
                    runtype=0;
                }
                continue;
            }
            diff.rgba = current.rgba^last.rgba;
            if (!(diff.rgba&0xFCFCFCFC) && run && runtype==1) goto smalldiff;
                
            if (EQCOLOR(current,cache[HASH(current)])) {
                RPK_PRINT(HASH(current));
            } else {
                if (!(diff.rgba&0xFCFCFCFC) && runtype!=2) {
                    if (run && runtype!=1 || run==32) {
                        RPK_PRINT(128);
                        run=0;
                    }
                    smalldiff:buffer[run++]=(diff.alpha|diff.blue<<2|diff.green<<4|diff.red<<6)&0xFF;
                    runtype=1;
                } else if (!(diff.rgba&type2mask.rgba)) {
                    if (run && runtype!=2 || run==32) {
                        RPK_PRINT(128);
                        run=0;
                    }
                    buffer[run*2]=(diff.red<<3|LRS(diff.green,3))&0xFF;
                    buffer[run*2+1]=(diff.green<<5|diff.blue&0x1F)&0xFF;
                    run++;
                    runtype=2;
                } else {
                    if (run && runtype!=3 || run==32) {
                        RPK_PRINT(128);
                        run=0;
                    }
                    buffer[run*channels]=current.red;
                    buffer[run*channels+1]=current.green;
                    buffer[run*channels+2]=current.blue;
                    if (channels==4) buffer[run*4+3]=current.alpha;
                    run++;
                    runtype=3;
                }
                cache[HASH(current)]=current;
            }
        }
        //rows_read++;
    }
    //Flush all buffers
    RPK_PRINT(0);
    
    *outlen = ct;
    return 0;
}

int rpk_decode(FILE *infile, size_t width, spng_ctx *ctx, size_t *outlen, uint8_t channels) {
    color cache[128] = {0};
    color current = (color){.alpha=255};
    color temp;
    unsigned int i;
    uint8_t cbyte = 0;
    uint8_t tempbyte = 0;
    uint8_t runtype;
    uint32_t run=0;
    uint8_t row[width*channels];
    //unsigned int rows_read = 0;
    int ret;
    *outlen = 0;
    
    do { 
        for (i=0;i<channels*width;i+=channels) {
            if (run) goto runcont; 
            RPK_READ(&cbyte,1,1,infile);
            switch(cbyte&0x80) {
                case 0:
                    current = cache[cbyte];
                    break;
                case 0x80:
                    if (!run) {
                        runtype = LRS(cbyte&0x60,5);
                        run = (cbyte&0x1F);
                        if (!runtype) {
                            if (run>=16) {
                                run &= 15;
                                if (run>=8) {
                                    run &= 7;
                                    RPK_READ(&tempbyte,1,1,infile);
                                    run = (run<<8)|tempbyte;
                                    run += 8;
                                }
                                RPK_READ(&tempbyte,1,1,infile);
                                run = (run<<8)|tempbyte;
                                run += 16;
                            }
                        }
                        run++;
                    }
                    runcont:run--;
                    switch (runtype) {
                        case 1:
                            RPK_READ(&tempbyte,1,1,infile);
                            current.red ^= LRS(tempbyte,6)&3;
                            current.green ^= LRS(tempbyte,4)&3;
                            current.blue ^= LRS(tempbyte,2)&3;
                            if (channels>3) current.alpha ^= tempbyte&3;
                            break;
                        case 2:
                            RPK_READ(&temp,1,2,infile);
                            current.red ^= LRS(temp.red,3)&0x1F;
                            current.green ^= (temp.red&7)<<3|LRS(temp.green,5);
                            current.blue ^= temp.green&0x1F;
                            break;
                        case 3:
                            RPK_READ(&current,1,channels,infile);
                    }
                    cache[HASH(current)]=current;
            }
            memcpy(row+i,&current,channels);
            *outlen += channels;
        }
    
        ret = spng_encode_row(ctx,row,channels*width);
        //rows_read++;
    } while (!ret);
    //If we make it here, we're missing an end of bytestream code,
    //so there is probably something wrong with the file.
    return !(ret==SPNG_EOI);
}


size_t rpk_write(const char *infile, const char *outfile) {
    FILE *inf;
	FILE *outf;
	size_t size, width;
    size_t byte_len;
    size_t limit = 1024 * 1024 * 64;
	char *encoded = NULL;
    uint32_t temp;
    int fmt = SPNG_FMT_RGBA8;
    rpk_desc desc;
    spng_ctx *ctx;
    
    inf = fopen(infile,"rb");
    outf = fopen(outfile,"wb");
    if (!inf||!outf) {
		goto error;
	}

    ctx = spng_ctx_new(0);

    if (!ctx) {
        goto error;
    }

    // Ignore and don't calculate chunk CRC's
    spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);    

    /* Set memory usage limits for storing standard and unknown chunks,
       this is important when reading untrusted files! */
    spng_set_chunk_limits(ctx, limit, limit);

    // Set source PNG
    spng_set_png_file(ctx, inf);

    struct spng_ihdr ihdr;

    if (spng_get_ihdr(ctx, &ihdr)||spng_decoded_image_size(ctx, fmt, &byte_len)||
        spng_decode_image(ctx, NULL, 0, fmt, SPNG_DECODE_PROGRESSIVE)) {
        goto error;
    }
    
    
    width = byte_len / (4*ihdr.height);
    
    //Construct description
    desc.width = width;
    desc.height = ihdr.height;
    desc.channels = 3+(ihdr.color_type>>2&1);
    //since we're just converting from png, probably safe to assume sRBG colorspace
    desc.colorspace = RPK_SRBG;
    
    
    
    //Write file header
    fprintf(outf,"rpk");
    temp = htonl(desc.width);
    fwrite(&temp, 1, 4, outf);
    temp = htonl(desc.height);
    fwrite(&temp, 1, 4, outf);
    fprintf(outf,"%c%c", desc.channels, desc.colorspace);


	if (rpk_encode(ctx, width, outf, &size, desc.channels)) {
		goto error;
	}
    

    //I have no idea what the file footer is for.
    //Only print 7 bytes because we printed 1 coming out of rpk_encode
    fwrite("\0\0\0\0\0\0\1",1,7,outf);
    fclose(outf);

    fclose(inf);
    
    spng_ctx_free(ctx);
	
	return size;
    error:
        fclose(inf);
        fclose(outf);
        spng_ctx_free(ctx);
        return -1;
}


size_t rpk_read(const char *infile, const char *outfile) {
	FILE *inf = fopen(infile, "rb");
    FILE *outf = fopen(outfile, "wb");
	size_t size;
    long bytes_read, px_len;
    char magic[3];
    rpk_desc desc;
    struct spng_ihdr ihdr = {0};
    spng_ctx *enc;
    int fmt;

    if (!inf || !outf) {
        goto error;
    }

    enc = spng_ctx_new(SPNG_CTX_ENCODER);

    if (!enc) {
        goto error;
    }


	//Check magic string
    if (fread(magic,1,3,inf)!=3||memcmp(magic,"rpk",3)) {
        goto error;
    }
    
    //Extract desc from header
    if (fread(&desc, 1, 10, inf)!=10) {
        goto error;
    }
    

    //Fix byte order on dimensions
    desc.width = ntohl(desc.width);
    desc.height = ntohl(desc.height);

    //Create PNG header
    ihdr.width = desc.width;
    ihdr.height = desc.height;
    ihdr.bit_depth = 8;
    ihdr.color_type = 4*desc.channels-10;
    
    if (spng_set_ihdr(enc,&ihdr)) {
        goto error;
    }
    
    //Set file for context
    spng_set_png_file(enc,outf);
    
    //Encoding format
    fmt = SPNG_FMT_PNG;
    
    
	if (spng_encode_image(enc, 0, 0, fmt, SPNG_ENCODE_PROGRESSIVE)||rpk_decode(inf, desc.width, enc, &size, desc.channels)) {
        goto error;
    }
    
    fclose(inf);
    fclose(outf);
    spng_ctx_free(enc);
	return size;

    error:
        fclose(inf);
        fclose(outf);
        spng_ctx_free(enc);
        return -1;
}
