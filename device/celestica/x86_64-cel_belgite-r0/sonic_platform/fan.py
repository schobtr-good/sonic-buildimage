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
    from .helper import APIHelper
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

FAN_MAX_SPEED = 28600
PSU_FAN_MAX_SPEED = 18000
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

    def __init__(self, fan_tray_index, fan_index=0, is_psu_fan=False, psu_index=0):
        FanBase.__init__(self)
        self.fan_index = fan_index
        self._api_helper = APIHelper()
        self.fan_tray_index = fan_tray_index
        self.is_psu_fan = is_psu_fan
        if self.is_psu_fan:
            self.psu_index = psu_index

    def get_direction(self):
        """
        Retrieves the direction of fan
        Returns:
            A string, either FAN_DIRECTION_INTAKE or FAN_DIRECTION_EXHAUST
            depending on fan direction
        """
        if not self.is_psu_fan:
            fan_direction_path = r"/sys/bus/i2c/drivers/pddf.fan/2-0032/fan%s_direction" % str(int(self.fan_tray_index)+1)
            try:
                with open(fan_direction_path, "r") as f:
                    fan_direction_val = f.read().strip()
                    direction = self.FAN_DIRECTION_INTAKE if fan_direction_val == "1" else self.FAN_DIRECTION_EXHAUST
            except FileNotFoundError:
                print("Error!!! Couldn't get the path:%s" % fan_direction_path)
                direction = "N/A"
        else:
            direction = self.FAN_DIRECTION_EXHAUST
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
        if not self.is_psu_fan:
            fan_speed_path = r"/sys/bus/i2c/drivers/pddf.fan/2-0032/fan%s_input" % str(int(self.fan_tray_index) + 1)
            try:
                with open(fan_speed_path, "r") as f:
                    fan_speed_val = f.read().strip()
            except FileNotFoundError:
                print("Error!!! Couldn't get the path:%s" % fan_speed_path)
                fan_speed_val = 0
            speed_percentage = int((int(fan_speed_val) / FAN_MAX_SPEED) * 100)
            return fan_speed_val if speed_percentage > 100 else speed_percentage
        else:
            if self.psu_index == 1:
                fan_speed_path = r"/sys/devices/pci0000:00/0000:00:12.0/i2c-1/i2c-4/4-0058/hwmon/hwmon2/fan1_input"
            elif self.psu_index == 2:
                fan_speed_path = r"/sys/devices/pci0000:00/0000:00:12.0/i2c-1/i2c-4/4-0059/hwmon/hwmon3/fan1_input"
            try:
                with open(fan_speed_path, "r") as f:
                    fan_speed_val = f.read().strip()
            except FileNotFoundError:
                print("Error!!! Couldn't get the path:%s" % fan_speed_path)
                fan_speed_val = 0
            speed_percentage = int((int(fan_speed_val) / PSU_FAN_MAX_SPEED) * 100)
            return fan_speed_val if speed_percentage > 100 else speed_percentage



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
        if not self.is_psu_fan:
            fan_pwm_path = r"/sys/bus/i2c/drivers/pddf.fan/2-0032/pwm%s" % str(int(self.fan_tray_index) + 1)
            try:
                with open(fan_pwm_path, "r") as f:
                    fan_pwm_val = f.read().strip()
            except FileNotFoundError:
                print("Error!!! Couldn't get the path:%s" % fan_pwm_path)
                fan_pwm_val = 0
            target = math.ceil(float(fan_pwm_val)*100/255)
        else:
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
        fan_name = FAN_NAME_LIST[self.fan_tray_index] if not self.is_psu_fan else "PSU-%s FAN-1" % self.psu_index
        print("------------------------------name:%s" % fan_name)
        return fan_name

    def get_presence(self):
        """
        Retrieves the presence of the PSU
        Returns:
            bool: True if PSU is present, False if not
        """
        if not self.is_psu_fan:
            fan_presence_path = "/sys/bus/i2c/drivers/pddf.fan/2-0032/fan%s_present" % str(int(self.fan_index) + 1)
            try:
                with open(fan_presence_path, "r") as f:
                    fan_presence_val = f.read().strip()
            except FileNotFoundError:
                print("Error!!! Couldn't get the path:%s" % fan_presence_path)
                fan_presence_val = 0       
        else:
            if self.psu_index == 1:
                fan_presence_path = r"/sys/devices/platform/pddf.cpld/psuL_prs"
            elif self.psu_index == 2:
                fan_presence_path = r"/sys/devices/platform/pddf.cpld/psuR_prs"
            try:
                with open(fan_presence_path, "r") as f:
                    fan_presence_val = f.read().strip()
            except FileNotFoundError:
                print("Error!!! Couldn't get the path:%s" % fan_presence_val)
                fan_presence_val = 0
        return True if int(fan_presence_val) == 0 else False

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        return self.get_presence()
