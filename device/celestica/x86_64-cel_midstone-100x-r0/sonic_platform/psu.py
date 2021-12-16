#!/usr/bin/env python
# -*- coding: utf-8 -*-
#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the PSUs status which are available in the platform
#
#############################################################################
try:
    from sonic_platform_base.psu_base import PsuBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

# Bus Path Config
Psu_Voltage = "/sys/bus/i2c/devices/7-00{}/hwmon/hwmon{}/in2_input"
Psu_Current = "/sys/bus/i2c/devices/7-00{}/hwmon/hwmon{}/curr2_input"
Psu_Power = "/sys/bus/i2c/devices/7-00{}/hwmon/hwmon{}/power2_input"
Psu_Fan_Speed = "/sys/bus/i2c/devices/7-00{}/hwmon/hwmon{}/fan1_input"


PSU_NAME_LIST = ["PSU-1", "PSU-2"]


class Psu(PsuBase):
    """Platform-specific Psu class"""

    def __init__(self, psu_index):
        PsuBase.__init__(self)
        self.index = psu_index
        self._api_helper = APIHelper()

    def get_speed(self):
        """
        Retrieves current PSU voltage output
        Returns:
            fan(int) speed or None
        """
        if self.index == 0:
            psu_fan_bus = "58"
            hwmon_value = "3"
        else:
            psu_fan_bus = "59"
            hwmon_value = "4"
        speed = self._api_helper.read_txt_file(Psu_Fan_Speed.format(psu_fan_bus, hwmon_value))
        return speed

    def get_voltage(self):
        """
        Retrieves current PSU voltage output
        Returns:
            A float number, the output voltage in volts,
            e.g. 12.1
        """
        if self.index == 0:
            psu_voltage_bus = 58
            hwmon_value = 3
        else:
            psu_voltage_bus = 59
            hwmon_value = 4
        psu_voltage = self._api_helper.read_txt_file(Psu_Voltage.format(psu_voltage_bus, hwmon_value))
        if psu_voltage:
            psu_voltage = float(psu_voltage) / 1000
        else:
            psu_voltage = 0.0
        return psu_voltage

    def get_current(self):
        """
        Retrieves present electric current supplied by PSU
        Returns:
            A float number, the electric current in amperes, e.g 15.4
        """
        if self.index == 0:
            psu_current_bus = 58
            hwmon_value = 3
        else:
            psu_current_bus = 59
            hwmon_value = 4
        psu_current = self._api_helper.read_txt_file(Psu_Current.format(psu_current_bus, hwmon_value))
        if psu_current:
            psu_current = float(psu_current) / 1000
        else:
            psu_current = 0.0
        return psu_current

    def get_power(self):
        """
        Retrieves current energy supplied by PSU
        Returns:
            A float number, the power in watts, e.g. 302.6
        """
        if self.index == 0:
            psu_power_bus = 58
            hwmon_value = 3
        else:
            psu_power_bus = 59
            hwmon_value = 4
        psu_power = self._api_helper.read_txt_file(Psu_Power.format(psu_power_bus, hwmon_value))
        if psu_power:
            psu_power = float(psu_power) / 1000
        else:
            psu_power = 0.0
        return psu_power

    def get_powergood_status(self):
        """
        Retrieves the powergood status of PSU
        Returns:
            A boolean, True if PSU has stablized its output voltages and passed all
            its internal self-tests, False if not.
        """
        return self.get_status()

    @staticmethod
    def set_status_led(color):
        """
        None BMC products do not support setting PSU LED. But in order to prevent frame call errors,
        the parameter color is still reserved
        """
        set_status_str = False
        print("Error: Not Support to Set PSU LED Status")
        return set_status_str

    @staticmethod
    def get_status_led():
        """
        None BMC products do not support setting PSU LED
        """
        status_str = None
        return status_str

    def get_name(self):
        """
        Retrieves the name of the device
            Returns:
            string: The name of the device
        """
        return PSU_NAME_LIST[self.index]

    def get_presence(self):
        """
        Retrieves the presence of the PSU
        Returns:
            bool: True if PSU is present, False if not
        """
        v = self.get_voltage()
        return True if v != 0.0 else False

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        psu_fan_speed = self.get_speed()
        psu_presence = self.get_presence()
        # if isinstance(psu_fan_speed, int) and psu_presence:
        if psu_fan_speed and psu_presence:
            return True
        else:
            return False
