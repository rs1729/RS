#!/usr/bin/env perl
## aprs-output provided by daniestevez
## axudp extensions provided by dl9rdz
use strict;
use warnings;
use IO::Socket::INET;
use Getopt::Long;

my $filename = undef;
my $date = undef;

my $mycallsign;
my $passcode;
my $comment;

my $udp;
GetOptions("u=s" => \$udp) or die "Error in command line arguments\n";

while (@ARGV) {
  $mycallsign = shift @ARGV;
  $passcode = shift @ARGV;
  $comment = shift @ARGV;
  $filename = shift @ARGV;
}

my $fpi;

if (defined $filename) {
  open($fpi, "<", $filename) or die "Could not open $filename: $!";
}
else {
  $fpi = *STDIN;
}

my $fpo = *STDOUT;


my $line;

my $hms;
my $lat; my $lon; my $alt;
my $sign = 1;
my $NS; my $EW;
my $str;

my $speed = 0.00;
my $course = 0.00;

my $callsign;

my $temp;

# axudp: encodecall: encode single call sign ("AB0CDE-12*") up to 6 letters/numbers, ssid 0..15, optional "*"; last: set in last call sign (dst/via)
sub encodecall{
	my $call = shift;
	my $last = shift;
	if(!($call =~ /^([A-Z0-9]{1,6})(-\d+|)(\*|)$/)) {
		die "Callsign $call not properly formatted";
	};
	my $callsign = $1 . ' 'x(6-length($1));
	my $ssid = length($2)>0 ? 0-$2 : 0;
	my $hbit = $3 eq '*' ? 0x80 : 0;
	my $encoded = join('',map chr(ord($_)<<1),split //,$callsign);
	$encoded .= chr($hbit | 0x60 | ($ssid<<1) | ($last?1:0));
	return $encoded;
}

# kissmkhead: input: list of callsigns (src, dest, repeater list); output: raw kiss frame header data
sub kissmkhead {
	my @calllist = @_;
	my $last = pop @calllist;
	my $enc = join('',map encodecall($_),@calllist);
	$enc .= encodecall($last, 1);
	return $enc;
}

#create CRC tab
my @CRCL;
my @CRCH;
my ($c, $crc,$i);
for $c (0..255) {
	$crc = 255-$c;
	for $i (0..7) {  $crc = ($crc&1) ? ($crc>>1)^0x8408 : ($crc>>1); }
	$CRCL[$c] = $crc&0xff;
	$CRCH[$c] = (255-($crc>>8))&0xff;
}
sub appendcrc {
	$_ = shift;
	my @data = split //,$_;
	my ($b, $l, $h)=(0,0,0);
	for(@data) { $b = ord($_) ^ $l; $l = $CRCL[$b] ^ $h; $h = $CRCH[$b]; }
	$_ .= chr($l) . chr($h);
	return $_;
}

my ($sock,$kissheader);
if($udp) {
	my ($udpserver,$udpport)=split ':',$udp;
	$udpserver = "127.0.0.1" unless $udpserver;
	$sock = new IO::Socket::INET(PeerAddr => $udpserver, PeerPort => $udpport, Proto => "udp", Timeout => 1) or die "Error creating socket";
	# $kissheader = kissmkhead(uc($mycallsign),"APRS","TCPIP*");
	$kissheader = kissmkhead(uc($mycallsign),"APRS");
}

print $fpo "user $mycallsign pass $passcode vers \"RS decoder\"\n";

while ($line = <$fpi>) {

    print STDERR $line; ## entweder: alle Zeilen ausgeben

    if ($line =~ /(\d\d):(\d\d):(\d\d\.?\d?\d?\d?).*\ +lat:\ *(-?\d*)(\.\d*)\ +lon:\ *(-?\d*)(\.\d*)\ +alt:\ *(-?\d*\.\d*).*/) {

    #print STDERR $line; ## oder: nur Zeile mit Koordinaten ausgeben

        $hms = $1*10000+$2*100+$3;

        if ($4 < 0) { $NS="S"; $sign *= -1; }
        else        { $NS="N"; $sign = 1}
        $lat = $sign*$4*100+$5*60;

        if ($6 < 0) { $EW="W"; $sign = -1; }
        else        { $EW="E"; $sign = 1; }
        $lon = $sign*$6*100+$7*60;

        $alt = $8*3.28084; ## m -> feet

        if ($line =~ /(\d\d\d\d)-(\d\d)-(\d\d).*/) {
            $date = $3*10000+$2*100+($1%100);
        }

        if ( ($line =~ /vH:\ *(\d+\.\d+)\ +D:\ *(\d+\.\d+).*/)
          or ($line =~ /vH:\ *(\d+\.\d+)m\/s\ +D:\ *(\d+\.\d+).*/) )
        {
            $speed = $1*3.6/1.852;  ## m/s -> knots
            $course = $2;
        }

	    if ($line =~ /\(([\w]+)\)/) {
	        $callsign = $1;
	    }

	    if ($line =~ /T=(-?[\d.]+)C/) {
	         $temp = " T=$1C";
	    }
	    else {
	         $temp = "";
	    }

	    $str = sprintf("$mycallsign>APRS,TCPIP*:;%-9s*%06dh%07.2f$NS/%08.2f${EW}O%03d/%03d/A=%06d$comment$temp", $callsign, $hms, $lat, $lon, $course, $speed, $alt);
	    print $fpo "$str\n";

	    if($sock) {
	        $str = (split(":",$str))[1];
		print $sock appendcrc($kissheader.chr(0x03).chr(0xf0).$str);
	    }

    }
    #elsif ($line =~ / # xdata = (.*)/) { ## nicht, wenn (oben) alle Zeilen ausgeben werden
    #    if ($1) {
    #        print STDERR $line;
    #    }
    #}
}

close $fpi;
close $fpo;

