#!/usr/bin/env python

#############################################################################
# Celestica Blackstone
#
# Platform and model specific sfp subclass, inherits from the base class,
# and provides the followings:
# - sfputil show presence
#############################################################################

try:
    import time
    from os import path
    from sonic_sfp.sfputilbase import SfpUtilBase
except ImportError as e:
    raise ImportError('%s - required module not found' % str(e))


class SfpUtil(SfpUtilBase):
    '''Platform-specific SfpUtil class'''

    PORT_START = 1
    PORT_END = 32

    QSFPDD_PORT_START = 1
    QSFPDD_PORT_END = 32
    EEPROM_OFFSET = 15

    PORT_INFO_PATH = '/sys/devices/platform/cls-xcvr'
    _port_to_eeprom_mapping = {}
    _port_to_i2cbus_mapping = {}

    @property
    def port_start(self):
        return self.PORT_START

    @property
    def port_end(self):
        return self.PORT_END

    @property
    def osfp_ports(self):
        return list(range(self.QSFPDD_PORT_START, self.QSFPDD_PORT_END + 1))

    @property
    def port_to_eeprom_mapping(self):
        return self._port_to_eeprom_mapping

    @property
    def port_to_i2cbus_mapping(self):
        return self._port_to_i2cbus_mapping

    def get_port_name(self, port_num):
        return 'QSFPDD{}'.format(port_num) if port_num in self.osfp_ports else 'SFP+{}' + str(port_num - self.QSFPDD_PORT_END)

    def __init__(self):
        # Override port_to_eeprom_mapping for class initialization
        eeprom_path = '/sys/bus/i2c/devices/i2c-{0}/{0}-0050/eeprom'

        for x in range(self.PORT_START, self.PORT_END+1):
            self.port_to_i2cbus_mapping[x] = (x + self.EEPROM_OFFSET) - 1
            self.port_to_eeprom_mapping[x] = eeprom_path.format(
                self.port_to_i2cbus_mapping[x])
        SfpUtilBase.__init__(self)

    def _read_val(self, path):
        ret = ''
        try:
            with open(path, 'r') as f:
                ret = f.readline()
        except IOError as e:
            print('Error: unable to open file: %s' % str(e))
        return ret

    def _write_val(self, path, val):
        try:
            with open(path, 'w') as f:
                f.write(val)
        except IOError as e:
            print('Error: unable to write file: %s' % str(e))
            return False
        return True

    def get_presence(self, port_num):

        # Check for invalid port_num
        if port_num not in list(range(self.port_start, self.port_end + 1)):
            return False

        # Get path for access port presence status
        port_name = self.get_port_name(port_num)
        sysfs_filename = 'qsfp_modprsL' if port_num in self.osfp_ports else 'sfp_modabs'

        # Read status
        status = self._read_val(path.join(
            self.PORT_INFO_PATH, port_name, sysfs_filename))

        # Module present is active low
        return True if int(status) == 0 else False

    def get_low_power_mode(self, port_num):

        # Check for invalid QSFP-DD port_num
        if port_num not in self.osfp_ports:
            return False

        # Read status
        port_name = self.get_port_name(port_num)
        status = self._read_val(path.join(
            self.PORT_INFO_PATH, port_name, 'qsfp_lpmode'))

        # LPmode is active high
        return True if int(status, 16) == 1 else False

    def set_low_power_mode(self, port_num, lpmode):

        # Check for invalid QSFP-DD port_num
        if port_num not in self.osfp_ports:
            return False

        return self._write_val(path.join(self.PORT_INFO_PATH, self.get_port_name(port_num), 'qsfp_lpmode'), hex(lpmode))

    def reset(self, port_num):

        # Check for invalid QSFP-DD port_num
        if port_num not in self.osfp_ports:
            return False

        sysfs_path = path.join(self.PORT_INFO_PATH,
                               self.get_port_name(port_num), 'qsfp_resetL')

        self._write_val(sysfs_path, hex(0))

        # Sleep 1 second to allow it to settle
        time.sleep(1)

        # Flip the bit back high and write back to the register to take port out of reset
        self._write_val(sysfs_path, hex(1))

        return True

    def get_transceiver_change_event(self, timeout=0):
        '''
        TBD: When the feature request.
        '''
        raise NotImplementedError
