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

#ifndef __IRDETO_H
#define __IRDETO_H

#define SYSTEM_IRDETO        0x0600
#define SYSTEM_BETA          0x1700

#define SYSTEM_NAME          "Irdeto"
#define SYSTEM_PRI           -10
#define SYSTEM_NAME2         "Irdeto2"
#define SYSTEM_PRI2          -8

#define TYPE_I1   0
#define TYPE_OP   1
#define TYPE_IV   2
#define TYPE_SEED 3
#define TYPE_PMK  4

#define PROV(keynr)         (((keynr)>>16)&0xFF)
#define TYPE(keynr)         (((keynr)>> 8)&0xFF)
#define ID(keynr)           (((keynr)   )&0xFF)
#define KEYSET(prov,typ,id) ((((prov)&0xFF)<<16)|(((typ)&0xFF)<<8)|((id)&0xFF))

#endif
