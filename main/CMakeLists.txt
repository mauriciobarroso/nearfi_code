# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS main.c passive_buzzer.c misc.c nvs.c cdns.c server.c
    INCLUDE_DIRS
    PRIV_INCLUDE_DIRS
    REQUIRES
    PRIV_REQUIRES
)

spiffs_create_partition_image(storage ../web FLASH_IN_PROJECT)