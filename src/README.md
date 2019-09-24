# Simulations
Here are applications for different simulations located.

# Build
To generate the binary files for each project, for mulitple configurations (with or without debug etc.), run `make builds`.

To build a single application, copy the desired configuration file from the `config` folder and replace `sdkconfig`, then issue `make -j $(nproc)`.

# Flash
The flash procedure depends on the kind of application, see the following sections for details.

## Non-OTA
To flash a non-OTA firmware, issue the following command:

`python esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 115200 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 bootloader.bin 0x10000 <application>.bin 0x8000 partitions_singleapp.bin`

## OTA
To flash an OTA firmware, issue the following command:

`python esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 115200 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0xd000 ota_data_initial.bin 0x1000 bootloader.bin 0x10000 <application>.bin 0x8000 partitions_two_ota.bin`
