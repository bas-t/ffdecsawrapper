
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[])
{
  unsigned char match[256];
  int i, max;
  int filt, mask, mode;
  int mam, manm;

  if(argc<2) {
    printf("missing operation mode\n");
    exit(1);
    }
  if(!strcasecmp(argv[1],"MATCH")) {
    if(argc<3) {
      printf("no matches given\n");
      exit(1);
      }
    memset(match,0,sizeof(match));
    printf("searching filter settings for:");
    for(i=2; i<argc; i++) {
      int l=strtol(argv[i],0,0);
      if(l<0 || l>255) {
        printf("allowed range 0-255 / 0x00-0xFF\n");
        exit(1);
        }
      match[l]=1;
      printf(" 0x%02x",l);
      }
    printf("\n");

    max=5;
    int mode=0;
    //for(mode=0; mode<256; mode++) {
      for(mask=0; mask<256; mask++) {
        mam =mask &  (~mode);
        manm=mask & ~(~mode);
        for(filt=0; filt<256; filt++) {
          int ok=1, miss=0;
          unsigned char mm[256];
          memset(mm,0,sizeof(mm));
          for(i=0; i<256; i++) {
            int xxor=filt^i;
            if((mam&xxor) || (manm && !(manm&xxor))) {
              if(match[i]!=0) {
                ok=0;
                miss=0;
                break;
                }
              }
            else {
              if(match[i]!=1) {
                ok=0;
                miss++; mm[i]=1;
                if(miss>max) break;
                }
              }
            }

          if(ok) {
            printf("found exact settings filt=%02x mask=%02x mode=%02x\n",filt,mask,mode);
            exit(0);
            }
          else if(miss>0 && miss<=max) {
            printf("mismatching");
            for(i=0; i<256; i++) if(mm[i]) printf(" %02x",i);
            printf(" - settings filt=%02x mask=%02x mode=%02x\n",filt,mask,mode);
            max=miss;
            }
          }
        }
      //}
    printf("no exact settings found\n");
    exit(1);
    }
  else if(!strcasecmp(argv[1],"TEST")) {
    if(argc<5) {
      printf("missing arguments\n");
      exit(1);
      }
    filt=strtol(argv[2],0,0);
    if(filt<0 || filt>255) {
      printf("filter range 0-255 exceeded\n");
      exit(1);
      }
    mask=strtol(argv[3],0,0);
    if(mask<0 || mask>255) {
      printf("mask range 0-255 exceeded\n");
      exit(1);
      }
    mode=strtol(argv[4],0,0);
    if(mode<0 || mode>255) {
      printf("mode range 0-255 exceeded\n");
      exit(1);
      }
    printf("settings file=%02x mask=%02x mode=%02x\n",filt,mask,mode);

    mam =mask &  (~mode);
    manm=mask & ~(~mode);
    printf("matching:");
    for(i=0; i<256; i++) {
      int xxor=filt^i;
      if((mam&xxor) || (manm && !(manm&xxor))) {}
      else
        printf(" %02x",i);
      }
    printf("\n");
    }
  else {
    printf("unknown operation mode\n");
    }
  exit(1);
}

