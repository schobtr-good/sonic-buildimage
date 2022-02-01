#!/usr/bin/python
#
# MRVL_CPO1 platform sensors. This script get the sensor data from BMC
# using ipmitool and display them in lm-sensor alike format.
#

import sys
import logging
import subprocess

IPMI_SDR_CMD = "ipmitool sdr elist"
SENSOR_GROUPS = [
    ('TEMP',    'Temperature'),
    ('Fan',     'Fan'),
    ('PSU',     'PSU'),
    ('BMC',     'BMC'),
    ('BB',      'Base board'),
    ('COME',    'COM-E'),
    ('SW',      'Switch board'),
    ('I89',     'I89'),
]


def ipmi_sensor_dump(cmd):
    ''' 
        Execute ipmitool command return dump output
        exit if any error occur.
    '''
    sensor_dump = ''
    try:
        sensor_dump = subprocess.check_output(cmd, shell=True)
    except subprocess.CalledProcessError as e:
        logging.error('Error! Failed to execute: {}'.format(cmd))
        sys.exit(1)
    return sensor_dump


def get_reading_object(sdr_elist_dump):
    '''
        Load sensor data from sdr elist dump to object

        Example format:
            Input sdr_elist_dump:
            Fan2_Status      | 07h | ok  | 29.2 | Present
            Fan2_Front       | 0Eh | ok  | 29.2 | 12000 RPM
            Fan2_Rear        | 46h | ok  | 29.2 | 14700 RPM
            PSU2_Status      | 39h | ok  | 10.2 | Presence detected
            PSU2_Fan         | 3Dh | ok  | 10.2 | 16000 RPM
            PSU2_VIn         | 3Ah | ok  | 10.2 | 234.30 Volts

            Output sensor_data:
            {
                'Fan sensors': [
                    ('Fan2_Status', 'Present'),
                    ('Fan2_Front', '12000 RPM'),
                    ('Fan2_Rear', '14700 RPM')
                ],
                'PSU sensors': [
                    ('PSU2_Status', 'Presence detected'),
                    ('PSU2_Fan', '16000 RPM'),
                    ('PSU2_VIn', '234.30 Volts')
                ]
            }
    '''
    sensor_data = {}
    max_name_width = 0

    for line in sdr_elist_dump.split("\n"):
        for sensor_group in SENSOR_GROUPS:
            if line.startswith(sensor_group[0]):
                sensor_name = line.split('|')[0].strip()
                sensor_val = line.split('|')[4].strip()

                sensor_list = sensor_data.get(sensor_group[1], [])
                sensor_list.append((sensor_name, sensor_val))
                sensor_data[sensor_group[1]] = sensor_list

                max_name_width = len(sensor_name) if len(
                    sensor_name) > max_name_width else max_name_width

    return sensor_data, max_name_width


def get_sensor_output_str(sensor_data, max_name_width):
    '''
        Convert sensor data object to readable string format.

        Example format:
            Input sensor_data:
            {
                'Fan sensors': [
                    ('Fan2_Status', 'Present'),
                    ('Fan2_Front', '12000 RPM'),
                    ('Fan2_Rear', '14700 RPM')
                    ],
                'PSU sensors': [
                    ('PSU2_Status', 'Presence detected'),
                    ('PSU2_Fan', '16000 RPM'),
                    ('PSU2_VIn', '234.30 Volts')
                    ]
            }

            Output output_string:
            Fan sensors
            Adapter: IPMI adapter
            Fan2 Status:   Present
            Fan2 Front:    12000 RPM
            Fan2 Rear:     14700 RPM

            PSU sensors
            Adapter: IPMI adapter
            PSU2 Status:   Presence detected
            PSU2 Fan:      16000 RPM
            PSU2 VIn:      234.30 Volts
    '''
    output_string = ''
    sensor_format = '{0:{width}}{1}\n'
    for key_sensor in sensor_data:
        output_string += "{}\n".format(key_sensor)
        output_string += "Adapter: IPMI adapter\n"
        sensor_list = sensor_data[key_sensor]
        for sensor_value in sensor_list:
            display_sensor_name = sensor_value[0].replace('_', ' ')
            output_string += sensor_format.format('{}:'.format(display_sensor_name),
                                                  sensor_value[1],
                                                  width=str(max_name_width+4))
        output_string += '\n'
    return output_string


def main():
    output_string = ''
    ipmi_sdr_elist = ipmi_sensor_dump(IPMI_SDR_CMD)
    sensor_object, max_name_width = get_reading_object(ipmi_sdr_elist)
    output_string += get_sensor_output_str(sensor_object, max_name_width)
    print(output_string)


if __name__ == '__main__':
    main()
