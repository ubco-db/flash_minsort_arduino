# Flash MinSort

Flash minsort is a fast sorting algorithm for embedded devices with minimal memory. This version is specifically for the Arduino.

Flash minsort has the following benefits:

1. Minimum memory usage of less than 1 KB.
2. Very efficient at sorting data that is partially sorted.
3. No use of dynamic memory (i.e. malloc()). 
4. Easy to use and include in existing projects. 
5. Open source license. Free to use for commerical and open source projects.

## License
[![License](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)

## Code Files

* flash_minsort.c, flash_minsort.h - implementation of flash minsort
* test_flash_minsort.h - test file
* in_memory_sort.c, in_memory_sort.h - implementation of quick sort
* serial_c_interface.c, serial_c_interface.h - serial output for Arduino
* ion_file.c, ion_file.h - file abstraction for files on SD card

#### Ramon Lawrence<br>University of British Columbia Okanagan
