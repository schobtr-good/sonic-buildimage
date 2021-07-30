#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the fan status which are available in the platform
#
#############################################################################

import json
import math
import os.path

try:
    from sonic_platform_base.fan_base import FanBase
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

EMC2305_PATH = "/sys/bus/i2c/drivers/emc2305/"
FAN_PATH = "/sys/devices/platform/belgitesmc/"
EMC2305_MAX_PWM = 255
EMC2305_FAN_PWM = "pwm{}"
EMC2305_FAN_TARGET = "fan{}_target"
EMC2305_FAN_INPUT = "pwm{}"
FAN_NAME_LIST = ["FAN-1", "FAN-2", "FAN-3"]
PSU_FAN_MAX_RPM = 11000
PSU_HWMON_PATH = "/sys/bus/i2c/devices/i2c-{0}/{0}-00{1}/hwmon"
PSU_I2C_MAPPING = {
    0: {
        "num": 13,
        "addr": "5b"
    },
    1: {
        "num": 12,
        "addr": "5a"
    },
}


class Fan(FanBase):
    """Platform-specific Fan class"""

    def __init__(self, fan_index=0):
        self.fan_index = fan_index

        FanBase.__init__(self)

    def get_direction(self):
        """
        Retrieves the direction of fan
        Returns:
            A string, either FAN_DIRECTION_INTAKE or FAN_DIRECTION_EXHAUST
            depending on fan direction
        """
        direction = self.FAN_DIRECTION_EXHAUST

        fan_direction_path = r"/sys/bus/i2c/drivers/pddf.fan/2-0032/fan%s_direction" % str(int(self.fan_index) + 1)
        with open(fan_direction_path, "r") as f:
            fan_direction_val = f.read()
        direction = self.FAN_DIRECTION_INTAKE if fan_direction_val == "1" else self.FAN_DIRECTION_EXHAUST

        return direction

    def get_speed(self):
        """
        Retrieves the speed of fan as a percentage of full speed
        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)

        Note:
            speed = pwm_in/255*100
        """
        speed = 10000

        return int(speed)

    def get_target_speed(self):
        """
        Retrieves the target (expected) speed of the fan
        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)

        Note:
            speed_pc = pwm_target/255*100

            0   : when PWM mode is use
            pwm : when pwm mode is not use
        """
        target = 0

        return target

    def get_speed_tolerance(self):
        """
        Retrieves the speed tolerance of the fan
        Returns:
            An integer, the percentage of variance from target speed which is
                 considered tolerable
        """
        return 10

    def set_speed(self, speed):
        """
        Sets the fan speed
        Args:
            speed: An integer, the percentage of full fan speed to set fan to,
                   in the range 0 (off) to 100 (full speed)
        Returns:
            A boolean, True if speed is set successfully, False if not

        Note:
            Depends on pwm or target mode is selected:
            1) pwm = speed_pc * 255             <-- Currently use this mode.
            2) target_pwm = speed_pc * 100 / 255
             2.1) set pwm{}_enable to 3

        """
        pwm = speed * 255 / 100

        return False

    # def set_status_led(self, color):
    #     """
    #     Sets the state of the fan module status LED
    #     Args:
    #         color: A string representing the color with which to set the
    #                fan module status LED
    #     Returns:
    #         bool: True if status LED state is set successfully, False if not
    #     """
    #     set_status_led = False
    #
    #     return set_status_led
    def set_status_led(self, color=None):
        """
        Sets the state of the fan module status LED
        Args:
            color: A string representing the color with which to set the
                   fan module status LED
        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        set_status_led = False

        return set_status_led

    def get_name(self):
        """
        Retrieves the name of the device
            Returns:
            string: The name of the device
        """
        fan_name = "FAN-1"
        return fan_name

    def get_presence(self):
        """
        Retrieves the presence of the PSU
        Returns:
            bool: True if PSU is present, False if not
        """

        fan_presence_path = "/sys/bus/i2c/drivers/pddf.fan/2-0032/fan%s_present" % str(int(self.fan_index) + 1)
        with open(fan_presence_path, "r") as f:
            fan_presence_val = f.read()
        return True if int(fan_presence_val) == 0 else False

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        return self.get_presence() and self.get_speed() > 0
