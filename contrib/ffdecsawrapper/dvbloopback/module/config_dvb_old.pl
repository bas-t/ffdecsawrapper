#!/bin/sh
# -*-Perl-*-
exec perl -x -- "$0" "$@"
#!perl -w

$debug = 0;

sub test_dvb_adapter(\@) {
  my($inc) = @_;
  my($vars)="";
  foreach $i (@$inc) {
    $vars .= "\"$i\" ";
  }
  unlink("dvbdevwrap.h");
  unlink("dvbdev.h");
  unlink("config-dvb/dvbdev.h");
  $cmd = "cd config-dvb && make $vars" . ($debug ? "" : "2>/dev/null 1>/dev/null");
  print "$cmd\n" if($debug);

  system("ln -sf chkdvb-2.6.v4l.c config-dvb/chkdvb.c");
  system("ln -sf ../dvbdev-2.6.38.h config-dvb/dvbdev.h");
   print "Using canned header\n";
    if(system("$cmd") == 0) {
    `echo "DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);" >> dvbdevwrap.h`; 
    `echo "#define wrap_dvb_reg_adapter(a, b, c) dvb_register_adapter(a, b, c, &dvblb_basedev->dev, adapter_nr)" >> dvbdevwrap.h`; 
     system("ln -sf dvbdev-2.6.38.h dvbdev.h"); 
    return 0;
  }
}

exit(test_dvb_adapter(@ARGV));

