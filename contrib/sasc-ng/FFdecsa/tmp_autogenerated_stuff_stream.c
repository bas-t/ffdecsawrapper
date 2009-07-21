/* FFdecsa -- fast decsa algorithm
 *
 * Copyright (C) 2003-2004  fatih89r
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */



// define statics only once, when STREAM_INIT
#ifdef STREAM_INIT
static group A[32+10][4]; // 32 because we will move back (virtual shift register)
static group B[32+10][4]; // 32 because we will move back (virtual shift register)
static group X[4];
static group Y[4];
static group Z[4];
static group D[4];
static group E[4];
static group F[4];
static group p;
static group q;
static group r;

static inline void trasp64_32_88ccw(unsigned char *data){
/* 64 rows of 32 bits transposition (bytes transp. - 8x8 rotate counterclockwise)*/
#define row ((unsigned int *)data)
  int i,j;
  for(j=0;j<64;j+=32){
    unsigned int t,b;
    for(i=0;i<16;i++){
      t=row[j+i];
      b=row[j+16+i];
      row[j+i]   = (t&0x0000ffff)      | ((b           )<<16);
      row[j+16+i]=((t           )>>16) |  (b&0xffff0000) ;
    }
  }
  for(j=0;j<64;j+=16){
    unsigned int t,b;
    for(i=0;i<8;i++){
      t=row[j+i];
      b=row[j+8+i];
      row[j+i]   = (t&0x00ff00ff)     | ((b&0x00ff00ff)<<8);
      row[j+8+i] =((t&0xff00ff00)>>8) |  (b&0xff00ff00);
    }
  }
  for(j=0;j<64;j+=8){
    unsigned int t,b;
    for(i=0;i<4;i++){
      t=row[j+i];
      b=row[j+4+i];
      row[j+i]   =((t&0x0f0f0f0f)<<4) |  (b&0x0f0f0f0f);
      row[j+4+i] = (t&0xf0f0f0f0)     | ((b&0xf0f0f0f0)>>4);
    }
  }
  for(j=0;j<64;j+=4){
    unsigned int t,b;
    for(i=0;i<2;i++){
      t=row[j+i];
      b=row[j+2+i];
      row[j+i]   =((t&0x33333333)<<2) |  (b&0x33333333);
      row[j+2+i] = (t&0xcccccccc)     | ((b&0xcccccccc)>>2);
    }
  }
  for(j=0;j<64;j+=2){
    unsigned int t,b;
    for(i=0;i<1;i++){
      t=row[j+i];
      b=row[j+1+i];
      row[j+i]   =((t&0x55555555)<<1) |  (b&0x55555555);
      row[j+1+i] = (t&0xaaaaaaaa)     | ((b&0xaaaaaaaa)>>1);
    }
  }
#undef row
}

static inline void trasp64_32_88cw(unsigned char *data){
/* 64 rows of 32 bits transposition (bytes transp. - 8x8 rotate clockwise)*/
#define row ((unsigned int *)data)
  int i,j;
  for(j=0;j<64;j+=32){
    unsigned int t,b;
    for(i=0;i<16;i++){
      t=row[j+i];
      b=row[j+16+i];
      row[j+i]   = (t&0x0000ffff)      | ((b           )<<16);
      row[j+16+i]=((t           )>>16) |  (b&0xffff0000) ;
    }
  }
  for(j=0;j<64;j+=16){
    unsigned int t,b;
    for(i=0;i<8;i++){
      t=row[j+i];
      b=row[j+8+i];
      row[j+i]   = (t&0x00ff00ff)     | ((b&0x00ff00ff)<<8);
      row[j+8+i] =((t&0xff00ff00)>>8) |  (b&0xff00ff00);
    }
  }
  for(j=0;j<64;j+=8){
    unsigned int t,b;
    for(i=0;i<4;i++){
      t=row[j+i];
      b=row[j+4+i];
      row[j+i]  =((t&0xf0f0f0f0)>>4) |   (b&0xf0f0f0f0);
      row[j+4+i]= (t&0x0f0f0f0f)     |  ((b&0x0f0f0f0f)<<4);
    }
  }
  for(j=0;j<64;j+=4){
    unsigned int t,b;
    for(i=0;i<2;i++){
      t=row[j+i];
      b=row[j+2+i];
      row[j+i]  =((t&0xcccccccc)>>2) |  (b&0xcccccccc);
      row[j+2+i]= (t&0x33333333)     | ((b&0x33333333)<<2);
    }
  }
  for(j=0;j<64;j+=2){
    unsigned int t,b;
    for(i=0;i<1;i++){
      t=row[j+i];
      b=row[j+1+i];
      row[j+i]  =((t&0xaaaaaaaa)>>1) |  (b&0xaaaaaaaa);
      row[j+1+i]= (t&0x55555555)     | ((b&0x55555555)<<1);
    }
  }
#undef row
}

//64-64----------------------------------------------------------
static inline void trasp64_64_88ccw(unsigned char *data){
/* 64 rows of 64 bits transposition (bytes transp. - 8x8 rotate counterclockwise)*/
#define row ((unsigned long long int *)data)
  int i,j;
  for(j=0;j<64;j+=64){
    unsigned long long int t,b;
    for(i=0;i<32;i++){
      t=row[j+i];
      b=row[j+32+i];
      row[j+i]   = (t&0x00000000ffffffffULL)      | ((b                      )<<32);
      row[j+32+i]=((t                      )>>32) |  (b&0xffffffff00000000ULL) ;
    }
  }
  for(j=0;j<64;j+=32){
    unsigned long long int t,b;
    for(i=0;i<16;i++){
      t=row[j+i];
      b=row[j+16+i];
      row[j+i]   = (t&0x0000ffff0000ffffULL)      | ((b&0x0000ffff0000ffffULL)<<16);
      row[j+16+i]=((t&0xffff0000ffff0000ULL)>>16) |  (b&0xffff0000ffff0000ULL) ;
    }
  }
  for(j=0;j<64;j+=16){
    unsigned long long int t,b;
    for(i=0;i<8;i++){
      t=row[j+i];
      b=row[j+8+i];
      row[j+i]   = (t&0x00ff00ff00ff00ffULL)     | ((b&0x00ff00ff00ff00ffULL)<<8);
      row[j+8+i] =((t&0xff00ff00ff00ff00ULL)>>8) |  (b&0xff00ff00ff00ff00ULL);
    }
  }
  for(j=0;j<64;j+=8){
    unsigned long long int t,b;
    for(i=0;i<4;i++){
      t=row[j+i];
      b=row[j+4+i];
      row[j+i]   =((t&0x0f0f0f0f0f0f0f0fULL)<<4) |  (b&0x0f0f0f0f0f0f0f0fULL);
      row[j+4+i] = (t&0xf0f0f0f0f0f0f0f0ULL)     | ((b&0xf0f0f0f0f0f0f0f0ULL)>>4);
    }
  }
  for(j=0;j<64;j+=4){
    unsigned long long int t,b;
    for(i=0;i<2;i++){
      t=row[j+i];
      b=row[j+2+i];
      row[j+i]   =((t&0x3333333333333333ULL)<<2) |  (b&0x3333333333333333ULL);
      row[j+2+i] = (t&0xccccccccccccccccULL)     | ((b&0xccccccccccccccccULL)>>2);
    }
  }
  for(j=0;j<64;j+=2){
    unsigned long long int t,b;
    for(i=0;i<1;i++){
      t=row[j+i];
      b=row[j+1+i];
      row[j+i]   =((t&0x5555555555555555ULL)<<1) |  (b&0x5555555555555555ULL);
      row[j+1+i] = (t&0xaaaaaaaaaaaaaaaaULL)     | ((b&0xaaaaaaaaaaaaaaaaULL)>>1);
    }
  }
#undef row
}

static inline void trasp64_64_88cw(unsigned char *data){
/* 64 rows of 64 bits transposition (bytes transp. - 8x8 rotate clockwise)*/
#define row ((unsigned long long int *)data)
  int i,j;
  for(j=0;j<64;j+=64){
    unsigned long long int t,b;
    for(i=0;i<32;i++){
      t=row[j+i];
      b=row[j+32+i];
      row[j+i]   = (t&0x00000000ffffffffULL)      | ((b                      )<<32);
      row[j+32+i]=((t                      )>>32) |  (b&0xffffffff00000000ULL) ;
    }
  }
  for(j=0;j<64;j+=32){
    unsigned long long int t,b;
    for(i=0;i<16;i++){
      t=row[j+i];
      b=row[j+16+i];
      row[j+i]   = (t&0x0000ffff0000ffffULL)      | ((b&0x0000ffff0000ffffULL)<<16);
      row[j+16+i]=((t&0xffff0000ffff0000ULL)>>16) |  (b&0xffff0000ffff0000ULL) ;
    }
  }
  for(j=0;j<64;j+=16){
    unsigned long long int t,b;
    for(i=0;i<8;i++){
      t=row[j+i];
      b=row[j+8+i];
      row[j+i]   = (t&0x00ff00ff00ff00ffULL)     | ((b&0x00ff00ff00ff00ffULL)<<8);
      row[j+8+i] =((t&0xff00ff00ff00ff00ULL)>>8) |  (b&0xff00ff00ff00ff00ULL);
    }
  }
  for(j=0;j<64;j+=8){
    unsigned long long int t,b;
    for(i=0;i<4;i++){
      t=row[j+i];
      b=row[j+4+i];
      row[j+i]   =((t&0xf0f0f0f0f0f0f0f0ULL)>>4) |   (b&0xf0f0f0f0f0f0f0f0ULL);
      row[j+4+i] = (t&0x0f0f0f0f0f0f0f0fULL)     |  ((b&0x0f0f0f0f0f0f0f0fULL)<<4);
    }
  }
  for(j=0;j<64;j+=4){
    unsigned long long int t,b;
    for(i=0;i<2;i++){
      t=row[j+i];
      b=row[j+2+i];
      row[j+i]   =((t&0xccccccccccccccccULL)>>2) |  (b&0xccccccccccccccccULL);
      row[j+2+i] = (t&0x3333333333333333ULL)     | ((b&0x3333333333333333ULL)<<2);
    }
  }
  for(j=0;j<64;j+=2){
    unsigned long long int t,b;
    for(i=0;i<1;i++){
      t=row[j+i];
      b=row[j+1+i];
      row[j+i]   =((t&0xaaaaaaaaaaaaaaaaULL)>>1) |  (b&0xaaaaaaaaaaaaaaaaULL);
      row[j+1+i] = (t&0x5555555555555555ULL)     | ((b&0x5555555555555555ULL)<<1);
    }
  }
#undef row
}

//64-128----------------------------------------------------------
static inline void trasp64_128_88ccw(unsigned char *data){
/* 64 rows of 128 bits transposition (bytes transp. - 8x8 rotate counterclockwise)*/
#define halfrow ((unsigned long long int *)data)
  int i,j;
  for(j=0;j<64;j+=64){
    unsigned long long int t,b;
    for(i=0;i<32;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+32+i)];
      halfrow[2*(j+i)]   = (t&0x00000000ffffffffULL)      | ((b                      )<<32);
      halfrow[2*(j+32+i)]=((t                      )>>32) |  (b&0xffffffff00000000ULL) ;
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+32+i)+1];
      halfrow[2*(j+i)+1]   = (t&0x00000000ffffffffULL)      | ((b                      )<<32);
      halfrow[2*(j+32+i)+1]=((t                      )>>32) |  (b&0xffffffff00000000ULL) ;
    }
  }
  for(j=0;j<64;j+=32){
    unsigned long long int t,b;
    for(i=0;i<16;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+16+i)];
      halfrow[2*(j+i)]   = (t&0x0000ffff0000ffffULL)      | ((b&0x0000ffff0000ffffULL)<<16);
      halfrow[2*(j+16+i)]=((t&0xffff0000ffff0000ULL)>>16) |  (b&0xffff0000ffff0000ULL) ;
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+16+i)+1];
      halfrow[2*(j+i)+1]   = (t&0x0000ffff0000ffffULL)      | ((b&0x0000ffff0000ffffULL)<<16);
      halfrow[2*(j+16+i)+1]=((t&0xffff0000ffff0000ULL)>>16) |  (b&0xffff0000ffff0000ULL) ;
    }
  }
  for(j=0;j<64;j+=16){
    unsigned long long int t,b;
    for(i=0;i<8;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+8+i)];
      halfrow[2*(j+i)]   = (t&0x00ff00ff00ff00ffULL)     | ((b&0x00ff00ff00ff00ffULL)<<8);
      halfrow[2*(j+8+i)] =((t&0xff00ff00ff00ff00ULL)>>8) |  (b&0xff00ff00ff00ff00ULL);
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+8+i)+1];
      halfrow[2*(j+i)+1]   = (t&0x00ff00ff00ff00ffULL)     | ((b&0x00ff00ff00ff00ffULL)<<8);
      halfrow[2*(j+8+i)+1] =((t&0xff00ff00ff00ff00ULL)>>8) |  (b&0xff00ff00ff00ff00ULL);
    }
  }
  for(j=0;j<64;j+=8){
    unsigned long long int t,b;
    for(i=0;i<4;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+4+i)];
      halfrow[2*(j+i)]   =((t&0x0f0f0f0f0f0f0f0fULL)<<4) |  (b&0x0f0f0f0f0f0f0f0fULL);
      halfrow[2*(j+4+i)] = (t&0xf0f0f0f0f0f0f0f0ULL)     | ((b&0xf0f0f0f0f0f0f0f0ULL)>>4);
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+4+i)+1];
      halfrow[2*(j+i)+1]   =((t&0x0f0f0f0f0f0f0f0fULL)<<4) |  (b&0x0f0f0f0f0f0f0f0fULL);
      halfrow[2*(j+4+i)+1] = (t&0xf0f0f0f0f0f0f0f0ULL)     | ((b&0xf0f0f0f0f0f0f0f0ULL)>>4);
    }
  }
  for(j=0;j<64;j+=4){
    unsigned long long int t,b;
    for(i=0;i<2;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+2+i)];
      halfrow[2*(j+i)]   =((t&0x3333333333333333ULL)<<2) |  (b&0x3333333333333333ULL);
      halfrow[2*(j+2+i)] = (t&0xccccccccccccccccULL)     | ((b&0xccccccccccccccccULL)>>2);
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+2+i)+1];
      halfrow[2*(j+i)+1]   =((t&0x3333333333333333ULL)<<2) |  (b&0x3333333333333333ULL);
      halfrow[2*(j+2+i)+1] = (t&0xccccccccccccccccULL)     | ((b&0xccccccccccccccccULL)>>2);
    }
  }
  for(j=0;j<64;j+=2){
    unsigned long long int t,b;
    for(i=0;i<1;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+1+i)];
      halfrow[2*(j+i)]   =((t&0x5555555555555555ULL)<<1) |  (b&0x5555555555555555ULL);
      halfrow[2*(j+1+i)] = (t&0xaaaaaaaaaaaaaaaaULL)     | ((b&0xaaaaaaaaaaaaaaaaULL)>>1);
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+1+i)+1];
      halfrow[2*(j+i)+1]   =((t&0x5555555555555555ULL)<<1) |  (b&0x5555555555555555ULL);
      halfrow[2*(j+1+i)+1] = (t&0xaaaaaaaaaaaaaaaaULL)     | ((b&0xaaaaaaaaaaaaaaaaULL)>>1);
    }
  }
#undef halfrow
}

static inline void trasp64_128_88cw(unsigned char *data){
/* 64 rows of 128 bits transposition (bytes transp. - 8x8 rotate clockwise)*/
#define halfrow ((unsigned long long int *)data)
  int i,j;
  for(j=0;j<64;j+=64){
    unsigned long long int t,b;
    for(i=0;i<32;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+32+i)];
      halfrow[2*(j+i)]   = (t&0x00000000ffffffffULL)      | ((b                      )<<32);
      halfrow[2*(j+32+i)]=((t                      )>>32) |  (b&0xffffffff00000000ULL) ;
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+32+i)+1];
      halfrow[2*(j+i)+1]   = (t&0x00000000ffffffffULL)      | ((b                      )<<32);
      halfrow[2*(j+32+i)+1]=((t                      )>>32) |  (b&0xffffffff00000000ULL) ;
    }
  }
  for(j=0;j<64;j+=32){
    unsigned long long int t,b;
    for(i=0;i<16;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+16+i)];
      halfrow[2*(j+i)]   = (t&0x0000ffff0000ffffULL)      | ((b&0x0000ffff0000ffffULL)<<16);
      halfrow[2*(j+16+i)]=((t&0xffff0000ffff0000ULL)>>16) |  (b&0xffff0000ffff0000ULL) ;
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+16+i)+1];
      halfrow[2*(j+i)+1]   = (t&0x0000ffff0000ffffULL)      | ((b&0x0000ffff0000ffffULL)<<16);
      halfrow[2*(j+16+i)+1]=((t&0xffff0000ffff0000ULL)>>16) |  (b&0xffff0000ffff0000ULL) ;
    }
  }
  for(j=0;j<64;j+=16){
    unsigned long long int t,b;
    for(i=0;i<8;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+8+i)];
      halfrow[2*(j+i)]   = (t&0x00ff00ff00ff00ffULL)     | ((b&0x00ff00ff00ff00ffULL)<<8);
      halfrow[2*(j+8+i)] =((t&0xff00ff00ff00ff00ULL)>>8) |  (b&0xff00ff00ff00ff00ULL);
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+8+i)+1];
      halfrow[2*(j+i)+1]   = (t&0x00ff00ff00ff00ffULL)     | ((b&0x00ff00ff00ff00ffULL)<<8);
      halfrow[2*(j+8+i)+1] =((t&0xff00ff00ff00ff00ULL)>>8) |  (b&0xff00ff00ff00ff00ULL);
    }
  }
  for(j=0;j<64;j+=8){
    unsigned long long int t,b;
    for(i=0;i<4;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+4+i)];
      halfrow[2*(j+i)]   =((t&0xf0f0f0f0f0f0f0f0ULL)>>4) |   (b&0xf0f0f0f0f0f0f0f0ULL);
      halfrow[2*(j+4+i)] = (t&0x0f0f0f0f0f0f0f0fULL)     |  ((b&0x0f0f0f0f0f0f0f0fULL)<<4);
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+4+i)+1];
      halfrow[2*(j+i)+1]   =((t&0xf0f0f0f0f0f0f0f0ULL)>>4) |   (b&0xf0f0f0f0f0f0f0f0ULL);
      halfrow[2*(j+4+i)+1] = (t&0x0f0f0f0f0f0f0f0fULL)     |  ((b&0x0f0f0f0f0f0f0f0fULL)<<4);
    }
  }
  for(j=0;j<64;j+=4){
    unsigned long long int t,b;
    for(i=0;i<2;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+2+i)];
      halfrow[2*(j+i)]   =((t&0xccccccccccccccccULL)>>2) |  (b&0xccccccccccccccccULL);
      halfrow[2*(j+2+i)] = (t&0x3333333333333333ULL)     | ((b&0x3333333333333333ULL)<<2);
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+2+i)+1];
      halfrow[2*(j+i)+1]   =((t&0xccccccccccccccccULL)>>2) |  (b&0xccccccccccccccccULL);
      halfrow[2*(j+2+i)+1] = (t&0x3333333333333333ULL)     | ((b&0x3333333333333333ULL)<<2);
    }
  }
  for(j=0;j<64;j+=2){
    unsigned long long int t,b;
    for(i=0;i<1;i++){
      t=halfrow[2*(j+i)];
      b=halfrow[2*(j+1+i)];
      halfrow[2*(j+i)]   =((t&0xaaaaaaaaaaaaaaaaULL)>>1) |  (b&0xaaaaaaaaaaaaaaaaULL);
      halfrow[2*(j+1+i)] = (t&0x5555555555555555ULL)     | ((b&0x5555555555555555ULL)<<1);
      t=halfrow[2*(j+i)+1];
      b=halfrow[2*(j+1+i)+1];
      halfrow[2*(j+i)+1]   =((t&0xaaaaaaaaaaaaaaaaULL)>>1) |  (b&0xaaaaaaaaaaaaaaaaULL);
      halfrow[2*(j+1+i)+1] = (t&0x5555555555555555ULL)     | ((b&0x5555555555555555ULL)<<1);
    }
  }
#undef halfrow
}
#endif


#ifdef STREAM_INIT
void stream_cypher_group_init(
  group         iA[8][4], // [In]  iA00,iA01,...iA73 32 groups  | Derived from key.
  group         iB[8][4], // [In]  iB00,iB01,...iB73 32 groups  | Derived from key.
  unsigned char *sb)      // [In]  (SB0,SB1,...SB7)...x32 32*8 bytes | Extra input.
#endif
#ifdef STREAM_NORMAL
void stream_cypher_group_normal(
  unsigned char *cb)    // [Out] (CB0,CB1,...CB7)...x32 32*8 bytes | Output.
#endif
{
#ifdef STREAM_INIT
  group in1[4];
  group in2[4];
#endif
  group extra_B[4];
  group fa,fb,fc,fd,fe;
  group s1a,s1b,s2a,s2b,s3a,s3b,s4a,s4b,s5a,s5b,s6a,s6b,s7a,s7b;
  group next_E[4];
  group tmp0,tmp1,tmp2,tmp3,tmp4;
#ifdef STREAM_INIT
  group *sb_g=(group *)sb;
#endif
#ifdef STREAM_NORMAL
  group *cb_g=(group *)cb;
#endif
  int aboff;
  int i,j,k,b;

#ifdef STREAM_INIT
#endif
#ifdef STREAM_NORMAL
#endif
#ifdef STREAM_INIT

#if GROUP_PARALLELISM==32
trasp64_32_88ccw(sb);
#endif
#if GROUP_PARALLELISM==64
trasp64_64_88ccw(sb);
#endif
#if GROUP_PARALLELISM==128
trasp64_128_88ccw(sb);
#endif

#endif

  aboff=32;

#ifdef STREAM_INIT
  // load first 32 bits of ck into A[aboff+0]..A[aboff+7]
  // load last  32 bits of ck into B[aboff+0]..B[aboff+7]
  // all other regs = 0
  for(i=0;i<8;i++){
    for(b=0;b<4;b++){
      A[aboff+i][b]=iA[i][b];
      B[aboff+i][b]=iB[i][b];
    }
  }
  for(b=0;b<4;b++){
    A[aboff+8][b]=FF0();
    A[aboff+9][b]=FF0();
    B[aboff+8][b]=FF0();
    B[aboff+9][b]=FF0();
  }
  for(b=0;b<4;b++){
    X[b]=FF0();
    Y[b]=FF0();
    Z[b]=FF0();
    D[b]=FF0();
    E[b]=FF0();
    F[b]=FF0();
  }
  p=FF0();
  q=FF0();
  r=FF0();
#endif


////////////////////////////////////////////////////////////////////////////////

  // EXTERNAL LOOP - 8 bytes per operation
  for(i=0;i<8;i++){


#ifdef STREAM_INIT
    for(b=0;b<4;b++){
      in1[b]=sb_g[8*i+4+b];
      in2[b]=sb_g[8*i+b];
    }
#endif

    // INTERNAL LOOP - 2 bits per iteration
    for(j=0; j<4; j++){


      // from A0..A9, 35 bits are selected as inputs to 7 s-boxes
      // 5 bits input per s-box, 2 bits output per s-box

      // we can select bits with zero masking and shifting operations
      // and synthetize s-boxes with optimized boolean functions.
      // this is the actual reason we do all the crazy transposition
      // stuff to switch between normal and bit slice representations.
      // this code really flies.

      fe=A[aboff+3][0];fa=A[aboff+0][2];fb=A[aboff+5][1];fc=A[aboff+6][3];fd=A[aboff+8][0];
/* 1000 1110  1110 0001   : lev  7: */ //tmp0=( fa^( fb^( ( ( ( fa|fb )^fc )|( fc^fd ) )^ALL_ONES ) ) );
/* 1110 0010  0011 0011   : lev  6: */ //tmp1=( ( fa|fb )^( ( fc&( fa|( fb^fd ) ) )^ALL_ONES ) );
/* 0011 0110  1000 1101   : lev  5: */ //tmp2=( fa^( ( fb&fd )^( ( fa&fd )|fc ) ) );
/* 0101 0101  1001 0011   : lev  5: */ //tmp3=( ( fa&fc )^( fa^( ( fa&fb )|fd ) ) );
/* 1000 1110  1110 0001   : lev  7: */ tmp0=FFXOR(fa,FFXOR(fb,FFXOR(FFOR(FFXOR(FFOR(fa,fb),fc),FFXOR(fc,fd)),FF1())));
/* 1110 0010  0011 0011   : lev  6: */ tmp1=FFXOR(FFOR(fa,fb),FFXOR(FFAND(fc,FFOR(fa,FFXOR(fb,fd))),FF1()));
/* 0011 0110  1000 1101   : lev  5: */ tmp2=FFXOR(fa,FFXOR(FFAND(fb,fd),FFOR(FFAND(fa,fd),fc)));
/* 0101 0101  1001 0011   : lev  5: */ tmp3=FFXOR(FFAND(fa,fc),FFXOR(fa,FFOR(FFAND(fa,fb),fd)));
      s1a=FFXOR(tmp0,FFAND(fe,tmp1));
      s1b=FFXOR(tmp2,FFAND(fe,tmp3));
//dump_mem("s1as1b-fe",&fe,BYPG,BYPG);
//dump_mem("s1as1b-fa",&fa,BYPG,BYPG);
//dump_mem("s1as1b-fb",&fb,BYPG,BYPG);
//dump_mem("s1as1b-fc",&fc,BYPG,BYPG);
//dump_mem("s1as1b-fd",&fd,BYPG,BYPG);

      fe=A[aboff+1][1];fa=A[aboff+2][2];fb=A[aboff+5][3];fc=A[aboff+6][0];fd=A[aboff+8][1];
/* 1001 1110  0110 0001   : lev  6: */ //tmp0=( fa^( ( fb&( fc|fd ) )^( fc^( fd^ALL_ONES ) ) ) );
/* 0000 0011  0111 1011   : lev  5: */ //tmp1=( ( fa&( fb^fd ) )|( ( fa|fb )&fc ) );
/* 1100 0110  1101 0010   : lev  6: */ //tmp2=( ( fb&fd )^( ( fa&fd )|( fb^( fc^ALL_ONES ) ) ) );
/* 0001 1110  1111 0101   : lev  5: */ //tmp3=( ( fa&fd )|( fa^( fb^( fc&fd ) ) ) );
/* 1001 1110  0110 0001   : lev  6: */ tmp0=FFXOR(fa,FFXOR(FFAND(fb,FFOR(fc,fd)),FFXOR(fc,FFXOR(fd,FF1()))));
/* 0000 0011  0111 1011   : lev  5: */ tmp1=FFOR(FFAND(fa,FFXOR(fb,fd)),FFAND(FFOR(fa,fb),fc));
/* 1100 0110  1101 0010   : lev  6: */ tmp2=FFXOR(FFAND(fb,fd),FFOR(FFAND(fa,fd),FFXOR(fb,FFXOR(fc,FF1()))));
/* 0001 1110  1111 0101   : lev  5: */ tmp3=FFOR(FFAND(fa,fd),FFXOR(fa,FFXOR(fb,FFAND(fc,fd))));
      s2a=FFXOR(tmp0,FFAND(fe,tmp1));
      s2b=FFXOR(tmp2,FFAND(fe,tmp3));

      fe=A[aboff+0][3];fa=A[aboff+1][0];fb=A[aboff+4][1];fc=A[aboff+4][3];fd=A[aboff+5][2];
/* 0100 1011  1001 0110   : lev  5: */ //tmp0=( fa^( fb^( ( fc&( fa|fd ) )^fd ) ) );
/* 1101 0101  1000 1100   : lev  7: */ //tmp1=( ( fa&fc )^( ( fa^fd )|( ( fb|fc )^( fd^ALL_ONES ) ) ) );
/* 0010 0111  1101 1000   : lev  4: */ //tmp2=( fa^( ( ( fb^fc )&fd )^fc ) );
/* 1111 1111  1111 1111   : lev  0: */ //tmp3=ALL_ONES;
/* 0100 1011  1001 0110   : lev  5: */ tmp0=FFXOR(fa,FFXOR(fb,FFXOR(FFAND(fc,FFOR(fa,fd)),fd)));
/* 1101 0101  1000 1100   : lev  7: */ tmp1=FFXOR(FFAND(fa,fc),FFOR(FFXOR(fa,fd),FFXOR(FFOR(fb,fc),FFXOR(fd,FF1()))));
/* 0010 0111  1101 1000   : lev  4: */ tmp2=FFXOR(fa,FFXOR(FFAND(FFXOR(fb,fc),fd),fc));
/* 1111 1111  1111 1111   : lev  0: */ tmp3=FF1();
      s3a=FFXOR(tmp0,FFAND(FFNOT(fe),tmp1));
      s3b=FFXOR(tmp2,FFAND(fe,tmp3));

      fe=A[aboff+2][3];fa=A[aboff+0][1];fb=A[aboff+1][3];fc=A[aboff+3][2];fd=A[aboff+7][0];
/* 1011 0101  0100 1001   : lev  7: */ //tmp0=( fa^( ( fc&( fa^fd ) )|( fb^( fc|( fd^ALL_ONES ) ) ) ) );
/* 0010 1101  0110 0110   : lev  6: */ //tmp1=( ( fa&fb )^( fb^( ( ( fa|fc )&fd )^fc ) ) );
/* 0110 0111  1101 0000   : lev  7: */ //tmp2=( fa^( ( fb&fc )|( ( ( fa&( fb^fd ) )|fc )^fd ) ) );
/* 1111 1111  1111 1111   : lev  0: */ //tmp3=ALL_ONES;
/* 1011 0101  0100 1001   : lev  7: */ tmp0=FFXOR(fa,FFOR(FFAND(fc,FFXOR(fa,fd)),FFXOR(fb,FFOR(fc,FFXOR(fd,FF1())))));
/* 0010 1101  0110 0110   : lev  6: */ tmp1=FFXOR(FFAND(fa,fb),FFXOR(fb,FFXOR(FFAND(FFOR(fa,fc),fd),fc)));
/* 0110 0111  1101 0000   : lev  7: */ tmp2=FFXOR(fa,FFOR(FFAND(fb,fc),FFXOR(FFOR(FFAND(fa,FFXOR(fb,fd)),fc),fd)));
/* 1111 1111  1111 1111   : lev  0: */ tmp3=FF1();
      s4a=FFXOR(tmp0,FFAND(fe,FFXOR(tmp1,tmp0)));
      s4b=FFXOR(FFXOR(s4a,tmp2),FFAND(fe,tmp3));

      fe=A[aboff+4][2];fa=A[aboff+3][3];fb=A[aboff+5][0];fc=A[aboff+7][1];fd=A[aboff+8][2];
/* 1000 1111  0011 0010   : lev  7: */ //tmp0=( ( ( fa&( fb|fc ) )^fb )|( ( ( fa^fc )|fd )^ALL_ONES ) );
/* 0110 1011  0000 1011   : lev  6: */ //tmp1=( fb^( ( fc^fd )&( fc^( fb|( fa^fd ) ) ) ) );
/* 0001 1010  0111 1001   : lev  6: */ //tmp2=( ( fa&fc )^( fb^( ( fb|( fa^fc ) )&fd ) ) );
/* 0101 1101  1101 0101   : lev  4: */ //tmp3=( ( ( fa^fb )&( fc^ALL_ONES ) )|fd );
/* 1000 1111  0011 0010   : lev  7: */ tmp0=FFOR(FFXOR(FFAND(fa,FFOR(fb,fc)),fb),FFXOR(FFOR(FFXOR(fa,fc),fd),FF1()));
/* 0110 1011  0000 1011   : lev  6: */ tmp1=FFXOR(fb,FFAND(FFXOR(fc,fd),FFXOR(fc,FFOR(fb,FFXOR(fa,fd)))));
/* 0001 1010  0111 1001   : lev  6: */ tmp2=FFXOR(FFAND(fa,fc),FFXOR(fb,FFAND(FFOR(fb,FFXOR(fa,fc)),fd)));
/* 0101 1101  1101 0101   : lev  4: */ tmp3=FFOR(FFAND(FFXOR(fa,fb),FFXOR(fc,FF1())),fd);
      s5a=FFXOR(tmp0,FFAND(fe,tmp1));
      s5b=FFXOR(tmp2,FFAND(fe,tmp3));

      fe=A[aboff+2][1];fa=A[aboff+3][1];fb=A[aboff+4][0];fc=A[aboff+6][2];fd=A[aboff+8][3];
/* 0011 0110  0010 1101   : lev  6: */ //tmp0=( ( ( fa&fc )&fd )^( ( fb&( fa|fd ) )^fc ) );
/* 1110 1110  1011 1011   : lev  3: */ //tmp1=( ( ( fa^fc )&fd )^ALL_ONES );
/* 0101 1000  0110 0111   : lev  6: */ //tmp2=( ( fa&( fb|fc ) )^( fb^( ( fb&fc )|fd ) ) );
/* 0001 0011  0000 0001   : lev  5: */ //tmp3=( fc&( ( fa&( fb^fd ) )^( fb|fd ) ) );
/* 0011 0110  0010 1101   : lev  6: */ tmp0=FFXOR(FFAND(FFAND(fa,fc),fd),FFXOR(FFAND(fb,FFOR(fa,fd)),fc));
/* 1110 1110  1011 1011   : lev  3: */ tmp1=FFXOR(FFAND(FFXOR(fa,fc),fd),FF1());
/* 0101 1000  0110 0111   : lev  6: */ tmp2=FFXOR(FFAND(fa,FFOR(fb,fc)),FFXOR(fb,FFOR(FFAND(fb,fc),fd)));
/* 0001 0011  0000 0001   : lev  5: */ tmp3=FFAND(fc,FFXOR(FFAND(fa,FFXOR(fb,fd)),FFOR(fb,fd)));
      s6a=FFXOR(tmp0,FFAND(fe,tmp1));
      s6b=FFXOR(tmp2,FFAND(fe,tmp3));

      fe=A[aboff+1][2];fa=A[aboff+2][0];fb=A[aboff+6][1];fc=A[aboff+7][2];fd=A[aboff+7][3];
/* 0111 1000  1001 0110   : lev  5: */ //tmp0=( fb^( ( fc&fd )|( fa^( fc^fd ) ) ) );
/* 0100 1001  0101 1011   : lev  6: */ //tmp1=( ( fb|fd )&( ( fa&fc )|( fb^( fc^fd ) ) ) );
/* 0100 1001  1011 1001   : lev  5: */ //tmp2=( ( fa|fb )^( ( fc&( fb|fd ) )^fd ) );
/* 1111 1111  1101 1101   : lev  3: */ //tmp3=( fd|( ( fa&fc )^ALL_ONES ) );
/* 0111 1000  1001 0110   : lev  5: */ tmp0=FFXOR(fb,FFOR(FFAND(fc,fd),FFXOR(fa,FFXOR(fc,fd))));
/* 0100 1001  0101 1011   : lev  6: */ tmp1=FFAND(FFOR(fb,fd),FFOR(FFAND(fa,fc),FFXOR(fb,FFXOR(fc,fd))));
/* 0100 1001  1011 1001   : lev  5: */ tmp2=FFXOR(FFOR(fa,fb),FFXOR(FFAND(fc,FFOR(fb,fd)),fd));
/* 1111 1111  1101 1101   : lev  3: */ tmp3=FFOR(fd,FFXOR(FFAND(fa,fc),FF1()));
      s7a=FFXOR(tmp0,FFAND(fe,tmp1));
      s7b=FFXOR(tmp2,FFAND(fe,tmp3));


/*
      we have just done this:
      
      int sbox1[0x20] = {2,0,1,1,2,3,3,0, 3,2,2,0,1,1,0,3, 0,3,3,0,2,2,1,1, 2,2,0,3,1,1,3,0};
      int sbox2[0x20] = {3,1,0,2,2,3,3,0, 1,3,2,1,0,0,1,2, 3,1,0,3,3,2,0,2, 0,0,1,2,2,1,3,1};
      int sbox3[0x20] = {2,0,1,2,2,3,3,1, 1,1,0,3,3,0,2,0, 1,3,0,1,3,0,2,2, 2,0,1,2,0,3,3,1};
      int sbox4[0x20] = {3,1,2,3,0,2,1,2, 1,2,0,1,3,0,0,3, 1,0,3,1,2,3,0,3, 0,3,2,0,1,2,2,1};
      int sbox5[0x20] = {2,0,0,1,3,2,3,2, 0,1,3,3,1,0,2,1, 2,3,2,0,0,3,1,1, 1,0,3,2,3,1,0,2};
      int sbox6[0x20] = {0,1,2,3,1,2,2,0, 0,1,3,0,2,3,1,3, 2,3,0,2,3,0,1,1, 2,1,1,2,0,3,3,0};
      int sbox7[0x20] = {0,3,2,2,3,0,0,1, 3,0,1,3,1,2,2,1, 1,0,3,3,0,1,1,2, 2,3,1,0,2,3,0,2};

      s12 = sbox1[ (((A3>>0)&1)<<4) | (((A0>>2)&1)<<3) | (((A5>>1)&1)<<2) | (((A6>>3)&1)<<1) | (((A8>>0)&1)<<0) ]
           |sbox2[ (((A1>>1)&1)<<4) | (((A2>>2)&1)<<3) | (((A5>>3)&1)<<2) | (((A6>>0)&1)<<1) | (((A8>>1)&1)<<0) ];
      s34 = sbox3[ (((A0>>3)&1)<<4) | (((A1>>0)&1)<<3) | (((A4>>1)&1)<<2) | (((A4>>3)&1)<<1) | (((A5>>2)&1)<<0) ]
           |sbox4[ (((A2>>3)&1)<<4) | (((A0>>1)&1)<<3) | (((A1>>3)&1)<<2) | (((A3>>2)&1)<<1) | (((A7>>0)&1)<<0) ];
      s56 = sbox5[ (((A4>>2)&1)<<4) | (((A3>>3)&1)<<3) | (((A5>>0)&1)<<2) | (((A7>>1)&1)<<1) | (((A8>>2)&1)<<0) ]
           |sbox6[ (((A2>>1)&1)<<4) | (((A3>>1)&1)<<3) | (((A4>>0)&1)<<2) | (((A6>>2)&1)<<1) | (((A8>>3)&1)<<0) ];
      s7 =  sbox7[ (((A1>>2)&1)<<4) | (((A2>>0)&1)<<3) | (((A6>>1)&1)<<2) | (((A7>>2)&1)<<1) | (((A7>>3)&1)<<0) ];
*/

      // use 4x4 xor to produce extra nibble for T3

      extra_B[3]=FFXOR(FFXOR(FFXOR(B[aboff+2][0],B[aboff+5][1]),B[aboff+6][2]),B[aboff+8][3]);
      extra_B[2]=FFXOR(FFXOR(FFXOR(B[aboff+5][0],B[aboff+7][1]),B[aboff+2][3]),B[aboff+3][2]);
      extra_B[1]=FFXOR(FFXOR(FFXOR(B[aboff+4][3],B[aboff+7][2]),B[aboff+3][0]),B[aboff+4][1]);
      extra_B[0]=FFXOR(FFXOR(FFXOR(B[aboff+8][2],B[aboff+5][3]),B[aboff+2][1]),B[aboff+7][0]);

      // T1 = xor all inputs
      // in1, in2, D are only used in T1 during initialisation, not generation
      for(b=0;b<4;b++){
        A[aboff-1][b]=FFXOR(A[aboff+9][b],X[b]);
      }

#ifdef STREAM_INIT
      for(b=0;b<4;b++){
        A[aboff-1][b]=FFXOR(FFXOR(A[aboff-1][b],D[b]),((j % 2) ? in2[b] : in1[b]));
      }
#endif


      // T2 =  xor all inputs
      // in1, in2 are only used in T1 during initialisation, not generation
      // if p=0, use this, if p=1, rotate the result left
      for(b=0;b<4;b++){
        B[aboff-1][b]=FFXOR(FFXOR(B[aboff+6][b],B[aboff+9][b]),Y[b]);
      }

#ifdef STREAM_INIT
      for(b=0;b<4;b++){
        B[aboff-1][b]=FFXOR(B[aboff-1][b],((j % 2) ? in1[b] : in2[b]));
      }
#endif


      // if p=1, rotate left (yes, this is what we're doing)
      tmp3=B[aboff-1][3];
      B[aboff-1][3]=FFXOR(B[aboff-1][3],FFAND(FFXOR(B[aboff-1][3],B[aboff-1][2]),p));
      B[aboff-1][2]=FFXOR(B[aboff-1][2],FFAND(FFXOR(B[aboff-1][2],B[aboff-1][1]),p));
      B[aboff-1][1]=FFXOR(B[aboff-1][1],FFAND(FFXOR(B[aboff-1][1],B[aboff-1][0]),p));
      B[aboff-1][0]=FFXOR(B[aboff-1][0],FFAND(FFXOR(B[aboff-1][0],tmp3),p));


      // T3 = xor all inputs
      for(b=0;b<4;b++){
        D[b]=FFXOR(FFXOR(E[b],Z[b]),extra_B[b]);
      }


      // T4 = sum, carry of Z + E + r
      for(b=0;b<4;b++){
        next_E[b]=F[b];
      }

      tmp0=FFXOR(Z[0],E[0]);
      tmp1=FFAND(Z[0],E[0]);
      F[0]=FFXOR(E[0],FFAND(q,FFXOR(Z[0],r)));
      tmp3=FFAND(tmp0,r);
      tmp4=FFOR(tmp1,tmp3);

      tmp0=FFXOR(Z[1],E[1]);
      tmp1=FFAND(Z[1],E[1]);
      F[1]=FFXOR(E[1],FFAND(q,FFXOR(Z[1],tmp4)));
      tmp3=FFAND(tmp0,tmp4);
      tmp4=FFOR(tmp1,tmp3);

      tmp0=FFXOR(Z[2],E[2]);
      tmp1=FFAND(Z[2],E[2]);
      F[2]=FFXOR(E[2],FFAND(q,FFXOR(Z[2],tmp4)));
      tmp3=FFAND(tmp0,tmp4);
      tmp4=FFOR(tmp1,tmp3);

      tmp0=FFXOR(Z[3],E[3]);
      tmp1=FFAND(Z[3],E[3]);
      F[3]=FFXOR(E[3],FFAND(q,FFXOR(Z[3],tmp4)));
      tmp3=FFAND(tmp0,tmp4);
      r=FFXOR(r,FFAND(q,FFXOR(FFOR(tmp1,tmp3),r))); // ultimate carry

/*
      we have just done this: (believe it or not)
      
      if (q) {
        F = Z + E + r;
        r = (F >> 4) & 1;
        F = F & 0x0f;
      }
      else {
          F = E;
      }
*/
      for(b=0;b<4;b++){
        E[b]=next_E[b];
      }

      // this simple instruction is virtually shifting all the shift registers
      aboff--;

/*
      we've just done this:

      A9=A8;A8=A7;A7=A6;A6=A5;A5=A4;A4=A3;A3=A2;A2=A1;A1=A0;A0=next_A0;
      B9=B8;B8=B7;B7=B6;B6=B5;B5=B4;B4=B3;B3=B2;B2=B1;B1=B0;B0=next_B0;
*/

      X[0]=s1a;
      X[1]=s2a;
      X[2]=s3b;
      X[3]=s4b;
      Y[0]=s3a;
      Y[1]=s4a;
      Y[2]=s5b;
      Y[3]=s6b;
      Z[0]=s5a;
      Z[1]=s6a;
      Z[2]=s1b;
      Z[3]=s2b;
      p=s7a;
      q=s7b;

#ifdef STREAM_NORMAL
      // require 4 loops per output byte
      // 2 output bits are a function of the 4 bits of D
      // xor 2 by 2
      cb_g[8*i+7-2*j]=FFXOR(D[2],D[3]);
      cb_g[8*i+6-2*j]=FFXOR(D[0],D[1]);
#endif


    } // INTERNAL LOOP


  } // EXTERNAL LOOP

  // move 32 steps forward, ready for next call
  for(k=0;k<10;k++){
    for(b=0;b<4;b++){
      A[32+k][b]=A[k][b];
      B[32+k][b]=B[k][b];
    }
  }


////////////////////////////////////////////////////////////////////////////////

#ifdef STREAM_NORMAL

#if GROUP_PARALLELISM==32
trasp64_32_88cw(cb);
#endif
#if GROUP_PARALLELISM==64
trasp64_64_88cw(cb);
#endif
#if GROUP_PARALLELISM==128
trasp64_128_88cw(cb);
#endif

#endif

#ifdef STREAM_INIT
#endif
#ifdef STREAM_NORMAL
#endif

}

