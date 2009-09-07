/*
 * Softcam plugin to VDR (C++)
 *
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nagra2.h"

// -- cAuxSrv ------------------------------------------------------------------

#ifdef HAS_AUXSRV
#include "network.h"
#define AUX_PROTOCOL_VERSION 2
int auxEnabled=0;
int auxPort=7777;
char auxAddr[80]="localhost";
char auxPassword[250]="auxserver";

class cAuxSrv : public cMutex {
private:
  cNetSocket so;
  //
  bool Login(void);
public:
  int Map(int map, unsigned char *data, int len, int outlen);
  };

bool cAuxSrv::Login()
{
  unsigned char buff[256];
  PRINTF(L_SYS_MAP,"auxsrv: connecting to %s:%d",auxAddr,auxPort);
  so.SetRWTimeout(7000);
  if(so.Connect(auxAddr,auxPort)) {
    buff[0]=0xA7;
    buff[1]=0x7A;
    buff[2]=0;
    int l=strlen(auxPassword);
    buff[3]=l;
    memcpy(&buff[4],auxPassword,l);
    buff[4+l]=0xFF;
    if(so.Write(buff,l+5)>0 &&
       so.Read(buff,9)>0 &&
       buff[0]==0x7A && buff[1]==0xA7 && buff[2]==0x00 && buff[3]==0x04 && buff[8]==0xff &&
       ((buff[4]<<8)|buff[5])==AUX_PROTOCOL_VERSION) return true;
    PRINTF(L_SYS_MAP,"auxsrv: login write failed");
    }
  so.Disconnect();
  return false;
}

int cAuxSrv::Map(int map, unsigned char *data, int len, int outlen)
{
  if(!auxEnabled) {
    PRINTF(L_SYS_MAP,"auxsrv: AUXserver is disabled!");
    return -1;
    }
  if(len>500 || outlen>500) return -1;
  cMutexLock lock(this);
  if(!so.Connected() && !Login()) return false;
  PRINTF(L_SYS_MAP,"auxsrv: calling map%02x",map);
  unsigned char buff[512];
  buff[0]=0xA7;
  buff[1]=0x7A;
  buff[2]=((len+1)>>8) & 0xff;
  buff[3]=(len+1)&0xff;
  buff[4]=map;
  memcpy(&buff[5],data,len);
  buff[len+5]=0xFF;
  if(so.Write(buff,len+6)>0) {
    if(so.Read(buff,5)>0) {
      if(buff[0]==0x7A && buff[1]==0xA7) {
        int l=buff[2]*256+buff[3];
        if(so.Read(buff+5,l,200)>0) {
          if(buff[4]==0x00) {
            int cycles=(buff[5]<<16)|(buff[6]<<8)|buff[7];
            if(l==outlen+4) {
              if(buff[l+4]==0xFF) {
                memcpy(data,buff+8,outlen);
                return cycles;
                }
              else PRINTF(L_SYS_MAP,"auxsrv: bad footer in map%02x response",map);
              }
            else PRINTF(L_SYS_MAP,"auxsrv: bad length in map%02x response (got=%d want=%d)",map,l-4,outlen);
            }
          else PRINTF(L_SYS_MAP,"auxsrv: map%02x not successfull (unsupported?)",map);
          }
        else PRINTF(L_SYS_MAP,"auxsrv: map%02x read failed (2)",map);
        }
      else PRINTF(L_SYS_MAP,"auxsrv: bad response to map%02x",map);
      }
    else PRINTF(L_SYS_MAP,"auxsrv: map%02x read failed",map);
    }
  else  PRINTF(L_SYS_MAP,"auxsrv: map%02x write failed",map);
  so.Disconnect();
  return -1;
}
#endif //HAS_AUXSRV

// -- cMap0101 ----------------------------------------------------------------

class cMap0101 : public cMapCore {
private:
  static const unsigned char primes[];
  static const int tim3b[][17];
  static const unsigned short msb3e[];
#ifdef HAS_AUXSRV
  cAuxSrv aux;
#endif
  //
  void MakePrime(unsigned char *residues, bool strong);
protected:
  virtual bool Map(int f, unsigned char *data, int l);
  };

const unsigned char cMap0101::primes[] = {
  0x03,0x05,0x07,0x0B,0x0D,0x11,0x13,0x17,0x1D,0x1F,0x25,0x29,0x2B,0x2F,0x35,0x3B,
  0x3D,0x43,0x47,0x49,0x4F,0x53,0x59,0x61,0x65,0x67,0x6B,0x6D,0x71,0x7F,0x83,0x89,
  0x8B,0x95,0x97,0x9D,0xA3,0xA7,0xAD,0xB3,0xB5,0xBF,0xC1,0xC5,0xC7,0xD3,0xDF,0xE3,
  0xE5,0xE9,0xEF,0xF1,0xFB
  };

const int cMap0101::tim3b[][17] = {
  { 2666,2804,2957,3105,3258,3406,3554,3707,3855,4008,4156,4304,4457,4605,4758,4906,5054 },
  { 3163,3316,3484,3652,3815,3983,4146,4314,4482,4645,4813,4976,5144,5312,5475,5643,5806 },
  { 3715,3888,4071,4254,4432,4615,4798,4981,5164,5342,5525,5708,5891,6074,6252,6435,6618 },
  { 4302,4490,4688,4886,5084,5282,5480,5678,5876,6074,6272,6470,6668,6866,7064,7262,7460 },
  { 4899,5107,5320,5533,5751,5964,6177,6390,6603,6821,7034,7247,7460,7673,7891,8104,8317 },
  { 5541,5759,5992,6220,6453,6681,6909,7142,7370,7603,7831,8059,8292,8520,8753,8981,9209 },
  { 6198,6431,6679,6927,7170,7418,7661,7909,8157,8400,8648,8891,9139,9387,9630,9878,10121 },
  { 6910,7163,7426,7689,7947,8210,8473,8736,8999,9257,9520,9783,10046,10309,10567,10830,11093 },
  { 7637,7905,8183,8461,8739,9017,9295,9573,9851,10129,10407,10685,10963,11241,11519,11797,12075 },
  { 8394,8682,8975,9268,9566,9859,10152,10445,10738,11036,11329,11622,11915,12208,12506,12799,13092 },
  { 9196,9494,9807,10115,10428,10736,11044,11357,11665,11978,12286,12594,12907,13215,13528,13836,14144 },
  { 10003,10346,10674,11002,11325,11653,11976,12304,12632,12955,13283,13606,13934,14262,14585,14913,15236 },
  { 10885,11218,11561,11904,12242,12585,12928,13271,13614,13952,14295,14638,14981,15324,15662,16005,16348 },
  { 11792,12145,12498,12856,13214,13572,13930,14288,14646,15004,15362,15720,16078,16436,16794,17152,17510 },
  { 12709,13087,13465,13843,14226,14604,14982,15360,15738,16121,16499,16877,17255,17633,18016,18394,18772 },
  { 13671,14069,14477,14880,15283,15686,16084,16492,16890,17298,17696,18099,18507,18905,19313,19711,20114 },
  { 14668,15091,15519,15947,16370,16798,17221,17654,18082,18505,18933,19356,19784,20212,20640,21068,21491 },
  };

void cMap0101::MakePrime(unsigned char *residues, bool strong)
{
  bool isPrime;
  AddMapCycles(strong?462:258);
  BN_copy(D,B);
  AddMapCycles(240);
  BN_zero(A);
  AddMapCycles(41);
  BN_set_word(A,2);
  A.SetPos(1);
  AddMapCycles(12);
  for(int i=2; i<=8; i++) { A.SetPos(i); AddMapCycles(9); }
  A.SetPos(0);
  AddMapCycles(8);
  if(strong) D.GetLE(residues+53,8);
  unsigned int counts[3] = { 0 };
  bool first=true;
  do {
    counts[0]++;;
    BN_add_word(B,2);
    if(first) { first=false; AddMapCycles(1600); }
    isPrime=true;
    for(int i=0; i<53; i++) {
      residues[i]+=2;
      unsigned char num=residues[i];
      unsigned char denom=primes[i];
      if(num>denom) {
        unsigned char r=0;
        while(denom>=r) { counts[1]++; r=(r<<1)|((num&0x80)>>7); num<<=1; }
        }
      residues[i]%=primes[i];
      if(residues[i]==0) { counts[2]++; isPrime=false; }
      }
    } while(!isPrime);
  cycles=1290+1465*counts[0]+counts[1]+13*counts[2];
}

bool cMap0101::Map(int f, unsigned char *data, int l)
{
  int sl=l;
  l=GetOpSize(l);
  switch(f) {
    case 0x21:
      AddMapCycles(169-6);
      IMakeJ();
      cycles=898;
      break;
    case 0x22:
      if(BN_is_zero(D)) { cycles=639-6; break; }
      l&=0x1fff;
      AddMapCycles(376);
      BN_zero(B);
      AddMapCycles(l>=(BN_num_bits(D)-1)/64*64 ? 244 : 210);
      BN_one(B);
      B.SetPos(1);
      AddMapCycles(17);
      for(int i=2; i<=8; i++) { B.SetPos(i); AddMapCycles(9); }
      AddMapCycles(44);
      BN_mod_lshift(B,B,l,D,ctx);
      break;
    case 0x23:
      AddMapCycles(169);
      IMonInit0();
      break;
    case 0x25:
      AddMapCycles(254);
      MakeJ0(B,D,C,wordsize<<6);
      // valid for wordsize 1 and 2
      AddMapCycles(795*wordsize-492);
      BN_zero(C);
      cycles=49+800*wordsize;
      break;
    case 0x29:
      {
      BN_add(B,B,C);
      bool b=BN_is_bit_set(B,wordsize<<6);
      if(b) BN_mask_bits(B,wordsize<<6);
      if(data) data[0]=b;
      cycles=501+(8*wordsize+3)/5*5-6;
      break; 
      }
    case 0x2a:
      {
      bool b=ModSub(B,B,D);
      if(data) data[0]=b;
      BN_zero(C);
      break;
      }
    case 0x2e:
    case 0x2F:
      if(l<=0) { cycles+=4; l=wordsize; }
      else if(l>17) { cycles+=5; l=17; }
      scalar.GetLE(data,l<<3);
      AddMapCycles(617+30*wordsize);
      for(int i=0; i<l; i++) {
       if(i&1) {
         unsigned char buf[8];
         J.PutLE(buf,8);
         AddMapCycles(10);
         for(int j=0; j<8; j++) {
           buf[j]=data[i*8+j];
           J.GetLE(buf,8);
           J.SetPos(j+1);
           AddMapCycles(14);
           }
         J.SetPos(0);
         AddMapCycles(12);
         }
       else
         AddMapCycles(114);
       }
      AddMapCycles(3);
      BN_mul(D,scalar,B,ctx);
      if(f&1) BN_add(D,D,C);
      BN_rshift(C,D,l<<6);
      BN_mask_bits(D,l<<6);
      D.Commit(l);
      BN_mask_bits(C,wordsize<<6);
      break;
    case 0x30:
    case 0x31:
      BN_sqr(D,B,ctx);
      if((f&1)) BN_add(D,D,C);
      BN_rshift(C,D,wordsize<<6);
      BN_rshift(J,B,((wordsize+1)/2)*128-64);
      BN_mask_bits(J,64);
      break;
    case 0x32:
      AddMapCycles(1000);
      l=min(34,l);
      if(!BN_is_zero(D)) {
        scalar.GetLE(data,l<<3);
        BN_div(C,B,scalar,D,ctx);
        BN_rshift(A,C,17*64);
        A.Commit(17);
        C.Commit(17);
        }
      BN_zero(J);
      break;
    case 0x36:
    case 0x38:
      AddMapCycles(230);
      MonMul0(B,f==0x36?A:B,B,C,D,J,wordsize);
      AddMapCycles(102);
      MonFin(B,D);
      break;
    case 0x3b:
      AddMapCycles(441);
      IMakeJ();
      AddMapCycles(46);
      IMonInit0(wordsize*60+4*l);
      scalar.GetLE(data,l<<3);
      MonMul(B,scalar,B,l);
      cycles=tim3b[wordsize-1][l-1]-6;
      break;
    case 0x3d:
      AddMapCycles(652);
      D.GetLE(data,wordsize<<3);
      AddMapCycles(514);
      IMakeJ();
      AddMapCycles(35);
      MonMul0(C,B,B,C,D,J,wordsize);
      AddMapCycles(143);
      BN_copy(A,B);
      AddMapCycles(73);
      MonExpNeg();
      MonMul(B,A,B);
      BN_zero(C);
      cycles+=rand()%(wordsize*20000)+2000;
      break;
    case 0x3c:
    case 0x3e:
    case 0x46:
      {
      if(f==0x46) l=1; else l=sl;
      if(l<=0) { l=wordsize; cycles+=4; }
      else if(l>wordsize) { l=wordsize; cycles+=l>17 ? 9:4; }
      scalar.GetLE(data,l<<3);
      int sbits=BN_num_bits(scalar);
      cycles+=3848+((sbits-1)/8)*650 - 11;
      int msb=data[(sbits-1)/8];
      for(int i=7; i>=1; --i) if(msb&(1<<i)) { cycles+=i*75-15; break; }
      for(int i=0; i<sbits; ++i) if(BN_is_bit_set(scalar,i)) cycles+=88;
      AddMapCycles(f==0x46 ? 400:441);
      if(BN_is_zero(scalar) || BN_num_bits(D)<=1) {
        IMakeJ();
        if(BN_num_bits(D)==1 || !BN_is_zero(scalar)) BN_zero(B);
        else BN_one(B);
        BN_one(A);
        }
      else {
        IMonInit();
        MonMul0(B,A,B,C,D,J,0);
        if(f==0x3c) AddMapCycles(2200+(rand()%(wordsize*2000)));
        MonFin(B,D);
        }
      BN_zero(C);
      if(f==0x46) cycles-=37;
      }
      break;
    case 0x4d:
      if(-0x018000==l)
        BN_mask_bits(B,64);
      else {
        BN_set_bit(B,(wordsize<<6)-1);
        if(-0x028000==l) BN_set_bit(B,(wordsize<<6)-2);
        }
      BN_set_bit(B,0);
      for(int i=0; i<53; i++) data[i]=BN_mod_word(B,primes[i]);
      BN_copy(A,B);
      BN_zero(C); BN_zero(D); BN_zero(J);
      break;
    case 0x4e:
    case 0x4f:
      MakePrime(data,f==0x4f);
      break;
    case 0x57:
#ifdef HAS_AUXSRV
      {
      int c=aux.Map(0x57,data,0x60,0x40);
      if(c>0) { cycles=c-6; break; }
      }
#endif
      {
      cBN a, b, x, y;
      if(l<2 || l>4) l=4;
      WS_START(l);
      l<<=3;
      D.GetLE(data+0*l,l);
      x.GetLE(data+1*l,l);
      y.GetLE(data+2*l,l);
      b.GetLE(data+3*l,l);
      a.GetLE(data+4*l,l);
      scalar.GetLE(data+5*l,l);
      bool doz=false;
      int scalarbits=BN_num_bits(scalar);
      if(scalarbits>=2) {
        if(BN_is_zero(x) && (BN_is_zero(y) || (BN_is_zero(b) && BN_num_bits(y)==1))) {
          BN_zero(Px);
          BN_copy(Py,y);
          BN_zero(Qz);
          MakeJ0(J,D);
          }
        else {
          CurveInit(a);
          ToProjective(0,x,y);
          BN_copy(Qx,Px);
          BN_copy(Qy,Py);
          for(int i=scalarbits-2; i>=0; i--) {
            DoubleP(0);
            if(BN_is_bit_set(scalar,i)) {
              BN_copy(A,Pz);
              if(BN_is_zero(Pz) || BN_is_zero(D)) {
                BN_copy(Px,Qx);
                BN_copy(Py,Qy);
                BN_copy(Pz,Qz);
                AddP(1);
                }
              else {
                doz=true;
                if(wordsize==4) {
                  BN_rshift(Py,Py,32);
                  BN_lshift(Py,Py,32);
                  BN_rshift(b,Qz,224);
                  BN_add(Py,Py,b);
                  }
                BN_mask_bits(Px,32);
                BN_lshift(b,Qz,32);
                BN_add(Px,Px,b);
                BN_mask_bits(Px,l<<3);
                AddP(0);
                }
              }
            }
          ToAffine();
          }
        }
      else {
        BN_copy(Px,x);
        BN_copy(Py,y);
        BN_zero(Qz);
        MakeJ0(J,D);
        }
      memset(data,0,0x40);
      Px.PutLE(&data[0x00],l);
      if(l<0x20 && doz) {
        unsigned char tmp[0x20];
        Qz.PutLE(tmp,l);
        memcpy(&data[l],&tmp[l-4],4);
        }
      Py.PutLE(&data[0x20],l);
      BN_zero(A);
      BN_zero(B);
      BN_zero(C);
      WS_END();
      break;
      }
    default:
      return false;
    }
  return true;
}

// -- cN2Prov0101 --------------------------------------------------------------

class cN2Prov0101 : public cN2Prov, public cN2Emu, private cMap0101 {
private:
  int desSize;
  DES_key_schedule desks1, desks2;
  unsigned char desblock[8];
  IdeaKS ks;
  cMapMemHW *hwMapper;
  // Randomiser
  unsigned int rnd, rndtime;
  //
  void AddRomCallbacks(void);
  bool RomCallbacks(void);
  bool ProcessMap(int f);
  bool ProcessDESMap(int f);
protected:
  int mecmAddr[2];
  int mecmKeyId;
  //
  virtual bool Algo(int algo, const unsigned char *hd, unsigned char *hw);
  virtual void DynamicHD(unsigned char *hd, const unsigned char *ed);
  virtual bool RomInit(void);
  virtual void Stepper(void);
  virtual void ReadHandler(unsigned char seg, unsigned short ea, unsigned char &op);
  virtual void TimerHandler(unsigned int num);
  virtual void AddMapCycles(unsigned int num) { AddCycles(num); }
  virtual unsigned int CpuCycles(void) { return Cycles(); }
public:
  cN2Prov0101(int Id, int Flags);
  virtual bool PostProcAU(int id, unsigned char *data);
  virtual int ProcessBx(unsigned char *data, int len, int pos);
  virtual int ProcessEx(unsigned char *data, int len, int pos);
  virtual int RunEmu(unsigned char *data, int len, unsigned short load, unsigned short run, unsigned short stop, unsigned short fetch, int fetch_len);
  virtual void PostDecrypt(bool ecm) { PostDecryptSetup(ecm); }
  };

static cN2ProvLinkReg<cN2Prov0101,0x0101,(N2FLAG_MECM|N2FLAG_POSTAU|N2FLAG_Bx|N2FLAG_Ex)> staticPL0101;

cN2Prov0101::cN2Prov0101(int Id, int Flags)
:cN2Prov(Id,Flags)
{
  mecmAddr[0]=0x91d7; mecmAddr[1]=0x92d7; mecmKeyId=0x106;
  seedSize=10;
  desSize=16; hwMapper=0;
  SetMapIdent(Id);
  hasReadHandler=true;
}

void cN2Prov0101::DynamicHD(unsigned char *hd, const unsigned char *ed)
{
  hd[5]=ed[5];
  hd[6]=(ed[7]&0xEF) | ((ed[6]&0x40)>>2);
  hd[7]=ed[8];
  hd[8]=(ed[9]&0x7F) | ((ed[6]&0x20)<<2);
  hd[9]=ed[6]&0x80;
}

bool cN2Prov0101::Algo(int algo, const unsigned char *hd, unsigned char *hw)
{
  if(algo!=0x40 && algo!=0x60) {
    PRINTF(L_SYS_ECM,"%04X: unknown MECM algo %02x",id,algo);
    return false;
    }
  if(!Init(id,102)) {
    PRINTF(L_SYS_ECM,"%04X: failed to initialize ROM",id);
    return false;
    }

  unsigned char keyNr=(algo>>5)&0x01;
  unsigned char mecmCode[256];
  GetMem(mecmAddr[keyNr],mecmCode,sizeof(mecmCode),0x80);
  cPlainKey *pk;
  unsigned char ideaKey[16];
  if(!(pk=keys.FindKey('N',mecmKeyId,keyNr,sizeof(ideaKey)))) {
    PRINTF(L_SYS_KEY,"missing %04x %02x MECM key",mecmKeyId,keyNr);
    return false;
    }
  pk->Get(ideaKey);
  idea.SetEncKey(ideaKey,&ks);
  idea.Decrypt(mecmCode,sizeof(mecmCode),&ks,0);
  HEXDUMP(L_SYS_RAWECM,mecmCode,sizeof(mecmCode),"decrypted MECM code");
  // check signature
  unsigned char data[256];
  memcpy(data,mecmCode,sizeof(data));
  RotateBytes(data,sizeof(data));
  SHA1(data,sizeof(data)-8,data);
  RotateBytes(data,20);
  if(memcmp(data,mecmCode,8)) {
    PRINTF(L_SYS_ECM,"%04X: MECM %02x decrypt signature failed",id,keyNr);
    return false;
    }

  memcpy(hw,hd,seedSize);
  ExpandInput(hw);

  SetSp(0x0FFF,0x0EF8);
  ClearBreakpoints();
  SetMem(0x0100,mecmCode+8,0x100-8);
  SetMem(0xa00,hd,seedSize);
  SetMem(0x0ba2,hw,0x80);
  AddBreakpoint(0x0000);
  AddRomCallbacks();
  SetPc(0x0100);
  while(!Run(100000)) {
    if(GetPc()==0x0000) {
      GetMem(0x0ba2,hw,0x80);
      return true;
      }
    else if(!RomCallbacks()) return false;
    }

/*
  if(algo==0x40) {
    const int hwCount=2;
    memcpy(hw,hd,hwCount+1);
    ExpandInput(hw);
    hw[0x18]|=1; hw[0x40]|=1;
    DoMap(SETSIZE,0,2);
    DoMap(IMPORT_A,hw,3);
    DoMap(IMPORT_D,hw+24);
    DoMap(0x3b);
    DoMap(EXPORT_C,hw);
    DoMap(IMPORT_A,hw+40,3);
    DoMap(IMPORT_D,hw+64);
    DoMap(0x3b);
    DoMap(EXPORT_C,hw+40);
    DoMap(0x43);
    DoMap(0x44,hw);
    DoMap(0x44,hw+64);
    memcpy(&hw[0],&hw[64+4],hwCount);
    memset(&hw[hwCount],0,128-hwCount);
    return true;
    }
  else if(algo==0x60) { // map 4D/4E/57
    hw[127]|=0x80; hw[0]|=0x01;
    DoMap(SETSIZE,0,16);
    DoMap(IMPORT_A,hw);
    DoMap(0x4d);
    DoMap(0x4e);
    DoMap(EXPORT_A,hw);
    RotateBytes(hw,16);
    DoMap(IMPORT_A,hw);
    DoMap(0x4e);
    DoMap(EXPORT_A,hw);
    DoMap(0x57,hw);
    return true;
    }

  PRINTF(L_SYS_ECM,"%04X: unknown MECM algo %02x",id,algo);
*/
  return false;
}

bool cN2Prov0101::PostProcAU(int id, unsigned char *data)
{
  if(data[1]&0x5f) return false;
  return true;
}

bool cN2Prov0101::RomInit(void)
{
  if(!AddMapper(hwMapper=new cMapMemHW(),HW_OFFSET,HW_REGS,0x00)) return false;
  SetPc(0x4000);
  SetSp(0x0FFF,0x0FE0);
  ClearBreakpoints();
  AddBreakpoint(0x537d);
  AddBreakpoint(0x8992);
  AddBreakpoint(0xA822);
  while(!Run(7000)) {
    switch(GetPc()) {
      case 0x537d:
        PRINTF(L_SYS_EMU,"%04x: ROM init successfull",id);
        return true;
      default:
        PRINTF(L_SYS_EMU,"%04x: ROM init failed: unexpected breakpoint",id);
        break;
      }
    }
  return false;
}

bool cN2Prov0101::ProcessMap(int f)
{
  unsigned short addr;
  unsigned char tmp[512];
  int l=GetOpSize(Get(0x48));
  int dl=l<<3;

  switch(f) {
    case SETSIZE:
      DoMap(f,0,Get(0x48));
      break;
    case IMPORT_J:
      l=1; dl=1<<3;
      // fall throught
    case IMPORT_A:
    case IMPORT_B:
    case IMPORT_C:
    case IMPORT_D:
    case IMPORT_LAST:
      addr=HILO(0x44);
      GetMem(addr,tmp,dl,0); DoMap(f,tmp,l);
      break;
    case EXPORT_J:
      l=1; dl=1<<3;
      // fall throught
    case EXPORT_A:
    case EXPORT_B:
    case EXPORT_C:
    case EXPORT_D:
    case EXPORT_LAST:
      addr=HILO(0x44);
      DoMap(f,tmp,l); SetMem(addr,tmp,dl,0);
      break;
    case SWAP_A:
    case SWAP_B:
    case SWAP_C:
    case SWAP_D:
      addr=HILO(0x44);
      GetMem(addr,tmp,dl,0); DoMap(f,tmp,l); SetMem(addr,tmp,dl,0);
      break;
    case CLEAR_A:
    case CLEAR_B:
    case CLEAR_C:
    case CLEAR_D:
    case COPY_A_B:
    case COPY_B_A:
    case COPY_A_C:
    case COPY_C_A:
    case COPY_C_D:
    case COPY_D_C:
      DoMap(f);
      break;
    case 0x22:
      DoMap(f,0,(Get(0x4a)<<8)|Get(0x49));
      break;
    case 0x29:
    case 0x2a:
      DoMap(f,tmp);
      Set(0x4b,tmp[0]);
      break;
    case 0x3c:
    case 0x3e:
      GetMem(HILO(0x44),tmp,dl,0);
      DoMap(f,tmp,Get(0x48));
      break;
    case 0x2e:
    case 0x2f:
    case 0x32:
    case 0x39:
    case 0x3b:
    case 0x3d:
      GetMem(HILO(0x44),tmp,dl,0);
      DoMap(f,tmp,l);
      break;
    case 0x21:
    case 0x23:
    case 0x25:
    case 0x30:
    case 0x31:
    case 0x36:
    case 0x38:
    case 0x3a:
    case 0x43:
      DoMap(f);
      break;
    case 0x44:
    case 0x45:
      GetMem(0x400,tmp,64,0);
      GetMem(0x440,tmp+64,28,0);
      DoMap(f,tmp,l);
      SetMem(0x400,tmp,64,0);
      SetMem(0x440,tmp+64,28,0);
      break;
    case 0x46:
      GetMem(HILO(0x44),tmp,8,0);
      DoMap(f,tmp);
      break;
    case 0x4d:
      DoMap(f,tmp,-((Get(0x48)<<16)|(Get(0x4a)<<8)|Get(0x49)));
      if(*tmp==0xff)
        Set(0x4b,0x00);
      else {
        Set(0x4b,*tmp);
        SetMem(0x400,tmp+1,wordsize*8>53?wordsize*8:53,0x00);
        }
      break;
    case 0x4e:
    case 0x4f:
      GetMem(0x400,tmp+1,53,0);
      if(f==0x4f) GetMem(HILO(0x44),tmp+54,8,0);
      DoMap(f,tmp);
      Set(0x4b,*tmp);
      SetMem(0x400,tmp+1,53,0);
      break;
    case 0x57:
      addr=HILO(0x46);
      l=wordsize; if(l<2 || l>4) l=4;
      dl=l<<3;
      for(int i=0; i<6; i++) GetMem(HILO(addr+i*2),tmp+i*dl,dl,0);
      DoMap(f,tmp);
      SetMem(0x400,tmp,0x40,0);
      memset(tmp,0,11*32);
      SetMem(0x440,tmp,11*32,0);
      break;
    default:
      PRINTF(L_SYS_EMU,"%04x: map call %02x not emulated",id,f);
      return false;
    }
  c6805::a=0; c6805::x=0; c6805::y=0; 
  return true;
}

bool cN2Prov0101::ProcessDESMap(int f)
{
  unsigned char data[16];
  switch(f) {
    case 0x05:  // 3DES encrypt
      DES_ecb2_encrypt(DES_CAST(desblock),DES_CAST(desblock),&desks1,&desks2,DES_ENCRYPT);
      break;
    case 0x06:  // 3DES decrypt
      DES_ecb2_encrypt(DES_CAST(desblock),DES_CAST(desblock),&desks1,&desks2,DES_DECRYPT);
      break;
    case 0x07:
      Set(0x2d,0x02); //XXX quick-hack or the real thing??
      break;
    case 0x0b:  // load DES data block from memory
      GetMem(HILO(0x25),desblock,8,Get(0x24));
      break;
    case 0x0c:  // store DES data block to memory
      SetMem(HILO(0x2b),desblock,8,Get(0x2a));
      break;
    case 0x0e:  // get DES key1 and key2
      GetMem(HILO(0x25),data,8,Get(0x24));
      DES_key_sched((DES_cblock *)data,&desks1);
      GetMem(HILO(0x28),data,8,Get(0x27));
      DES_key_sched((DES_cblock *)data,&desks2);
      break;
    case 0x0f:  // set DES size
      desSize=Get(0x2d);
      if(desSize!=0x10 && desSize!=0x18) {
        PRINTF(L_SYS_EMU,"%04x: invalid DES key size %02x",id,desSize);
        return false;
        }
      break;
    default:
      PRINTF(L_SYS_EMU,"%04x: DES map call %02x not emulated",id,f);
      return false;
  }
  return true;
}

bool cN2Prov0101::RomCallbacks(void)
{
  bool dopop=true;
  unsigned int ea=GetPc();
  if(ea&0x8000) ea|=(cr<<16);
  switch(ea) {
    case 0x3840: //MAP Handler
    case 0x00A822:
      if(!ProcessMap(a)) return false;
      if(Interrupted()) dopop=false;
      break;
    case 0x3844: //DES Handler
    case 0x008992:
      if(!ProcessDESMap(a)) return false;
      break;
    case 0x5F23: //Erase_RAM_and_Hang
    case 0x5F27: //Erase_RAM_and_Hang_Lp
    case 0x5F5E: //BrainDead
      PRINTF(L_SYS_EMU,"%04x: emu hung at %04x",id,ea);
      return false;
    case 0x70A6: //Write_Row_EEP_RC2_Len_A_To_RC1
      {
      unsigned short rc1=HILO(0x47);
      unsigned short rc2=HILO(0x4a);
      for(int i=0; i<a; i++) Set(0x80,rc1++,Get(dr,rc2++));
      Set(0x4a,rc2>>8);
      Set(0x4b,rc2&0xff);
      break;
      }
    case 0x7BFE: //Write_Row_EEPROM_A_from_X_to_RC1
      {
      unsigned short rc1=HILO(0x47);
      unsigned short rc2=c6805::x;
      for(int i=0; i<a; i++) Set(0x80,rc1++,Get(rc2++));
      break;
      }
    case 0x7CFF: //UPDATE_USW_03DD_03DE
      Set(0x30E8,Get(0x03DD));
      Set(0x30E9,Get(0x03DE));
      break;
    case 0x00A23C: //IDEA_Generate_Expanded_Key
      {
      unsigned char key[16];
      GetMem(0x0a20,key,16);
      idea.SetEncKey(key,&ks);
      break;
      }
    case 0x00A2E9: //IDEA_Cypher
      {
      unsigned char data[8];
      GetMem(0x070,data,8);
      idea.Encrypt(data,8,data,&ks,0);
      SetMem(0x070,data,8);
      break;
      }
    default:
      PRINTF(L_SYS_EMU,"%04X: unknown ROM breakpoint %04x",id,ea);
      return false;
    }
  if(dopop) {
    if(ea>=0x8000) PopCr();
    PopPc();
    }
  return true;
}

void cN2Prov0101::AddRomCallbacks(void)
{
  AddBreakpoint(0xA822); // map handler
  AddBreakpoint(0x3840);
  AddBreakpoint(0x3844);
  AddBreakpoint(0xA23C); //IDEA 
  AddBreakpoint(0xA2E9);
  AddBreakpoint(0x70A6);
  AddBreakpoint(0x7BFE);
  AddBreakpoint(0x7CFF);
  AddBreakpoint(0x5F23);
  AddBreakpoint(0x5F27);
  AddBreakpoint(0x5F5E);
}

int cN2Prov0101::ProcessBx(unsigned char *data, int len, int pos)
{
  if(Init(id,102)) {
    SetMem(0x92,data+pos-1,len-pos+1);
    SetPc(0x93);
    SetSp(0x0FFF,0x0EF8);
    ClearBreakpoints();
    AddBreakpoint(0x9569);
    AddBreakpoint(0x0000);
    AddRomCallbacks();
    while(!Run(1000)) {
      if(GetPc()==0x9569) {
        GetMem(0x80,data,len);
        return a;
        }
      else if(GetPc()==0x0000) break;
      else if(!RomCallbacks()) break;
      }
    }
  return -1;
}

int cN2Prov0101::ProcessEx(unsigned char *data, int len, int pos)
{
  if(Init(id,102)) {
    SetMem(0x80,data,len);
    SetPc(0x9591);
    SetSp(0x0FFF,0x0EF8);
    Push(0x99); //push the bug-table return onto the stack
    Push(0x95);
    Push(0x00);
    Set(0x00,0x62,0x26);
    Set(0x00,0x63,0x02);
    Set(0x00,0x03d3,len-0x12);
    ClearBreakpoints();
    AddBreakpoint(0x9569);
    AddBreakpoint(0x9599);
    AddRomCallbacks();
    while(!Run(10000)) {
      if(GetPc()==0x9569) {
        GetMem(0x80,data,len);
        return max((int)a,6);
        }
      else if(GetPc()==0x9599) break;
      else if(!RomCallbacks()) break;
      }
    }
  return -1;
}

int cN2Prov0101::RunEmu(unsigned char *data, int len, unsigned short load, unsigned short run, unsigned short stop, unsigned short fetch, int fetch_len)
{
  if(Init(id,102)) {
    SetSp(0x0FFF,0x0EF8);
    SetMem(load,data,len);
    SetPc(run);
    ClearBreakpoints();
    AddBreakpoint(stop);
    if(stop!=0x0000) AddBreakpoint(0x0000);
    AddRomCallbacks();
    while(!Run(100000)) {
      if(GetPc()==0x0000 || GetPc()==stop) {
        GetMem(fetch,data,fetch_len);
        return 1;
        }
      else if(!RomCallbacks()) break;
      }
    }
  return -1;
}

void cN2Prov0101::ReadHandler(unsigned char seg, unsigned short ea, unsigned char &op)
{
   if(ea==0x2f70) op=random()&0xFF;
   else if(ea==0x2f71) {
     unsigned int n=random()&0xFF;
     unsigned int cy=Cycles();
     if(cy>rndtime && cy<=rndtime+4) {
       unsigned int o=sn8(rnd);
       n=(n&0xE3) | (o&4) | ((o>>1)&16) | ((o^(o>>1))&8);
       }
     op=rnd=n; rndtime=cy;
     }
}

void cN2Prov0101::TimerHandler(unsigned int num)
{
  if(hwMapper) {
    int mask=hwMapper->AddCycles(num);
    if(mask) DisableTimers(13);
    for(int t=0; mask; mask>>=1,t++)
      if(mask&1) {
        if(t==2) {
          PRINTF(L_SYS_EMU,"0101: Timer interrupt %u @ %04x",t,GetPc());
          RaiseException(9);
          if(Interruptible()) throw(t);
          }
        }
    }
}

void cN2Prov0101::Stepper(void)
{
  unsigned short pc=GetPc();
  if(pc>=0x93 && pc<=0xE0) { 	// pc in EMM data
    unsigned char op=Get(pc);
    if((op&0xF0)==0x00) { 	// current opcode BRCLR/BRSET
      int fake=0; 		// 1=branch -1=non-branch
      if(Get(pc+3)==0x81) 	// next opcode == RTS
        fake=1;			// fake branch
      else {
        unsigned char off=Get(pc+2);
        unsigned short target=pc+3+off;
        if(off&0x80) target-=0x100;
        if(Get(target)==0x81) 	// branch target == RTS
          fake=-1;		// fake non-branch
        }
      if(fake) {
        unsigned short ea=Get(pc+1);
        unsigned char val=Get(dr,ea);
        int bit=1<<((op&0xF)>>1);
        // set/clr bit according to fake-mode and opcode
        if((fake>0 && (op&0x01)) || (fake<0 && !(op&0x01)))  {
          if(val&bit) loglb->Printf("*");
          val&=~bit;
          }
        else {
          if(!(val&bit)) loglb->Printf("*");
          val|=bit;
          }
        Set(dr,ea,val);
        }
      }
    }
}

// -- cN2Prov0901 --------------------------------------------------------------

class cN2Prov0901 : public cN2Prov0101 {
public:
  cN2Prov0901(int Id, int Flags);
  };

static cN2ProvLinkReg<cN2Prov0901,0x0901,(N2FLAG_MECM|N2FLAG_POSTAU|N2FLAG_Bx|N2FLAG_Ex)> staticPL0901;

cN2Prov0901::cN2Prov0901(int Id, int Flags)
:cN2Prov0101(Id,Flags)
{
  mecmAddr[0]=0x91f5; mecmAddr[1]=0x92f5; mecmKeyId=0x907;
}
