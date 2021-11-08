try:
    from sonic_platform_pddf_base.pddf_fan import PddfFan
    import os
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")
FAN_DIRECTION_FILE_PATH = "/var/fan_direction"


class Fan(PddfFan):
    """PDDF Platform-Specific Fan class"""

    def __init__(self, tray_idx, fan_idx=0, pddf_data=None, pddf_plugin_data=None, is_psu_fan=False, psu_index=0):
        # idx is 0-based
        PddfFan.__init__(self, tray_idx, fan_idx, pddf_data, pddf_plugin_data, is_psu_fan, psu_index)


    def get_speed_tolerance(self):
        """
        Retrieves the speed tolerance of the fan

        Returns:
            An integer, the percentage of variance from target speed which is
                 considered tolerable
        """
        # Fix the speed vairance to 10 percent. If it changes based on platforms, overwrite
        # this value in derived pddf fan class
        return 20
    
    
    def get_presence(self):
        #Overwirte the PDDF Common since the FANs on Belgite are all Fixed and present
        return True 

    def get_direction(self):
        """
        Retrieves the direction of fan

        Returns:
            A string, either FAN_DIRECTION_INTAKE or FAN_DIRECTION_EXHAUST
            depending on fan direction
        """
        if self.is_psu_fan:
            cmd_num = "58" if self.fans_psu_index == 1 else "59"
            cmd = "i2cget -y -f 4 0x%s 0x80" % cmd_num
            res = os.popen(cmd).read()
            # F2B 
            if res.strip() == "0x01":
                direction = "EXHAUST"
            else:
                direction = "INTAKE"
        else:
            direction = "INTAKE"
            with open(FAN_DIRECTION_FILE_PATH, "r") as f:
                fan_direction = f.read()
                if fan_direction.strip() == "FB":
                    direction = "EXHAUST"
        return direction
    

    def get_status(self):
        speed = self.get_speed_rpm()
        status = True if (speed != 0) else False
        return status

