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

float const susm[9] = {0, 1, 0, -1, 0, 1, 0, 0, 1};
float const josm[9] = {0, -1, 1, 1, 0, 0, 0, 0, 1};
float const stangam[9] = {-1, 0, 1, 0, -1, 1, 0, 0, 1};
float const dreaptam[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

double readFloat(const char* fn) {
    double rez = 0.0;
    FILE*fl;
    if (fl = fopen(fn, "r")) {
        fscanf(fl, "%lf", &rez);
        fclose(fl);
    }
    return rez;
}

int rotateScreen(Orientation targetRR, Formfactor form) {
    if (!targetRR) return 0;

    Display *disp;
    XRRScreenResources *screen;
    XRROutputInfo *info;
    XRRCrtcInfo *crtc_info;
    int iscres;
    int icrtc;
    int doRotation = 0;

    disp = XOpenDisplay(0);
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

    float const *v;
    union {
        float vals[9];
        char raw[sizeof(float) * 9];
    } data;
    switch (targetRR) {
    case upward: v = susm; break;
    case downward: v = josm; break;
    case leftward: v = stangam; break;
    case rightward: v = dreaptam; break;
    }
    for (int u = 0; u < 9; ++u) data.vals[u] = v[u];
    XIDeviceInfo *devs = XIQueryDevice(disp, XIAllDevices, &iscres);
    for (int i = 0; i < iscres; ++i) {
        int isVK = !strcmp("virtual-keyboard", devs[i].name) ||
                !strcmp("virtual-touchpad", devs[i].name);
        int isTouch = doRotation && (!strcmp("HDP0001:00 2ABB:8102", devs[i].name)
                || !strncmp("Wacom HID 169 Pen ", devs[i].name, 18));
        if (!isVK && !isTouch) continue;
        Atom* props = XIListProperties(disp, devs[i].deviceid, &icrtc);
        for (int j = 0; j < icrtc; ++j) {
            char * propName = XGetAtomName(disp, props[j]);
            if (isTouch && strcmp("Coordinate Transformation Matrix", propName) ||
                    isVK && strcmp("Device Enabled", propName)) {
                XFree(propName);
                continue;
            }
            Atom ptype;
            int pformat;
            unsigned long int icount, ba;
            unsigned char *buf;
            if (XIGetProperty(disp, devs[i].deviceid, props[j], 0, 36, False, AnyPropertyType, &ptype, &pformat, &icount, &ba, &buf) == Success) {
                if (DEBUG) {
                    printf("dbg:type=%d, format=%d, ba=%d, icount=%d, val=%d\n",
                        ptype, pformat, ba, icount, buf[0]);
                    printf("pn=%s, isVK=%d\n", propName, isVK);
                }
                if (isVK) {
                    if (DEBUG) printf("form=%d, orig= %d\n", form, buf[0]);
                    if (form == laptop && buf[0] == 0 || form == tablet && buf[0] != 0) {
                        double duty_cycle = readFloat("/sys/class/pwm/pwmchip1/pwm0/period") / 2.0;
                        int duty_cycleI = lrint(duty_cycle);
                        if (DEBUG) printf("set prop from %d to %d, duty_cycle=%d\n", buf[0], form == laptop, duty_cycleI);
                        buf[0] = form == laptop;
                        XIChangeProperty(disp, devs[i].deviceid, props[j], ptype,
                            pformat, PropModeReplace, buf, 1);
                        FILE* pwm = fopen("/sys/class/pwm/pwmchip1/pwm0/duty_cycle", "w");
                        if (pwm) {
                            fprintf(pwm, "%d", buf[0] ? duty_cycleI : 0);
                            if (DEBUG) printf("set pwm=%d\n", buf[0] ? duty_cycleI : 0);
                            fclose(pwm);
                        } else {
                            perror ("cannot set led.");
                        }
                    }
                } else {
                    XIChangeProperty(disp, devs[i].deviceid, props[j], ptype,
                        pformat, PropModeReplace, data.raw, 9);
                }
                XSync(disp, False);
                XFree(buf);
            }
            XFree(propName);
        }
        XFree (props);
    }
    XIFreeDeviceInfo(devs);

    return XCloseDisplay(disp);
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
        devName[curDev++] = i;
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
        struct { Cartezian ecran, tastatura; } primar, secundar;
    } senzori_bruti;
    struct {
        struct { Cartezian ecran, tastatura; } cart;
        struct { Polar ecran, tastatura; } pol;
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
    double temp = data.calculat.cart.tastatura.x;
    data.calculat.cart.tastatura.x = data.calculat.cart.tastatura.y;
    data.calculat.cart.tastatura.y = 0.0 - temp;
}

void convertToPolar() {
    cart2pol(&data.calculat.cart.ecran, &data.calculat.pol.ecran);
    cart2pol(&data.calculat.cart.tastatura, &data.calculat.pol.tastatura);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && !strcmp("-q", argv[1])) doRporting = 0;
    init_accels();
    for (;;) {
        sleep(1);
        if (read_accels()) continue;
        calculateAverage();
        rotate_keyboard_with_90_deg_over_z();
        convertToPolar();
        double alon = fabs(data.calculat.pol.ecran.lon);
        Orientation poz = data.calculat.pol.ecran.lat > 70 || data.calculat.pol.ecran.lat < -70 ? horizontal :
            alon > 135 ? upward : alon < 45 ? downward :
            data.calculat.pol.ecran.lon < 0 ? rightward : leftward;
        double relativeTilt = (atan2(data.calculat.cart.ecran.z, data.calculat.cart.ecran.x) -
            atan2(data.calculat.cart.tastatura.z, data.calculat.cart.tastatura.x)) * 180.0 / PI;
        if (relativeTilt > 180) relativeTilt -= 360;
        if (relativeTilt < -180) relativeTilt += 360;
        Formfactor coinc = alon > 80 && alon < 100 ? undefinedFF :
            relativeTilt > -170 && relativeTilt < -10 ? laptop :
            relativeTilt > 10 || relativeTilt <= -170 ? tablet : borderFF;
        if (doRporting) {
            char const *cpoz = "";
            switch (poz) {
            case horizontal: cpoz = "horiz"; break;
            case upward: cpoz = "upward"; break;
            case downward: cpoz = "downward"; break;
            case leftward: cpoz = "leftward"; break;
            case rightward: cpoz = "rightward"; break;
            }
            char const *ccoinc[] = {"lap", "tab", "undef", "bor"};
            printf("%10.0f%8.2f%8.2f%10s%10.0f%8.2f%8.2f%6s\n",
                data.calculat.pol.ecran.alt,
                data.calculat.pol.ecran.lat,
                data.calculat.pol.ecran.lon,
                cpoz[poz],
                data.calculat.pol.tastatura.alt,
                data.calculat.pol.tastatura.lat,
                data.calculat.pol.tastatura.lon,
                ccoinc[coinc]);
        }
        rotateScreen(poz, coinc);
    }
}
