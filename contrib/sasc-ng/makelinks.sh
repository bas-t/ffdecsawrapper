#!/bin/bash

SRC="$1"
DEST="$2"
PAR="../.."
SLEN=`echo -n "$SRC" | wc -c`
SLEN=`expr $SLEN + 2`


mkdir -p "$DEST"

(cd "$PAR"; find "$SRC" -name contrib -prune -o -name .hg -prune -o -type f -regex ".*\(Makefile.*\|\.\(h\|c\|mk\|po\|pl\)\)") |
 while read i; do
   base=`echo -n "$i" | cut -c $SLEN-999`
   dir=`dirname "$base"`
   mkdir -p "$DEST/$dir"
   COR=`echo -n "$DEST/$dir" | sed -e 'sX/\.XX' | sed -e 'sX[^/]*X..Xg'`
   if [ "$base" != "contrib" ]; then
     ln -sf "$PAR/$COR/$SRC/$base" "$DEST/$base"
   fi
 done
