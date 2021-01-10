#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEBUG 0
#define GOODIX_DEV "i2c-GDIX1001:00"
#define GOODIX_DRV_PATH "/sys/bus/i2c/drivers/Goodix-TS"
#define WACOM_DEV "i2c-WCOM0019:00"
#define WACOM_DRV_PATH "/sys/bus/i2c/drivers/i2c_hid"

typedef enum Formfactor {laptop, tablet, undefinedFF, borderFF} Formfactor;

double readFloat(const char* fn) {
    double rez = 0.0;
    FILE*fl;
    if (fl = fopen(fn, "r")) {
        fscanf(fl, "%lf", &rez);
        fclose(fl);
    }
    return rez;
}

#define maxName sizeof("/sys/bus/iio/devices/iio:device999999999999999/in_accel_x_raw")
#define maxDevs 4

char rawValsFileNames[maxDevs][3][maxName];

void init_accels() {
    int devNrs[maxDevs];
    char devName[PATH_MAX];
    int curDev = 0;
    for (int i = 0; i < 20 && curDev < maxDevs; ++i) {
        if (PATH_MAX <= snprintf(devName, PATH_MAX,
                    "/sys/bus/iio/devices/iio:device%d/in_accel_x_raw", i)) {
            perror("init_accels MAXPATHLEN not enough");
            exit(10);
        }
        FILE *fl = fopen(devName, "r");
        if (!fl) continue;
        devNrs[curDev++] = i;
        fclose(fl);
    }
    if (curDev != maxDevs) {
        perror("not found all devices.");
        exit(11);
    }
    for (int i = 0; i < maxDevs; ++ i) {
        snprintf(rawValsFileNames[i][0], maxName,
                "/sys/bus/iio/devices/iio:device%d/in_accel_x_raw", devNrs[i]);
        snprintf(rawValsFileNames[i][1], maxName,
                "/sys/bus/iio/devices/iio:device%d/in_accel_y_raw", devNrs[i]);
        snprintf(rawValsFileNames[i][2], maxName,
                "/sys/bus/iio/devices/iio:device%d/in_accel_z_raw", devNrs[i]);
    }
}

typedef struct Cartezian {double x, y, z;} Cartezian;
typedef struct Polar{double alt, lat, lon;} Polar;

#define PI 3.141592

void cart2pol(const Cartezian *cart, Polar *pol) {
    pol->alt = sqrt(cart->x * cart->x + cart->y * cart->y + cart->z * cart->z);
    pol->lat = atan2(cart->z, sqrt(cart->x * cart->x + cart->y * cart->y)) * 180.0 / PI;
    pol->lon = atan2(cart->y, cart->x) * 180.0 / PI;
}

int doRporting = 1;

union {
    double raw_vals[maxDevs][3];
    struct {
        struct { Cartezian screen, keyboard; } cart;
        struct { Polar screen, keyboard; } pol;
    } calculat;
} data;

int read_accels() {
    for (int i = 0; i < maxDevs; ++i) for (int j = 0; j < 3; ++j)
        data.raw_vals[i][j] = readFloat(rawValsFileNames[i][j]);
    int badData = 0;
    if (DEBUG) {
        printf("rawData:");
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j)
            printf("  %g", data.raw_vals[i][j]);
        printf("\n");
    }
    for (int i = 0; i < 2; ++i) for (int j = 0; j < 3; ++j)
        if (fabs(data.raw_vals[i][j] - data.raw_vals[i + 2][j]) > 100000.0) {
            if (DEBUG) printf ("%f %f\n", data.raw_vals[i][j], data.raw_vals[i + 1][j]);
            badData = 1;
        }
    if (DEBUG) printf(badData ? "naspa\n" : "bun\n");
    return badData;
}

void calculateAverage() {
    for (int i = 0; i < 2; ++i) for (int j = 0; j < 3; ++j)
        data.raw_vals[i][j] = (data.raw_vals[i][j] + data.raw_vals[i + 2][j]) / 2.0;
}

void rotate_keyboard_with_90_deg_over_z() {
    double temp = data.calculat.cart.keyboard.x;
    data.calculat.cart.keyboard.x = data.calculat.cart.keyboard.y;
    data.calculat.cart.keyboard.y = 0.0 - temp;
}

void convertToPolar() {
    cart2pol(&data.calculat.cart.screen, &data.calculat.pol.screen);
    cart2pol(&data.calculat.cart.keyboard, &data.calculat.pol.keyboard);
}

Formfactor getFormfactor() {
    double alon = fabs(data.calculat.pol.screen.lon);
    double relativeTilt = (atan2(data.calculat.cart.screen.z, data.calculat.cart.screen.x) -
            atan2(data.calculat.cart.keyboard.z, data.calculat.cart.keyboard.x)) * 180.0 / PI;
    if (relativeTilt > 180) relativeTilt -= 360;
    if (relativeTilt < -180) relativeTilt += 360;
    return alon > 80 && alon < 100 ? undefinedFF :
        relativeTilt > -150 && relativeTilt < -10 ? laptop :
        relativeTilt > 10 || relativeTilt <= -150 ? tablet : borderFF;
}

Formfactor lastFF = undefinedFF;
enum {activateKeyboard, activatePen} whatToActivate = activateKeyboard;

void writeText(char const * const text, char const * const file) {
    int f = open(file, O_WRONLY);
    if (f < 0) return;
    int r = write(f, text, strlen(text));
    if (doRporting) printf("write %s => %s, ret=%i\n", text, file, r);
    close(f);
}

int exists(char const * const dirName) {
    DIR *isActiveDir = opendir(dirName);
    if (isActiveDir) {
        closedir(isActiveDir);
        return 1;
    }
    return 0;
}

#define activate(drvName) if (!exists(drvName##_DRV_PATH "/" drvName##_DEV)) writeText(drvName##_DEV, drvName##_DRV_PATH "/bind");
#define deactivate(drvName) if (exists(drvName##_DRV_PATH "/" drvName##_DEV)) writeText(drvName##_DEV, drvName##_DRV_PATH "/unbind");

int main(int argc, char *argv[]) {
    if (argc > 1 && !strcmp("-q", argv[1])) doRporting = 0;
    init_accels();
    for (;;) {
        sleep(1);
        if (read_accels()) continue;
        calculateAverage();
        rotate_keyboard_with_90_deg_over_z();
        convertToPolar();
        Formfactor newFF = getFormfactor();
        if (doRporting) {
            char const *ccoinc[] = {"lap", "tab", "undef", "bor"};
            printf("%10.0f%8.2f%8.2f%10.0f%8.2f%8.2f%6s\n",
                data.calculat.pol.screen.alt,
                data.calculat.pol.screen.lat,
                data.calculat.pol.screen.lon,
                data.calculat.pol.keyboard.alt,
                data.calculat.pol.keyboard.lat,
                data.calculat.pol.keyboard.lon,
                ccoinc[newFF]);
        }
        if (newFF != undefinedFF && newFF != borderFF) {
            if (laptop == (lastFF = newFF)) {
                if (!exists(WACOM_DRV_PATH "/" WACOM_DEV)
                        && !exists(GOODIX_DRV_PATH "/" GOODIX_DEV)) {
                    if (activateKeyboard == whatToActivate) {
                        deactivate(WACOM);
                        activate(GOODIX);
                    } else {
                        deactivate(GOODIX);
                        activate(WACOM);
                    }
                }
            } else {
                whatToActivate = exists(WACOM_DRV_PATH "/" WACOM_DEV) ? activatePen : activateKeyboard;
                deactivate(GOODIX);
                deactivate(WACOM);
            }
        }
    }
}
