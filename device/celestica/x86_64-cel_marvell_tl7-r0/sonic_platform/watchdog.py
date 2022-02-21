#############################################################################
# Celestica MRVL_CPO1
#
# Watchdog contains an implementation of SONiC Platform Base API
#
#############################################################################

try:
    import os
    import time
    import ctypes
    from sonic_platform_base.watchdog_base import WatchdogBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

PLATFORM_CPLD_PATH = '/sys/devices/platform/ctrl_cpld/'
GETREG_FILE = 'getreg'
SETREG_FILE = 'setreg'
WDT_TIMER_REG = '0XA181'
WDT_ENABLE_REG = '0xA182'
WDT_KEEP_ALVIVE_REG = '0xA184'
ENABLE_CMD = '0x1'
DISABLE_CMD = '0x0'
WDT_COMMON_ERROR = -1
CLOCK_MONOTONIC = 1

ARMED_TIME_FILE = "/tmp/wdt_armed_time"


class _timespec(ctypes.Structure):
    _fields_ = [
        ('tv_sec', ctypes.c_long),
        ('tv_nsec', ctypes.c_long)
    ]


class Watchdog(WatchdogBase):

    def __init__(self):
        self._api_helper = APIHelper()

        # Init cpld reg path
        self.setreg_path = os.path.join(PLATFORM_CPLD_PATH, SETREG_FILE)
        self.getreg_path = os.path.join(PLATFORM_CPLD_PATH, GETREG_FILE)

        # Set default value
        self.timeout = self._gettimeout()
        self._set_armed_time_file()

        self._librt = ctypes.CDLL('librt.so.1', use_errno=True)
        self._clock_gettime = self._librt.clock_gettime
        self._clock_gettime.argtypes = [
            ctypes.c_int, ctypes.POINTER(_timespec)]

    def _set_armed_time_file(self):
        if not os.path.exists(ARMED_TIME_FILE):
            f = open(ARMED_TIME_FILE, 'w')
            f.write(0)
            f.close()

    def _enable(self):
        """
        Turn on the watchdog timer
        """
        # echo 0xA182 0x1 > /sys/devices/platform/ctrl_cpld/setreg
        enable_val = '{} {}'.format(WDT_ENABLE_REG, ENABLE_CMD)
        return self._api_helper.write_txt_file(self.setreg_path, enable_val)

    def _disable(self):
        """
        Turn off the watchdog timer
        """
        # echo 0xA182 0x0 > /sys/devices/platform/ctrl_cpld/setreg
        disable_val = '{} {}'.format(WDT_ENABLE_REG, DISABLE_CMD)
        return self._api_helper.write_txt_file(self.setreg_path, disable_val)

    def _keepalive(self):
        """
        Keep alive watchdog timer
        """
        # echo 0xA184 0x1 > /sys/devices/platform/ctrl_cpld/setreg
        enable_val = '{} {}'.format(WDT_KEEP_ALVIVE_REG, ENABLE_CMD)
        return self._api_helper.write_txt_file(self.setreg_path, enable_val)

    def _settimeout(self, seconds):
        """
        Set watchdog timer timeout
        @param seconds - timeout in seconds
        @return is the actual set timeout
        """
        second_int = {
            30: 1,
            60: 2,
            180: 3,
            240: 4,
            300: 5,
            420: 6,
            600: 7,
        }.get(seconds)
        return self._api_helper.write_txt_file(self.setreg_path, second_int), seconds

    def _gettimeout(self):
        """
        Get watchdog timeout
        @return watchdog timeout
        """
        hex_str = self._api_helper.get_cpld_reg_value(
            self.getreg_path, WDT_TIMER_REG)
        bin_val = bin(int(hex_str, 16))[2:].zfill(3)
        return {
            '001': 30,
            '010': 60,
            '011': 180,
            '100': 240,
            '101': 300,
            '110': 420,
            '111': 600
        }.get(bin_val)

    def _get_time(self):
        """
        To get clock monotonic time
        """
        ts = _timespec()
        if self._clock_gettime(CLOCK_MONOTONIC, ctypes.pointer(ts)) != 0:
            self._errno = ctypes.get_errno()
            return 0
        return ts.tv_sec + ts.tv_nsec * 1e-9

    #################################################################

    def arm(self, seconds):
        """
        Arm the hardware watchdog with a timeout of <seconds> seconds.
        If the watchdog is currently armed, calling this function will
        simply reset the timer to the provided value. If the underlying
        hardware does not support the value provided in <seconds>, this
        method should arm the watchdog with the *next greater* available
        value.
        Returns:
            An integer specifying the *actual* number of seconds the watchdog
            was armed with. On failure returns -1.
        """

        avaliable_second = [30, 60, 180, 240, 300, 420, 600]
        ret = WDT_COMMON_ERROR
        status = False

        if seconds < 0 or seconds not in avaliable_second:
            print("The second input is invalid, available options are:")
            print(avaliable_second)
            return ret

        try:
            if self.timeout != seconds:
                status, self.timeout = self._settimeout(seconds)

                if not status:
                    return ret

            if self.is_armed():
                self._keepalive()
            else:
                self._enable()

            ret = self.timeout
            self._api_helper.write_txt_file(
                ARMED_TIME_FILE, str(self._get_time()))
        except IOError as e:
            print(e)
            pass

        return ret

    def disarm(self):
        """
        Disarm the hardware watchdog
        Returns:
            A boolean, True if watchdog is disarmed successfully, False if not
        """
        disarmed = False
        if self.is_armed():
            try:
                self._disable()
                disarmed = True
                self._api_helper.write_txt_file(
                    ARMED_TIME_FILE, 0)
            except IOError as e:
                print(e)
                pass

        return disarmed

    def is_armed(self):
        """
        Retrieves the armed state of the hardware watchdog.
        Returns:
            A boolean, True if watchdog is armed, False if not
        """
        hex_str = self._api_helper.get_cpld_reg_value(
            self.getreg_path, WDT_ENABLE_REG)
        return int(hex_str, 16) == int(ENABLE_CMD, 16)

    def get_remaining_time(self):
        """
        If the watchdog is armed, retrieve the number of seconds remaining on
        the watchdog timer
        Returns:
            An integer specifying the number of seconds remaining on thei
            watchdog timer. If the watchdog is not armed, returns -1.
        """

        if not self.is_armed():
            return -1

        armed_time = float(
            self._api_helper.read_one_line_file(ARMED_TIME_FILE))

        if armed_time > 0 and self.timeout != 0:
            cur_time = self._get_time()

            if cur_time <= 0:
                return 0

            diff_time = int(cur_time - armed_time)

            if diff_time > self.timeout:
                return self.timeout
            else:
                return self.timeout - diff_time

        return 0
