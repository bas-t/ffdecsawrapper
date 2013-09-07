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

#ifndef __LOG_IRDETO_H
#define __LOG_IRDETO_H

#include "log-sys.h"

#define L_SYS        5
#define L_SYS_RAWEMM   LCLASS(L_SYS,L_SYS_LASTDEF<<1)
#define L_SYS_RAWECM   LCLASS(L_SYS,L_SYS_LASTDEF<<2)
#define L_SYS_ALL    LALL(L_SYS_RAWECM)

#endif
