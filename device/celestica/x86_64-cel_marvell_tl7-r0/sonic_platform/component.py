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

FPGA_VERSION_PATH = "/sys/devices/platform/fpga-sys/version"
FPGA_SW_VERSION_PATH = "/sys/devices/platform/Marvell_Switch_FPGA/FPGA/version"
CTRL_CPLD_VERSION_PATH = "/sys/devices/platform/ctrl_cpld/version"
COME_CPLD_VERSION_PATH = "/sys/devices/platform/come_cpld/version"
BIOS_VERSION_PATH = "/sys/class/dmi/id/bios_version"
BMC_VERSION_CMD = "ipmitool raw 0x32 0x8f 0x08 0x01"
FAN_CPLD_VERSION_CMD = "ipmitool raw 0x3a 0x64 02 01 00"
COMPONENT_NAME_LIST = ["BMC", "BIOS", "CTRL_CPLD", "COME_CPLD", "FAN_CPLD",
                       "LED_CPLD", "BASE_FPGA", "SW_FPGA"]
COMPONENT_DES_LIST = ["BMC", "BIOS", "CTRL_CPLD", "COME_CPLD", "FAN_CPLD",
                      "LED_CPLD", "BASE_FPGA", "SW_FPGA"]


class Component(ComponentBase):
    """Platform-specific Component class"""

    DEVICE_TYPE = "component"

    def __init__(self, component_index):
        ComponentBase.__init__(self)
        self.index = component_index
        self._api_helper = APIHelper()
        self.name = self.get_name()

    def _isfloat(self, num):
        try:
            float(num)
            return True
        except ValueError:
            return False

    def __get_bios_version(self):
        # Retrieves the BIOS firmware version
        with open(BIOS_VERSION_PATH, 'r') as fd:
            bios_version = fd.read()
            return bios_version.strip()

    def __get_cpld_version(self):
        if self.name == "CTRL_CPLD":
            try:
                with open(CTRL_CPLD_VERSION_PATH, 'r') as fd:
                    CTRL_CPLD_version = fd.read()
                    return CTRL_CPLD_version.strip()
            except Exception as e:
                return None
        elif self.name == "COME_CPLD":
            try:
                with open(COME_CPLD_VERSION_PATH, 'r') as fd:
                    bcpld_version = fd.read()
                    return bcpld_version.strip()
            except Exception as e:
                return None
        elif self.name == "FAN_CPLD":
            status, ver = self._api_helper.run_command(FAN_CPLD_VERSION_CMD)
            version = int(ver.strip(), 16)
            return str(version)
        else:
            return None

    def __get_fpga_version(self):
        # Retrieves the FPGA firmware version
        version_path = FPGA_VERSION_PATH if "BASE" in self.name else FPGA_SW_VERSION_PATH
        try:
            with open(version_path, 'r') as fd:
                version = fd.read()
                version_list = version.strip().split("x")
                fpga_version = version_list[1] if len(
                    version_list) > 1 else version_list[0]
                return float(fpga_version) if self._isfloat(fpga_version) else fpga_version.strip()
        except Exception as e:
            print(e)
            return None

    def __get_bmc_version(self):
        # Retrieves the BMC firmware version
        status, ver = self._api_helper.run_command(BMC_VERSION_CMD)
        return ver.strip()

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
        fw_version = None

        if "BIOS" in self.name:
            fw_version = self.__get_bios_version()
        elif "CPLD" in self.name:
            fw_version = self.__get_cpld_version()
        elif "FPGA" in self.name:
            fw_version = self.__get_fpga_version()
        elif "BMC" in self.name:
            version = self.__get_bmc_version()
            version_1 = int(version.strip().split(" ")[0], 16)
            version_2 = int(version.strip().split(" ")[1], 16)
            fw_version = "%s.%s" % (version_1, version_2)

        return fw_version

    def get_available_firmware_version(self, image_path):
        """
        Retrieves the available firmware version of the component
        Note: the firmware version will be read from image
        Args:
            image_path: A string, path to firmware image
        Returns:
            A string containing the available firmware version of the component
        """
        return "N/A"

    def install_firmware(self, image_path):
        """
        Installs firmware to the component
        This API performs firmware installation only: this may/may not be the same as firmware update.
        In case platform component requires some extra steps (apart from calling Low Level Utility)
        to load the installed firmware (e.g, reboot, power cycle, etc.) - this must be done manually by user
        Note: in case immediate actions are required to complete the component firmware update
        (e.g., reboot, power cycle, etc.) - will be done automatically by API and no return value provided
        Args:
            image_path: A string, path to firmware image
        Returns:
            A boolean, True if install was successful, False if not
        """
        return False

    def update_firmware(self, image_path):
        """
        Updates firmware of the component
        This API performs firmware update: it assumes firmware installation and loading in a single call.
        In case platform component requires some extra steps (apart from calling Low Level Utility)
        to load the installed firmware (e.g, reboot, power cycle, etc.) - this will be done automatically by API
        Args:
            image_path: A string, path to firmware image
        Raises:
            RuntimeError: update failed
        """
        raise RuntimeError

    def auto_update_firmware(self, image_path, boot_type):
        """
        Updates firmware of the component
        This API performs firmware update automatically based on boot_type: it assumes firmware installation
        and/or creating a loading task during the reboot, if needed, in a single call.
        In case platform component requires some extra steps (apart from calling Low Level Utility)
        to load the installed firmware (e.g, reboot, power cycle, etc.) - this will be done automatically during the reboot.
        The loading task will be created by API.
        Args:
            image_path: A string, path to firmware image
            boot_type: A string, reboot type following the upgrade
                         - none/fast/warm/cold
        Returns:
            Output: A return code
                return_code: An integer number, status of component firmware auto-update
                    - return code of a positive number indicates successful auto-update
                        - status_installed = 1
                        - status_updated = 2
                        - status_scheduled = 3
                    - return_code of a negative number indicates failed auto-update
                        - status_err_boot_type = -1
                        - status_err_image = -2
                        - status_err_unknown = -3
        Raises:
            RuntimeError: auto-update failure cause
        """
        raise RuntimeError
