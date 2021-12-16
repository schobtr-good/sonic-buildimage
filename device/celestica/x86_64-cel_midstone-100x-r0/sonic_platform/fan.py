#!/usr/bin/env python
# -*- coding: utf-8 -*-

#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the fan status which are available in the platform
#
#############################################################################
import math
try:
    from sonic_platform_base.fan_base import FanBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

FAN_NAME_LIST = ["FAN-1F", "FAN-1R", "FAN-2F", "FAN-2R",
                 "FAN-3F", "FAN-3R", "FAN-4F", "FAN-4R"]
# Bus Path Config
Fan_Speed = "/sys/bus/i2c/devices/2-000d/fan{}_{}_speed_rpm"
Psu_Fan_Speed = "/sys/bus/i2c/devices/7-00{}/hwmon/hwmon{}/fan1_input"
Fan_Direction = "/sys/bus/i2c/devices/2-000d/fan{}_direction"
Fan_Target_Speed = "/sys/bus/i2c/devices/2-000d/pwm{}"
Fan_Set_Speed = "/sys/bus/i2c/devices/2-000d/pwm{}"
Fan_Set_Led = "/sys/bus/i2c/devices/2-000d/fan{}_led"
Fan_Get_Led = "/sys/bus/i2c/devices/2-000d/fan{}_led"
Fan_Presence = "/sys/bus/i2c/devices/2-000d/fan{}_present"

# define fan max(min) speed
MAX_OUTLET = 12600  # F2B EXHAUST
MAX_INLET = 10300  # B2F INTAKE

# Allowable error range between pwm and actual value
SPEED_TOLERANCE = 10

# Fan led value
FAN_LED_OFF_VALUE = "3"
FAN_LED_RED_VALUE = "2"
FAN_LED_GREEN_VALUE = "1"


class Fan(FanBase):
    """Platform-specific Fan class"""

    def __init__(self, fan_tray_index, fan_index=0, is_psu_fan=False, psu_index=0):
        self.fan_index = fan_index
        self.fan_tray_index = fan_tray_index
        self.is_psu_fan = is_psu_fan
        if self.is_psu_fan:
            self.psu_index = psu_index
        self._api_helper = APIHelper()
        self.index = self.fan_tray_index * 2 + self.fan_index

    def get_direction(self):
        """
        Retrieves the direction of fan
        Returns:
            A string, either FAN_DIRECTION_INTAKE or FAN_DIRECTION_EXHAUST
            depending on fan direction
        """
        direction = self.FAN_DIRECTION_EXHAUST
        if self.fan_tray_index < 4:
            fan_tray_index_ = self.fan_tray_index + 1
            direction_value = self._api_helper.read_txt_file(Fan_Direction.format(fan_tray_index_))
            if int(direction_value) == 1:
                direction = self.FAN_DIRECTION_INTAKE
        else:
            # NOT Support
            direction = "NA"
        return direction

    def get_speed(self):
        """
        Retrieves the speed of fan as a percentage of full speed
        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)
        Note:
            M = 150
            Max F2B = 12600 RPM
            Max B2F = 10300 RPM
        """
        speed = 0
        max_rpm = MAX_INLET if self.fan_index % 2 == 0 else MAX_OUTLET
        if self.fan_tray_index < 4:
            fan_tray_index_ = self.fan_tray_index + 1
            fan_r_f = "rear" if self.fan_index % 2 == 0 else "front"
            rpm_speed = self._api_helper.read_txt_file(Fan_Speed.format(fan_tray_index_, fan_r_f))
            if rpm_speed:
                rpm_speed = int(rpm_speed)
                speed = int(rpm_speed * 100 / max_rpm)
            else:
                rpm_speed = 0
        else:
            if self.psu_index == 5:
                psu_fan_bus = "58"
                hwmon_value = "3"
            else:
                psu_fan_bus = "59"
                hwmon_value = "4"
            rpm_speed = self._api_helper.read_txt_file(Psu_Fan_Speed.format(psu_fan_bus, hwmon_value))
            if rpm_speed:
                rpm_speed = int(rpm_speed)
                speed = int(rpm_speed * 100 / max_rpm)
            else:
                rpm_speed = 0
        return speed if speed <= 100 else rpm_speed

    def get_target_speed(self):
        """
        Retrieves the target (expected) speed of the fan
        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 255 (full speed)
        Note:
            speed_pc = pwm_target*100/255
        """
        if self.fan_tray_index < 4:
            pwm = self._api_helper.read_txt_file(Fan_Target_Speed.format(self.fan_tray_index+1))
            target = math.ceil(float(pwm) * 100 / 255) if pwm else "N/A"

        else:
            # PSU Fan Not Support
            target = "N/A"
        return target

    @staticmethod
    def get_speed_tolerance():
        """
        Retrieves the speed tolerance of the fan
        Returns:
            An integer, the percentage of variance from target speed which is
                 considered tolerable
        """
        return SPEED_TOLERANCE

    def set_speed(self, speed):
        """
        Sets the fan speed
        Args:
            speed: An integer, the percentage of full fan speed to set fan to,
                   in the range 0 (off) to 255 (full speed)
        Returns:
            A boolean, True if speed is set successfully, False if not
        """
        if int(speed) > 255:
            print("Error:Set the fan speed value should be between 0 and 255")
            return False
        if self.fan_tray_index < 4:
            try:
                with open(Fan_Set_Speed.format(self.fan_tray_index + 1), "w") as f:
                    f.write(speed)
                return True
            except Exception as E:
                print("Error: Set fan speed has error, cause '%s'" % E)
                return False

    def set_status_led(self, color):
        """
        Sets the state of the fan module status LED
        Args:
            color: A string representing the color with which to set the
                   fan module status LED
        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        if self.fan_tray_index < 4:
            led_value = {
                self.STATUS_LED_COLOR_GREEN: FAN_LED_GREEN_VALUE,
                self.STATUS_LED_COLOR_RED: FAN_LED_RED_VALUE,
                self.STATUS_LED_COLOR_OFF: FAN_LED_OFF_VALUE
            }.get(color)
            try:
                with open(Fan_Set_Led.format(self.fan_tray_index+1), "w") as f:
                    f.write(led_value)
            except Exception as E:
                print("Error: Set fan%s led fail! cause '%s'" % (self.fan_tray_index+1, E))
                return False
        else:
            print("Error, Couldn't set PSU led status")
            return False

    def get_status_led(self):
        """
        Gets the state of the fan status LED
        Returns:
            A string, one of the predefined STATUS_LED_COLOR_* strings above
        Note:
            STATUS_LED_COLOR_GREEN = "green"
            STATUS_LED_COLOR_RED = "red"
            STATUS_LED_COLOR_OFF = "off"
        """
        if self.fan_tray_index < 4:
            with open(Fan_Get_Led.format(self.fan_tray_index+1), "r") as f:
                color = f.read().strip()
            status_led = {
                "off": self.STATUS_LED_COLOR_OFF,
                "green": self.STATUS_LED_COLOR_GREEN,
                "red": self.STATUS_LED_COLOR_AMBER,
            }.get(color, self.STATUS_LED_COLOR_OFF)

        else:
            status_led = "NA"
            print("Error: Not Support Get PSU LED Status")
        return status_led

    def get_name(self):
        """
        Retrieves the name of the device
            Returns:
            string: The name of the device
        """

        if not self.is_psu_fan:
            fan_name = FAN_NAME_LIST[self.fan_tray_index * 2 + self.fan_index]
        else:
            fan_name = "PSU-{} FAN-1".format(self.psu_index - 4)
        return fan_name

    def get_presence(self):
        """
        Retrieves the presence of the FAN
        Returns:
            bool: True if FAN is present, False if not
        """
        presence = False
        if self.fan_tray_index < 4:
            with open(Fan_Presence.format(self.fan_tray_index + 1), "r") as f:
                presence_value = int(f.read().strip())
            if presence_value == 0:
                presence = True
        else:
            if self.psu_index == 5:
                psu_fan_bus = "58"
                hwmon_value = "3"
            else:
                psu_fan_bus = "59"
                hwmon_value = "4"
            with open(Psu_Fan_Speed.format(psu_fan_bus, hwmon_value), "r") as f:
                rpm_speed = int(f.read().strip())
            if int(rpm_speed) != 0:
                presence = True

        return presence

    @staticmethod
    def get_model():
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        # Not Support
        model = "Unknown"
        return model

    @staticmethod
    def get_serial():
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        # Not Support
        serial = "Unknown"
        return serial

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """

        return self.get_presence() and self.get_speed() > 0
