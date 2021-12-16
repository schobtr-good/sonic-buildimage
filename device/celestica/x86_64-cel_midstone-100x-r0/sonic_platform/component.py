#!/usr/bin/env python

#############################################################################
# Celestica
#
# Component contains an implementation of SONiC Platform Base API and
# provides the components firmware management function
#
#############################################################################

try:
    from sonic_platform_base.component_base import ComponentBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

# Bus Path Config
FPGA_VERSION_PATH = "/sys/devices/platform/fpga-board/version"
SYSCPLD_VERSION_PATH = "/sys/devices/platform/sys_cpld/version"
SWCPLD1_VERSION_PATH = "/sys/bus/i2c/devices/i2c-10/10-0030/version"
SWCPLD2_VERSION_PATH = "/sys/bus/i2c/devices/i2c-10/10-0031/version"
SWCPLD3_VERSION_PATH = "/sys/bus/i2c/devices/i2c-10/10-0032/version"
SWCPLD4_VERSION_PATH = "/sys/bus/i2c/devices/i2c-10/10-0033/version"
COME_CPLD_VERSION_PATH = "/sys/bus/i2c/devices/i2c-10/10-0033/version"  # TODO need to change
BIOS_VERSION_PATH = "/sys/class/dmi/id/bios_version"


COMPONENT_NAME_LIST = ["FPGA", "SYSCPLD", "SWCPLD1", "SWCPLD2",
                       "SWCPLD3",  "SWCPLD4", "COME_CPLD", "Main_BIOS", "Backup_BIOS"]
COMPONENT_DES_LIST = ["Used for managering the CPU and expanding I2C channels",
                      "Used for managing the CPU",
                      "Used for managing QSFP+ ports (1-16)",
                      "Used for managing QSFP+ ports (17-32)",
                      "Used for managing QSFP+ ports (33-48)",
                      "Used for managing QSFP+ ports (49-64)",
                      "COME CPLD VERSION",
                      "Main basic Input/Output System",
                      "Backup basic Input/Output System"]

# Get the currently active bios
Get_Active_Bios = "i2cget -f -y 2 0x0d 0x23"


class Component(ComponentBase):
    """Platform-specific Component class"""

    DEVICE_TYPE = "component"

    def __init__(self, component_index):
        ComponentBase.__init__(self)
        self.index = component_index
        self._api_helper = APIHelper()
        self.name = self.get_name()

    def __get_bios_version(self):
        """
        Retrieves the BIOS firmware version
        """
        status, result = self._api_helper.run_command(Get_Active_Bios)
        active_bios = bin(int(result, 16))[-2:]
        if active_bios == "01":
            if self.name == "Main_BIOS":
                bios_version = self._api_helper.read_txt_file(BIOS_VERSION_PATH)
            else:
                bios_version = "na"

        else:
            if self.name == "Backup_BIOS":
                bios_version = self._api_helper.read_txt_file(BIOS_VERSION_PATH)
            else:
                bios_version = "NA"
        return bios_version

    def __get_cpld_version(self):
        """
        Get cpld version
        """
        version = "NA"
        if self.name == "SYSCPLD":
            version = self._api_helper.read_txt_file(SYSCPLD_VERSION_PATH)
        elif self.name == "SWCPLD1":
            version = self._api_helper.read_txt_file(SWCPLD1_VERSION_PATH)
        elif self.name == "SWCPLD2":
            version = self._api_helper.read_txt_file(SWCPLD2_VERSION_PATH)
        elif self.name == "SWCPLD3":
            version = self._api_helper.read_txt_file(SWCPLD3_VERSION_PATH)
        elif self.name == "SWCPLD4":
            version = self._api_helper.read_txt_file(SWCPLD4_VERSION_PATH)
        elif self.name == "COME_CPLD":
            version = self._api_helper.read_txt_file(COME_CPLD_VERSION_PATH)
        return version

    def __get_fpga_version(self):
        """
        Get fpga version
        """
        version = self._api_helper.read_txt_file(FPGA_VERSION_PATH)
        return version

    def get_name(self):
        """
        Retrieves the name of the component
         Returns:
            A string containing the name of the component
        """
        return COMPONENT_NAME_LIST[self.index]

    def get_description(self):
        """
        Retrieves the description of the component
            Returns:
            A string containing the description of the component
        """
        return COMPONENT_DES_LIST[self.index]

    def get_firmware_version(self):
        """
        Retrieves the firmware version of module
        Returns:
            string: The firmware versions of the module
        """
        if "BIOS" in self.name:
            fw_version = self.__get_bios_version()
        elif "CPLD" in self.name:
            fw_version = self.__get_cpld_version()
        else:
            fw_version = self.__get_fpga_version()
        return fw_version

    @staticmethod
    def install_firmware(image_path):
        """
        Install firmware to module
        Args:
            image_path: A string, path to firmware image
        Returns:
            A boolean, True if install successfully, False if not
        """
        # Not Support
        return False

    @staticmethod
    def update_firmware(image_path):
        # Not Support
        return False
