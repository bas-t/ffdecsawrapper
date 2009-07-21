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

#ifndef __NAGRA_DEBUG_H
#define __NAGRA_DEBUG_H

#define DEBUG_EMU      // debug CardEmu (very verbose!)
#define DEBUG_EMU_0x80 // if the above is enabled, limit output to range x080-0xc0
//#define DEBUG_STAT     // give some statistics on CardEmu
//#define DEBUG_MAP      // debug file mapping code
//#define DEBUG_NAGRA    // debug Nagra crypt code
//#define DEBUG_LOG      // debug Nagra logger code

#ifdef DEBUG_EMU
#define dee(x) { (x); }
#ifdef DEBUG_EMU_0x80
#define de(x) { if(pc80flag) { (x); } }
#else
#define de(x) { (x); }
#endif
#else
#define de(x) ;
#define dee(x) ;
#endif

#ifdef DEBUG_NAGRA
#define dn(a) { a; }
#else
#define dn(a) ;
#endif

#ifdef DEBUG_LOG
#define dl(x) { (x); }
#else
#define dl(x) ; 
#endif

#endif

