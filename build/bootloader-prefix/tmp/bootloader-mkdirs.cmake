# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/Espressif/frameworks/esp-idf-v5.3.1/components/bootloader/subproject"
  "D:/Documents/test/esp32/usbhid/build/bootloader"
  "D:/Documents/test/esp32/usbhid/build/bootloader-prefix"
  "D:/Documents/test/esp32/usbhid/build/bootloader-prefix/tmp"
  "D:/Documents/test/esp32/usbhid/build/bootloader-prefix/src/bootloader-stamp"
  "D:/Documents/test/esp32/usbhid/build/bootloader-prefix/src"
  "D:/Documents/test/esp32/usbhid/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Documents/test/esp32/usbhid/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Documents/test/esp32/usbhid/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
