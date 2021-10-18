#!/bin/bash
# Set SYSLED to Green, assuming everything came up fine
sudo i2cset -f -y 0x2 0x32 0x43 0xec
