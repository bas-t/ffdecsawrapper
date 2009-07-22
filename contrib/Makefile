#
# Softcam plugin to VDR
#
# This code is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
# Or, point your browser to http://www.gnu.org/copyleft/gpl.html

all: cccam_ca.so bsdiff bspatch
.PHONY: clean


cccam_ca.so: cccam_ca.c
	gcc -O -fbuiltin -fomit-frame-pointer -fPIC -shared -o $@ $< -ldl

bsdiff:		bsdiff.c
	gcc -O3 -o $@ $< -lbz2
bspatch:	bspatch.c
	gcc -O3 -o $@ $< -lbz2

clean:
	@-rm -f cccam_ca.so bsdiff bspatch
