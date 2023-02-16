
#mkfifo /tmp/sdr.0
#mkfifo /tmp/sdr.1
#mkfifo /tmp/sdr.2

#mkfifo log.0
#mkfifo log.1
#mkfifo log.2

TERM="gnome-terminal"

if [ $1 = "xfce4" ]; then
    TERM="xfce4-terminal"
fi

echo $TERM

if [ $TERM = "xfce4-terminal" ]; then
    $TERM --geometry=64x61+270+140 --hide-menubar -T term0 -H -e './scan_multi_rs.pl'

    $TERM --geometry=180x20+800+836 --hide-menubar -T term3 -H -e 'cat log.2'
    $TERM --geometry=180x20+800+488 --hide-menubar -T term2 -H -e 'cat log.1'
    $TERM --geometry=180x20+800+140 --hide-menubar -T term1 -H -e 'cat log.0'
else
    $TERM --geometry=64x61+200+140 --hide-menubar -e 'bash -c "./scan_multi_rs.pl; read line"'

    $TERM --geometry=180x20+800+880 --hide-menubar -e 'bash -c "cat log.2; read line"'
    $TERM --geometry=180x20+800+510 --hide-menubar -e 'bash -c "cat log.1; read line"'
    $TERM --geometry=180x20+800+140 --hide-menubar -e 'bash -c "cat log.0; read line"'
fi

