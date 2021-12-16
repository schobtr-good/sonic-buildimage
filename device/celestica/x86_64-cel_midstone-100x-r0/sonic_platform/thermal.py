#!/usr/bin/env python

#############################################################################
# Celestica
#
# Thermal contains an implementation of SONiC Platform Base API and
# provides the thermal device status which are available in the platform
#
#############################################################################

import os
import os.path

try:
    from sonic_platform_base.thermal_base import ThermalBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

NULL_VAL = "N/A"
I2C_ADAPTER_PATH = "/sys/class/i2c-adapter"

THERMAL_INFO = [
    {'name': 'CPU_TEMP',    # 0
     'bus_path': '/sys/devices/platform/coretemp.0/hwmon/'},
    {'name': 'TEMP_BB_U3',  # 1
     'bus_path': '/sys/bus/i2c/devices/8-004d/hwmon/'},
    {'name': 'TEMP_SW_U25', # 2
     'bus_path': '/sys/bus/i2c/devices/8-004a/hwmon/'},
    {'name': 'TEMP_SW_U26', # 3
     'bus_path': '/sys/bus/i2c/devices/8-004d/hwmon/'},
    {'name': 'TEMP_SW_U16', # 4
     'bus_path': '/sys/bus/i2c/devices/8-0049/hwmon/'},
    {'name': 'TEMP_SW_U52', # 5
     'bus_path': '/sys/bus/i2c/devices/8-0048/hwmon/'},
    {'name': 'TEMP_SW_CORE',    # 6
     'sensor_path': '/sys/bus/i2c/devices/8-0068/iio:device0/in_voltage0_raw'},
    {'name': 'PSU1_Temp1',  # 7
     'bus_path': '/sys/bus/i2c/devices/7-0058/hwmon/'},
    {'name': 'PSU1_Temp2',  # 8
     'bus_path': '/sys/bus/i2c/devices/7-0058/hwmon/'},
    {'name': 'PSU1_Temp3',  # 9
     'bus_path': '/sys/bus/i2c/devices/7-0058/hwmon/'},
    {'name': 'PSU2_Temp1',  # 10
     'bus_path': '/sys/bus/i2c/devices/7-0059/hwmon/'},
    {'name': 'PSU2_Temp2',  # 11
     'bus_path': '/sys/bus/i2c/devices/7-0059/hwmon/'},
    {'name': 'PSU2_Temp3',  # 12
     'bus_path': '/sys/bus/i2c/devices/7-0059/hwmon/'},
]

thermal_temp_dict = {
    "CPU_TEMP": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL, "max": 103, "high_critical_threshold": 105},
    "TEMP_BB_U3": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                   "max": {"B2F": 55, "F2B": NULL_VAL}, "high_critical_threshold": {"B2F": 60, "F2B": NULL_VAL}},
    "TEMP_SW_U25": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                    "max": NULL_VAL, "high_critical_threshold": NULL_VAL},
    "TEMP_SW_U26": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                    "max": NULL_VAL, "high_critical_threshold": NULL_VAL},
    "TEMP_SW_U16": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                    "max": {"B2F": NULL_VAL, "F2B": 55},
                    "high_critical_threshold": {"B2F": NULL_VAL, "F2B": 60}},
    "TEMP_SW_U52": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                    "max": NULL_VAL, "high_critical_threshold": NULL_VAL},
    "TEMP_SW_CORE": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                     "max": 105, "high_critical_threshold": 110},
    "PSU1_Temp1": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                   "max": {"B2F": 61, "F2B": 61}, "high_critical_threshold": NULL_VAL},
    "PSU1_Temp2": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                   "max": {"B2F": 107, "F2B": 108}, "high_critical_threshold": NULL_VAL},
    "PSU1_Temp3": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                   "max": {"B2F": 106, "F2B": 91}, "high_critical_threshold": NULL_VAL},
    "PSU2_Temp1": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                   "max": {"B2F": 61, "F2B": 61}, "high_critical_threshold": NULL_VAL},
    "PSU2_Temp2": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                   "max": {"B2F": 107, "F2B": 108}, "high_critical_threshold": NULL_VAL},
    "PSU2_Temp3": {"low_critical_threshold": NULL_VAL, "min": NULL_VAL,
                   "max": {"B2F": 106, "F2B": 91}, "high_critical_threshold": NULL_VAL},
}


class Thermal(ThermalBase):
    """Platform-specific Thermal class"""

    SS_CONFIG_PATH = "/usr/share/sonic/device/x86_64-cel_midstone-100x-r0/sonic_platform/sensors.conf"

    def __init__(self, thermal_index, airflow):

        self.index = thermal_index
        self._api_helper = APIHelper()
        self._airflow = str(airflow).upper()
        self._thermal_info = THERMAL_INFO[self.index]
        self.name = self.get_name()
        self.__get_thermal_bus_info()

    def __get_thermal_bus_info(self):
        """
        Complete other path information according to the corresponding BUS path
        """
        folder_path = self._thermal_info.get("bus_path")
        if folder_path:
            sub_folder_name = os.popen("ls %s" % folder_path).read().strip()
            if self._thermal_info.get("bus_path").endswith("/"):
                sub_folder_path = folder_path + sub_folder_name
            else:
                sub_folder_path = folder_path + "/" + sub_folder_name
            if self.index < 6:  # 0-5
                self._thermal_info["sensor_path"] = sub_folder_path + "/temp1_input"
                self._thermal_info["max_temp"] = sub_folder_path + "/temp1_max"
            elif self.index == 6:  # 6
                pass
            elif self.index < 10:  # 7-9
                self._thermal_info["sensor_path"] = sub_folder_path + "/temp%s_input" % (self.index - 6)
            else:  # 10-12
                self._thermal_info["sensor_path"] = sub_folder_path + "/temp%s_input" % (self.index - 9)

    def __set_threshold(self, temperature):
        temp_file_path = self._thermal_info.get("max_temp", "N/A")
        try:
            with open(temp_file_path, 'w') as fd:
                fd.write(str(temperature))
            return True
        except IOError:
            return False

    def get_temperature(self):
        """
        Retrieves current temperature reading from thermal
        Returns:
            A float number of current temperature in Celsius up to nearest thousandth
            of one degree Celsius, e.g. 30.125
        """

        temperature = self._api_helper.read_txt_file(self._thermal_info.get("sensor_path", "N/A"))
        if temperature:
            temperature = float(temperature) / 1000
        else:
            temperature = 0
        if self.name == "TEMP_SW_CORE":
            a, b, c, v = -124.28, -422.03, 384.62, temperature
            temperature = a * (v ** 2) + (b * v) + c
        return float("{:.3f}".format(temperature))

    def get_high_threshold(self):
        """
        Retrieves the high threshold temperature of thermal
        Returns:
            A float number, the high threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        high_threshold = thermal_temp_dict.get(self.name).get("max")
        if isinstance(high_threshold, dict):
            high_threshold = high_threshold.get(self._airflow)
        if high_threshold != NULL_VAL:
            high_threshold = float("{:.3f}".format(high_threshold))
        return high_threshold

    def get_low_threshold(self):
        """
        Retrieves the low threshold temperature of thermal
        Returns:
            A float number, the low threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        low_threshold = thermal_temp_dict.get(self.name).get("min")
        if low_threshold != NULL_VAL:
            low_threshold = float("{:.3f}".format(low_threshold))
        return low_threshold

    def set_high_threshold(self, temperature):
        """
        Sets the high threshold temperature of thermal
        Args :
            temperature: A float number up to nearest thousandth of one degree Celsius,
            e.g. 30.125
        Returns:
            A boolean, True if threshold is set successfully, False if not
        """
        temp_file = "temp1_max"
        is_set = self.__set_threshold(int(temperature) * 1000)
        file_set = False
        if is_set:
            try:
                with open(self.SS_CONFIG_PATH, 'r+') as f:
                    content = f.readlines()
                    f.seek(0)
                    ss_found = False
                    for idx, val in enumerate(content):
                        if self.name in val:
                            ss_found = True
                        elif ss_found and temp_file in val:
                            content[idx] = "    set {} {}\n".format(
                                temp_file, temperature)
                            f.writelines(content)
                            file_set = True
                            break
            except IOError:
                file_set = False

        return is_set & file_set

    @staticmethod
    def set_low_threshold(temperature):
        """
        Sets the low threshold temperature of thermal
        Args : 
            temperature: A float number up to nearest thousandth of one degree Celsius,
            e.g. 30.125
        Returns:
            A boolean, True if threshold is set successfully, False if not
        """
        # Not Support
        return False

    def get_high_critical_threshold(self):
        """
        Retrieves the high critical threshold temperature of thermal
        Returns:
            A float number, the high critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        high_critical_threshold = thermal_temp_dict.get(self.name).get("high_critical_threshold")
        if isinstance(high_critical_threshold, dict):
            high_critical_threshold = high_critical_threshold.get(str(self._airflow).upper())
        if high_critical_threshold != NULL_VAL:
            high_critical_threshold = float("{:.3f}".format(float(high_critical_threshold)))
        return high_critical_threshold

    def get_low_critical_threshold(self):
        """
        Retrieves the low critical threshold temperature of thermal
        Returns:
            A float number, the low critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        low_critical_threshold = thermal_temp_dict.get(self.name).get("low_critical_threshold")
        if low_critical_threshold != NULL_VAL:
            low_critical_threshold = float("{:.3f}".format(float(low_critical_threshold)))
        return low_critical_threshold

    def get_name(self):
        """
        Retrieves the name of the thermal device
            Returns:
            string: The name of the thermal device
        """
        return self._thermal_info.get("name")

    def get_presence(self):
        """
        Retrieves the presence of the PSU
        Returns:
            bool: True if PSU is present, False if not
        """
        return os.path.isfile(self._thermal_info.get("sensor_path", NULL_VAL))

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        return self._thermal_info.get("model", NULL_VAL)

    @staticmethod
    def get_serial():
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        return NULL_VAL

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        if not self.get_presence():
            return False
        return True
