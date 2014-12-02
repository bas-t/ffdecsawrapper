/*
   DVBLoopback - plugin_dummy.c
   Copyright Alan Nisota 2006

   This file is part of DVBLoopback.

    DVBLoopback is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    DVBLoopback is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DVBLoopback; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include "list.h"

// Header Needed for Demuxer
#include <linux/dvb/dmx.h>

// Header Needed for Frontend
#include <linux/dvb/frontend.h>

// Define your plugin load order here
//
// Must be even, < 32, >= 8, and unique (total of 12 plugins allowed currently)
#define PLUGIN_ID 16
#include "debug.h"

// YOU MUST INCLUDE THIS
//
// Provides Structs for parser commands
#include "process_req.h"

// YOU MUST INCLUDE THIS
//
// Provides Structs for message passing
#include "msg_passing.h"


/* LongOpt Array 
 *
 * option constructor: String name, int has_arg, StringBuffer flag, int val
 *
 *      name    - The long option String. 
 *      has_arg - Indicates whether the option has no argument (NO_ARGUMENT) 0, a 
 *                required argument (REQUIRED_ARGUMENT) 1 or an optional argument 
 *                (OPTIONAL_ARGUMENT) 2. 
 *      flag    - If non-null, this is a location to store the value of "val" 
 *                when this option is encountered, otherwise "val" is treated 
 *                as the equivalent short option character. 
 *      val     - The value to return for this long option, or the equivalent 
 *                single letter option to emulate if flag is null.
 *
 * MUST BE TERMINATED WITH {0,0,0,0} (Null Option?)
 */

static int dummy_value = 0;
static struct option Dummy_Opts[] = {
  {"dummy-param", 2, &dummy_value, 'd'},
  {0, 0, 0, 0},
};

static void connect_dummy(struct parser_adpt *pc_all)
{
  printf("Connect Called\n");
}

static struct option *parseopt_dummy(arg_enum_t cmd)
{
  if(cmd == ARG_INIT) {
    return Dummy_Opts;
  } 
  if(cmd == ARG_HELP) {
    printf("   --dummy-param <adapter number> :\n");
  } 
  if(! dummy_value)
    return NULL;

  switch(dummy_value) {
    case 'd':
      if(optarg){
      	printf("Attaching to adapter%i\n", atoi(optarg));
      }
      break;
  }
  //must reset dummy_value after every call
  dummy_value = 0;
  return NULL;
}

/* struct plugin_cmd is used to pass information about this plugin to the main loop
   
   plugin_cmd struct is defined as:
  
   struct plugin_cmd {
     struct list_head list;
     int plugin;
     struct option * (*parse_args)(arg_enum_t);
     void (*connect)(struct parser_adpt *);
     void (*launch)();
     void (*message)(struct msg *, unsigned int priority);
     void (*send_msg)(struct parser_cmds *pc, int msg);
     void (*shutdown)();
   };

   'list' and 'plugin' MUST be initialized properly (see below) but all others
   may be set to 'NULL' when that feature is not required.
  
   ---
   struct list_head list;

   Any structure which is used as a list must have 'struct list_head list;' as
   the first element.  This will be managed by the list-handling functions, so
   just initialize it to '{NULL, NULL}'.  there should never be a need to work
   with the elements of a list, since the functions and macros do that for you.


   ---
   int plugin;

   'plugin' is the identifier for this plugin.  It should be unique compared to
   any other plugins used.  Other plugins can pass messages to this one by
   specifying this identifier
   
   ---
   struct option * (*parse_args)(arg_enum_t);

   function which deals with option parsing.  It should behave as follows:
     when arg_enum_t is 'ARG_INIT' returns a pointer to the parameter structure
     when arg_enum_t is 'ARG_HELP' displays a help message describing cmdline
          arguments
     in all other cases, check the cmdline variable and process arguments as
          needed

   ---

   void (*connect)(struct parser_adpt *);

   connect will be called once for each adapter that is being used. (So if 3
   cards are used, 'connect' will be called 3 times each time with a different
   parser_adpt parameter.
   All device-level initialization should be done here.

   parser_adpt is defined as:

   struct parser_adpt {
     struct parser_cmds *frontend;
     struct parser_cmds *demux;
     struct parser_cmds *dvr;
     struct parser_cmds *ca;
   };

   each element of parser_adpt supplies information about a given device

  struct parser_cmds {
    int type;
    pthread_t thread;
    struct common_data *common;
    struct list_head pre_open;
    struct list_head post_open;
    struct list_head pre_close;
    struct list_head post_close;
    struct list_head pre_read;
    struct list_head post_read;
    struct list_head pre_poll;
    struct list_head post_poll;
    struct list_head pre_ioctl;
    struct list_head post_ioctl;
    unsigned char *mmap;
    ...
  }

  type is the type of device defined in this structure.  It will be one of:
    DVB_DEVICE_FRONTEND, DVB_DEVICE_DEMUX, DVB_DEVICE_DVR, -1

  thread is the processing thread handling this device.  Plugins should not
    normally need to access this parameter

  common contains information common to all devices on the current adapter.
    This should primarily be used to determine the adapter number

  all of the pre/post lists contain commands to be executed before/after
  receiving a query for the given syscall.
  normally inside the 'connect' function a plugin would do something like:
    struct cmd_list *fe_postioctl = register_cmd(fe_tune);
    list_add_tail(&fe_postioctl->list, &pc_all->frontend->post_ioctl); 

    which indicates that the 'fe_tune' function inside the current plugin will
    be called after an ioctl has been processed (this could be used to post-
    process the output of an ioctl from the 'real' card)

  mmap conatins a pointer that is mmaped to the virtual device (but only for the
    dvr device).  It will contain the results of a 'read' in the case of
    a post_read callback, or can be filled in by the plugin during a pre_read
    call instead of allowing a read from the real card to happen.

  the parser_cmds struct contains additional elements, but these should be
    ignored by plugins

  ---
  void (*launch)();

  launch is called once all plugins have gone through 'connect' for all adapters
  it is often used to spawn processing threads needed by the plugins

  ---
  void (*message)(struct msg *, unsigned int priority);

  message is used to receive a message from another plugin


  ---
  void (*send_msg)(struct parser_cmds *pc, int msg);

  send_msg is used to tell this plugin that another plugin is expecting to
    receive messages from this one.  Normally this just indicates that the current
    plugin should broadcast messages.

  ---
  void (*shutdown)();

  shutdown is called to try to cleanup gracefully before exitiong
*/ 

static struct plugin_cmd plugin_cmds = {{NULL, NULL}, PLUGIN_ID, "dummy",
                     parseopt_dummy, connect_dummy, NULL, NULL, NULL, NULL, NULL};
int __attribute__((constructor)) __dummy_init(void)
{
  // communicates the existance of teh current plugin to the main program.
  list_add(&plugin_cmds.list, &plugin_cmdlist);
  return 0;
}
