#include <stdio.h>
#include <unistd.h>

#define TACH_FILE_NAME "/dev/gpiotach1.0"
#define GPIO_FILE_NAME "/sys/class/gpio/gpio60/value"

int main(int argc, const char *argv)
{
    FILE *gpo_file;

    gpo_file = fopen(GPIO_FILE_NAME, "wb");

    if (gpo_file == NULL) {
        printf("Error opening the GPO file\n");
        return -1;
    }

    printf("Writing a 0\n");
    // Write a 1 to the file
    fprintf(gpo_file, "0");

    // Sleep for 100ms
    usleep(100000);

    printf("Writing a 1\n");
    fprintf(gpo_file, "1");

    usleep(100000);

    FILE *tach_file = fopen(TACH_FILE_NAME, "rb");
    if (tach_file == NULL)
    {
        printf("Error opening the tach file\n");
	goto cleanup;
    }
    
    int value;
    int numToRead = sizeof(value);
    int numRead = fread(&value, 1, numToRead, tach_file);

    if (numRead != numToRead)
    {
        printf("Num read was %d instead of %d\n", numRead, numToRead);
        printf("Value is %d\n", value);
        goto cleanup;
    }

    printf("Value read from the file was %d\n", value);
    fclose(tach_file);

    fclose(gpo_file);
    printf("Hello World\n");
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
