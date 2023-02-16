#!/usr/bin/env perl

use strict;
use warnings;
use 5.010;

##
## gcc scan_fft_pow.c -lm -o scan_pow
## gcc dft_detect.c -lm -o dft_detect
## #build decoders in RS/demod/mod/ and RS/imet/imet4iq.c
##
## #make FIFOs
## mkfifo /tmp/sdr.0; mkfifo log.0
## mkfifo /tmp/sdr.1; mkfifo log.1
## mkfifo /tmp/sdr.2; mkfifo log.2
## #use scan_multi.sh
##


my $verbose = 1;
my $stderr_null = " 2>/dev/null ";

my $j;
my $max_terms = 3;
# mkfifo /tmp/sdr.X
# mkfifo log.X  , X=0,1,2
my $log = "log";
my $sdr = "/tmp/sdr";

my $ppm = 0;                    # set ppm offset of rtl-sdr
my $gain = 20.7;                # set gain, e.g. 40.2
my $center_freq = 403420e3;     # off-center, rtl_sdr-mirrors ... e.g. 404620e3

my $if_sr = 48e3;
my $decimate = 40; # 20..50        # 32*48000 = 1536000
my $band_sr = $if_sr * $decimate;  # 40*48000 = 1920000


if ($ARGV[0]) {
    $band_sr = $ARGV[0];
    if ($ARGV[1]) {
        $decimate = $ARGV[1];
        if ($decimate < 20) { $decimate = 20; }
        if ($decimate > 50) { $decimate = 50; }
        $if_sr = $band_sr / $decimate;
        if ($ARGV[2]) {
            $center_freq = $ARGV[2];
        }
    }
}


print "\n";

print "[scan ".($center_freq-$band_sr/2).":".($center_freq).":".($center_freq+$band_sr/2)."]\n";
print "\n";

my $str_rtlsdr = "rtl_sdr -g $gain -p ".$ppm." -f ".$center_freq." -s ".$band_sr;
print $str_rtlsdr."\n";

my $bps = 8; # rtl-sdr: 8bit


print "\n";


my $iqraw = "rtlsdr.raw";
my $ts = "-n ".(8*$band_sr);
system("$str_rtlsdr $ts $iqraw");

my $powfile = "peaks.txt";

system("./scan_pow - $band_sr $bps $iqraw > $powfile");
system("python plot_fft_pow.py &");


my @rs_matrix;

$rs_matrix[2][0] = "dfm";
$rs_matrix[2][1] = "9600"; # lp-filter bw
$rs_matrix[2][2] = "./dfm09mod --ecc --ptu -vv --IQ";  # --auto  ## DFM9 = DFM

$rs_matrix[3][0] = "rs41";
$rs_matrix[3][1] = "9600"; # lp-filter bw
$rs_matrix[3][2] = "./rs41mod --ptu2 -vx --IQ";  # --auto

$rs_matrix[4][0] = "rs92";
$rs_matrix[4][1] = "9600";
$rs_matrix[4][2] = "./rs92mod --ecc --crc -v --IQ"; # -e brdc / -a almanac

$rs_matrix[5][0] = "m10"; # scan: carrier offset
$rs_matrix[5][1] = "9600";
$rs_matrix[5][2] = "./m10mod -c --ptu -v --dc --IQ";

$rs_matrix[6][0] = "m20"; # scan: carrier offset
$rs_matrix[6][1] = "9600";
$rs_matrix[6][2] = "./mXXmod -c --ptu -v --dc --IQ";

$rs_matrix[7][0] = "imet4";
$rs_matrix[7][1] = "12000";
$rs_matrix[7][2] = "./imet4iq -v --dc --lpIQ --iq";

$rs_matrix[8][0] = "lms6";
$rs_matrix[8][1] = "9600";
$rs_matrix[8][2] = "./lms6Xmod --vit2 --ecc -v --IQ";

$rs_matrix[10][0] = "";

my $detect = "./dft_detect";


my $fh;
open ($fh, '<', "$powfile") or die "error open '$powfile': $!\n";

my @rs_array = ();
my @peakarray = ();
my $num_peaks;

my $num_lines = 0;
my $line;

my $ret;
my $filter = 12e3; # 12kHz detect-bw

while ($line = <$fh>) {
    $num_lines += 1;

    if ( ($line =~ /peak: *([+-]?\d*\.\d*) = .*/) )
    {
        my $fq = $1;
        if ($verbose) { print "[ $fq ] "; }
        print $line; # no chomp
        #my $lp = $filter/($if_sr*2.0);
        my $cmd = "$detect -t 10 --IQ $fq - $band_sr $bps $iqraw 2>/dev/null";
        #if ($verbose) { print $cmd."\n"; }
        system("$cmd");
        $ret = $? >> 8;
        if ($ret & 0x80) {
            $ret = - (0x100 - $ret);  #  ($ret & 0x80) = core dump
        }
        if ($ret) {
            push @peakarray, $fq;
            push @rs_array, $ret;
            print "\n";
        }
    }
}


$num_peaks = scalar(@peakarray);
print "rs-peaks: ".$num_peaks."\n";

my $rs;
my $decoder;


# mkfifo /tmp/sdr.X ...
# mkfifo log.X

my $tee = " > $sdr.0";

for ($j = 0; $j < $num_peaks; $j++) {
    if ($j < $max_terms) {
        if ($j > 0) {
            $tee = " | tee $sdr.$j ".$tee;
        }

        my $idx = abs($rs_array[$j]);
        if ($idx > 1 && $idx < 9) {
            $rs = $rs_matrix[$idx][0];
            $filter = $rs_matrix[$idx][1];
            my $inv = ($rs_array[$j] < 0 ? "-i" : "");
            if ($idx >= 5 && $idx <= 7) { $inv = ""; } # || $idx==8
            $decoder = sprintf("%s %s", $rs_matrix[$idx][2], $inv);

            #my $lp = $filter/($if_sr*2.0);

            my $pid = fork();
            die if not defined $pid;
            if (not $pid) {
                my $rs_str = sprintf("%s_%.0fkHz", $rs, ($center_freq+$band_sr*$peakarray[$j])*1e-3);
                if ($verbose) { print "\nrs: <".$rs_str.">\n"; }
                my $cmd = "$decoder ".($peakarray[$j])." - $band_sr $bps $sdr.$j 2>/dev/null > $log.$j";
                if ($verbose) { print $cmd."\n"; }
                system("$cmd");
                exit;
            }
        }
    }
}


sleep(1);
print "\n";

$ts = "-n ".(120*$band_sr);
#$ts = "";

if ($num_peaks > 0)
{
    my $cmd = "$str_rtlsdr $ts - $tee";
    if ($verbose) { print $cmd."\n"; }
    system("$cmd"); # timeout
}

#wait();

