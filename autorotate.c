#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/XInput2.h>
#include <math.h>

#define DEBUG 0

typedef enum Orientation {horizontal = 0,
    upward = RR_Rotate_270, downward = RR_Rotate_90,
    leftward = RR_Rotate_180, rightward = RR_Rotate_0} Orientation;
typedef enum Formfactor {laptop, tablet, undefinedFF, borderFF} Formfactor;

int needSwapDims(int curR, int targR) {
    if (curR == RR_Rotate_90 || curR == RR_Rotate_270) {
        if (targR == RR_Rotate_0 || targR == RR_Rotate_180) return 1;
    } else {
        if (targR == RR_Rotate_90 || targR == RR_Rotate_270) return 1;
    }
    return 0;
}

void modifyProperty(Display *disp, char const *devices[], char *property_name, void* content, int count) {
    int devCount = 0;
    XIDeviceInfo *devs = XIQueryDevice(disp, XIAllDevices, &devCount);
    if (!devs) return;
    for (int i = 0; i < devCount; ++i) {
        {
            int devNotFound = 1;
            for (int j = 0; devices[j][0] && devNotFound; ++j)
                devNotFound = strncmp(devices[j], devs[i].name, strlen(devices[j]));
            if (devNotFound) continue;
        }
        int propCount;
        Atom* props = XIListProperties(disp, devs[i].deviceid, &propCount);
        if (!props) continue;
        for (int j = 0; j < propCount; ++j) {
            {
                char * propName = XGetAtomName(disp, props[j]);
                int notFound = strcmp(property_name, propName);
                XFree(propName);
                if (notFound) continue;
            }
            Atom ptype;
            int pformat;
            unsigned long int icount, ba;
            unsigned char *buf;
            if (Success != XIGetProperty(disp, devs[i].deviceid, props[j],
                        0, 0, False, AnyPropertyType,
                        &ptype, &pformat, &icount, &ba, &buf)) continue;
            XIChangeProperty(disp, devs[i].deviceid, props[j],
                    ptype, pformat, PropModeReplace, content, count);
            XSync(disp, False);
            XFree(buf);
        }
        XFree (props);
    }
    XIFreeDeviceInfo(devs);
}

float const upMatrix[9] = {0, 1, 0, -1, 0, 1, 0, 0, 1};
float const downMatrix[9] = {0, -1, 1, 1, 0, 0, 0, 0, 1};
float const leftMatrix[9] = {-1, 0, 1, 0, -1, 1, 0, 0, 1};
float const rightMatrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

double readFloat(const char* fn) {
    double rez = 0.0;
    FILE*fl;
    if (fl = fopen(fn, "r")) {
        fscanf(fl, "%lf", &rez);
        fclose(fl);
    }
    return rez;
}

int rotateScreen(Display *disp, Orientation targetRR) {
    if (!targetRR) return 0;

    XRRScreenResources *screen;
    XRROutputInfo *info;
    XRRCrtcInfo *crtc_info;
    int iscres;
    int icrtc;
    int doRotation = 0;

    screen = XRRGetScreenResources (disp, DefaultRootWindow(disp));
    if (DEBUG) fprintf(stderr, "iscres=%d\n", screen->noutput);
    for (iscres = 0; iscres < screen->noutput; ++iscres) {
        info = XRRGetOutputInfo (disp, screen, screen->outputs[iscres]);
        if (info->connection == RR_Connected) {
            if (DEBUG) fprintf(stderr, "on=%s, icrtc=%d\n", info->name, info->ncrtc);
            if (!strcmp(info->name, "DSI-1")) for (icrtc = 0; icrtc < info->ncrtc; ++icrtc) {
                int ow = DisplayWidth(disp, iscres),
                    oh = DisplayHeight(disp, iscres),
                    owmm = DisplayWidthMM(disp, iscres),
                    ohmm = DisplayHeightMM(disp, iscres);
                crtc_info = XRRGetCrtcInfo (disp, screen, screen->crtcs[icrtc]);
                if (DEBUG) fprintf(stderr, "==> %dx%d+%dx%d,m=%d \n",
                        crtc_info->x, crtc_info->y, crtc_info->width,
                        crtc_info->height, crtc_info->mode);
                if (crtc_info->width == ow && crtc_info->height == oh) {
                    if (needSwapDims(crtc_info->rotation & 0xF, targetRR)) {
                        int ssx;
                        ssx = ow; ow = oh; oh = ssx;
                        ssx = owmm; ohmm = owmm; ohmm = ssx;
                    }
                    if (targetRR != crtc_info->rotation & 0xF) {
                        XRRSetCrtcConfig (disp, screen, screen->crtcs[icrtc], crtc_info->timestamp,
                            crtc_info->x, crtc_info->y, crtc_info->mode,
                            crtc_info->rotation & ~0xF | targetRR,
                            crtc_info->outputs, crtc_info->noutput);
                        XRRSetScreenSize (disp, DefaultRootWindow(disp), ow, oh, owmm, ohmm);
                        doRotation = 1;
                    }
                }
                XRRFreeCrtcInfo(crtc_info);
            }
        }
        XRRFreeOutputInfo (info);
    }
    XRRFreeScreenResources(screen);
    void *v;
    switch (targetRR) {
    case upward: v = (void *)upMatrix; break;
    case downward: v = (void *)downMatrix; break;
    case leftward: v = (void *)leftMatrix; break;
    case rightward: v = (void *)rightMatrix; break;
    }
    char const *tDevices[] = {"HDP0001:00 2ABB:8102", "Wacom HID 169 Pen ", ""};
    modifyProperty(disp, tDevices, "Coordinate Transformation Matrix", v, 9);
}

void activateKeyboard(Display *disp, int activateKeyboard) {
    char const *vkDevices[] = {"virtual-keyboard", "virtual-touchpad", ""};
    modifyProperty(disp, vkDevices, "Device Enabled", &activateKeyboard, 1);
}

#define maxName sizeof("/sys/bus/iio/devices/iio:device99999999/in_accel_x_raw")
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
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j)
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

Orientation getOrientation() {
    double alon = fabs(data.calculat.pol.screen.lon);
    return data.calculat.pol.screen.lat > 70 || data.calculat.pol.screen.lat < -70 ? horizontal :
        alon > 135 ? upward : alon < 45 ? downward :
        data.calculat.pol.screen.lon < 0 ? rightward : leftward;
}

Formfactor getFormfactor() {
    double alon = fabs(data.calculat.pol.screen.lon);
    double relativeTilt = (atan2(data.calculat.cart.screen.z, data.calculat.cart.screen.x) -
            atan2(data.calculat.cart.keyboard.z, data.calculat.cart.keyboard.x)) * 180.0 / PI;
    if (relativeTilt > 180) relativeTilt -= 360;
    if (relativeTilt < -180) relativeTilt += 360;
    return alon > 80 && alon < 100 ? undefinedFF :
        relativeTilt > -170 && relativeTilt < -10 ? laptop :
        relativeTilt > 10 || relativeTilt <= -170 ? tablet : borderFF;
}

Orientation lastOrient = horizontal;
Formfactor lastFF = undefinedFF;

int main(int argc, char *argv[]) {
    if (argc > 1 && !strcmp("-q", argv[1])) doRporting = 0;
    init_accels();
    for (;;) {
        sleep(1);
        if (read_accels()) continue;
        calculateAverage();
        rotate_keyboard_with_90_deg_over_z();
        convertToPolar();
        Orientation newOrient = getOrientation();
        Formfactor newFF = getFormfactor();
        if (doRporting) {
            char const *cpoz = "";
            switch (newOrient) {
            case horizontal: cpoz = "horiz"; break;
            case upward: cpoz = "upward"; break;
            case downward: cpoz = "downward"; break;
            case leftward: cpoz = "leftward"; break;
            case rightward: cpoz = "rightward"; break;
            }
            char const *ccoinc[] = {"lap", "tab", "undef", "bor"};
            printf("%10.0f%8.2f%8.2f%10s%10.0f%8.2f%8.2f%6s\n",
                data.calculat.pol.screen.alt,
                data.calculat.pol.screen.lat,
                data.calculat.pol.screen.lon,
                cpoz,
                data.calculat.pol.keyboard.alt,
                data.calculat.pol.keyboard.lat,
                data.calculat.pol.keyboard.lon,
                ccoinc[newFF]);
        }
        Display *disp = XOpenDisplay(NULL);
        if (!disp) continue;
        if (newOrient != horizontal && newOrient != lastOrient)
            rotateScreen(disp, lastOrient = newOrient);
        if (newFF != undefinedFF && newFF != borderFF && newFF != lastFF)
            activateKeyboard(disp, laptop == (lastFF = newFF));
        XCloseDisplay(disp);
    }
}
