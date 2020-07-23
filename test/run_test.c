#include <stdio.h>
#include <unistd.h>

#define TACH_FILE_NAME "/dev/gpiotach1.0"
#define GPIO_FILE_NAME "/sys/class/gpio/gpio60/value"

#define DEFAULT_FREQUENCY_HZ 50
#define DEFAULT_TIME_S 2

int doPulse(FILE *file, int delay_ms) {
    int rval = 0;
    if (fprintf(file, "0") != 1) {
        return 1;
    }
    if (fflush(file) != 0) {
        return 1;
    }
    usleep(delay_ms * 1000);
    if (fprintf(file, "1") != 1) {
        return 1;
    }
    if (fflush(file) != 0) {
        return 1;
    }
    usleep(delay_ms * 1000);
    return 0;
}

int simulatePulses(FILE *file, int frequency_hz, int time_ms) {
    int numPulses = frequency_hz * time_ms / 1000;
    int delay_ms = 1000 / frequency_hz / 2;
    int rval = 0;
    int i;

    printf("Starting pulses at %d ms intervals for %d pulses\n", delay_ms, numPulses);

    for (i = 0; i < numPulses; i++) {
        if (rval = doPulse(file, delay_ms) != 0) {
            break;
        }
    }
    return rval;
}

int main(int argc, const char *argv) {
    FILE *gpo_file;

    gpo_file = fopen(GPIO_FILE_NAME, "w");

    if (gpo_file == NULL) {
        printf("Error opening the GPO file\n");
        return -1;
    }

    simulatePulses(gpo_file, DEFAULT_FREQUENCY_HZ, DEFAULT_TIME_S * 1000);

    FILE *tach_file = fopen(TACH_FILE_NAME, "rb");
    if (tach_file == NULL) {
        printf("Error opening the tach file\n");
	goto cleanup;
    }
    
    int value;
    int numToRead = sizeof(value);
    int numRead = fread(&value, 1, numToRead, tach_file);

    if (numRead != numToRead) {
        printf("Num read was %d instead of %d\n", numRead, numToRead);
        printf("Value is %d\n", value);
        goto cleanup;
    }

    printf("Value read from the file was %d\n", value);
    fclose(tach_file);

    fclose(gpo_file);
    return 0;

cleanup:
    if (tach_file != NULL) {
        fclose(tach_file);
    }
    if (gpo_file != NULL) {
        fclose(gpo_file);
    }
    return -1;
}
