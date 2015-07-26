#include <stdio.h>
#include <stdlib.h>

#include <stdint.h>

#include <string.h>
#include <math.h>

#include <stdarg.h>

#define INDENT "    "


/* FIXME: the temperature sensor is an NTC thermistor  */
int temp_convert(uint16_t temp_input)
{
	return temp_input;
}

/* GPS outputs are in DDMM.MMMMM format, this needs to be converted to be in decimal degrees for gmaps */

double gps_correct(double input)
{
	input = input / 100.0; // shift the minutes part to right of the decimal

	double deg = trunc(input);
	double min = input - deg;

	return deg + min * 100.0 / 60.0;
}

void time_print(FILE *f, uint32_t time, uint32_t date)
{
	int day    = date / 10000;
	int month  = date / 100 - day * 100;
	int year   = date - month * 100 - day * 10000;
    year += 2000; // we want the four digit year

	int hour   = time / 10000;
	int minute = time / 100 - hour * 100;
	int second = time - minute * 100 - hour * 10000;

	fprintf(f, "  time: %.2d-%.2d-%.2dT%.2d:%.2d:%.2d+00:00\n", year, month, day, hour, minute, second);
}

/* FIXME: The accelerometer outputs are uncalibrated to the point of uselessness */
void accel_print(FILE *f, uint16_t accel[3])
{
//	float ticks_per = 77.8; // there is about 77.8 ADC counts per g, sen
	float offset[3] = {1.65, 1.65, 1.65};
	float sens[3]   = {0.0627, 0.0627, 0.0627};

    float converted[3];
	int i;

	for(i = 0; i < 3; i++) {
        converted[i] = ((float) accel[i] / 4095.0 - offset[i]) / sens[i];
	}

	fprintf(f, "  acceleration: [%.6f, %.6f, %.6f]\n", converted[0], converted[1], converted[2]);
}

int main(int argc, char **argv)
{
	FILE *bin, *yaml;

	if(argc < 2) {
		fprintf(stderr, "Usage: %s <binfile>\n", argv[0]);
		return -1;
	}

	bin = fopen(argv[1], "rb");
	if(!bin) {
		fprintf(stderr, "Unable to open file: %s\n", argv[1]);
		return -1;
	}

    char *filename = strdup(argv[1]);
    int len = strlen(filename);
	strcpy(filename + len - 3, "yml");

	yaml = fopen(filename, "w");
    if(!yaml) {
		fprintf(stderr, "unable to open yaml file '%s' for writing, using \n", argv[2]);
		yaml = stdout;
		return -1;
	}


	/* print the YAML metadata header */

	fputs("metadata:\n", yaml);
	fputs(INDENT, yaml); fputs("team_number: 3\n", yaml);
	fputs("\n", yaml);
	fputs("samples:\n", yaml);

	do {
		uint32_t index, time, date;
		uint16_t accel[3], temp;
		float lat, lng;

		int count = 0;
		                                   // 0 0
		count += fread(&index, 4, 1, bin); // 1 1
		count += fread(&time,  4, 1, bin); // 1 2
		count += fread(&date,  4, 1, bin); // 1 3
		count += fread(&accel, 2, 3, bin); // 3 6
		count += fread(&lat,   4, 1, bin); // 1 7
		count += fread(&lng,   4, 1, bin); // 1 8
		count += fread(&temp,  2, 1, bin); // 1 9

		if(count == 9) { // all items read successfully
			// these are always generated
			fputs(INDENT, yaml); fprintf(yaml, "- temperature: %d\n", temp_convert(temp));
			fputs(INDENT, yaml); accel_print(yaml, accel);

			/* if the high bit is set then we had a fix, so lat
			 * and long coordinates should be generated
			 */

			if(index & (1 << 31)) { // fix flag set
				fputs(INDENT, yaml); time_print(yaml, time, date);

				fputs(INDENT, yaml); fprintf(yaml, "  latitude: %f\n",  gps_correct(lat));
				fputs(INDENT, yaml); fprintf(yaml, "  longitude: %f\n", gps_correct(lng));
			}

			fputs("\n", yaml);
		} else {

		    if(feof(bin)) {
                break;
		    }

			fprintf(stderr, "Error reading file: %s", argv[1]);
			return -1;
		}
	} while( !feof(bin) && !ferror(bin));

	fclose(yaml);
	fclose(bin);

    return 0;
}
