#!/usr/bin/env python

#############################################################################
# Celestica Blackstone
#
# Platform and model specific sfp subclass, inherits from the base class,
# and provides the followings:
# - sfputil show presence
#############################################################################

try:
    from sonic_sfp.sfputilbase import SfpUtilBase
except ImportError as e:
    raise ImportError('%s - required module not found' % str(e))

PORT_START = 1
PORT_END = 32

QSFPDD_PORT_START = 1
QSFPDD_PORT_END = 32
EEPROM_OFFSET = 12

MEDIA_TYPE_OFFSET = 0
MEDIA_TYPE_WIDTH = 1

QSFP_DD_MODULE_ENC_OFFSET = 3
QSFP_DD_MODULE_ENC_WIDTH = 1

SFP_TYPE_LIST = [
    '03'  # SFP/SFP+/SFP28 and later
]
QSFP_TYPE_LIST = [
    '0c',  # QSFP
    '0d',  # QSFP+ or later
    '11'  # QSFP28 or later
]
QSFP_DD_TYPE_LIST = [
    '18'  # QSFP_DD Type
]
OSFP_TYPE_LIST = [
    '19'  # OSFP 8X Type
]


class SfpUtil(SfpUtilBase):
    '''Platform-specific SfpUtil class'''

    _port_to_eeprom_mapping = {}
    _port_to_i2cbus_mapping = {}

    def __init__(self):
        # Override port_to_eeprom_mapping for class initialization
        eeprom_path = '/sys/bus/i2c/devices/i2c-{0}/{0}-0050/eeprom'

        for x in range(PORT_START, PORT_END+1):
            self.port_to_i2cbus_mapping[x] = (x + EEPROM_OFFSET) - 1
            self.port_to_eeprom_mapping[x] = eeprom_path.format(
                self.port_to_i2cbus_mapping[x])
        SfpUtilBase.__init__(self)

    @property
    def port_start(self):
        return PORT_START

    @property
    def port_end(self):
        return PORT_END

    @property
    def osfp_ports(self):
        return list(range(QSFPDD_PORT_START, QSFPDD_PORT_END + 1))

    @property
    def port_to_eeprom_mapping(self):
        return self._port_to_eeprom_mapping

    @property
    def port_to_i2cbus_mapping(self):
        return self._port_to_i2cbus_mapping

    def get_media_type(self, port_num):
        """
        Reads optic eeprom byte to determine media type inserted
        """
        eeprom_raw = self._read_eeprom_bytes(
            self.port_to_eeprom_mapping[port_num], MEDIA_TYPE_OFFSET, MEDIA_TYPE_WIDTH)
        if eeprom_raw is not None:
            if eeprom_raw[0] in SFP_TYPE_LIST:
                sfp_type = 'SFP'
            elif eeprom_raw[0] in QSFP_TYPE_LIST:
                sfp_type = 'QSFP'
            elif eeprom_raw[0] in QSFP_DD_TYPE_LIST:
                sfp_type = 'QSFP_DD'
            else:
                # Set native port type if EEPROM type is not recognized/readable
                sfp_type = 'QSFP_DD'
        else:
            sfp_type = 'QSFP_DD'

        return sfp_type

    def get_presence(self, port_num):
        """
        :param port_num: Integer, index of physical port
        :returns: Boolean, True if tranceiver is present, False if not
        """
        return True

    def get_low_power_mode(self, port_num):
        """
        :param port_num: Integer, index of physical port
        :returns: Boolean, True if low-power mode enabled, False if disabled
        """
        if self.get_media_type(port_num) == 'QSFP_DD':
            lpmode = self._read_eeprom_bytes(
                self.port_to_eeprom_mapping[port_num], QSFP_DD_MODULE_ENC_OFFSET, QSFP_DD_MODULE_ENC_WIDTH)
            if lpmode and int(lpmode[0]) >> 1 == 1:
                return True

        return False

    def set_low_power_mode(self, port_num, lpmode):
        """
        :param port_num: Integer, index of physical port
        :param lpmode: Boolean, True to enable low-power mode, False to disable it
        :returns: Boolean, True if low-power mode set successfully, False if not
        """
        if self.get_media_type(port_num) == 'QSFP_DD':
            write_val = 0x10 if lpmode is True else 0x0
            return self._write_eeprom_bytes(self.port_to_eeprom_mapping[port_num], 26, 1, bytearray([write_val]))

        return False

    def reset(self, port_num):
        """
        :param port_num: Integer, index of physical port
        :returns: Boolean, True if reset successful, False if not
        """
        # TBD
        return False

    def get_transceiver_change_event(self, timeout=0):
        """
        :param timeout in milliseconds. The method is a blocking call. When timeout is
         zero, it only returns when there is change event, i.e., transceiver plug-in/out
         event. When timeout is non-zero, the function can also return when the timer expires.
         When timer expires, the return status is True and events is empty.
        :returns: (status, events)
        :status: Boolean, True if call successful and no system level event/error occurred,
         False if call not success or system level event/error occurred.
        :events: dictionary for physical port index and the SFP status,
         status='1' represent plug in, '0' represent plug out like {'0': '1', '31':'0'}
         when it comes to system level event/error, the index will be '-1',
         and status can be 'system_not_ready', 'system_become_ready', 'system_fail',
         like {'-1':'system_not_ready'}.
        """
        # TBD
        return
