/*
 * simhub_udp.c - the car snapshot (carstate.c) sent to SimHub as UDP packets in Forza Motorsport 7's "Data Out" "car dash"
 * format (311 bytes, little-endian; the layout is the one the community documents, see docs/simhub-telemetry.md). SimHub
 * has no generic UDP input for a game of its own, but it reads this format natively, so with the game set to
 * "Forza Motorsport 7" in SimHub the dashboards, LEDs, bass shakers and motion rigs work without a plugin.
 *
 * Settings (environment):
 *   BO3_SIMHUB_UDP=<port> | <host>:<port>   on, and where to send (a port alone means this computer, 127.0.0.1)
 *   BO3_SIMHUB_RPM=0                        send rpm 0 and gear 1 instead of the made-up ones below
 *   BO3_SIMHUB_LOG=1                        log what is sent
 *
 * What is real and what is not. From the snapshot: speed, the accelerations, the turn rates, wheel spin, wheel slip, the
 * pedals, steering, position and the impacts. Not in the game's car (Burnout 3 has no gearbox): rpm and gear. They are
 * made up from the speed (five bands, the rpm rising through each) so that rev lights and engine shakers do something.
 * The signs of the accelerations and the wheel order are as the snapshot gives them (carstate.h), unverified in SimHub.
 *
 * One thread, 60 packets a second while a race runs (a snapshot no older than 300 ms); one packet with the race off when
 * it stops (a pause, a menu), then silence until the next race.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_BAD (-1)
#endif

#include "carstate.h"
#include "simhub.h"

#define PKT_SIZE 311
#define HZ 60
#define MAX_AGE_MS 300u

static struct sockaddr_in s_to;
static sock_t s_sock = SOCK_BAD;
static int s_fake_rpm = 1, s_log;

/* Little-endian writers into the packet being built. */
static uint8_t *s_p;
static void put_f(double v) { float f = (float)v; memcpy(s_p, &f, 4); s_p += 4; }
static void put_i(int32_t v) { memcpy(s_p, &v, 4); s_p += 4; }
static void put_u(uint32_t v) { memcpy(s_p, &v, 4); s_p += 4; }
static void put_u16(uint16_t v) { memcpy(s_p, &v, 2); s_p += 2; }
static void put_b(uint8_t v) { *s_p++ = v; }
static uint8_t to_u8(double v) { return (uint8_t)(v < 0.0 ? 0.0 : v > 255.0 ? 255.0 : v + 0.5); }
static uint8_t to_s8(double v) { return (uint8_t)(int8_t)(v < -127.0 ? -127.0 : v > 127.0 ? 127.0 : (v < 0.0 ? v - 0.5 : v + 0.5)); }

/* Rpm and gear from the speed: Burnout 3 has neither. Five bands over 0..70 m/s, 3000..8000 rpm through each, idle at a stop. */
static void fake_engine(double speed, double *rpm, int *gear)
{
    const double band = 14.0, lo = 3000.0, hi = 8000.0;
    int g = (int)(speed / band);

    if (speed < 0.5) {
        *rpm = 1000.0;
        *gear = 1;
        return;
    }
    if (g > 4)
        g = 4;
    *gear = g + 1;
    {
        double t = (speed - g * band) / band;

        t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
        *rpm = lo + (hi - lo) * t;
    }
}

/* One packet. cs NULL: the race is off (IsRaceOn 0, the rest zero). dist: metres driven this race; rumble 0..1: the
 * surface rumble the impacts make (the bass shakers' road feel). */
static void build(uint8_t *buf, const bo3_carstate *cs, double dist, double rumble)
{
    double speed = 0.0, rpm = 0.0, vx = 0.0, vz = 0.0;
    int gear = 0, i;

    memset(buf, 0, PKT_SIZE);
    s_p = buf;
    if (cs) {
        speed = (double)cs->speed;
        vx = speed * sin((double)cs->slip_angle);
        vz = speed * cos((double)cs->slip_angle);
        if (s_fake_rpm)
            fake_engine(fabs(speed), &rpm, &gear);
        else
            gear = 1;
    }
    /* ---- the "sled" part: 232 bytes ---- */
    put_i(cs ? 1 : 0);                                      /* IsRaceOn */
    put_u(cs ? (uint32_t)(cs->race_time * 1000.0f) : 0u);   /* TimestampMS */
    put_f(8000.0);                                          /* EngineMaxRpm */
    put_f(1000.0);                                          /* EngineIdleRpm */
    put_f(rpm);                                             /* CurrentEngineRpm */
    put_f(cs ? cs->sway : 0.0);                             /* acceleration x: to the car's right */
    put_f(cs ? cs->accel_up : 0.0);                         /* y: up */
    put_f(cs ? cs->surge : 0.0);                            /* z: forward */
    put_f(vx);                                              /* velocity x (right), */
    put_f(cs ? cs->vel_up : 0.0);                           /* y (up), */
    put_f(vz);                                              /* z (forward), in the car's frame */
    put_f(cs ? cs->pitch_rate : 0.0);                       /* angular velocity x (pitch), */
    put_f(cs ? cs->yaw_rate : 0.0);                         /* y (yaw), */
    put_f(cs ? cs->roll_rate : 0.0);                        /* z (roll) */
    put_f(0.0); put_f(0.0); put_f(0.0);                     /* Yaw, Pitch, Roll angles: not in the snapshot */
    for (i = 0; i < 4; i++) put_f(0.5);                     /* NormalizedSuspensionTravel: mid travel */
    for (i = 0; i < 4; i++) put_f(cs ? cs->slip : 0.0);     /* TireSlipRatio */
    for (i = 0; i < 4; i++) put_f(cs ? cs->spin[i] : 0.0);  /* WheelRotationSpeed, rad/s */
    for (i = 0; i < 4; i++) put_f(0.0);                     /* WheelOnRumbleStrip */
    for (i = 0; i < 4; i++) put_f(0.0);                     /* WheelInPuddleDepth */
    for (i = 0; i < 4; i++) put_f(rumble);                  /* SurfaceRumble: the impacts, decaying */
    for (i = 0; i < 4; i++) put_f(cs ? cs->slip_angle : 0.0); /* TireSlipAngle */
    for (i = 0; i < 4; i++) put_f(cs ? cs->slip : 0.0);     /* TireCombinedSlip */
    for (i = 0; i < 4; i++) put_f(0.0);                     /* SuspensionTravelMeters */
    put_i(1); put_i(0); put_i(100);                         /* CarOrdinal, CarClass, CarPerformanceIndex */
    put_i(2); put_i(8);                                     /* DrivetrainType (AWD), NumCylinders */
    /* ---- the "dash" part: 79 bytes ---- */
    put_f(cs ? cs->pos[0] : 0.0); put_f(cs ? cs->pos[1] : 0.0); put_f(cs ? cs->pos[2] : 0.0);
    put_f(fabs(speed));                                     /* Speed, m/s */
    put_f(0.0); put_f(0.0);                                 /* Power, Torque */
    for (i = 0; i < 4; i++) put_f(0.0);                     /* TireTemp */
    put_f(0.0);                                             /* Boost */
    put_f(0.0);                                             /* Fuel */
    put_f(dist);                                            /* DistanceTraveled */
    put_f(0.0); put_f(0.0);                                 /* BestLap, LastLap */
    put_f(cs ? cs->race_time : 0.0);                        /* CurrentLap */
    put_f(cs ? cs->race_time : 0.0);                        /* CurrentRaceTime */
    put_u16(0);                                             /* LapNumber */
    put_b(0);                                               /* RacePosition */
    put_b(to_u8(cs ? cs->throttle * 255.0 : 0.0));          /* Accel */
    put_b(to_u8(cs ? cs->brake * 255.0 : 0.0));             /* Brake */
    put_b(0);                                               /* Clutch */
    put_b(0);                                               /* HandBrake */
    put_b((uint8_t)gear);                                   /* Gear */
    put_b(to_s8(cs ? cs->steer * 127.0 : 0.0));             /* Steer, -127..127 */
    put_b(0);                                               /* NormalizedDrivingLine */
    put_b(0);                                               /* NormalizedAIBrakeDifference */
}

static void send_pkt(const uint8_t *buf)
{
    sendto(s_sock, (const char *)buf, PKT_SIZE, 0, (const struct sockaddr *)&s_to, sizeof s_to);
}

static void sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };

    nanosleep(&ts, NULL);
#endif
}

#ifdef _WIN32
static DWORD WINAPI run(LPVOID arg)
#else
static void *run(void *arg)
#endif
{
    uint8_t buf[PKT_SIZE];
    bo3_carstate cs;
    int was_on = 0;
    double dist = 0.0, rumble = 0.0, last_t = 0.0;
    uint32_t last_impact = 0, sent = 0;

    (void)arg;
    for (;;) {
        if (bo3_carstate_get(&cs, MAX_AGE_MS)) {
            double dt = 1.0 / HZ;

            if (!was_on || cs.race_time < last_t - 1.0f)    /* a new race: the distance starts again */
                dist = 0.0;
            last_t = cs.race_time;
            dist += fabs((double)cs.speed) * dt;
            if (cs.impact_count != last_impact) {
                last_impact = cs.impact_count;
                if (cs.impact_strength > 0.0f) {
                    double k = (double)cs.impact_strength / 6000.0;

                    rumble = k > 1.0 ? 1.0 : sqrt(k);      /* the same compression the wheel's hits use */
                }
            }
            rumble *= 0.88;                                 /* ~0.25 s decay at 60 Hz */
            build(buf, &cs, dist, rumble);
            send_pkt(buf);
            was_on = 1;
            if (s_log && (++sent % 60u) == 1u)
                fprintf(stderr, "[SIMHUB] t %.1f s, %.1f m/s, rpm %.0f (made up), %u packets\n", (double)cs.race_time,
                        (double)cs.speed, 0.0, sent);
        } else if (was_on) {
            build(buf, NULL, 0.0, 0.0);
            send_pkt(buf);
            was_on = 0;
            rumble = 0.0;
        }
        sleep_ms(1000u / HZ);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

void bo3_simhub_start(void)
{
    const char *e = getenv("BO3_SIMHUB_UDP");
    char host[128] = "127.0.0.1";
    const char *colon;
    int port;

    if (!e || !*e)
        return;
    colon = strrchr(e, ':');
    if (colon) {
        size_t n = (size_t)(colon - e);

        if (n >= sizeof host)
            n = sizeof host - 1;
        memcpy(host, e, n);
        host[n] = 0;
        port = atoi(colon + 1);
    } else {
        port = atoi(e);
    }
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "[SIMHUB] BO3_SIMHUB_UDP=%s: not a port, SimHub telemetry stays off\n", e);
        return;
    }
    s_fake_rpm = !(getenv("BO3_SIMHUB_RPM") && getenv("BO3_SIMHUB_RPM")[0] == '0');
    s_log = getenv("BO3_SIMHUB_LOG") && getenv("BO3_SIMHUB_LOG")[0] == '1';
#ifdef _WIN32
    {
        WSADATA w;

        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
            fprintf(stderr, "[SIMHUB] WSAStartup failed, SimHub telemetry stays off\n");
            return;
        }
    }
#endif
    memset(&s_to, 0, sizeof s_to);
    s_to.sin_family = AF_INET;
    s_to.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &s_to.sin_addr) != 1) {
        fprintf(stderr, "[SIMHUB] BO3_SIMHUB_UDP=%s: '%s' is not an IPv4 address, SimHub telemetry stays off\n", e, host);
        return;
    }
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock == SOCK_BAD) {
        fprintf(stderr, "[SIMHUB] no UDP socket, SimHub telemetry stays off\n");
        return;
    }
    bo3_carstate_request();                 /* before main.c's drive_log_start: the sampler runs only if asked */
#ifdef _WIN32
    {
        HANDLE h = CreateThread(NULL, 0, run, NULL, 0, NULL);

        if (h)
            CloseHandle(h);
    }
#else
    {
        pthread_t t;

        if (pthread_create(&t, NULL, run, NULL) == 0)
            pthread_detach(t);
    }
#endif
    fprintf(stderr, "[SIMHUB] sending Forza Motorsport 7 'car dash' packets (%d bytes, %d Hz) to %s:%d\n", PKT_SIZE, HZ,
            host, port);
}
