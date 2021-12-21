#!/usr/bin/env python

#############################################################################
# Celestica Blackstone
#
# Platform and model specific sfp subclass, inherits from the base class,
# and provides the followings:
# - sfputil show presence
#############################################################################

import os


try:
    import time
    import subprocess
    from os import path
    from sonic_sfp.sfputilbase import SfpUtilBase
except ImportError as e:
    raise ImportError('%s - required module not found' % str(e))

GETREG_FILE = 'getreg'
SETREG_FILE = 'setreg'
QSFP_PRS_FILE = 'qsfp_modprsL'
QSFP_LPMODE_FILE = 'qsfp_lpmode'

PORT_ID_SEL_REG = '0x10'
PORT_CTRL_REG = '0x11'

SW_CPLD1_PATH = "/sys/bus/i2c/devices/10-0030/CPLD1/"
SW_CPLD2_PATH = "/sys/bus/i2c/devices/11-0031/CPLD2/"

ACTIVE_LOW = 0
ACTIVE_HIGH = 1

PORT_START = 1
PORT_END = 32

SW_CPLD2_PORT_START = 17

QSFPDD_PORT_START = 1
QSFPDD_PORT_END = 32
EEPROM_OFFSET = 15

PORT_INFO_PATH = '/sys/devices/platform/cls-xcvr'


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

    def _get_port_name(self, port_num):
        return 'QSFPDD{}'.format(port_num)

    def _read_val(self, path):
        ret = ''
        try:
            with open(path, 'r') as f:
                ret = f.readline()
        except IOError as e:
            print('Error: unable to open file: %s' % str(e))
        return ret

    def _run_command(self, command):
        status = False
        output = ""
        try:
            p = subprocess.Popen(
                command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            raw_data, err = p.communicate()
            if err == '' or err == b'':
                status, output = True, raw_data.decode(
                    "utf-8") if type(raw_data) is bytes else raw_data
        except Exception:
            pass
        return status, output

    def _get_cpld_path(self, port_num):
        return SW_CPLD1_PATH if port_num < SW_CPLD2_PORT_START else SW_CPLD2_PATH

    def _set_reg(self, port_num, reg, val):
        set_reg_file = path.join(self._get_cpld_path(port_num), SETREG_FILE)
        # return self._write_val(set_reg_file, '{} {}'.format(reg, val))
        return self._run_command('echo {} {} > {}'.format(reg, val, set_reg_file))

    def _get_reg(self, port_num, reg):
        get_reg_file = path.join(self._get_cpld_path(port_num), GETREG_FILE)
        # self._write_val(get_reg_file, reg)
        # return self._read_val(get_reg_file)
        return self._run_command('echo {0} > {1} | cat {1}'.format(reg, get_reg_file))

    def get_presence(self, port_num):
        # Get port_name
        port_name = self._get_port_name(port_num)

        # Read status
        status = self._read_val(path.join(
            PORT_INFO_PATH, port_name, QSFP_PRS_FILE))

        # Module present is active low
        return int(status) == ACTIVE_LOW

    def get_low_power_mode(self, port_num):
        # Get port_name
        port_name = self._get_port_name(port_num)

        # Read status
        status = self._read_val(path.join(
            self.PORT_INFO_PATH, port_name, QSFP_LPMODE_FILE))

        # LPmode is active high
        return int(status, 16) == ACTIVE_HIGH

    def set_low_power_mode(self, port_num, lpmode):

        # Set selected port
        sel_port_num = port_num - \
            (SW_CPLD2_PORT_START - 1) if port_num >= SW_CPLD2_PORT_START else port_num
        self._set_reg(port_num, PORT_ID_SEL_REG, hex(sel_port_num))

        status, port_ctrl_val = self._get_reg(port_num, PORT_CTRL_REG)
        if not status:
            return False

        ctrl_val = (17 | int(port_ctrl_val, 16)) if lpmode else (
            16 & int(port_ctrl_val, 16))

        return self._set_reg(port_num, PORT_CTRL_REG, ctrl_val)

    def reset(self, port_num):

        # Set selected port
        sel_port_num = port_num - \
            (SW_CPLD2_PORT_START - 1) if port_num >= SW_CPLD2_PORT_START else port_num
        self._set_reg(port_num, PORT_ID_SEL_REG, hex(sel_port_num))

        status, port_ctrl_val = self._get_reg(port_num, PORT_CTRL_REG)
        if not status:
            return False

        reset_ctrl_val = (1 & int(port_ctrl_val, 16))
        set_reset = self._set_reg(port_num, PORT_CTRL_REG, reset_ctrl_val)
        time.sleep(1)
        unreset_ctrl_val = (16 | int(port_ctrl_val, 16))
        unset_reset = self._set_reg(port_num, PORT_CTRL_REG, unreset_ctrl_val)

        return set_reset[0] & unset_reset[0]

    def get_transceiver_change_event(self, timeout=0):
        '''
        TBD: When the feature request.
        '''
        raise NotImplementedError
