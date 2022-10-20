#!/usr/bin/env python

#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the fan status which are available in the platform
#
#############################################################################

try:
    import re
    from sonic_platform_base.fan_base import FanBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")


FAN_NAME_LIST = ["FAN-1F", "FAN-1R", "FAN-2F", "FAN-2R", "FAN-3F", "FAN-3R",
                 "FAN-4F", "FAN-4R", "FAN-5F", "FAN-5R", "FAN-6F", "FAN-6R", "FAN-7F", "FAN-7R"]

IPMI_OEM_NETFN = "0x3A"

# IPMI_OEM_NETFN + 0x26 + fan id: 0-7 + pwm:20-100
IPMI_SET_FAN_SPEED_CMD = "0x26 {} {}"
# IPMI_OEM_NETFN + 0x62 + fan id: 0-7
IPMI_AIR_FLOW_CMD = "0x62 {}"
# IPMI_OEM_NETFN + 0x26 + fan id: 0-7
IPMI_FAN_PRESENT_CMD = "0x26 0x03 {}"
# IPMI_OEM_NETFN + 0x64 + fanboard id + r/w flag + REG
IPMI_FAN_TARGET_SPEED_CMD = "0x64 0x02 0x01 {}"

# GET Fan RPM
# IPMI_OEM_NETFN + 0x26 + fan id: 0-7
IPMI_SS_READ_CMD = "0x2d {}"
IPMI_SENSOR_NETFN = "0x04"
FAN_SS_RPM_REG = ["0x43", "0x4A", "0x44", "0x4b", "0x45", "0x4c",
                  "0x46", "0x4d", "0x47", "0x4e", "0x48", "0x4f", "0x49", "0x50"]
PSU_FAN_SS_RPM_REG = ["0x2b", "0x34"]

# FAN1-FAN2 FAN CPLD TARGET SPEED REGISTER
FAN_TARGET_SPEED_REG = ["0x22", "0x32", "0x42", "0x52", "0x62", "0x72", "0x82"]

# IPMI_OEM_NETFN + 0x39 + fan led id: 4-0x0a + fan color: 0-2
IPMI_SET_FAN_LED_CMD = "0x39 0x02 {} {}"
# IPMI_OEM_NETFN + 0x39 + fan led id: 4-0x0a
IPMI_GET_FAN_LED_CMD = "0x39 0x01 {}"

IPMI_SET_PWM = "0x02 {} {}"
IPMI_FRU_PRINT_ID = "ipmitool fru print {}"
IPMI_SENSOR_LIST_CMD = "ipmitool sensor"
IPMI_FRU_MODEL_KEY = "Board Part Number"
IPMI_FRU_SERIAL_KEY = "Board Serial"

MAX_OUTLET = 29600
MAX_INLET = 31700
SPEED_TOLERANCE = 10

# IPMT_OEM_NETFN + 0x3E + {bus} + {8 bit address} + {read count} + 0x3B:PSU FAN SPEED REG
IPMI_PSU_TARGET_SPEED_CMD = "0x3E {} {} 1 0x3B"
PSU_I2C_BUS = "0x06"
PSU_I2C_ADDR = ["0xB0", "0xB2"]    # PSU1 and PSU2 I2C ADDR
PSU_FAN = "PSU{}_Fan"
PSU_MAX_RPM = 26500

FAN_FRONT = "Fan{}_Front"
FAN_REAR = "Fan{}_Rear"

FAN_LED_OFF_CMD = "0x00"
FAN_LED_GREEN_CMD = "0x01"
FAN_LED_RED_CMD = "0x02"
FAN1_LED_CMD = "0x04"

PSU1_STATUS_REG = "0x3a"
PSU2_STATUS_REG = "0x3b"


class Fan(FanBase):
    """Platform-specific Fan class"""

    def __init__(self, fan_tray_index, fan_index=0, is_psu_fan=False, psu_index=0):
        self.fan_index = fan_index
        self.fan_tray_index = fan_tray_index
        self.is_psu_fan = is_psu_fan
        if self.is_psu_fan:
            self.psu_index = psu_index
        self._api_helper = APIHelper()
        self.index = self.fan_tray_index * 2 + self.fan_index

    def get_direction(self):
        """
        Retrieves the direction of fan
        Returns:
            A string, either FAN_DIRECTION_INTAKE or FAN_DIRECTION_EXHAUST
            depending on fan direction
        """
        direction = self.FAN_DIRECTION_EXHAUST
        status, raw_flow = self._api_helper.ipmi_raw(
            IPMI_OEM_NETFN, IPMI_AIR_FLOW_CMD.format(hex(self.fan_tray_index)))
        if status and raw_flow == "01":
            direction = self.FAN_DIRECTION_INTAKE

        return direction

    def get_speed(self):
        """
        Retrieves the speed of fan as a percentage of full speed
        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)
        """
        if self.is_psu_fan:
            max_rpm = PSU_MAX_RPM
            fan_reg = PSU_FAN_SS_RPM_REG[self.psu_index]
        else:
            max_rpm = MAX_OUTLET if self.fan_index % 2 == 0 else MAX_INLET
            fan_reg = FAN_SS_RPM_REG[self.index]

        status, raw_ss_read = self._api_helper.ipmi_raw(
            IPMI_SENSOR_NETFN, IPMI_SS_READ_CMD.format(fan_reg))

        ss_read = raw_ss_read.split()[0]
        rpm_speed = int(ss_read, 16)*150
        speed = int(float(rpm_speed)/max_rpm * 100)

        return speed

    def get_target_speed(self):
        """
        Retrieves the target (expected) speed of the fan
        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)

        Note:
            speed_pc = pwm_target/255*100

            0   : when PWM mode is use
            pwm : when pwm mode is not use
        """
        if self.is_psu_fan:
            target = self.get_speed()
        else:
            get_target_speed_cmd = IPMI_FAN_TARGET_SPEED_CMD.format(
                FAN_TARGET_SPEED_REG[self.fan_tray_index])  # raw speed: 0-255
            status, get_target_speed_res = self._api_helper.ipmi_raw(
                IPMI_OEM_NETFN, get_target_speed_cmd)
            target = int(
                round(float(int(get_target_speed_res, 16)) * 100 / 255))

        return target

    def get_speed_tolerance(self):
        """
        Retrieves the speed tolerance of the fan
        Returns:
            An integer, the percentage of variance from target speed which is
                 considered tolerable
        """
        return SPEED_TOLERANCE

    def set_speed(self, speed):
        """
        Sets the fan speed
        Args:
            speed: An integer, the percentage of full fan speed to set fan to,
                   in the range 0 (off) to 100 (full speed)
        Returns:
            A boolean, True if speed is set successfully, False if not
        """
        return False  # Controlled by BMC

    def set_status_led(self, color):
        """
        Sets the state of the fan module status LED
        Args:
            color: A string representing the color with which to set the
                   fan module status LED
        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        led_cmd = {
            self.STATUS_LED_COLOR_GREEN: FAN_LED_GREEN_CMD,
            self.STATUS_LED_COLOR_RED: FAN_LED_RED_CMD,
            self.STATUS_LED_COLOR_OFF: FAN_LED_OFF_CMD
        }.get(color)

        fan_selector = hex(int(FAN1_LED_CMD, 16) + self.fan_tray_index)
        status, set_led = self._api_helper.ipmi_raw(
            IPMI_OEM_NETFN, IPMI_SET_FAN_LED_CMD.format(fan_selector, led_cmd))
        set_status_led = False if not status else True

        return set_status_led

    def get_status_led(self):
        """
        Gets the state of the fan status LED
        Returns:
            A string, one of the predefined STATUS_LED_COLOR_* strings above
        """
        fan_selector = hex(int(FAN1_LED_CMD, 16) + self.fan_tray_index)
        status, hx_color = self._api_helper.ipmi_raw(
            IPMI_OEM_NETFN, IPMI_GET_FAN_LED_CMD.format(fan_selector))

        status_led = {
            "00": self.STATUS_LED_COLOR_OFF,
            "01": self.STATUS_LED_COLOR_GREEN,
            "02": self.STATUS_LED_COLOR_RED,
        }.get(hx_color, "Unknown")

        return status_led

    ##############################################################
    ###################### Device methods ########################
    ##############################################################

    def get_name(self):
        """
        Retrieves the name of the device
            Returns:
            string: The name of the device
        """
        fan_name = FAN_NAME_LIST[self.fan_tray_index*2 + self.fan_index] if not self.is_psu_fan else "PSU-{} FAN-{}".format(
            self.psu_index+1, self.fan_index+1)

        return fan_name

    def _find_value(self, in_string):
        result = re.search("^.+ ([0-9a-f]{2}) .+$", in_string)
        return result.group(1) if result else result

    def get_presence(self):
        """
        Retrieves the presence of the FAN
        Returns:
            bool: True if FAN is present, False if not
        """
        presence = False
        if self.is_psu_fan:
            psu_presence = False
            psu_pstatus_key = globals(
            )['PSU{}_STATUS_REG'.format(self.psu_index+1)]
            status, raw_status_read = self._api_helper.ipmi_raw(
                IPMI_SENSOR_NETFN, IPMI_SS_READ_CMD.format(psu_pstatus_key))
            status_byte = self._find_value(raw_status_read)

            if status:
                presence_int = (int(status_byte, 16) >> 0) & 1
                psu_presence = True if presence_int else False

            return psu_presence

        status, raw_present = self._api_helper.ipmi_raw(
            IPMI_OEM_NETFN, IPMI_FAN_PRESENT_CMD.format(hex(self.index / 2)))

        if status and raw_present == "00":
            presence = True

        return presence

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        model = "Unknown"
        #ipmi_fru_idx = self.fan_tray_index + FAN1_FRU_ID
        # status, raw_model = self._api_helper.ipmi_fru_id(
        #    ipmi_fru_idx, IPMI_FRU_MODEL_KEY)

        #fru_pn_list = raw_model.split()
        # if len(fru_pn_list) > 4:
        #    model = fru_pn_list[4]

        return model

    def get_serial(self):
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        serial = "Unknown"
        #ipmi_fru_idx = self.fan_tray_index + FAN1_FRU_ID
        # status, raw_model = self._api_helper.ipmi_fru_id(
        #    ipmi_fru_idx, IPMI_FRU_SERIAL_KEY)

        #fru_sr_list = raw_model.split()
        # if len(fru_sr_list) > 3:
        #    serial = fru_sr_list[3]

        return serial

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        return self.get_presence() and self.get_speed() > 0