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

#define SC_NAME "sc"
#define SC_MAGIC { 0,'S','C',0xc4,0x5e,0xa2 }

#define OP_PROVIDES 0
#define OP_ADDPID   1
#define OP_DELPID   2
#define OP_TUNE     3
#define OP_ALLOWED  4

struct ScLink {
  char magic[6];
  short op;
  const cDevice *dev;
  union {
    unsigned short *caids;
    struct {
      int pid;
      int type;
      } pids;
    struct {
      int source;
      int transponder;
      } tune;
    } data;
  int num;
  };

static const char magic[] = SC_MAGIC;

void PrepareScLink(struct ScLink *link, const cDevice *dev, int op)
{
  memcpy(link->magic,magic,sizeof(link->magic));
  link->op=op;
  link->dev=dev;
  link->num=-1;
}

int DoScLinkOp(cPlugin *p, struct ScLink *link)
{
  if(p) {
    p->SetupParse((const char *)link,"");
    return link->num;
    }
  return -1;
}

