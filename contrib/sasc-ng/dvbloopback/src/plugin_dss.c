/*
   DVBLoopback
   Copyright Alan Nisota 2006

   This file is part of DVBLoopback.

    DVBLoopback is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    DVBLoopback is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DVBLoopback; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#ifdef USE_DSS

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#include <netinet/in.h>

#include "list.h"
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include "msg_passing.h"
#include "process_req.h"

#define PLUGIN_ID 14
#define DBG_NAME "DSS"
#include "debug.h" //This is required to happen AFTER PLUGIN_ID is defined

#define DSS_SYMBOLRATE 22211000
#define DSS_PMTPID 0x20
#define DSS_NITPID 0x10
#define DSS_NETWORKID 0x1f0f
#define TS_SIZE 188
#define PTS_ONLY         0x80
#define PTS_DTS          0xC0

struct dss_hdr {
  int packet_framing;
  int bundle_boundary;
  int control_flag;
  int control_sync;
  int scid;
  int continuity_counter;
  int header_designator_type;
  int header_designator_id;
  int toggle_flag;
};

struct dss_pkt {
  struct list_head list;
  struct dss_hdr hdr;
  int64_t        rts;
  uint8_t        buf[184+127+1];
  int            buf_len;
  int            cont;
  int            start;
  int            fd;
  //Audio related stuff
  int            is_video;
  int            audio_len;
  int            audio_packlen;
  uint8_t        audio_pack[16];
  int            audio_tmp_len;
  uint8_t        audio_tmp[4];
};

struct cir {
  int pip_flag;
  int dip_flag;
  int use_heap;
  int long_name_flag;
  int logo_flag;
  int scid_size_flag;
  int service_paradigm_indicator;
  int number_of_scids;
  int pip_transponder;
  int dip_transponder;
  int channel_transponder;
  int virt_channel_number;
  int network_id;
  uint8_t short_name[4];
  int scid[16];
  int scid_type[16];
};

struct cssm {
  int number_of_channels;
  int segment_size;
  int buf_len;
  uint8_t buf[65536];
};

struct segment_list {
  int number_of_segments;
  int up_scid;
  int pip_scid;
  int dip_scid;
  int default_network_id;
  int provider_id;
  int checksum;
};

struct mpg {
  struct segment_list segl;
  struct cssm cssm;
  struct cir *cir;
  int cir_len;
  int num_channels;
  int current_transponder;
};

struct dss {
  struct list_head list;
  int adapt;
  pthread_mutex_t lock;
  struct list_head pkt_ll; //struct dss_pkt
  struct mpg mpg;
  uint32_t old_standard;
  int is_dss;
  uint8_t *buf;
  uint32_t buf_len;
  uint32_t buf_used;
  uint8_t unused[131];
  uint8_t unused_len;
  
};

static int dss_opt = 0;
static int opt_enable = 0;
static char * opt_file = NULL;
static struct option dss_Opts[] = {
  {"dss-enable", 0, &dss_opt, 'e'},
  {"dss-file", 1, &dss_opt, 'f'},
  {0, 0, 0, 0},
};

LIST_HEAD(dsslist);
LIST_HEAD(dsspkt_empty_queue);

//taken and adapted from libdtv, (c) Rolf Hakenes
// CRC32 lookup table for polynomial 0x04c11db7
static unsigned int crc_table[256] = {
   0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
   0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
   0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
   0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
   0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
   0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
   0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
   0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
   0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
   0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
   0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
   0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
   0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
   0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
   0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
   0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
   0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
   0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
   0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
   0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
   0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
   0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
   0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
   0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
   0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
   0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
   0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
   0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
   0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
   0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
   0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
   0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
   0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
   0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
   0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
   0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
   0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
   0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
   0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
   0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
   0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
   0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
   0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4};

unsigned int crc32_04c11db7 (const unsigned char *d, int len, unsigned int crc)
{
   register int i;
   const unsigned char *u=(unsigned char*)d; // Saves '& 0xff'

   for (i=0; i<len; i++)
      crc = (crc << 8) ^ crc_table[((crc >> 24) ^ *u++)];

   return crc;
}
static void get_pespts(uint8_t *spts,uint8_t *pts)
{

        pts[0] = 0x01 |
                ((spts[0] & 0xC0) >>5);
        pts[1] = ((spts[0] & 0x3F) << 2) |
                ((spts[1] & 0xC0) >> 6);
        pts[2] = 0x01 | ((spts[1] & 0x3F) << 2) |
                ((spts[2] & 0x80) >> 6);
        pts[3] = ((spts[2] & 0x7F) << 1) |
                ((spts[3] & 0x80) >> 7);
        pts[4] = 0x01 | ((spts[3] & 0x7F) << 1);
}
static int write_pes_header(uint8_t id, int length , uint64_t PTS, uint64_t DTS,
		     uint8_t *obuf, int stuffing, uint8_t ptsdts)
{
	uint8_t le[2];
	uint8_t dummy[3];
	uint8_t *pts;
	uint8_t ppts[5];
	uint32_t lpts;
	uint8_t *dts;
	uint8_t pdts[5];
	uint32_t ldts;
	int c;
	uint8_t headr[3] = {0x00, 0x00, 0x01};
	
	lpts = htonl((PTS/300ULL) & 0x00000000FFFFFFFFULL);
	pts = (uint8_t *) &lpts;
	get_pespts(pts,ppts);
	if ((PTS/300ULL) & 0x0000000100000000ULL) ppts[0] |= 0x80;

	ldts = htonl((DTS/300ULL) & 0x00000000FFFFFFFFULL);
	dts = (uint8_t *) &ldts;
	get_pespts(dts,pdts);
	if ((DTS/300ULL) & 0x0000000100000000ULL) pdts[0] |= 0x80;

	c = 0;
	memcpy(obuf+c,headr,3);
	c += 3;
	memcpy(obuf+c,&id,1);
	c++;

	le[0] = 0;
	le[1] = 0;
	length -= 6;

	le[0] |= ((uint8_t)(length >> 8) & 0xFF); 
	le[1] |= ((uint8_t)(length) & 0xFF); 
	memcpy(obuf+c,le,2);
	c += 2;

	dummy[0] = 0x80;
	dummy[1] = 0;
	dummy[2] = stuffing;
	
	if (ptsdts == PTS_ONLY){
		dummy[2] += 5;
		dummy[1] |= PTS_ONLY;
		ppts[0] |= 0x20;
	} else 	if (ptsdts == PTS_DTS){
		dummy[2] += 10;
		dummy[1] |= PTS_DTS;
		ppts[0] |= 0x30;
		pdts[0] |= 0x10;
	}
		

	memcpy(obuf+c,dummy,3);
	c += 3;

	if (ptsdts == PTS_ONLY){
		memcpy(obuf+c,ppts,5);
		c += 5;
	} else if ( ptsdts == PTS_DTS ){
		memcpy(obuf+c,ppts,5);
		c += 5;
		memcpy(obuf+c,pdts,5);
		c += 5;
	}

	memset(obuf+c,0xFF,stuffing);
	c += stuffing;

	return c;
}
static int write_ts_header(int pid, int payload_start, int count,
		    int64_t SCR, uint8_t *obuf, int stuff)
{
	int c = 0;
	uint8_t *scr;
	uint32_t lscr;
	uint16_t scr_ext = 0;

	obuf[c++] = 0x47;
	obuf[c++] = (payload_start ? 0x40 : 0x00) | ((pid >> 8) & 0x1f);
	obuf[c++] = pid & 0xff;
	obuf[c++] = ((SCR >= 0 || stuff) ? 0x30 : 0x10) | count;
	if (SCR >= 0|| stuff) {
		if (stuff)
			stuff--;
		int size = stuff;
		unsigned char flags = 0;
		if(SCR >= 0) {
			if(size < 7)
				size = 7;
			flags |= 0x10;
		}
		obuf[c++] = size;
		if(size) {
			obuf[c++] = flags;
			size--;
		}
		if(SCR >= 0) {
			uint8_t bit;
			lscr = (uint32_t) ((SCR/300ULL) & 0xFFFFFFFFULL);
 			bit = (lscr & 0x01) << 7;
			lscr = htonl(lscr >> 1);

			scr = (uint8_t *) &lscr;
			scr_ext = (uint16_t) ((SCR%300ULL) & 0x1FFULL);
			obuf[c++] = scr[0];
			obuf[c++] = scr[1];
			obuf[c++] = scr[2];
			obuf[c++] = scr[3];
			obuf[c++] = bit | 0x7e | (scr_ext >> 8);
			obuf[c++] = scr_ext & 0xff;
			size -= 6;
		}
		while(size-- > 0)
			obuf[c++] = 0xff;
	}
	return c;
}
//extract PTS from Picture frame
int get_ptsdts(unsigned char *buf, int len, uint64_t *pts, uint64_t *dts)
{
  int picture_coding_type;
  int hdr_len;
  uint32_t pts1, dts1;
  picture_coding_type = (buf[5] >> 3) & 0x07;
  hdr_len = (picture_coding_type > 1) ? 9 : 8;
  if(buf[hdr_len + 3] == 0xb5)
    hdr_len += 9;
  if(buf[hdr_len + 3] == 0xb2) {
    pts1 = ((buf[hdr_len+6] & 0x03)   << 30) +
           ((buf[hdr_len+7] & 0x7f) << 23) +
           ((buf[hdr_len+8])          << 15) +
           ((buf[hdr_len+9] & 0x7f) << 8) +
           buf[hdr_len+10];
    dts1 = ((buf[hdr_len+13] & 0x03)   << 30) +
           ((buf[hdr_len+14] & 0x7f) << 23) +
           ((buf[hdr_len+15])          << 15) +
           ((buf[hdr_len+16] & 0x7f) << 8) +
           buf[hdr_len+17];
    //NOTE:  This is wrong.  DSS timestamps only have a resolution of 2^32/300
    dprintf2("pts: %08x/%f dts: %08x/%f\n", pts1, pts1 / 27000000.0, dts1, dts1 / 27000000.0);
    *pts = pts1;
    *dts = dts1;
    return 1;
  }
  return 0;
}
struct dss_pkt *parse_dss_hdr(unsigned char *buf, struct dss *dss)
{
  struct dss_pkt *pkt;
  int scid = ((buf[1] << 8) + buf[2]) & 0x0fff;
  
  ll_find_elem(pkt, dss->pkt_ll, hdr.scid, scid, struct dss_pkt);
  if(! pkt) {
    //dprintf0("Found unexpected scid: %d %02x %02x %02x ...\n", scid, buf[0], buf[1], buf[2]);
    return NULL;
  }
  pkt->hdr.packet_framing         = buf[1] >> 7;
  pkt->hdr.bundle_boundary        = (buf[1] >> 6) & 0x01;
  pkt->hdr.control_flag           = (buf[1] >> 5) & 0x01;
  pkt->hdr.control_sync           = (buf[1] >> 4) & 0x01;
  pkt->hdr.continuity_counter     = (buf[3] >> 4);
  pkt->hdr.header_designator_type = (buf[3] >> 2) & 0x03;
  pkt->hdr.header_designator_id   = buf[3] & 0x03;
  return pkt;
}

void write_dummy_ts(uint8_t *ts)
{
  static const uint8_t data[TS_SIZE] = {0x47, 0x1f, 0xff, 0x10};
  memcpy(ts, data, TS_SIZE);
}


//We should always write out a frame here
int write_ts_frame(struct dss_pkt *pkt, uint8_t *ts)
{
  int len;
  int stuff;
  int pos = 0;

  stuff = (pkt->buf_len < TS_SIZE - 4) ? TS_SIZE - 4 - pkt->buf_len : 0;

  pos = write_ts_header(pkt->hdr.scid, pkt->start, pkt->cont,
                        pkt->rts, ts, stuff);
  pkt->cont = (pkt->cont + 1) & 0x0f;
  len = TS_SIZE - pos;
  pkt->rts = -1;
  pkt->start = 0;
  memcpy(ts+pos, pkt->buf, len);
  pkt->buf_len -= len;
  //This is horribly inefficient!
  if(pkt->buf_len > 0)
    memmove(pkt->buf, pkt->buf + len, pkt->buf_len);
  else 
    pkt->buf_len = 0;
  return 1;
}

void write_ts(uint8_t **out, uint8_t *buf, int len, struct dss_pkt *pkt)
{
  uint8_t ts[188];

  if(pkt->hdr.bundle_boundary) {
    while(pkt->buf_len != 0) {
      if(write_ts_frame(pkt, ts)) {
        memcpy(*out,  ts, TS_SIZE);
        *out += TS_SIZE;
      }
    }
    pkt->start = 1;
    pkt->hdr.bundle_boundary = 0;
  }
  memcpy(pkt->buf + pkt->buf_len, buf, len);
  pkt->buf_len += len;
  if(pkt->buf_len >= 184) {
    if(write_ts_frame(pkt, ts)) {
      memcpy(*out,  ts, TS_SIZE);
      *out += TS_SIZE;
    }
  }
}

int make_pes(uint8_t *outbuf, uint8_t *inbuf, struct dss_pkt *pkt)
{
  int ptsdts;
  uint64_t pts, dts;
  if(get_ptsdts(inbuf, 127, &pts, &dts))
    ptsdts = PTS_DTS;
  else
    ptsdts = 0;
  if(ptsdts && pkt)
    pkt->rts = dts;
  return write_pes_header(0xE0, 6, pts, dts, outbuf, 0, ptsdts);
}

void parse_aux(uint8_t *buf, struct dss_pkt *pkt)
{
  if((buf[0] & 0x3f) > 0x01)
    return;
  pkt->rts = (buf[3] << 24) | (buf[4] << 16) | (buf[5] << 8) | buf[6];
}
void get_scids(struct cir *cir, int basescid)
{
  static const uint8_t sc[16][17] = {
    {}, //0x00
    {2, 0x0f, 0}, //0x01
    {9, 0x0f, 0, 1, 2, 3, 4, 5, 6, 7}, //0x02
    {9, 0x0f, 1, 1, 2, 3, 4, 5, 6, 7}, //0x03
    {9, 0x0f, 2, 1, 2, 3, 4, 5, 6, 7}, //0x04
    {9, 0x0f, 3, 1, 2, 3, 4, 5, 6, 7}, //0x05
    {}, //0x06
    {}, //0x07
    {}, //0x08
    {}, //0x09
    {9, 0x0f, 0, 1, 2, 3, 4, 5, 6, 7}, //0x0a
    {}, //0x0b
    {1, 0x00}, //0x0c
    {1, 0x0d}, //0x0d
    {1, 0x0c}, //0x0e
    {1, 0x0f}, //0x0f
  };
  int spi = cir->service_paradigm_indicator;
  int i;

  for(i = 0; i < cir->number_of_scids; i++) {
    cir->scid[i] = basescid+i;
    if(i < sc[spi][0])
      cir->scid_type[i] = sc[spi][i+1];
    else
      cir->scid_type[i] = sc[spi][sc[spi][0]];
  }
  if(spi == 0x0a)
    cir->scid_type[cir->number_of_scids-1] = 0x09;
}
void read_extended_chaninfo(struct cir *cir, uint8_t *heap)
{
  int i, offset;

  if(cir->service_paradigm_indicator == 0) {
    for(i=0; i < cir->number_of_scids; i++) {
      if(! cir->scid_size_flag) {
        if((i & 0x01) == 0) {
          cir->scid[i] = heap[0];
          cir->scid_type[i] = heap[1] >> 4;
          heap++;
        } else {
          cir->scid[i] = heap[1];
          cir->scid_type[i] = heap[0] & 0x0f;
          heap+=2;
        }
      } else {
        if((i & 0x01) == 0) {
          cir->scid[i] = (heap[0] << 4) | (heap[1] >> 4);
          cir->scid_type[i] = heap[1] & 0x0f;
        } else {
          cir->scid[i] = ((heap[0] << 8) | heap[1] >> 4) & 0xfff;
          cir->scid_type[i] = heap[0] >> 4;
        }
        heap+=2;
      }
    }
  } else {
    int basescid;
    basescid = cir->scid_size_flag ? ((heap[0] << 4) | (heap[1] >> 4)) :
                                     heap[0];
    get_scids(cir, basescid);
    heap+=2;
  }
  if(cir->pip_flag == 0x02)
    heap+=2;
  if(cir->dip_flag == 0x02)
    heap+=2;
  if(cir->long_name_flag) {
    //we could do a strcpy here
    while(*heap)
      heap++;
    heap++;
  }
  if(cir->logo_flag)
    heap++;
  offset = 1;
  if(*heap & 0x20) offset+=2; //altcn_flag
  if(*heap & 0x10) offset+=2; //ext4_flag
  if(*heap & 0x08) offset+=1; //altci_flag
  if(*heap & 0x04) cir->network_id = heap[offset]; //altni_flag
}

void process_cir(struct cir *cir, uint8_t *cssm, int satid,
                 int provid, uint8_t *heap)
{
  int i;
  int offset = provid ? 1 : 0;
  cir->network_id = satid;
  cir->pip_flag = (cssm[0] >> 2) & 0x03;
  cir->dip_flag = cssm[0] & 0x03;
  cir->use_heap = cssm[1] & 0x80 ? 1 : 0;
  cir->long_name_flag = cssm[1] & 0x40;
  cir->logo_flag = cssm[1] & 0x20;
  cir->scid_size_flag = (cssm[1] >> 4) & 0x01;
  cir->service_paradigm_indicator = cssm[1] & 0x0f;
  cir->number_of_scids = cssm[2+offset];
  cir->pip_transponder = cssm[3+offset];
  cir->dip_transponder = cssm[4+offset];
  cir->channel_transponder = cssm[5+offset];
  cir->virt_channel_number = cssm[6+offset] << 8 | cssm[7+offset];
  memcpy(cir->short_name, &cssm[8+offset], 4);
  if(cir->use_heap) {
    int index = (cssm[15+4*offset] << 8) | cssm[16+4*offset];
    read_extended_chaninfo(cir, heap + index);
  } else {
    get_scids(cir, cssm[15+4*offset]);
  }
  //we have added a channel
  dprintf2("%d(%c%c%c%c) tp:%d sat:%02x", cir->virt_channel_number,
         cir->short_name[0], cir->short_name[1], cir->short_name[2],
         cir->short_name[3], cir->channel_transponder, cir->network_id);
  for(i = 0; i < cir->number_of_scids; i++)
    dprintf2(" %03x:%02x", cir->scid[i], cir->scid_type[i]);
  dprintf2("\n");
}

int get_avail_chan(struct mpg *mpg, uint32_t *indices, int max_chan)
{
  int i, j = 0;
  int cur_tp = mpg->current_transponder;
  dprintf2("Searching %d channels on sat:%d tp:%d\n", mpg->num_channels, mpg->segl.default_network_id, cur_tp);
  for(i = 0; i < mpg->num_channels && j < max_chan; i++) {
    if(mpg->cir[i].network_id != mpg->segl.default_network_id ||
       mpg->cir[i].channel_transponder != cur_tp)
      continue;
    indices[j++] = i;
    dprintf2("  Found channel: %d\n", mpg->cir[i].virt_channel_number);
  }
  dprintf2("Found %d channels\n", j);
  return j;
}

void write_chaninfo(uint8_t **out, struct mpg *mpg)
{
  static int patcount = 0, pmtcount = 0, nitcount = 0;
  int pos, i, j, pmtpos, patpos = 9, chan_cnt;
  uint8_t buf[188];
  //PMT Program number = 1
  //PMT PID = 0x20
  uint8_t pat[184] = {0x00, 0x00, 0xb0, 0x00, 0xfe, 0xef, 0xc1, 0x00, 0x00};
  uint8_t pmt[184] ={0x00, 0x02, 0xb0, 0x00, 0x00, 0x00, 0xc1, 0x00, 0x00,
                     0x00, 0x00, 0xf0, 0x00};
  uint8_t nit[184] ={0x00, 0x40, 0xf0, 0x0d, 0x00, 0x00, 0xff, 0x00, 0x00,
                     0xf0, 0x00, 0xf0, 0x00};
  uint32_t chanidx[40]; //40 is max channels in a single PAT frame

  chan_cnt = get_avail_chan(mpg, chanidx, 40);
  if(!chan_cnt)
    return;
  patcount = (patcount + 1) & 0x0f;
  //PAT
  //add NIT info
  pat[patpos++] = 0;
  pat[patpos++] = 0;
  pat[patpos++] = 0xe0 | ((DSS_NITPID >> 8) & 0x1f);
  pat[patpos++] = DSS_NITPID & 0xff;
  for(i = 0; i < chan_cnt; i++) {
    pat[patpos++] = (mpg->cir[chanidx[i]].virt_channel_number >> 8) & 0xff;
    pat[patpos++] = mpg->cir[chanidx[i]].virt_channel_number & 0xff;
    pat[patpos++] = 0xe0 | ((DSS_PMTPID >> 8) & 0x1f);
    pat[patpos++] = DSS_PMTPID & 0xff;
  }
  pat[3] = (patpos + 4 - 3 - 1);
  *(uint32_t *)(pat+patpos)= htonl(crc32_04c11db7(pat+1, patpos-1, 0xffffffff));
  patpos+=4;
  pos = write_ts_header(0x00, 1, patcount, -1, buf, 0);
  memcpy(buf+pos, pat, patpos);
  pos += patpos;
  memset(buf+pos, 0xff, TS_SIZE - pos);
  memcpy(*out,  buf, TS_SIZE);
  *out += TS_SIZE;
  //PMT
  for(i = 0; i < chan_cnt; i++) {
    pos = 0;
    pmtpos = 13;
    pos += write_ts_header(DSS_PMTPID, 1, pmtcount, -1, buf+pos, 0);
    for(j = 0; j < mpg->cir[i].number_of_scids; j++) {
      uint8_t type;
      uint32_t pid = mpg->cir[i].scid[j];
      uint8_t lang[3] = {0, 0, 0};
      switch(mpg->cir[i].scid_type[j]) {
        case 0x08:
        case 0x0f:
          type = 0x02;
          break;
        case 0x09:
          type = 0x81;
          break;
        case 0x00:
          type = 0x04;
          lang[0] = 'e'; lang[1] = 'n'; lang[2] = 'g';
          break;
        case 0x01:
          type = 0x04;
          lang[0] = 's'; lang[1] = 'p'; lang[2] = 'a';
          break;
        case 0x02:
          type = 0x04;
          lang[0] = 'f'; lang[1] = 'r'; lang[2] = 'e';
          break;
        case 0x03:
          type = 0x04;
          lang[0] = 'd'; lang[1] = 'e'; lang[2] = 'u';
          break;
        case 0x04:
          type = 0x04;
          lang[0] = 'i'; lang[1] = 't'; lang[2] = 'a';
          break;
        case 0x05:
          type = 0x04;
          lang[0] = 'j'; lang[1] = 'p'; lang[2] = 'n';
          break;
        case 0x06:
          type = 0x04;
          lang[0] = 'k'; lang[1] = 'o'; lang[2] = 'r';
          break;
        case 0x07:
          type = 0x04;
          lang[0] = 'z'; lang[1] = 'h'; lang[2] = 'o';
          break;
      }
      pmt[pmtpos++] = type;
      pmt[pmtpos++] = 0xe0 | (0xff & (pid >> 8));
      pmt[pmtpos++] = 0xff & pid;
      pmt[pmtpos++] = 0xf0;
      if(lang[0]) {
        pmt[pmtpos++] = 0x06;
        pmt[pmtpos++] = 0x0a;
        pmt[pmtpos++] = 0x04;
        pmt[pmtpos++] = lang[0];
        pmt[pmtpos++] = lang[1];
        pmt[pmtpos++] = lang[2];
        pmt[pmtpos++] = 0x00;
      } else {
        pmt[pmtpos++] = 0x00;
      }
    }
    pmt[3] = pmtpos + 4/*crc*/ - 3 - 1/*pointer_field*/;
    pmt[4] = (mpg->cir[i].virt_channel_number >> 8) & 0xff;
    pmt[5] = mpg->cir[i].virt_channel_number & 0xff;
    pmt[9] = 0xf0 | (0xff & (mpg->cir[i].scid[0] >> 8));
    pmt[10] = 0xff & mpg->cir[i].scid[0];
    *(uint32_t *)&pmt[pmtpos] = htonl(crc32_04c11db7(&pmt[1], pmtpos -1,
                                      0xffffffff));
    pmtpos+=4;
    memcpy(buf+pos, pmt, pmtpos);
    pos += pmtpos;
    memset(buf+pos, 0xff, TS_SIZE - pos);
    memcpy(*out,  buf, TS_SIZE);
    *out += TS_SIZE;
    pmtcount = (pmtcount+1) & 0x0f;
  }
  //NIT
  pos = 0;
  nit[4] |= (DSS_NETWORKID >> 8) & 0xff;
  nit[5] |= DSS_NETWORKID & 0xff;
  *(uint32_t *)&nit[13] = htonl(crc32_04c11db7(&nit[1], 12, 0xffffffff));
  pos += write_ts_header(DSS_NITPID, 1, nitcount, -1, *out, 0);
  memcpy(*out + pos, nit, 17);
  pos+=17;
  memset(*out+pos, 0xff, TS_SIZE - pos);
  *out += TS_SIZE;
  nitcount = (nitcount+1) & 0x0f;
}

void parse_mpg(uint8_t **out, struct mpg *mpg, uint8_t *buf)
{
  uint8_t seg_sync[4] = {0xaa, 0x55, 0xa5, 0xa5};
  uint32_t *segsync = (uint32_t *)seg_sync;
  uint8_t cssm_sync[4] = {0x55, 0xaa, 0x5a, 0x5a};
  uint32_t *cssmsync = (uint32_t *)cssm_sync;
  struct segment_list *segl = &mpg->segl;
  struct cssm         *cssm = &mpg->cssm;
  uint8_t *p, *heap;
  int i, offset;

  if(segl->number_of_segments == 0) {
    if (*(uint32_t *)buf == *segsync && (buf[4] & 0xf0) == 0) {
      segl->number_of_segments = buf[19];
      segl->up_scid            = (buf[53] << 8 | buf[54]) & 0xfff;
      segl->pip_scid           = (buf[55] << 8 | buf[56]) & 0xfff;
      segl->dip_scid           = (buf[57] << 8 | buf[58]) & 0xfff;
      segl->default_network_id = buf[97];
      segl->provider_id        = buf[107];
      segl->checksum           = buf[125] << 8 | buf[126];
      segl->number_of_segments--;
      mpg->num_channels = 0;
    }
    return;
  }
  if(cssm->segment_size == 0 && *(uint32_t *)buf == *cssmsync) {
    cssm->number_of_channels = buf[5];
    cssm->segment_size       = buf[6] << 8 | buf[7];
    cssm->buf_len = 0;
    dprintf2("Searching for %d channels in %d bytes (ns:%d)\n",
           cssm->number_of_channels, cssm->segment_size,segl->number_of_segments);
  }
  if(cssm->segment_size) {
    memcpy(cssm->buf + cssm->buf_len, buf, 127);
    cssm->buf_len += 127;
    if(cssm->segment_size > 127) {
      cssm->segment_size -= 127;
      return;
    }
    p = &cssm->buf[8];
    offset = segl->provider_id ? 1 : 0;
    heap = &p[(17 + 4 * offset) * cssm->number_of_channels];
    mpg->num_channels += cssm->number_of_channels;
    if(mpg->cir_len < mpg->num_channels) {
      dprintf1("Reallocating CIR: %d < %d\n", mpg->cir_len, mpg->num_channels);
      mpg->cir = (struct cir *)realloc(mpg->cir,
                                       mpg->num_channels * sizeof(struct cir));
      mpg->cir_len = mpg->num_channels;
    }
    for(i = cssm->number_of_channels; i > 0; i--) {
      process_cir(&mpg->cir[mpg->num_channels-i], p, segl->default_network_id,
                  segl->provider_id, heap);
      p += 17 + 4*offset;
    }
    cssm->segment_size = 0;
  }
  if(--segl->number_of_segments == 0)
    write_chaninfo(out, mpg);
}

void write_audio_pack(uint8_t **out, uint8_t *pack, struct dss_pkt *pkt)
{
  pkt->audio_len = (pack[4] << 8) | pack[5];
  assert(pkt->audio_len);
  pkt->hdr.bundle_boundary = 1;
  if(pack[3] == 0xbf) {
    //This is already a PES stream
    write_ts(out, pack, 11, pkt);
  } else {
    //Need to convert MPEG1 pkt to MPEG2-PES
    //for our purpose the only difference is that the PES is 3 bytes longer
    int i, len;
    for(i = 10; i >= 6; i--)
      pack[i+3] = pack[i];
    len = pkt->audio_len + 3;
    pack[4] = (len >> 8) & 0xff;
    pack[5] = len & 0xff;
    pack[6] = 0x80;
    pack[7] = 0x80;
    pack[8] = 0x05;
    write_ts(out, pack, 14, pkt);
  }
  pkt->audio_len -=5;
  pkt->audio_packlen = 0;
}

//Try to find a start code, since audio frames aren't aligned to DSS
//boundaries
void find_audio(uint8_t **out, uint8_t *buf, int buflen, struct dss_pkt *pkt)
{
  int len;
  int ok = 0;
  if(pkt->audio_packlen) {
    memcpy(pkt->audio_pack + pkt->audio_packlen, buf, 11 - pkt->audio_packlen);
    buf += 11 - pkt->audio_packlen;
    buflen -= 11 - pkt->audio_packlen;
    write_audio_pack(out, pkt->audio_pack, pkt);
  }
  len = (pkt->audio_len < buflen) ? pkt->audio_len : buflen;
  if(len) {
    write_ts(out, buf, len, pkt);
    pkt->audio_len -= len;
    ok = 1;
  }
  if(len == buflen || pkt->audio_len)
    return;
  if(pkt->audio_tmp_len) {
    //we know there are 4 bytes of header in front of buf, so let's overwrite
    buf = buf - pkt->audio_tmp_len;
    buflen += pkt->audio_tmp_len;
    memcpy(buf, pkt->audio_tmp, pkt->audio_tmp_len);
    pkt->audio_tmp_len = 0;
  }
  while(len + 4 <= buflen) {
    if(buf[len] == 0x00  && buf[len+1] == 0x00 && buf[len+2] == 0x01 &&
       ((buf[len+3] & 0xe0) == 0xc0 || buf[len+3] == 0xbf)) {
      int packlen = (11 < buflen - len) ? 11 : buflen - len;
      memcpy(pkt->audio_pack, buf + len, packlen);
      pkt->audio_packlen = packlen;
      if(packlen == 11) {
        write_audio_pack(out, pkt->audio_pack, pkt);
        len += 11;
        if(len < buflen) {
          write_ts(out, buf + len, buflen - len, pkt);
          pkt->audio_len -= buflen - len;
        }
      }
      return;
    }
    assert(ok != 1);
    len++;
  }
  if (len == buflen)
    return;
  pkt->audio_tmp_len = buflen - len;
  memcpy(pkt->audio_tmp, buf + len, pkt->audio_tmp_len);
  return;
}

void parse_dss(struct dss *dss, uint8_t *data, int len) {
  int pos;
  uint8_t *buf, *nextbuf, *out = dss->buf + dss->buf_used;
  struct dss_pkt *pktptr;
  if(dss->unused_len) {
    memcpy(dss->unused + dss->unused_len, data, 131 - dss->unused_len);
    nextbuf = dss->unused;
    pos = -dss->unused_len;
  } else {
    nextbuf = data;
    pos = 0;
  }
  while (pos <= len - 131) {
    pos+= 131;
    buf = nextbuf;
    if(dss->unused_len) {
      nextbuf = data + (131 - dss->unused_len);
      dss->unused_len = 0;
    } else {
      nextbuf = buf + 131;
    }
    if((pktptr = parse_dss_hdr(buf, dss)) == NULL)
      continue;
    if(pktptr->hdr.control_flag == 0) {
      //can't handle encrypted streams
      continue;
    }
    if(pktptr->hdr.header_designator_type == 0) {
      //Check the aux packet to see if there's an RTS in there
      //We're currently using the DTS for the SCR, so no need to parse
      //AUX packets at the moment
      //parse_aux(buf+4, pktptr);
      continue;
    }
    if(pktptr->hdr.scid == 0x009) {
        //do pat/pmt stuff here
        parse_mpg(&out, &dss->mpg, buf+4);
        continue;
    }
    if(pktptr->hdr.bundle_boundary) {
      //We should only get here for PES or ES data packets
      pktptr->is_video = 1;
      if(buf[7] >= 0xb9) {
        //This is a PES packet nothing else to do
        write_ts(&out, buf+4, 127, pktptr);
      } else {
        //This is an ES frame, and we need to add a PES to it
        uint8_t peshdr[TS_SIZE];
        int peslen;
        peslen = make_pes(peshdr, buf+4, pktptr);
        write_ts(&out, peshdr, peslen, pktptr);
        write_ts(&out, buf+4, 127, pktptr);
      }
    } else {
      //This is a continuation of the previous frame
      //Do we need to find the end 1st?
      if(! pktptr->is_video) {
        find_audio(&out, buf+4, 127, pktptr);
      } else {
        write_ts(&out, buf+4, 127, pktptr);
      }
    }
  }
  dss->buf_used = out - dss->buf;
  if(pos < len) {
    dprintf3("Storing %d bytes %02x %02x %02x...\n", len - pos, data[pos], data[pos+1], data[pos+2]);
    memcpy(dss->unused, data + pos, len - pos);
    dss->unused_len = len - pos;
  }
}

static uint32_t get_transponder(uint32_t freq)
{
#define DSS_LOF 11250000L
  int i;
  static uint32_t freqmap[] = {
    12224, 12239, 12253, 12268, 12282, 12297, 12311, 12326, 12341, 12355,
    12370, 12381, 12399, 12414, 12428, 12443, 12457, 12472, 12486, 12501,
    12516, 12530, 12545, 12559, 12574, 12588, 12603, 12618, 12632, 12647,
    12661, 12676};
  freq = (freq + DSS_LOF) / 1000L;
  for(i = 0; i < 32; i++) {
    if(freq >= freqmap[i] - 2 && freq <= freqmap[i] + 2) {
      dprintf0("Tuned to transponder: %d\n", i);
      return i;
    }
  }
  return 32;
}
static struct dss *find_dss_from_pc(struct parser_cmds *pc)
{
  struct list_head *ptr;
  list_for_each(ptr, &dsslist) {
    struct dss *dss = list_entry(ptr, struct dss);
    if(dss->adapt == pc->common->virt_adapt);
      return dss;
  }
  return NULL;
}
static void fe_tune(struct parser_cmds *pc, struct poll_ll *fdptr,
                    cmdret_t *result, int *ret,
                    unsigned long int cmd, unsigned char *data)
{
  int is_dss = 0;
  uint32_t freq;
  struct dvb_frontend_parameters_new fep2;
  struct dss *dss = find_dss_from_pc(pc);
  
  if(cmd == FE_SET_FRONTEND) {
    dprintf1("Tuning frontend\n");
    struct dvb_frontend_parameters *fep =(struct dvb_frontend_parameters *)data;
    is_dss = (fep->u.qpsk.symbol_rate == DSS_SYMBOLRATE);
    freq = fep->frequency;
  } else if(cmd == FE_SET_FRONTEND2) {
    dprintf1("Tuning frontend\n");
    struct dvb_frontend_parameters_new *fep =
          (struct dvb_frontend_parameters_new *)data;
    if(dss->old_standard == FE_DVB_S || dss->old_standard == FE_QPSK)
      is_dss = (fep->u.qpsk.symbol_rate == DSS_SYMBOLRATE);
    else if(dss->old_standard == FE_DVB_S2)
      is_dss = (fep->u.qpsk2.symbol_rate == DSS_SYMBOLRATE);
    else
      return;
    freq = fep->frequency;
  } else if(cmd == FE_SET_STANDARD) {
    dprintf1("Setting standard to %u\n", (uint32_t)data);
    dss->old_standard = (*(unsigned long *)data) & 0xffffffff;
    return;
  }
  if(! is_dss) {
    dprintf1("Got non DSS frontend message\n");
//    if(dss->is_dss)
//      ioctl(fdptr->fd, FE_SET_STANDARD, dss->old_standard);
    return;
  }
  pthread_mutex_lock(&dss->lock);
  if(ioctl(fdptr->fd, FE_SET_STANDARD, FE_DSS))
    return;
  dss->mpg.current_transponder = get_transponder(freq);
  dss->is_dss = 1;
  dss->buf_used = 0;
  dss->unused_len = 0;
  fep2.frequency = freq;
  fep2.inversion = INVERSION_AUTO;
  fep2.u.qpsk2.symbol_rate = 20000000;
  fep2.u.qpsk2.fec_inner = FEC_5_6;
  fep2.u.qpsk2.modulation = MOD_DSS_QPSK;
  usleep(200000);
  *ret = ioctl(fdptr->fd, FE_SET_FRONTEND2, &fep2);
  usleep(200000);
  pthread_mutex_unlock(&dss->lock);
  *result = CMD_STOPALL;
  dprintf0("Switched to DSS mode\n");
  return;
}

static void fe_close(struct parser_cmds *pc, struct poll_ll *fdptr,
                     cmdret_t *result, int *ret,
                     unsigned long int cmd, unsigned char *data)
{
  struct dss *dss = find_dss_from_pc(pc);
  dss->is_dss = 0;
  pthread_mutex_lock(&dss->lock);
  while(! list_empty(&dss->pkt_ll)) {
    struct dss_pkt * pkt = list_entry(dss->pkt_ll.next, struct dss_pkt);
    list_del(&pkt->list);
    list_add(&pkt->list, &dsspkt_empty_queue);
  }
  pthread_mutex_unlock(&dss->lock);
}

static void close_demux(struct parser_cmds *pc, struct poll_ll *fdptr,
                        cmdret_t *result, int *ret,
                        unsigned long int cmd, unsigned char *data)
{
  struct dss_pkt *pkt;
  struct dss *dss = find_dss_from_pc(pc);
  pthread_mutex_lock(&dss->lock);
  ll_find_elem(pkt, dss->pkt_ll, fd, fdptr->fd, struct dss_pkt);
  if(pkt) {
    list_del(&pkt->list);
    list_add(&pkt->list, &dsspkt_empty_queue);
  }
  pthread_mutex_unlock(&dss->lock);
}
static void set_demux(struct parser_cmds *pc, struct poll_ll *fdptr,
                      cmdret_t *result, int *ret,
                      unsigned long int cmd, unsigned char *data)
{
  struct dss_pkt *pkt;
  struct dmx_pes_filter_params *dmx;
  struct dss *dss= find_dss_from_pc(pc);
  if(! dss->is_dss) {
    dprintf2("Ignoring demux ioctl\n");
    return;
  }
  if(cmd == DMX_SET_PES_FILTER) {
    dmx = (struct dmx_pes_filter_params *)data;
    if(dmx->output != DMX_OUT_TS_TAP) {
      dprintf0("Tried to open DMX in non TAP mode\n");
      *ret = 1;
      *result = CMD_STOPALL;
      return;
    }
    if(dmx->pid == 0x00) {
      //PAT requested let's open 009
      ll_find_elem(pkt, dss->pkt_ll, hdr.scid, 0x009, struct dss_pkt);
      if(pkt) {
        *ret = 0;
        *result = CMD_STOPALL;
        return;
      }
      dmx->pid = 0x009;
    } else if(dmx->pid == DSS_PMTPID) {
      *ret = 0;
      *result = CMD_STOPALL;
      return;
    }
    pthread_mutex_lock(&dss->lock);
    pop_entry_from_queue(pkt, &dsspkt_empty_queue, struct dss_pkt);
    bzero(pkt, sizeof( struct dss_pkt));
    pkt->hdr.scid = dmx->pid;
    pkt->fd = fdptr->fd;
    pkt->rts = -1;
    list_add(&pkt->list, &dss->pkt_ll);
    *ret = ioctl(fdptr->fd, cmd, dmx);
    pthread_mutex_unlock(&dss->lock);
    *result = CMD_STOPALL;
    dprintf0("Set demux ioctl on pid %d\n", pkt->hdr.scid);
  }
}

static void dvr_open(struct parser_cmds *pc, struct poll_ll *fdptr,
                     cmdret_t *result, int *ret,
                     unsigned long int cmd, unsigned char *data)
{
  struct dss *dss= find_dss_from_pc(pc);
  dss->buf_used = 0;
  dss->unused_len = 0;
}
static void dvr_preread(struct parser_cmds *pc, struct poll_ll *fdptr,
                     cmdret_t *result, int *ret,
                     unsigned long int cmd, unsigned char *data)
{
  struct dss *dss= find_dss_from_pc(pc);
  struct dvblb_custommsg *ci = (struct dvblb_custommsg *)data;
  static int fd = -1;
  int bytes;

  if(! dss->is_dss)
    return;
  if(fd == -1) {
    fd = open(opt_file, O_RDONLY);
  }
  bytes = read(fd, pc->mmap, ci->u.count);
  if(bytes == 0)
    lseek(fd, 0, SEEK_SET);
  ci->u.count = bytes;
  *ret = bytes;
  *result = CMD_SKIPCALL;
}
//NOTE: This is very broken.  We don't return the number of bytes requested ever
static void dvr_read(struct parser_cmds *pc, struct poll_ll *fdptr,
                     cmdret_t *result, int *ret,
                     unsigned long int cmd, unsigned char *data)
{
  struct dvblb_custommsg *ci = (struct dvblb_custommsg *)data;
  struct dss *dss= find_dss_from_pc(pc);
  if(! dss->is_dss)
    return;
  if(dss->buf_len < ci->u.count + dss->buf_used) {
    dprintf2("Reallocating %d < %d\n", dss->buf_len + dss->buf_used, ci->u.count);
    dss->buf = (uint8_t *)realloc(dss->buf, dss->buf_used + ci->u.count);
    dss->buf_len = ci->u.count;
  }
  if(*ret > 0) {
    dprintf3("Reading %d bytes(%08x): %02x %02x %02x %02x ...\n", *ret, pc->mmap, pc->mmap[0], pc->mmap[1], pc->mmap[2], pc->mmap[3]);
    parse_dss(dss, pc->mmap, *ret);
    if(dss->buf_used) {
      int max = ((int)dss->buf_used < *ret) ? dss->buf_used : *ret;
      memcpy(pc->mmap, dss->buf, max);
      *ret = max;
      memmove(dss->buf, dss->buf + max, dss->buf_used - max);
      dss->buf_used -= max;
    } else {
      write_dummy_ts(pc->mmap);
      if(*ret < TS_SIZE) {
        memcpy(dss->buf, pc->mmap + *ret, TS_SIZE - *ret);
        dss->buf_used = TS_SIZE - *ret;
      } else {
        *ret = TS_SIZE;
      }
    }
    dprintf3("Writing %d bytes(%08x): %02x %02x %02x %02x ...\n", *ret, pc->mmap, pc->mmap[0], pc->mmap[1], pc->mmap[2], pc->mmap[3]);
  }
}

static void connect_dss(struct parser_adpt *pc_all)
{
  struct cmd_list *fe_preioctl = register_cmd(fe_tune);
  struct cmd_list *demux_preioctl =  register_cmd(set_demux);
  struct cmd_list *demux_postclose = register_cmd(close_demux);
  struct cmd_list *fe_postclose = register_cmd(fe_close);
  struct cmd_list *dvr_postread = register_cmd(dvr_read);
  struct cmd_list *dvr_preread1 = register_cmd(dvr_preread);
  struct cmd_list *dvr_postopen = register_cmd(dvr_open);
  struct dss *dss;

  if(! opt_enable)
    return;
  dprintf0("Enabling DSS for adapter %d\n", pc_all->frontend->common->virt_adapt);
  dss = (struct dss *)malloc(sizeof(struct dss));
  bzero(dss, sizeof(struct dss));
  INIT_LIST_HEAD(&dss->pkt_ll);
  dss->buf = (uint8_t *)malloc(2000000);
  dss->buf_len = 2000000;
  dss->adapt = pc_all->frontend->common->virt_adapt;
  pthread_mutex_init(&dss->lock, NULL);
  list_add(&fe_preioctl->list, &pc_all->frontend->pre_ioctl);
  list_add(&fe_postclose->list, &pc_all->frontend->post_close);
  list_add(&demux_preioctl->list, &pc_all->demux->pre_ioctl);
  list_add(&demux_postclose->list, &pc_all->demux->post_close);
  list_add(&dvr_postread->list, &pc_all->dvr->post_read);
  list_add(&dvr_postopen->list, &pc_all->dvr->post_open);
  if(opt_file != NULL)
    list_add(&dvr_preread1->list, &pc_all->dvr->pre_read);
  list_add_tail(&dss->list, &dsslist);
}

static struct option *parseopt_dss(arg_enum_t cmd)
{
  if(cmd == ARG_INIT) {
    return dss_Opts;
  } 
  if(cmd == ARG_HELP) {
    printf("   --dss-file <file> : Read DSS stream from <file> instead of adapter\n");
    printf("   --dss-enable      : Enable DSS to DVB mapping\n");
  } 
  if(! dss_opt)
    return NULL;

  switch(dss_opt) {
    case 'e':
      opt_enable = 1;
      break;
    case 'f':
      opt_file = optarg;
      break;
  }
  dss_opt = 0;
  return NULL;
}

//list, plugin_id, name, parse_args, connect, launch, message, send_msg
static struct plugin_cmd plugin_cmds = {{NULL, NULL}, PLUGIN_ID, "dss",
                parseopt_dss, connect_dss, NULL, NULL, NULL, NULL, NULL};
int __attribute__((constructor)) __dss_init(void)
{
  list_add(&plugin_cmds.list, &plugin_cmdlist);
  return 0;
}
#endif
