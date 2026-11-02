/*
 * heypcb-esp32-sidecar: runs an ESP32 (classic, Xtensa LX6) firmware image
 * on lcgamboa/qemu's esp32-picsimlab machine (libqemu-xtensa.so, loaded with
 * dlopen) and turns the machine's callbacks into a line protocol on
 * stdin/stdout, so every device model (sensors, displays) lives in the
 * program that drives it, outside this GPL process. It is the ESP32-C3's
 * heypcb-c3-sidecar (../esp32c3-qemu/sidecar.c) for the classic chip: the
 * same protocol, line for line, so one client reads both.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * It links QEMU (GPLv2) at run time; README.md says where both sources are.
 *
 * Usage: heypcb-esp32-sidecar <libqemu-xtensa.so> <rom dir> <flash.bin>
 * The QEMU arguments are FIXED here and never taken from the caller: the
 * library carries slirp, socket chardevs and a gdb stub, reached only through
 * options, and its socket measurement holds for exactly these arguments. The
 * flash path's commas are doubled (QEMU's option escape) so it stays one value.
 *
 * Time is the machine's own: `-icount shift=4,sleep=off` counts it in
 * instructions (16 ns each), so a callback waiting on the engine costs the
 * firmware nothing, and an idle firmware's waits are skipped (a batch run goes
 * faster than real time). Every "t" below is microseconds of that clock
 * (QEMU_CLOCK_VIRTUAL); without it the sidecar refuses to run.
 *
 * shift=4 (62.5 M instructions per simulated second; the chip runs 240 MHz),
 * not the C3's shift=3: the machine runs two Xtensa cores on one host
 * thread, and a busy loop() cost 3.1-4.1 s of host CPU per simulated second
 * at shift=3, 2.4 s at shift=4 (measured, 2026-11-02). Timers and millis()
 * follow the virtual clock and stay exact (a 250 ms blink is 250.0 ms); only
 * code timed by counting instructions runs about 3.8x slow.
 *
 * Pins: GPIO0..39 as the chip numbers them. The machine reports outputs and
 * directions for GPIO0..31 only (its GPIO block models GPIO_OUT/ENABLE, not
 * GPIO_OUT1/ENABLE1), so GPIO32 and GPIO33 driven as outputs are never
 * reported; every GPIO0..39 can be driven as an input. GPIO20, 24 and 28..31
 * do not exist on the chip and are not mapped.
 *
 * Events, stdout, one per line:
 *   R <t>                            ready: the machine is built, the clock starts here
 *   T <t>                            the clock, every 50 ms of wall time (a quiet firmware still advances)
 *   G <gpio> <0|1> <t>               the firmware drove a pin
 *   D <gpio> <in|out> <t>            a pin's direction changed
 *   U <uart> <hex bytes> <t>         serial output, flushed at newline or 64 bytes
 *   I <seq> <bus> <addr> <kind> <data> <t>
 *       an I2C event; kinds as the fork's i2c_event: 0 START_RECV,
 *       1 START_SEND, 2 START_SEND_ASYNC, 3 FINISH, 4 NACK, 5 WRITE (data =
 *       the byte), 6 READ. For kinds 0, 1, 2 and 6 <seq> is nonzero and the
 *       sidecar waits for "A <seq> <value>"; other kinds carry seq 0.
 * Commands, stdin, one per line:
 *   A <seq> <value>    the answer to question <seq>: START 0 = ACK, nonzero = NACK;
 *                      READ = the byte. An answer to any other seq is dropped.
 *   P <gpio> <0|1>     drive an input pin
 *   V <channel> <raw>  an ADC channel's value
 *   Q                  stop (end of stdin stops too)
 * Unanswered for 2 s of wall time, a START is NACKed and a READ answers 0xFF,
 * and "E unanswered <seq>" says so.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    void (*write_pin)(int pin, int value);
    void (*dir_pin)(int pin, int value);
    int (*i2c_event)(const uint8_t id, const uint8_t addr, const uint16_t event);
    uint8_t (*spi_event)(const uint8_t id, const uint16_t event);
    void (*uart_tx_event)(const uint8_t id, const uint8_t value);
    const short int *pinmap;
    void (*rmt_event)(const uint8_t channel, const uint32_t config0, const uint32_t value);
} callbacks_t;

/* Board pins 1..40 are GPIO0..39 (the fork reports pins by this index); -1
 * is a GPIO the chip does not have, which the machine leaves unconnected. */
static const short int pinmap[] = {40,
                                   0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
                                   10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                   -1, 21, 22, 23, -1, 25, 26, 27, -1, -1,
                                   -1, -1, 32, 33, 34, 35, 36, 37, 38, 39};

static int64_t (*clock_ns)(int type);
static void (*set_pin)(int pin, int value);
static void (*set_apin)(int chn, int value);
static pthread_mutex_t out_lock = PTHREAD_MUTEX_INITIALIZER;

static long long now_us(void) { return (long long)(clock_ns(1 /* QEMU_CLOCK_VIRTUAL */) / 1000); }

static void emit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void emit(const char *fmt, ...) {
    va_list ap;
    pthread_mutex_lock(&out_lock);
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
    pthread_mutex_unlock(&out_lock);
}

/* ---- answers to I2C questions, handed over from the stdin thread by sequence ---- */
static pthread_mutex_t ans_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ans_cond = PTHREAD_COND_INITIALIZER;
static unsigned long asking;   /* the question waiting, 0 when none */
static int ans_ready;
static long ans_value;
static unsigned long next_seq;

/* Registered before the question is sent, so an answer can never arrive first. */
static void expect(unsigned long seq) {
    pthread_mutex_lock(&ans_lock);
    asking = seq;
    ans_ready = 0;
    pthread_mutex_unlock(&ans_lock);
}

static long ask(unsigned long seq, long fallback) {
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += 2;
    pthread_mutex_lock(&ans_lock);
    while (!ans_ready) {
        if (pthread_cond_timedwait(&ans_cond, &ans_lock, &until) == ETIMEDOUT) break;
    }
    int got = ans_ready;
    long v = got ? ans_value : fallback;
    asking = 0; /* a late answer to this seq is dropped */
    ans_ready = 0;
    pthread_mutex_unlock(&ans_lock);
    if (!got) emit("E unanswered %lu\n", seq);
    return v;
}

static void cb_write_pin(int pin, int value) {
    if (pin > 0 && pin <= pinmap[0] && pinmap[pin] >= 0) emit("G %d %d %lld\n", pinmap[pin], value ? 1 : 0, now_us());
}
static void cb_dir_pin(int pin, int dir) {
    /* pin -1 is the machine's "sync" (a register the GPIO matrix reads changed): not a pin. */
    if (pin > 0 && pin <= pinmap[0] && pinmap[pin] >= 0) emit("D %d %s %lld\n", pinmap[pin], dir ? "out" : "in", now_us());
}
static int cb_i2c(const uint8_t id, const uint8_t addr, const uint16_t ev) {
    unsigned kind = ev & 0xff, data = ev >> 8;
    int question = kind <= 2 || kind == 6;
    unsigned long seq = question ? ++next_seq : 0;
    if (question) expect(seq);
    emit("I %lu %u %u %u %u %lld\n", seq, id, addr, kind, data, now_us());
    if (!question) return 0;
    if (kind <= 2) return (int)ask(seq, 1); /* START: nonzero NACKs */
    return (int)(ask(seq, 0xff) & 0xff);
}
static uint8_t cb_spi(const uint8_t id, const uint16_t ev) {
    (void)id;
    (void)ev;
    return 0xff;
}

static char uart_buf[3][64];
static int uart_len[3];
static pthread_mutex_t uart_lock = PTHREAD_MUTEX_INITIALIZER;
static void uart_flush_locked(int id) {
    if (!uart_len[id]) return;
    char hex[64 * 2 + 1];
    for (int i = 0; i < uart_len[id]; i++) sprintf(hex + 2 * i, "%02x", (unsigned char)uart_buf[id][i]);
    emit("U %d %s %lld\n", id, hex, now_us());
    uart_len[id] = 0;
}
static void cb_uart(const uint8_t id, const uint8_t v) {
    if (id > 2) return;
    pthread_mutex_lock(&uart_lock);
    uart_buf[id][uart_len[id]++] = (char)v;
    if (v == '\n' || uart_len[id] == (int)sizeof uart_buf[id]) uart_flush_locked(id);
    pthread_mutex_unlock(&uart_lock);
}
static void cb_rmt(const uint8_t c, const uint32_t a, const uint32_t b) {
    (void)c;
    (void)a;
    (void)b;
}

static void stop(void) {
    pthread_mutex_lock(&uart_lock);
    for (int i = 0; i < 3; i++) uart_flush_locked(i);
    pthread_mutex_unlock(&uart_lock);
    emit("X %lld\n", now_us());
    _exit(0);
}

static void *stdin_loop(void *arg) {
    (void)arg;
    char line[128];
    while (fgets(line, sizeof line, stdin)) {
        unsigned long seq = 0;
        long a = 0, b = 0;
        if (line[0] == 'A' && sscanf(line + 1, "%lu %ld", &seq, &a) == 2) {
            pthread_mutex_lock(&ans_lock);
            if (seq && seq == asking) {
                ans_value = a;
                ans_ready = 1;
                pthread_cond_signal(&ans_cond);
            }
            pthread_mutex_unlock(&ans_lock);
        } else if (line[0] == 'P' && sscanf(line + 1, "%ld %ld", &a, &b) == 2 && set_pin) {
            for (int i = 1; i <= pinmap[0]; i++)
                if (pinmap[i] >= 0 && pinmap[i] == a) set_pin(i, b ? 1 : 0);
        } else if (line[0] == 'V' && sscanf(line + 1, "%ld %ld", &a, &b) == 2 && set_apin) {
            if (a >= 0 && a < 16) set_apin((int)a, (int)b);
        } else if (line[0] == 'Q') {
            break;
        }
    }
    stop(); /* Q, or the engine closed our stdin: it is done with this board */
    return NULL;
}

static void *tick_loop(void *arg) {
    (void)arg;
    for (;;) {
        struct timespec d = {0, 50 * 1000 * 1000};
        nanosleep(&d, NULL);
        emit("T %lld\n", now_us());
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <libqemu-xtensa.so> <rom dir> <flash.bin>\n", argv[0]);
        return 2;
    }
    if (strlen(argv[3]) > 1024 || strlen(argv[2]) > 1024) {
        fprintf(stderr, "refused: a path over 1024 bytes\n");
        return 2;
    }
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    void (*reg)(void *) = dlsym(h, "qemu_picsimlab_register_callbacks");
    void (*qinit)(int, char **) = dlsym(h, "qemu_init");
    int (*qloop)(void) = dlsym(h, "qemu_main_loop");
    clock_ns = dlsym(h, "qemu_clock_get_ns");
    set_pin = dlsym(h, "qemu_picsimlab_set_pin");
    set_apin = dlsym(h, "qemu_picsimlab_set_apin");
    if (!reg || !qinit || !qloop) {
        fprintf(stderr, "refused: the library is not the picsimlab fork\n");
        return 1;
    }
    /* Honoured or refused: wall time is never passed off as the machine's. */
    if (!clock_ns) {
        fprintf(stderr, "refused: the library exports no qemu_clock_get_ns, so the machine's time cannot be read\n");
        return 1;
    }
    static callbacks_t cb = {cb_write_pin, cb_dir_pin, cb_i2c, cb_spi, cb_uart, pinmap, cb_rmt};
    reg(&cb);

    /* The flash path as one option value: QEMU escapes a comma by doubling it. */
    char drive[2200] = "file=";
    size_t n = strlen(drive);
    for (const char *p = argv[3]; *p; p++) {
        if (*p == ',') drive[n++] = ',';
        drive[n++] = *p;
    }
    drive[n] = 0;
    strcat(drive, ",if=mtd,format=raw");
    /* Fixed: no -netdev, no -chardev socket, no -gdb, no -monitor. Timer
     * group 0's watchdog, which the machine arms at reset, is off, as on the C3. */
    char *qargv[] = {argv[0], "-M", "esp32-picsimlab", "-nographic", "-nodefaults",
                     "-L", argv[2], "-drive", drive,
                     "-global", "driver=timer.esp32.timg,property=wdt_disable,value=true",
                     "-icount", "shift=4,sleep=off", "-nic", "none", NULL};
    int qargc = (int)(sizeof qargv / sizeof qargv[0]) - 1;

    qinit(qargc, qargv);
    emit("R %lld\n", now_us());
    pthread_t t;
    pthread_create(&t, NULL, stdin_loop, NULL);
    pthread_create(&t, NULL, tick_loop, NULL);
    return qloop();
}
