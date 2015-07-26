engg4810
========

A package tracking device based on the TI TIVA C. 

firmware/main.c
	Main application loop, acceleromter sample processing and SD card output.

firmware/analog.c
	ADC management, handles continuous aquisition from the 
	analog accelerometer and temperature sensor. Aquired samples
	are placed into a buffer using DMA.

firmware/sdcard.c
	A from-scratch SD card driver I started writing until someone 
	pointed out there was one built into FatFS. This code is unused.

firmware/gps.c
	Configures the NEO-6M GPS module and parses the NEMA output to
	get time and position information.

firmware/misc.c
	Fault ISR handlers, debug_printf()

parser/
	Parses the files written to the SD card into YAML format.
	This includes a code::blocks project file, but it's all standard C.

