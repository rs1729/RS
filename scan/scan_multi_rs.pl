#!/usr/bin/env perl

use strict;
use warnings;
use 5.010;


my $verbose = 1;
my $stderr_null = " 2>/dev/null ";

my $j;
my $max_terms = 3;
# mkfifo /tmp/sdr.X
# mkfifo log.X  , X=0,1,2
my $log = "log";
my $sdr = "/tmp/sdr";

my $ppm = 45;
my $gain = 40.2;

my $if_sr = 48e3;
my $decimate = 40; # 20..50        # 32*48000 = 1536000
my $band_sr = $if_sr * $decimate;  # 40*48000 = 1920000

my $center_freq = 404620e3; # off-center, rtl_sdr-mirrors ...

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
my $b2f_conv;
if ($bps == 16) { $b2f_conv = "convert_s16_f"; }
else            { $b2f_conv = "convert_u8_f"; }


print "\n";


my $iqraw = "rtlsdr.raw";
system("$str_rtlsdr -n ".(8*$band_sr)." $iqraw");

my $powfile = "peaks.txt";

system("./scan_pow - $band_sr $bps $iqraw > $powfile");
system("python plot_fft_pow.py &");


my @rs_matrix;

$rs_matrix[2][0] = "dfm";
$rs_matrix[2][1] = "9600"; # lp-filter bw
$rs_matrix[2][2] = "./dfm09 --ecc --ptu -v";  # --auto  ## DFM9 = -DFM

$rs_matrix[3][0] = "rs41";
$rs_matrix[3][1] = "9600"; # lp-filter bw
$rs_matrix[3][2] = "./rs41 --ecc2 --crc --ptu -vx";

$rs_matrix[4][0] = "rs92";
$rs_matrix[4][1] = "9600";
$rs_matrix[4][2] = "./rs92 --ecc --crc -v"; # -e brdc / -a almanac

$rs_matrix[8][0] = "lms6";
$rs_matrix[8][1] = "9600";
$rs_matrix[8][2] = "./lms6 --vit --ecc -v";

$rs_matrix[5][0] = "m10"; # scan: carrier offset
$rs_matrix[5][1] = "9600";
$rs_matrix[5][2] = "./m10 -c -vv";

$rs_matrix[6][0] = "imet1ab";
$rs_matrix[6][1] = "32000";  # > detect-bw
$rs_matrix[6][2] = "./imet1ab_dft -v";

$rs_matrix[7][0] = "imet4";
$rs_matrix[7][1] = "12000";
$rs_matrix[7][2] = "./imet1rs_dft -r";

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
        my $lp = $filter/($if_sr*2.0);
        my $cmd = "cat $iqraw | csdr $b2f_conv | csdr shift_addition_cc ".(-$fq)." $stderr_null| ".
                  "csdr fir_decimate_cc $decimate 0.005 $stderr_null| csdr bandpass_fir_fft_cc -$lp $lp 0.02 $stderr_null| ".
                  "csdr fmdemod_quadri_cf | csdr limit_ff | csdr convert_f_s16 | ".
                  "sox -t raw -r $if_sr -b 16 -e signed - -t wav - 2>/dev/null | $detect -t 10 2>/dev/null";
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
            if ($idx == 5 || $idx == 7) { $inv = ""; } # || $idx==8
            $decoder = sprintf("%s %s", $rs_matrix[$idx][2], $inv);

            my $lp = $filter/($if_sr*2.0);

            my $pid = fork();
            die if not defined $pid;
            if (not $pid) {
                my $rs_str = sprintf("%s_%.0fkHz", $rs, ($center_freq+$band_sr*$peakarray[$j])*1e-3);
                if ($verbose) { print "\nrs: <".$rs_str.">\n"; }
                my $cmd = "cat $sdr.$j | csdr $b2f_conv | csdr shift_addition_cc ".(-$peakarray[$j])." $stderr_null| ".
                          "csdr fir_decimate_cc $decimate 0.005 $stderr_null| csdr bandpass_fir_fft_cc -$lp $lp 0.02 $stderr_null| ".
                          "csdr fmdemod_quadri_cf | csdr limit_ff | csdr convert_f_s16 | ".
                          "sox -t raw -r $if_sr -b 16 -e signed - -t wav - 2>/dev/null | $decoder 2>/dev/null > $log.$j";
                if ($verbose) { print $cmd."\n"; }
                system("$cmd");
                exit;
            }
        }
    }
}


sleep(1);
print "\n";

if ($num_peaks > 0)
{
    my $cmd = "$str_rtlsdr -n ".(60*$band_sr)." - $tee";
    if ($verbose) { print $cmd."\n"; }
    system("$cmd"); # timeout
}

#wait();

