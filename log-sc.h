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

#ifndef ___LOG_SC_H
#define ___LOG_SC_H

#include "log.h"

#define L_SC_ERROR    LCLASS(L_SC,0x2)
#define L_SC_INIT     LCLASS(L_SC,0x4)

#define L_SC_LASTDEF  L_SC_INIT
#define L_SC_DEFDEF   L_SC_ERROR|L_SC_INIT
#define L_SC_DEFNAMES "error","init"

#endif //___LOG_SC_H
