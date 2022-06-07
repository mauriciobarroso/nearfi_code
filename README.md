# NearFi
## Description
NearFi is a proximity Wi-Fi repeater based on ESP32-S2 and ESP-IDF.

## 1. Install prerequisites
To compile with ESP-IDF you need execute the next commands to get the necessary packages:

```
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-setuptools cmake ninja-build ccache libffi-dev libssl-dev dfu-util
```

## 2. Get ESP-IDF
To build this application for the ESP32-S2, you need the software libraries provided by Espressif in ESP-IDF repository.

To get ESP-IDF, navigate to your installation directory and execute the next commands:

```
mkdir ~/esp
cd ~/esp
git clone -b release/v4.2 --recursive https://github.com/espressif/esp-idf.git
```

The version 4.2 of ESP-IDF is mandatory.

## 3. Set up the tools
Aside from the ESP-IDF, you also need to install the tools used by ESP-IDF, such as the compiler, debugger, Python packages, etc. Execute the next commands:

```
cd ~/esp/esp-idf
./install.sh esp32s2
```

## 4. Se up the environment variables
In the terminal where you are going to use ESP-IDF, run:

```
cd ~/esp/esp-idf
. $HOME/esp/esp-idf/export.sh
```

## 5. Clone the project
To buld and flash this application you need clone it in your workspace with the next command:

```
git clone --recursive https://github.com/mauriciobarroso/nearfi_code.git
```

## 6. Build, flash and monitor the output
The project is ready to compile. The default settings are in `sdkconfig.defaults` file. To build the project execute the next commands in the project folder:

```
idf.py fullclean
idf.py set-target esp32s2
idf.py build
```

To flash the binary files created you need execute the next command:

```
esptool.py --chip esp32s2 --port /dev/ttyUSB1 --baud 460800 --before=default_reset --after=no_reset --no-stub write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB 0x1000 /home/mauricio/projects/getbit/nearfi/nearfi_code/build/bootloader/bootloader.bin && idf.py flash monitor
```
 Once you are done flashing the binary files, you can see the output on the monitor openned with the last command. Reset the device and wait the monitor show something like this:
 
```
ESP-ROM:esp32s2-rc4-20191025
Build:Oct 25 2019
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
SPIWP:0xee
mode:DIO, clock div:1
Valid secure boot key blocks: 0
secure boot verification succeeded
load:0x3ffe6268,len:0x7c
load:0x3ffe62e4,len:0x804
load:0x4004c000,len:0x1478
load:0x40050000,len:0x2c04
entry 0x4004c2a0
I (314) cache: Instruction cache 	: size 16KB, 4Ways, cache line size 16Byte
I (315) cpu_start: Pro cpu up.
I (315) cpu_start: Application information:
I (319) cpu_start: Project name:     NearFi
I (324) cpu_start: App version:      1.2.0
I (329) cpu_start: Compile time:     Jun  7 2022 15:24:56
I (335) cpu_start: ELF file SHA256:  13571ac874202adb...
I (341) cpu_start: ESP-IDF:          v4.2-dirty
I (346) cpu_start: Single core mode
I (351) heap_init: Initializing. RAM available for dynamic allocation:
I (358) heap_init: At 3FFD6238 len 00025DC8 (151 KiB): DRAM
I (364) heap_init: At 3FFFC000 len 00003A10 (14 KiB): DRAM
I (370) cpu_start: Pro cpu start user code
I (432) spi_flash: detected chip: generic
I (433) spi_flash: flash io: dio
I (433) cpu_start: Starting scheduler on PRO CPU.
I (436) app: Initializing components instances
I (463) Button: Initializing button component...
I (463) gpio: GPIO[21]| InputEn: 1| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:3 
I (466) Buzzer: Initializing buzzer component...
I (472) gpio: GPIO[4]| InputEn: 0| OutputEn: 1| OpenDrain: 0| Pullup: 0| Pulldown: 0| Intr:0 
I (481) wifi: Initializing wifi component...
I (489) wifi:wifi driver task: 3ffdfa34, prio:23, stack:6656, core=0
I (492) system_api: Base MAC address is not set
I (497) system_api: read default base MAC address from EFUSE
I (519) wifi:wifi firmware version: 1865b55
I (519) wifi:wifi certification version: v7.0
I (519) wifi:config NVS flash: enabled
I (520) wifi:config nano formating: disabled
I (524) wifi:Init data frame dynamic rx buffer num: 32
I (528) wifi:Init management frame dynamic rx buffer num: 32
I (534) wifi:Init management short buffer num: 32
I (538) wifi:Init dynamic tx buffer num: 32
I (542) wifi:Init static rx buffer size: 1600
I (546) wifi:Init static rx buffer num: 8
I (550) wifi:Init dynamic rx buffer num: 32
I (554) wifi_init: rx ba win: 16
I (558) wifi_init: tcpip mbox: 32
I (562) wifi_init: udp mbox: 32
I (566) wifi_init: tcp mbox: 32
I (569) wifi_init: tcp tx win: 32000
I (574) wifi_init: tcp rx win: 32000
I (578) wifi_init: tcp mss: 1460
I (582) wifi_init: WiFi IRAM OP enabled
I (586) wifi_init: WiFi RX IRAM OP enabled
I (591) wifi_init: LWIP IRAM OP enabled
I (923) phy: phy_version: 603, 72dfd77, Jul  7 2020, 19:57:05, 0, 2
I (924) wifi:enable tsf
I (925) wifi:mode : sta (7c:df:a1:54:b6:d4) + softAP (7c:df:a1:54:b6:d5)
I (928) wifi:Total power save buffer number: 16
I (932) wifi:Init max length of beacon: 752/752
I (936) wifi:Init max length of beacon: 752/752
I (941) app: Other event
I (944) app: Other event
I (947) app: Other event
I (950) wifi: Starting provisioning...
I (955) wifi:mode : sta (7c:df:a1:54:b6:d4)
I (960) app: Other event
I (964) wifi:mode : sta (7c:df:a1:54:b6:d4) + softAP (7c:df:a1:54:b6:d5)
I (969) wifi:Total power save buffer number: 16
I (974) app: Other event
I (977) wifi:Total power save buffer number: 16
I (980) app: Other event
I (984) app: Other event
W (986) wifi_prov_scheme_softap: Error adding mDNS service! Check if mDNS is running
I (995) wifi_prov_mgr: Provisioning started with service name : PROV_54B6D4 
I (1002) app: WIFI_PROV_START
I (1007) app: *****************************
I (1011) app: * Station MAC: 7CDFA154B6D4 *
I (1016) app: * Soft-AP MAC: 7CDFA154B6D5 *
I (1021) app: *****************************
I (1025) app: Creating RTOS tasks...
```
