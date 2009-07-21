-------
FFdecsa
-------

This directory contains patches to use FFdecsa with vdr, by means of a
new FFdecsa-based SoftCSA.

You don't need a SoftCSA patch!!!

Step by step instructions:

- create a directory somewhere, we will call this dir $BASE

- download vdr-1.3.11.tar.bz2 and put it in $BASE

- download vdr-sc-0.3.15.tar.gz and put it in $BASE

- download FFdecsa-1.0.0.tar.bz2 and put it in $BASE

- cd $BASE

- tar xvjf vdr-1.3.11.tar.bz2

- cd vdr-1.3.11/PLUGINS/src/

- tar xvzf ../../../vdr-sc-0.3.15.tar.gz

- ln -s sc-0.3.15 sc

- cd $BASE/vdr-1.3.11

- tar xvjf ../FFdecsa-1.0.0.tar.bz2

- ln -s FFdecsa-1.0.0 FFdecsa

- patch -p1 <PLUGINS/src/sc-0.3.15/patches/vdr-1.3.10-sc.diff

- patch -p1 <FFdecsa/vdr_patches/vdr-1.3.11-FFdecsa.diff

- cd FFdecsa

- optional: edit Makefile

- make

- ./FFdecsa_test

- cd $BASE/vdr-1.3.11

- cp Make.config.template Make.config

- optional: edit Make.config

- make

- make plugins

Good luck!
