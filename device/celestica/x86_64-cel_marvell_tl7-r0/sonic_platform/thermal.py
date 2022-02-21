#############################################################################
# Celestica
#
# Thermal contains an implementation of SONiC Platform Base API and
# provides the thermal device status which are available in the platform
#
#############################################################################


try:
    from sonic_platform_base.thermal_base import ThermalBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

IPMI_SENSOR_NETFN = "0x04"
IPMI_SS_READ_CMD = "0x2D {}"
IPMI_SS_THRESHOLD_CMD = "0x27 {}"
HIGH_TRESHOLD_SET_KEY = "unc"
NOT_AVALIABLE = "N/A"


class Thermal(ThermalBase):
    """Platform-specific Thermal class"""

    def __init__(self, thermal_index):
        ThermalBase.__init__(self)
        self._api_helper = APIHelper()

        self.index = thermal_index
        self.THERMAL_LIST = [
            ('TEMP_CPU', 'CPU Temperature Sensor', '0x05'),
            ('TEMP_FB_U52', 'Fan Board Temperature Sensor U52', '0x01'),
            ('TEMP_FB_U17', 'Fan Board Temperature Sensor U17',  '0x02'),
            ('TEMP_DIMM0', 'Switchboard Right Inlet Temperature Sensor', '0x62'),
            ('TEMP_DIMM1', 'Baseboard Temperature Sensor', '0x63'),
            ('TEMP_SW_Internal', 'ASIC Internal Temperature Sensor', '0x06'),
            ('TEMP_SW_U2', 'IR3595 Chip Left Temperature Sensor', '0x04'),
            ('TEMP_SW_U52', 'IR3595 Chip Right Temperature Sensor', '0x07'),
            ('TEMP_SW_U16', 'IR3584 Chip Temperature Sensor', '0x08'),
        ]
        self.sensor_id = self.THERMAL_LIST[self.index][0]
        self.sensor_des = self.THERMAL_LIST[self.index][1]
        self.sensor_reading_addr = self.THERMAL_LIST[self.index][2]

    def get_temperature(self):
        """
        Retrieves current temperature reading from thermal
        Returns:
            A float number of current temperature in Celsius up to nearest thousandth
            of one degree Celsius, e.g. 30.125
        """
        temperature = 0.0
        status, raw_ss_read = self._api_helper.ipmi_raw(
            IPMI_SENSOR_NETFN, IPMI_SS_READ_CMD.format(self.sensor_reading_addr))
        if status and len(raw_ss_read.split()) > 0:
            ss_read = raw_ss_read.split()[0]
            temperature = float(int(ss_read, 16))
        return temperature

    def get_high_threshold(self):
        """
        Retrieves the high threshold temperature of thermal
        Returns:
            A float number, the high threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        high_threshold = 0.0
        status, raw_up_thres_read = self._api_helper.ipmi_raw(
            IPMI_SENSOR_NETFN, IPMI_SS_THRESHOLD_CMD.format(self.sensor_reading_addr))
        if status and len(raw_up_thres_read.split()) > 6:
            ss_read = raw_up_thres_read.split()[4]
            high_threshold = float(int(ss_read, 16))
        return high_threshold if high_threshold != 0.0 else self.get_high_critical_threshold()

    def get_low_threshold(self):
        """
        Retrieves the low threshold temperature of thermal
        Returns:
            A float number, the low threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        return NOT_AVALIABLE

    def set_high_threshold(self, temperature):
        """
        Sets the high threshold temperature of thermal
        Args :
            temperature: A float number up to nearest thousandth of one degree Celsius,
            e.g. 30.125
        Returns:
            A boolean, True if threshold is set successfully, False if not
        """
        return False  # Controlled by BMC

    def set_low_threshold(self, temperature):
        """
        Sets the low threshold temperature of thermal
        Args :
            temperature: A float number up to nearest thousandth of one degree Celsius,
            e.g. 30.125
        Returns:
            A boolean, True if threshold is set successfully, False if not
        """
        return False  # Controlled by BMC

    def get_high_critical_threshold(self):
        """
        Retrieves the high critical threshold temperature of thermal
        Returns:
            A float number, the high critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        high_critical_threshold = 0.0
        status, raw_up_thres_read = self._api_helper.ipmi_raw(
            IPMI_SENSOR_NETFN, IPMI_SS_THRESHOLD_CMD.format(self.sensor_reading_addr))
        if status and len(raw_up_thres_read.split()) > 6:
            ss_read = raw_up_thres_read.split()[5]
            high_critical_threshold = float(int(ss_read, 16))
        return high_critical_threshold if high_critical_threshold != 0.0 else NOT_AVALIABLE

    ##############################################################
    ###################### Device methods ########################
    ##############################################################

    def get_name(self):
        """
        Retrieves the name of the thermal device
            Returns:
            string: The name of the thermal device
        """
        return self.THERMAL_LIST[self.index][0]

    def get_presence(self):
        """
        Retrieves the presence of the device
        Returns:
            bool: True if device is present, False if not
        """
        return True if self.get_temperature() > 0 else False

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        return self.sensor_des

    def get_serial(self):
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        return "Unknown"

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        return self.get_presence()
