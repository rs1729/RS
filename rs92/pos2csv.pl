#!/usr/bin/env perl
#EA4GKQ - Angel
use strict;
use warnings;

my $filename = $ARGV[0];
my $fh;
if (defined $filename) {
  open($fh, "<", $filename) or die "Could not open $filename: $!";
}
else {
  $fh = *STDIN;
}



my $line;
my $hms;
my $lat; my $lon; my $alt;
my $matricula;
my $temp;
my $date;
 
while ($line = <$fh>) {
    if ($line =~ /(\d\d:\d\d:\d\d).*\ +lat:\ *(-?\d*\.\d*)\ +lon:\ *(-?\d*\.\d*)\ +alt:\ *(-?\d*\.\d*).*/) {

        $hms = $1;
        $lat = $2;
        $lon = $3;
        $alt = $4;
	    if ($line =~ /\(([\w]+)\)/) {
	        $matricula = $ 1 ;
	    }			
	    if ($line =~ /T=(-?[\d.]+)C/) {
	         $temp = $1;
	    }
	    else {
	         $temp = "";
	    }		
        if ($line =~ /(\d\d\d\d)-(\d\d)-(\d\d).*/) {
            $date = "$3/$2/$1";
        }		
        print  "$date,$hms,$matricula,$lon,$lat,$alt,$temp\n";
    }
}
 

