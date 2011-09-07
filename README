This is a "plugin" for the Video Disk Recorder (VDR).

See the file COPYING for license information.

Description: SoftCAM for Irdeto, Seca, Viaccess, Nagra, Conax & Cryptoworks

-----------------------------------------------------------------------



What is it ?
------------

First: Most certainly it's not legal to use this software in most countries of
the world. But probably you already know this...

SC means softcam, which means a software CAM emulation.

The plugin captures the DVB devices in an early startup stage of VDR startup
(before VDR itself has got access to them) and makes VDR believe that there is
another CAM connected to the device. This CAM is emulated from the plugin by
providing a cut-down EN50221 interface for communication with the VDR core. From
VDR views there is no difference between a real hardware CAM and the emulated
CAM. 

The plugin decrypts the scrambling codewords from the incomming ECM stream. The
actual descrambling of the video stream is either done by the ECD chip on
full-featured DVB cards or with the included FFdecsa implementation on budget
cards.

This piece of software is originaly based on (and still contains code from)
mgcam (a standalone CAM emulation). Many thanks to the (anonymous) author for
his really fine piece of software :-)



Requirements
------------

* DVB driver from dvb-kernel 2.6 or 2.4 branch with applied patches
* a patched firmware version 2620 or newer
* VDR 1.6.0 or newer
* Openssl package version 0.9.7 or newer



How to setup ?
--------------

First you should start with a recent dvb-kernel driver (cvs recomended). Copy
the patched firmware in place and apply at least the dvb-cwidx patch. Make sure
that you use a patched firmware if you intend to use the plugin together with a
full-featured DVB card. You definitely need a patched firmware in this case, but
only recent versions support concurrent recording! Recompile the driver, unload
the modules, install the new ones and reload the DVB driver. If you suffer from
ARM crashes, add "hw_sections=0" while loading the dvb-ttpci module.

Contrary to older plugin versions (before 0.7.0) you MUST NOT apply patches to
the VDR core (neither vdr-sc nor ffdecsa/softcsa) except if:
- you are using VDR 1.7.8 or lower and want to view channels with split ECM

To correctly decode channels with split ECM (i.e. audio and video encrypted
with different CW) you need to apply a patch to the VDR core if you are using
a VDR version before 1.7.9. You can find the "vdr-1.6.0-2-streamca.diff" file
in the patches subdirectory. It has been tested with VDR 1.6.0-2 only, but
probably will apply to other VDR versions as well.

You must have installed the openssl development files. For most distributions
this means to install openssl-devel package. You should use a openssl package
with AES and IDEA enabled, as support for openssl without these will be removed
in the near future.

**** VDR 1.7.11+ notes ****
If you want to use VDR 1.7.11+ together with a full-featured DVB card:
- the source code for the dvbhddevice and/or dvbsddevice has to be available in
  the VDR plugin source directory, i.e. at the parent directory level of this
  plugin source. The presence of these plugin sources will be detected
  automatically and support for them will be enabled at compile time.
- the compiled dvbhddevice and/or dvbsddevice plugin libraries (i.e. the *.so
  files) have to be available in the VDR plugin library directory at runtime.
- The dvbhddevice and/or dvbsddevice are loaded internaly from this pluign. It's
  not necessary to load them on the VDR commandline (i.e. with parameter -P). If
  however a device plugin implements additional methods (like a plugin setup
  menu) it might be necessary to additionaly load it from VDR commandline AFTER
  this plugin to enable the functionality.

Now follow the VDR instruction to compile plugins (make plugins). Beside the
core plugin (libvdr-sc.so), the make process (if successfull) creates an
additional shared library object for every supported system (libsc-*.so). You
can enable/disable individual systems by adding or removing the shared library
from your VDR plugin lib directory.

There are some make options to adapt FFdecsa to your system, which can be
given on the make command line or added to Make.config. Please refer to
README.FFdecsa for it.

Note that in combination with other plugins which create devices (e.g.
softdevice) it's essential that this plugin is loaded before any of these
plugins, i.e. as a rule of thumb put this plugins first on the VDR commandline.
The plugin will fail on startup if the plugin load order results in mismatched
device numbering inside VDR.

Note that some budget card drivers provide a CA device too. This might make VDR
and the plugin detect the card as a full-featured card, thus disabling FFdecsa.
You should use commandline option -B to force detection as a budget card in such
a case. This commandline option takes a device number as argument and may appear
multiple times (compare VDR commandline option -D).
If you have a full-featured card with Full-TS hardware modification, you need
specify option -B for this card to correctly activate hybrid mode.

By default the plugin logs his messages to console only. For testing purpose
it's a good idea to start VDR in foreground so you can see any error messages.
Other available log targets are file and syslog. You can enable/disable any of
the log targets in the plugin setup menu. For the file target you have to set a
destination filename too. If you set a filesize limit (in KByte) and the logfile
grows bigger than the limit, the current logfile is renamed to logfile.old and a
new logfile is started. If the filesize limit is zero, the logfile never is
rotated.



Pre-compiled libraries
----------------------


There is the possibility that encryption systems are provided in binary, pre-
compiled only form. During make process, all pre-compiled libraries are copied
to your VDR plugin lib directory.

Please be aware, that pre-compiled libraries are more or less bound to the hard-
& software configuration they have been build on. Currently the build system is
Intel 32bit, gcc 4.3.3, glibc 2.9. If your system differs too much, it may be
impossible to use the pre-compiled libraries.

Obviously, pre-compiled libraries cannot be exchanged between different SC
and/or VDR API versions. Be aware that if you patch your VDR core and this patch
involves changes to header files (*.h) this might change the VDR API even if the
API version number hasn't changed. This may lead to silent malfunction/failure
of pre-compiled libraries. In particular you should stay away from thread.h and
tools.h as classes from there are used at many, many places.

The naming scheme for the libraries is libsc-<MODULE>-<SCAPI>.so.<APIVERSION>,
e.g. libsc-cardclient-2.so.1.3.47



CAID and VDR CICAM setup
------------------------

The activation of the SC is controlled by your CICAM setup. As general setup
(which is not SC specific) you should leave the CA values (in channels.conf)
set to zero and let VDR's channel scanner (autopid) fill in the correct values.
Don't touch the CA values afterwards.
In the plugin setup menu, you now have to specify for which DVB cards the SC
should be activated. The first two cards can be setup from the menu. If you
need more, you can edit the setup.conf file manualy and add up to 10 cards.

A real hardware CAM normaly knows which CAIDs it supports. With SC the situation
is a bit different. There is support for a wide range of encryption system and
cardclients. This results in a huge number of supported CAIDs, but for most of
them it's uncertain if SC will actualy be able to decrypt a channel for them. On
the other hand VDR limits the number of supported CAIDs to 16 for a CAM slot (64
CAIDs in VDR 1.5.3 or later), so SC is able to announce a small number of CAIDs
only. This is not as bad as it sounds, as VDR will try a CAM if ANY of the
channel CAIDs matches the CAIDs announced by the CAM.
On startup and at regular intervals the plugin scans the channels list and
builds a chain of CAIDs. The CAIDs are assigned to the simulated CAM. 

To reduce the number of CAIDs SC has to deal with, you should obey some rules:
-Remove all libsc-* files for encryption system which you don't intend to use
 (e.g. SHL seems pretty useless nowadays).
-When using a cardclient, be as precise as possible with the CAID/MASK values in
 the cardclient.conf file. Using wide open 0000/0000 is not recommended.
-Add CAIDs which you cannot use due to lack of keys to the ignore setup in
 override.conf.




Concurrent Recordings
---------------------

There is an entries in the plugin setup menu to control concurrent usage of a
full-featured DVB card. You should enable concurrent usage only if you are using
the special patched firmware AND a patched DVB driver. Note that toggling the
flag will take effect the next time the plugin is idle on that specific DVB card
only (i.e. no channel is being decrypted).

There is no possibility to limit the number of concurrent streams. VDR itself
has no limit in concurrent streams (neither FTA nor encrypted) and as the VDR
core control all aspects of operation, there is no way to enforce a limit
(beside disabling concurrent encrypted streams at all).



Additional files
----------------

All config files are expected to be located in the subdirectory "sc" of VDRs
plugin config directory. The user executing VDR should have write permissions
for this directory, as the plugin will create cache files there.

The keyfile must be named "SoftCam.Key". Any updated keys are saved back to this
file. At this the structure of the file (e.g. comments) is preserved as far as
possible. Updated key are inserted near to the old one (there is a setup option
to selected if the old key is commented out or deleted), while new keys are
inserted close to the top of the file.

Certain parameters (normaly auto-configured from CAT/PMT) can be overriden by
entries in override.conf. See example file in the "examples" subdirectory for
format. Override configuration is needed if the provider fails to broadcast
correct data in CAT/PMT only.

For Irdeto, Seca and Viaccess AU you need valid subscription card data, which
have to be located in the files "Ird-Beta.KID", "Seca.KID" or "Viaccess.KID".
See the files in the "examples" subdirectory for file formats.

Note, that for this @SHL implementation the key must be in Z 00 00 <key> format
(the V 000000 00 <key> format doesn't work).

For Seca2 support you need binary files which contain the hash & mask tables.
The file format is the same as for Yankse. The files must be located in the
"seca" subdirectory. The name sheme is s2_TTTT_XXXX.bin where TTTT is
one of "hash","mt" and XXXX is the provider ID (e.g. s2_hash_0064.bin,
s2_mt_0070.bin). The hash file must be 1536 bytes long. The mt file is normaly
16384 bytes long, but this may differ for your provider. For advanced Seca2
providers you may need additional table files. At the moment these are
s2_sse.bin, s2_sse_XXXX.bin and s2_cw_XXXX.bin.

For Nagra1 AU you need appropriate binary Rom and Eeprom files. The files have
to be named "ROMX.bin", "ROMXext.bin" or "eepX_Z.bin", where X is the ROM number
(decimal) and Z is the upper part of the provider ID (hexadecimal). The Eeprom
files may be updated from the EMM data, take care that the permissions are set
right. The plugin searches for these files in the "nagra" subdirectory.

For Nagra2 AU some providers need binary Rom and Eeprom files. The files have to
be named "ROMxxx.bin" and "EEPyy_xxx.bin", where xxx is the ROM version (e.g.
102) and yy is the upper part of the provider ID (e.g. 09 for BEV). The files
must contain the joined contents of all Rom/Eeprom pages. The plugin searches
for these files in the "nagra" subdirectory.



External key updates
--------------------

If key updates are available from external sources (e.g. website) only, they may
be feed from a shell script. To enable this, you have to specify the script name
with commandline option "-E". The script will be called at a regular interval
(currently 15 minutes) or whenever a needed key is not available (but not more
often than every 2 minutes). The script has to output the keys to it's stdout in
the same format as for the key file. The script may output several keys in one
call (each key on a seperate line). You can find an example script in the
"examples" subdirectory.



Smartcard support
-----------------

For most encrpytion systems this plugin supports original subscription
smartcards (e.g. from a Phoenix/Smartmouse ISO interface connected to a serial
port).

The configuartion of the smartcard slots if done in the "cardslot.conf" file
(see example file for format).

Some smartcards need additional information to establish communication with the
card (e.g. certificate or box key for camcrypt). These information must be
available in the "smartcard.conf" file (see example file for format) or you card
won't work correctly.

If you insert a card into a interface the card is autodetected (your interface
should use the CD line to signal card presence or it won't work) and
initialised (this may take some seconds to complete). You can use the setup
menu to see which cards are currently inserted and detected. You can remove a
smartcard at any time without prior action, but of course this will disrupt
decryption if you are tuned to a channel which requires the card.



Cardserver client
-----------------

The cardclient is a client for several cardservers. Supported cardservers are :
CCcam, gbox, radegast, newcamd, camd33 (tcp), camd35 (udp), cardd, buffy and
aroureos and servers which are protocol compatible with the mentioned ones.

You can configure as many clients for different servers as you want. The client
configuration is read from the file "cardclient.conf". Every line in the file
defines a client-server connection. The line starts with the client name and is
followed by additional arguments which depend on the client type. See the file
"examples/cardclient.conf.example" for format and arguments.

The connections are tried in the order they are defined in the conf file until
a valid decryption is obtained. After that, the decryption sticks to that
particular connection until next channel switch.



Summary of commandline options
------------------------------

-B N    --budget=N        forces DVB device N to budget mode (using FFdecsa)
-E CMD  --external-au=CMD script for external key updates



SVDR interface
--------------

The plugin implements a SVDR interface. Supported commands are:
   RELOAD
     Reload all configuration files (only if the softcam isn't active at the
     moment).
     Return codes: 550 - Softcam active, can't reload files.
                   901 - Reloading files not entirely successfull. Most of the
                         time this will leave you with an unusable softcam.
                   900 - Reload successfull.

   KEY string
     Parse the given string and add the key to the key database (as if it was
     received from EMM stream).
     Return codes: 501 - Syntax error.
                   900 - Key added.
                   901 - Invalid key format or key already known.

   LOG <on|off> module.option[,module.option][,...]
     Enables or disables all given message classes.
     Return codes: 501 - Syntax error or unknown message class.
                   900 - Options set and saved.

   LOGCFG
     Display all available message classes and report their status. This can be
     usefull if you want to provide an external GUI or whatever to handle the
     message classes.
     Return codes: 901 - No message classes available.
                   900 - Message class status (multi line reply).

   LOGFILE <on|off> [filename]
     Enables or disables logging to file and optionaly sets the filename.",
     Return codes: 501 - Syntax error.
                   900 - Logfile option set and saved.
