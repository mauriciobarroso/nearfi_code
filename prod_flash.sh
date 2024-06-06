#!/bin/bash

# Get OTA URL and server certificate paths
OTA_FILE_URL=$1
OTA_SERVER_CERT=$2

# Create sdkconfig.temp to store OTA parameters
touch sdkconfig.temp
SDKCONFIG_TEMP="sdkconfig.temp"

# Check if the parameters are empty
if [ -z "$OTA_FILE_URL" ] || [ -z "$OTA_SERVER_CERT" ]; then
    echo "Invalid parameters"
    exit 1
fi

# Append OTA parameters in skdconfig.temp
echo "CONFIG_OTA_ENABLE=y" >> $SDKCONFIG_TEMP
echo "CONFIG_OTA_FILE_URL=\"$OTA_FILE_URL\"" >> $SDKCONFIG_TEMP
echo "CONFIG_OTA_SERVER_CERT=\"$OTA_SERVER_CERT\"" >> $SDKCONFIG_TEMP

# Build project with security options for ESP32-S3
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.security;sdkconfig.temp" set-target esp32s3 build

# Flash bootloader, app, ota data and NVS data
esptool.py --chip esp32s3 -p /dev/ttyUSB1 -b 460800 --before=default_reset --after=no_reset --no-stub write_flash --flash_mode dio --flash_freq 80m --flash_size keep 0x0 build/bootloader/bootloader.bin 0x20000 build/NearFi.bin 0x8000 build/partition_table/partition-table.bin 0x11000 build/ota_data_initial.bin