**This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.**



**This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.**



**You should have received a copy of the GNU General Public License long with this program.  If not, see http://www.gnu.org/licenses/**



**The 'master'and 'stable' branches are based on a rev 620 (latest) checkout of the old vdr-sasc-ng plugin, the 'oldstable' branch is based on rev 570**



 **Due to 'Hollywood' legislation, the use of this software is illegal in most countrys. (but not in my country, hehe)**

 **The purpose of this git repo is to make FFdecsa (via FFdecsawrapper's loopback interface) available for use with MythTV and such.**

 **You can use it with kernel version 3.x and up, including more recent kernels. Best results with 3.14 and up (latest ok test: linux-3.18.1).**

 **2.x kernels are not supported anymore.**

 **If you have to compile and install newest dvb drivers from v4l (or TBS and other v4l-based out of kernel drivers), you should apply proper dvb-mutex patch to your sources prior to compiling. If you are not sure on how to do this:**

 - `Basic instructions for v4l are here. <http://www.lursen.org/wiki/V4l_and_ffdecsawrapper>`_

 **Note that you should not use v4l drivers if your DVB adapter is supported 'out of the box' by your kernel.**

 **If you are NOT using out-of-tree drivers and you are running Debian/Ubuntu/Mythbuntu with a clean unmodified stock kernel, you don't need to recompile it anymore. The running kernel will be properly patched during configuration of FFdecsawrapper. That is, if your sources.list is ok and the source for your kernel is available from the repo's. Any missing build-deps for FFdecsawrapper and/or patching the dvb-core.ko kernel module will also be 'automagically' installed.**

 **USAGE:**

 - `Basic instructions on how tu use this software (in combination with MythTV). <http://www.lursen.org/wiki/FFdecsawrapper_with_MythTV_and_Oscam_on_Debian/Ubuntu>`_

