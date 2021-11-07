#############################################################################
# PDDF
# Module contains an implementation of SONiC Platform Base API and
# provides the platform information
#
#############################################################################

try:
    import json
    from sonic_platform_pddf_base.pddfparse import PddfParse 
    from sonic_platform_base.platform_base import PlatformBase
    from sonic_platform.chassis import Chassis
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")


class PddfPlatform(PlatformBase):
    """
    PDDF Generic Platform class
    """
    pddf_data = {}
    pddf_plugin_data = {}

    def __init__(self):
        # Initialize the JSON data
        self.pddf_data = PddfParse()
        with open('/usr/share/sonic/platform/pddf/pd-plugin.json') as pd:
            self.pddf_plugin_data = json.load(pd)

        if not self.pddf_data or not self.pddf_plugin_data:
            print("Error: PDDF JSON data is not loaded properly ... Exiting")
            raise ValueError

        PlatformBase.__init__(self)
        self._chassis = Chassis(self.pddf_data, self.pddf_plugin_data)

class Platform(PddfPlatform):
    """
    PDDF Platform-Specific Platform Class
    """

    def __init__(self):
        PddfPlatform.__init__(self)

    # Provide the functions/variables below for which implementation is to be overwritten
