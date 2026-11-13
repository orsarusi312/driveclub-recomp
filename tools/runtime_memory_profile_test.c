/* Driver for independent synthetic images built from the public patch XML. */
#include "../src/runtime_memory_profile.h"
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc!=3) return 3;
    FILE *f=fopen(argv[1],"rb");
    if (!f || fseek(f,0,SEEK_END)) return 3;
    long length=ftell(f);
    if (length<0 || length>32*1024*1024 || fseek(f,0,SEEK_SET)) return 3;
    unsigned char *image=malloc(length ? (size_t)length : 1);
    if (!image || fread(image,1,(size_t)length,f)!=(size_t)length) return 3;
    fclose(f);
    uint64_t pool=strtoull(argv[2],NULL,10)*UINT64_C(1024)*1024;
    int ok=dc_memory_profile_check(image,(size_t)length,pool,stdout);
    free(image);
    return ok ? 0 : 2;
}
