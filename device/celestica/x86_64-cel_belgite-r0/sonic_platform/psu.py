#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the PSUs status which are available in the platform
#
#############################################################################
import re
import os.path
import sonic_platform

try:
    from sonic_platform_base.psu_base import PsuBase
    from sonic_platform.fan import Fan
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")
TLV_ATTR_TYPE_MODEL = 2
TLV_ATTR_TYPE_SERIAL = 5
PSU_EEPROM_PATH = "/sys/bus/i2c/devices/{}-00{}/eeprom"
HWMON_PATH = "/sys/bus/i2c/devices/i2c-{0}/{0}-00{1}/hwmon"
FAN_MAX_RPM = 11000
PSU_NAME_LIST = ["PSU-R", "PSU-L"]
PSU_NUM_FAN = [1, 1]
PSU_I2C_MAPPING = {
    0: {
        "num": 4,
        "addr": "58",
        "eeprom_addr": "50"
    },
    1: {
        "num": 4,
        "addr": "59",
        "eeprom_addr": "51"
    },
}


class Psu(PsuBase):
    """Platform-specific Psu class"""

    def __init__(self, psu_index):
        PsuBase.__init__(self)
        self.index = psu_index
        self.psu_path = "/sys/devices/platform/pddf.cpld/"
        self.psu_presence = "psu{}_prs"
        self.psu_oper_status = "psu{}_status"
        self.i2c_num = PSU_I2C_MAPPING[self.index]["num"]
        self.i2c_addr = PSU_I2C_MAPPING[self.index]["addr"]
        self.hwmon_path = HWMON_PATH.format(self.i2c_num, self.i2c_addr)
        self.eeprom_addr = PSU_EEPROM_PATH.format(self.i2c_num, PSU_I2C_MAPPING[self.index]["eeprom_addr"])
        PsuBase.__init__(self)

    def __read_txt_file(self, file_path):
        try:
            with open(file_path, 'r') as fd:
                data = fd.read()
                return data.strip()
        except IOError:
            pass
        return ""

    def __search_file_by_contain(self, directory, search_str, file_start):
        for dirpath, dirnames, files in os.walk(directory):
            for name in files:
                file_path = os.path.join(dirpath, name)
                if name.startswith(file_start) and search_str in self.__read_txt_file(file_path):
                    return file_path
        return None

    def read_fru(self, path, attr_type):
        if os.path.exists(path):
            with open(path, 'rb') as f:
                all_info = f.read()
            all_info = str(all_info)[46:].replace("\\", "-")

            info_len_hex_list = re.findall(r"-xc(\w)", all_info)[:-1]
            start_index = 4
            info_list = list()
            for info_len in info_len_hex_list:
                info_len = int(info_len, 16)
                info_list.append(all_info[start_index:start_index + info_len].strip())
                start_index += info_len + 4
            try:
                return info_list[int(attr_type) - 1]
            except IndexError as E:
                print("[PSU] has error: %s" % E)
                return "N/A"
        else:
            print("[PSU] Can't find path to eeprom : %s" % path)
            return SyntaxError

    def get_voltage(self):
        """
        Retrieves current PSU voltage output
        Returns:
            A float number, the output voltage in volts,
            e.g. 12.1
        """
        psu_voltage = 0.0
        voltage_name = "in{}_input"
        voltage_label = "vout1"

        vout_label_path = self.__search_file_by_contain(
            self.hwmon_path, voltage_label, "in")
        if vout_label_path:
            dir_name = os.path.dirname(vout_label_path)
            basename = os.path.basename(vout_label_path)
            in_num = ''.join(list(filter(str.isdigit, basename)))
            vout_path = os.path.join(
                dir_name, voltage_name.format(in_num))
            vout_val = self.__read_txt_file(vout_path)
            psu_voltage = float(vout_val) / 1000

        return psu_voltage

    def get_current(self):
        """
        Retrieves present electric current supplied by PSU
        Returns:
            A float number, the electric current in amperes, e.g 15.4
        """
        psu_current = 0.0
        current_name = "curr{}_input"
        current_label = "iout1"

        curr_label_path = self.__search_file_by_contain(
            self.hwmon_path, current_label, "cur")
        if curr_label_path:
            dir_name = os.path.dirname(curr_label_path)
            basename = os.path.basename(curr_label_path)
            cur_num = ''.join(list(filter(str.isdigit, basename)))
            cur_path = os.path.join(
                dir_name, current_name.format(cur_num))
            cur_val = self.__read_txt_file(cur_path)
            psu_current = float(cur_val) / 1000

        return psu_current

    def get_power(self):
        """
        Retrieves current energy supplied by PSU
        Returns:
            A float number, the power in watts, e.g. 302.6
        """
        psu_power = 0.0
        current_name = "power{}_input"
        current_label = "pout1"

        pw_label_path = self.__search_file_by_contain(
            self.hwmon_path, current_label, "power")
        if pw_label_path:
            dir_name = os.path.dirname(pw_label_path)
            basename = os.path.basename(pw_label_path)
            pw_num = ''.join(list(filter(str.isdigit, basename)))
            pw_path = os.path.join(
                dir_name, current_name.format(pw_num))
            pw_val = self.__read_txt_file(pw_path)
            psu_power = float(pw_val) / 1000000

        return psu_power

    def get_powergood_status(self):
        """
        Retrieves the powergood status of PSU
        Returns:
            A boolean, True if PSU has stablized its output voltages and passed all
            its internal self-tests, False if not.
        """
        return self.get_status()

    def set_status_led(self, color):
        """
        Sets the state of the PSU status LED
        Args:
            color: A string representing the color with which to set the PSU status LED
                   Note: Only support green and off
        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        # Hardware not supported
        return False

    def get_status_led(self):
        """
        Gets the state of the PSU status LED
        Returns:
            A string, one of the predefined STATUS_LED_COLOR_* strings above
        """
        # Hardware not supported
        return self.STATUS_LED_COLOR_OFF

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
        psu_location = ["R", "L"]
        presences_status = self.__read_txt_file(
            self.psu_path + self.psu_presence.format(psu_location[self.index])) or 0

        return int(presences_status) == 0

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        psu_location = ["R", "L"]
        power_status = self.__read_txt_file(
            self.psu_path + self.psu_oper_status.format(psu_location[self.index])) or 0

        return int(power_status) == 1

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        model = self.read_fru(self.eeprom_addr, TLV_ATTR_TYPE_MODEL)
        if not model:
            return "N/A"
        return model

    def get_serial(self):
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        serial = self.read_fru(self.eeprom_addr, TLV_ATTR_TYPE_SERIAL)
        if not serial:
            return "N/A"
        return serial
