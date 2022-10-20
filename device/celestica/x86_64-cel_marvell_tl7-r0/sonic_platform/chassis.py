#!/usr/bin/env python

#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the Chassis information which are available in the platform
#
#############################################################################

try:
    from sonic_platform_base.chassis_base import ChassisBase
    from sonic_platform_base.sonic_sfp.sfputilhelper import SfpUtilHelper
    from sonic_py_common import device_info
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

NUM_COMPONENT = 8
NUM_FAN_TRAY = 7
NUM_THERMAL = 9
NUM_FAN = 2
NUM_PSU = 2
NUM_SFP = 18

RESET_REGISTER = "0xA106"
HOST_REBOOT_CAUSE_PATH = "/host/reboot-cause/"
REBOOT_CAUSE_FILE = "reboot-cause.txt"
GETREG_PATH = "/sys/devices/platform/ctrl_cpld/getreg"


class Chassis(ChassisBase):
    """Platform-specific Chassis class"""

    def __init__(self):
        ChassisBase.__init__(self)

        self._api_helper = APIHelper()
        self.sfp_module_initialized = False
        self._is_host = self._api_helper.is_host()

        self._initialize_eeprom()
        self._initialize_components()
        self._initialize_fan()
        self._initialize_psu()
        self._initialize_thermals()

    def _initialize_eeprom(self):
        from sonic_platform.eeprom import Tlv
        self._eeprom = Tlv()

    def _initialize_components(self):
        from sonic_platform.component import Component
        for index in range(0, NUM_COMPONENT):
            component = Component(index)
            self._component_list.append(component)

    def _initialize_fan(self):
        from sonic_platform.fan import Fan
        for fant_index in range(0, NUM_FAN_TRAY):
            for fan_index in range(0, NUM_FAN):
                fan = Fan(fant_index, fan_index)
                self._fan_list.append(fan)

    def _initialize_psu(self):
        from sonic_platform.psu import Psu
        for index in range(0, NUM_PSU):
            psu = Psu(index)
            self._psu_list.append(psu)

    def _initialize_thermals(self):
        from sonic_platform.thermal import Thermal
        for index in range(0, NUM_THERMAL):
            thermal = Thermal(index)
            self._thermal_list.append(thermal)

    def _initialize_sfp(self):
        sfputil_helper = SfpUtilHelper()
        port_config_file_path = device_info.get_path_to_port_config_file()
        sfputil_helper.read_porttab_mappings(port_config_file_path, 0)

        from sonic_platform.sfp import Sfp
        for index in range(0, NUM_SFP):
            sfp = Sfp(index, sfputil_helper.logical[index])
            self._sfp_list.append(sfp)
        self.sfp_module_initialized = True

    def get_base_mac(self):
        """
        Retrieves the base MAC address for the chassis
        Returns:
            A string containing the MAC address in the format
            'XX:XX:XX:XX:XX:XX'
        """
        return self._eeprom.get_mac()

    def get_serial_number(self):
        """
        Retrieves the hardware serial number for the chassis
        Returns:
            A string containing the hardware serial number for this chassis.
        """
        return self._eeprom.get_serial()

    def get_system_eeprom_info(self):
        """
        Retrieves the full content of system EEPROM information for the chassis
        Returns:
            A dictionary where keys are the type code defined in
            OCP ONIE TlvInfo EEPROM format and values are their corresponding
            values.
        """
        return self._eeprom.get_eeprom()

    def get_reboot_cause(self):
        """
        Retrieves the cause of the previous reboot
        Returns:
            A tuple (string, string) where the first element is a string
            containing the cause of the previous reboot. This string must be
            one of the predefined strings in this class. If the first string
            is "REBOOT_CAUSE_HARDWARE_OTHER", the second string can be used
            to pass a description of the reboot cause.

            REBOOT_CAUSE_POWER_LOSS = "Power Loss"
            REBOOT_CAUSE_THERMAL_OVERLOAD_CPU = "Thermal Overload: CPU"
            REBOOT_CAUSE_THERMAL_OVERLOAD_ASIC = "Thermal Overload: ASIC"
            REBOOT_CAUSE_THERMAL_OVERLOAD_OTHER = "Thermal Overload: Other"
            REBOOT_CAUSE_INSUFFICIENT_FAN_SPEED = "Insufficient Fan Speed"
            REBOOT_CAUSE_WATCHDOG = "Watchdog"
            REBOOT_CAUSE_HARDWARE_OTHER = "Hardware - Other"
            REBOOT_CAUSE_NON_HARDWARE = "Non-Hardware"

        """
        reboot_cause_path = (HOST_REBOOT_CAUSE_PATH + REBOOT_CAUSE_FILE)
        sw_reboot_cause = self._api_helper.read_txt_file(
            reboot_cause_path) or "Unknown"
        hw_reboot_cause = self._api_helper.get_cpld_reg_value(
            GETREG_PATH, RESET_REGISTER)

        prev_reboot_cause = {
            '0x11': (self.REBOOT_CAUSE_POWER_LOSS, "The last reset is Power on reset"),
            '0x22': (self.REBOOT_CAUSE_NON_HARDWARE, "The last reset is soft-set CPU warm reset"),
            '0x33': (self.REBOOT_CAUSE_NON_HARDWARE, "The last reset is soft-set CPU cold reset"),
            '0x55': (self.REBOOT_CAUSE_NON_HARDWARE, "The last reset is CPU cold reset"),
            '0x66': (self.REBOOT_CAUSE_WATCHDOG, "The last reset is gpio watchdog reset.t"),
            '0x77': (self.REBOOT_CAUSE_NON_HARDWARE, "The last reset is power cycle reset"),
            '0x88': (self.REBOOT_CAUSE_WATCHDOG, "The last reset is hw watchdog reset")

        }.get(hw_reboot_cause, (self.REBOOT_CAUSE_HARDWARE_OTHER, 'Unknown reason'))

        if sw_reboot_cause != 'Unknown':
            prev_reboot_cause = (
                self.REBOOT_CAUSE_NON_HARDWARE, sw_reboot_cause)

        return prev_reboot_cause

    ##############################################################
    ####################### Other methods ########################
    ##############################################################

    def get_watchdog(self):
        """
        Retreives hardware watchdog device on this chassis
        Returns:
            An object derived from WatchdogBase representing the hardware
            watchdog device
        """
        if self._watchdog is None:
            from watchdog import Watchdog
            self._watchdog = Watchdog()

        return self._watchdog

    def get_thermal_manager(self):
        from thermal_manager import ThermalManager
        return ThermalManager

    ##############################################################
    ######################## SFP methods #########################
    ##############################################################

    def get_num_sfps(self):
        """
        Retrieves the number of sfps available on this chassis
        Returns:
            An integer, the number of sfps available on this chassis
        """
        if not self.sfp_module_initialized:
            self._initialize_sfp()

        return len(self._sfp_list)

    def get_all_sfps(self):
        """
        Retrieves all sfps available on this chassis
        Returns:
            A list of objects derived from SfpBase representing all sfps
            available on this chassis
        """
        if not self.sfp_module_initialized:
            self._initialize_sfp()

        return self._sfp_list

    def get_sfp(self, index):
        """
        Retrieves sfp represented by (1-based) index <index>
        Args:
            index: An integer, the index (1-based) of the sfp to retrieve.
            The index should be the sequence of a physical port in a chassis,
            starting from 1.
            For example, 1 for Ethernet0, 2 for Ethernet4 and so on.
        Returns:
            An object dervied from SfpBase representing the specified sfp
        """
        if not self.sfp_module_initialized:
            self._initialize_sfp()
        return super(Chassis, self).get_sfp(index-1)

    ##############################################################
    ###################### Device methods ########################
    ##############################################################

    def get_name(self):
        """
        Retrieves the name of the device
            Returns:
            string: The name of the device
        """
        return self._api_helper.hwsku

    def get_presence(self):
        """
        Retrieves the presence of the Chassis
        Returns:
            bool: True if Chassis is present, False if not
        """
        return True

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        return self._eeprom.get_pn()

    def get_serial(self):
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        return self.get_serial_number()

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        return True