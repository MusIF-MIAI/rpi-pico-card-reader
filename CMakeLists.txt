cmake_minimum_required(VERSION 3.13)

# Pico 2 W. The CYW43 pins (GP23/24/25/29) are reserved; wireless unused so far.
set(PICO_BOARD pico2_w)

include(pico_sdk_import.cmake)

project(ge120_cardreader C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(ge120_cardreader
    src/main.c
    src/core1_main.c
    src/ipc.c
    src/feeder.c
    src/wire_tx.c
    src/wire_rx.c
    src/status_pins.c
    src/usb_composite.c
    src/usb_descriptors.c
    src/console.c
    src/storage.c
    src/fat_ro.c
    src/deck.c
    src/deckload.c
    src/surgery.c
    src/transcode.c
    src/config.c
    src/monitor.c
)

pico_generate_pio_header(ge120_cardreader ${CMAKE_CURRENT_LIST_DIR}/pio/presenter.pio)
pico_generate_pio_header(ge120_cardreader ${CMAKE_CURRENT_LIST_DIR}/pio/re_capture.pio)

target_include_directories(ge120_cardreader PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# TODO(step 2): vendor FatFs (ff15) under lib/fatfs and add it here; storage.c
# is a stub until then.

target_link_libraries(ge120_cardreader
    pico_stdlib
    pico_multicore
    pico_flash
    hardware_pio
    hardware_irq
    hardware_timer
    hardware_dma
    tinyusb_device
    tinyusb_board
)

# All stdio via USB CDC (interface 0 of the composite device); UART free.
pico_enable_stdio_usb(ge120_cardreader 0)   # we run our own TinyUSB CDC console
pico_enable_stdio_uart(ge120_cardreader 0)

pico_add_extra_outputs(ge120_cardreader)
