#!/usr/bin/env python

import subprocess
import sys
import re

try:
    from sonic_psu.psu_base import PsuBase
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

IPMI_RAW_SS_READ_CMD = "ipmitool raw 0x4 0x2d"
PSU1_SS_ID = "0x3a"
PSU2_SS_ID = "0x3b"
NUM_PSU = 2


class PsuUtil(PsuBase):
    """Platform-specific PSUutil class"""

    def __init__(self):
        PsuBase.__init__(self)

    def _run_command(self, command):
        proc = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE)
        (out, err) = proc.communicate()

        if proc.returncode != 0:
            sys.exit(proc.returncode)

        return out

    def _find_value(self, in_string):
        result = re.search("^.+ ([0-9a-f]{2}) .+$", in_string)
        if result:
            return result.group(1)
        else:
            return result

    def get_num_psus(self):
        """
        Retrieves the number of PSUs available on the device
        :return: An integer, the number of PSUs available on the device
        """
        return NUM_PSU

    def get_psu_status(self, index):
        """
        Retrieves the oprational status of power supply unit (PSU) defined
                by 1-based index <index>
        :param index: An integer, 1-based index of the PSU of which to query status
        :return: Boolean, True if PSU is operating properly, False if PSU is faulty
        """
        psu_id = PSU1_SS_ID if index == 1 else PSU2_SS_ID
        res_string = self._run_command(IPMI_RAW_SS_READ_CMD + ' ' + psu_id)
        status_byte = self._find_value(res_string)

        if status_byte is None:
            return False

        failure_detected = (int(status_byte, 16) >> 1) & 1
        input_lost = (int(status_byte, 16) >> 3) & 1
        
        return not (failure_detected or input_lost)

    def get_psu_presence(self, index):
        """
        Retrieves the presence status of power supply unit (PSU) defined
                by 1-based index <index>
        :param index: An integer, 1-based index of the PSU of which to query status
        :return: Boolean, True if PSU is plugged, False if not
        """
        psu_id = PSU1_SS_ID if index == 1 else PSU2_SS_ID
        res_string = self._run_command(IPMI_RAW_SS_READ_CMD + ' ' + psu_id)
        status_byte = self._find_value(res_string)

        if status_byte is None:
            return False

        presence = (int(status_byte, 16) >> 0) & 1
        return presence
