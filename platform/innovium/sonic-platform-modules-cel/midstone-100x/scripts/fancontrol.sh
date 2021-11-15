#! /bin/sh

### BEGIN INIT INFO
# Provides:          fancontrol
# Required-Start:    $remote_fs
# Required-Stop:     $remote_fs
# Default-Start:     2 3 4 5
# Default-Stop:
# Short-Description: fancontrol
# Description:       fancontrol configuration selector
### END INIT INFO

. /lib/lsb/init-functions

[ -f /etc/default/rcS ] && . /etc/default/rcS
PATH=/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin
DAEMON=/usr/local/bin/fancontrol
DESC="fan speed regulator"
NAME="fancontrol"
PIDFILE=/var/run/fancontrol.pid
MAIN_CONF=/usr/share/sonic/device/x86_64-cel_midstone-100x-r0/fancontrol
DEVPATH=/sys/bus/i2c/drivers/platform_fan/2-000d
test -x $DAEMON || exit 0

init() {
	FANFAULT=$(cat ${DEVPATH}/fan1_fault)
    [ $FANFAULT -eq 1 ] && DIRECTION=$(cat ${DEVPATH}/fan1_direction)
	FANDIR=$([ $DIRECTION -eq 1 ] && echo "B2F" || echo "F2B")

	CONF=${MAIN_CONF}-${FANDIR}
       
}

install() {
    find /var/lib/docker/overlay*/ -path */sbin/fancontrol -exec cp /usr/local/bin/fancontrol {} \;
}

case "$1" in
start)
    init
    cp $CONF $MAIN_CONF
    ;;
install)
    install
    ;;
*)
    log_success_msg "Usage: /etc/init.d/fancontrol {start} | {install}"
    exit 1
    ;;
esac

exit 0
