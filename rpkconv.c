#include "rpk.h"
#include <stdlib.h>


#define STR_ENDS_WITH(S, E) (strcmp(S + strlen(S) - (sizeof(E)-1), E) == 0)


int main(int argc, char **argv) {
	if (argc<3) {
        printf("Usage: %s infile outfile\n",argv[0]);
        return 1;
    }
    
    
	if (STR_ENDS_WITH(argv[1], ".png")) {
        //Encode to RPK
        if (!STR_ENDS_WITH(argv[2], ".rpk")) {
            printf("At least one filename must end with .rpk\n");
            return 1;
        }
        return rpk_write(argv[1],argv[2])<0;
	} else {
        //Decode from RPK
        if (!STR_ENDS_WITH(argv[2], ".png")) {
            printf("At least one filename must end with .png\n");
            return 1;
        }
        return rpk_read(argv[1],argv[2])<0;
    }
}
