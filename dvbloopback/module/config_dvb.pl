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
  $cmd = "cd config-dvb && make $vars" . ($debug ? "" : "2>/dev/null 1>/dev/null");
  print "$cmd\n" if($debug);

    if(system("$cmd") == 0) {
    `echo "DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);" >> dvbdevwrap.h`; 
    `echo "#define wrap_dvb_reg_adapter(a, b, c) dvb_register_adapter(a, b, c, &dvblb_basedev->dev, adapter_nr)" >> dvbdevwrap.h`; 
    return 0;
  }
}

exit(test_dvb_adapter(@ARGV));

