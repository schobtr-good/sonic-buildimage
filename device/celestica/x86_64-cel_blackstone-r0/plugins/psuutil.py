#############################################################################
# Celestica Blackstone
#
# Platform-specific PSU status interface for SONiC
# provides the followings:
# - Number of PSUs
# - Operational status of PSUs
# - Presence status of PSUs
#############################################################################

try:
    import sys
    import subprocess
    from sonic_psu.psu_base import PsuBase
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

# BMC IPMI config
IPMI_RAW_COMMAND = "ipmitool raw"
IPMI_SENSOR_NETFN = "0x04"
IPMI_EVENT_CMD = "0x2b"
IPMI_SENSOR_MAPPING = {
    1: "9",
    2: "10"
}
IPMI_PSU_PRESENCE_BIT = 0
IPMI_PSU_FAILURE_BIT = 1
IPMI_PSU_INPUT_LOST_BIT = 3

# PSUs config
NUM_OF_PSUS = 2


class PsuUtil(PsuBase):
    """Platform-specific PSUutil class"""

    def __init__(self):
        PsuBase.__init__(self)

    def _run_command(self, command):
        proc = subprocess.Popen(
            command, shell=True, universal_newlines=True, stdout=subprocess.PIPE)
        (out, err) = proc.communicate()
        if proc.returncode != 0:
            print("PSUutil Error: cannot get PSUs data from BMC")
            sys.exit(proc.returncode)

        return out

    def get_num_psus(self):
        """
        Retrieves the number of PSUs available on the device
        :return: An integer, the number of PSUs available on the device
        """
        return NUM_OF_PSUS

    def get_psu_status(self, index):
        """
        Retrieves the operational status of power supply unit (PSU) defined
                by 1-based index <index>
        :param index: An integer, 1-based index of the PSU of which to query status
        :return: Boolean, True if PSU is operating properly, False if PSU is faulty
        """
        psu_status_cmd = " ".join(
            [IPMI_RAW_COMMAND, IPMI_SENSOR_NETFN, IPMI_EVENT_CMD, IPMI_SENSOR_MAPPING.get(index)])
        res = self._run_command(psu_status_cmd)

        status_byte = res.split()[1]
        failure_detected = (int(status_byte, 16) >> IPMI_PSU_FAILURE_BIT) & 1
        input_lost = (int(status_byte, 16) >> IPMI_PSU_INPUT_LOST_BIT) & 1

        return False if (failure_detected or input_lost) else True

    def get_psu_presence(self, index):
        """
        Retrieves the presence status of power supply unit (PSU) defined
                by 1-based index <index>
        :param index: An integer, 1-based index of the PSU of which to query status
        :return: Boolean, True if PSU is plugged, False if not
        """
        psu_status_cmd = " ".join(
            [IPMI_RAW_COMMAND, IPMI_SENSOR_NETFN, IPMI_EVENT_CMD, IPMI_SENSOR_MAPPING.get(index)])

        res = self._run_command(psu_status_cmd)
        status_byte = res.split()[1]
        presence = (int(status_byte, 16) >> IPMI_PSU_PRESENCE_BIT) & 1

        return presence or False
