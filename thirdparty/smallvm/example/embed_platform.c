// embed_platform.c — Platform stubs for embedding the MicroBlocks VM.
//
// The VM in vm/ is written for microcontrollers, so it forward-declares a
// large set of "platform" functions (timing, GPIO, BLE, display, serial,
// persistence, etc.) and expects each board port to provide them. When we
// embed the VM in an arbitrary host program, we don't have any of those
// peripherals, so this file:
//
//   * provides real implementations for the handful we DO need:
//       - timing functions (microsecs/millisecs/...)
//       - the IDE serial transport, here backed by a pseudo-terminal
//       - the persistent code store, backed by a plain file on disk
//   * provides empty stubs for everything else, so the linker is happy.
//
// Compiled with -D GNUBLOCKS, which is the same define the existing
// linux+pi/buildVMLinux.sh uses to take the "desktop simulator" code paths
// inside the VM (e.g. file-backed persistence, no BLE, etc.).

#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "mem.h"
#include "interp.h"
#include "persist.h"

// ---------- Timing -----------------------------------------------------------

static long startSecs = 0;

static void embed_initTimers(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    startSecs = now.tv_sec;
}

uint32 microsecs(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint32) ((1000000UL * (now.tv_sec - startSecs)) + now.tv_usec);
}

uint32 millisecs(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint32) ((1000UL * (now.tv_sec - startSecs)) + (now.tv_usec / 1000));
}

uint64 totalMicrosecs(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint64) ((1000000ULL * (now.tv_sec - startSecs)) + now.tv_usec);
}

uint32 seconds(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint32) (now.tv_sec - startSecs);
}

void handleMicosecondClockWrap(void) {} // 64-bit timer, no wrap needed on Linux

void delay(unsigned long ms) {
    if (ms == 0) return;
    usleep((useconds_t) ms * 1000);
}

// ---------- Pseudo-terminal IDE transport -----------------------------------

static int pty_fd = -1;

int embed_open_pty(void) {
    pty_fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (pty_fd < 0) return -1;

    struct termios settings;
    tcgetattr(pty_fd, &settings);
    cfmakeraw(&settings);
    tcsetattr(pty_fd, TCSANOW, &settings);
    grantpt(pty_fd);
    unlockpt(pty_fd);

    // Also drop the path in a well-known place, matching the convention
    // used by the linux+pi VM so that other tooling that looks there keeps
    // working.
    FILE *f = fopen("/tmp/ublocksptyname", "w");
    if (f) {
        fprintf(f, "%s", ptsname(pty_fd));
        fclose(f);
    }
    return pty_fd;
}

const char *embed_pty_name(void) {
    return pty_fd >= 0 ? ptsname(pty_fd) : "(not open)";
}

void embed_close_pty(void) {
    if (pty_fd >= 0) {
        close(pty_fd);
        pty_fd = -1;
    }
    remove("/tmp/ublocksptyname");
}

int recvBytes(uint8 *buf, int count) {
    if (pty_fd < 0) return 0;
    int n = (int) read(pty_fd, buf, count);
    return n < 0 ? 0 : n;
}

int sendBytes(uint8 *buf, int start, int end) {
    if (pty_fd < 0) return 0;
    int n = (int) write(pty_fd, &buf[start], end - start);
    return n < 0 ? 0 : n;
}

// captureIncomingBytes is provided by runtime.c; we don't override it.
void restartSerial(void) {}

// With GNUBLOCKS defined, runtime.c does NOT compile its own ideConnected()
// (see the "#if !defined(GNUBLOCKS) || defined(EMSCRIPTEN)" guard). We're
// expected to provide one. "Connected" here means the PTY is open and the
// IDE has sent us a message in the last few seconds — same heuristic the
// stock Arduino build uses.
int ideConnected(void) {
    if (pty_fd < 0) return 0;
    if (lastRcvTime == 0) {
        // No message yet, but if a reader is attached to the PTY at all we
        // still want outputString/sendData to flow. Treat the PTY being
        // open as "connected" until we hear from the IDE for the first
        // time; the lastRcvTime check below takes over afterwards.
        return 1;
    }
    uint32 now = microsecs();
    uint32 elapsed = (lastRcvTime > now) ? now : (now - lastRcvTime);
    return elapsed < (5 * 1000000);
}

// ---------- Persistent code store (file-backed) -----------------------------
//
// With GNUBLOCKS + USE_CODE_FILE, persist.c keeps a RAM-resident "flash"
// buffer and mirrors writes to a file so that scripts survive restarts.
// We're responsible for managing the file. This mirrors what
// linux+pi/linux.c does.

static const char *codeFileName = "ublockscode.bin";
static FILE *codeFile = NULL;

// Setter so embed_main.cpp can point the persistent store at a user-supplied
// path (e.g. ./vm_embed test.ubcode). Must be called before primsInit() so
// that the first restoreScripts() reads from the right file.
void embed_set_code_file(const char *path) {
    if (path && *path) codeFileName = path;
}

int initCodeFile(uint8 *flash, int flashByteCount) {
    codeFile = fopen(codeFileName, "ab+");
    if (!codeFile) return 0;
    fseek(codeFile, 0L, SEEK_END);
    long fileSize = ftell(codeFile);
    if (fileSize > flashByteCount) fileSize = flashByteCount;
    fseek(codeFile, 0L, SEEK_SET);
    if (fileSize > 0) {
        size_t got = fread(flash, 1, fileSize, codeFile);
        (void) got;
    }
    return 1;
}

void initFileSystem(void) {} // no-op; nothing to mount on a host filesystem

void writeCodeFile(uint8 *code, int byteCount) {
    if (!codeFile) return;
    fwrite(code, 1, byteCount, codeFile);
    fflush(codeFile);
}

void writeCodeFileWord(int word) {
    if (!codeFile) return;
    fwrite(&word, 1, 4, codeFile);
    fflush(codeFile);
}

void clearCodeFile(int cycleCount) {
    if (codeFile) fclose(codeFile);
    remove(codeFileName);
    codeFile = fopen(codeFileName, "ab+");
    if (!codeFile) return;
    uint32 header = ('S' << 24) | (cycleCount & 0xFFFFFF);
    fwrite(&header, 1, 4, codeFile);
    fflush(codeFile);
}

// File primitives that persist.c references through this interface (we don't
// implement an in-VM file system, so these are no-ops).
void createFile(const char *fileName)     { (void) fileName; }
void deleteFile(const char *fileName)     { (void) fileName; }
int  fileExists(const char *fileName)     { (void) fileName; return 0; }
int  hasStartupSnapshot(void)             { return 0; }
void snapshotCodeToFile(char *n, int b)   { (void) n; (void) b; }
void loadCodeSnapshot(char *fileName)     { (void) fileName; }

// ---------- Board / I/O stubs -----------------------------------------------
//
// None of these matter for an embedded host — they only exist because the
// VM is written assuming a board is present. The few values that ARE
// observable through MicroBlocks scripts are kept sensible.

const char *boardType(void)               { return "Embed"; }
void hardwareInit(void)                   {}
int  readI2CReg(int d, int r)             { (void) d; (void) r; return 0; }
void writeI2CReg(int d, int r, int v)     { (void) d; (void) r; (void) v; }
int  hasI2CPullups(void)                  { return 0; }

int  pinCount(void)                       { return 0; }
int  mapDigitalPinNum(int p)              { return p; }
void setPinMode(int p, int m)             { (void) p; (void) m; }
void turnOffPins(void)                    {}
void updateMicrobitDisplay(void)          {}
void resetRadio(void)                     {}
// hasPSRAM, resetTimer, checkButtons, processStartupGesture, and
// captureIncomingBytes are all provided by the VM core (mem.c / interp.c /
// runtime.c) so we don't redefine them here.
void stopPWM(void)                        {}
void stopServos(void)                     {}
void stopTone(void)                       {}
int  readAnalogMicrophone(void)           { return 0; }
void setPicoEdSpeakerPin(int p)           { (void) p; }
void showMicroBitPixels(int b, int x, int y) { (void) b; (void) x; (void) y; }
void setAllNeoPixels(int p, int n, int c) { (void) p; (void) n; (void) c; }
void turnOffInternalNeoPixels(void)       {}
void lightSleep(int msecs)                { delay((unsigned long) msecs); }
void deepSleep(int secs)                  { delay((unsigned long) secs * 1000); }
void systemReset(void)                    {}

int mbDisplayColor = 0xFFFFFF;
int useTFT = 0;
int isCodingBox = 0;

// TFT stubs
void tftInit(void)                        {}
void tftClear(void)                       {}
void tftSetHugePixel(int x, int y, int s) { (void) x; (void) y; (void) s; }
void tftSetHugePixelBits(int bits)        { (void) bits; }

// CoCube stubs
void cocubeSensorInit(void)               {}
void cocubeSensorUpdate(void)             {}

// BLE state + stubs
int  BLE_allowShutdown = 0;
int  BLE_connected_to_IDE = 0;
int  bleRunning = 0;
char BLE_ThreeLetterID[4] = "abc";

void BLE_initThreeLetterID(void)          {}
void BLE_start(void)                      {}
void BLE_stop(void)                       {}
void BLE_pauseAdvertising(void)           {}
void BLE_resumeAdvertising(void)          {}
void BLE_setPicoAdvertisingData(char *n, const char *u) { (void) n; (void) u; }
void BLE_setEnabled(int f)                { (void) f; }
int  BLE_isEnabled(void)                  { return 0; }
void BLE_UART_ReceiveCallback(uint8 *d, int n) { (void) d; (void) n; }
void BLE_UART_Send(uint8 *d, int n)       { (void) d; (void) n; }
void getMACAddress(uint8 *sixBytes)       { memset(sixBytes, 0, 6); }

// File-transfer / fileSys helpers that the VM expects on storage-bearing
// boards. We don't support file transfer, so just ignore the message.
void processFileMessage(int msgType, int dataSize, char *data) {
    (void) msgType; (void) dataSize; (void) data;
}

// ---------- I/O primitive stubs ---------------------------------------------
//
// These are the per-primitive C functions that the matching "addXxxPrims"
// registration would otherwise hook up. We provide minimal returns; the
// custom-host build doesn't expose any of these blocks anyway.

OBJ primAnalogPins(OBJ *args)             { (void) args; return int2obj(0); }
OBJ primDigitalPins(OBJ *args)            { (void) args; return int2obj(0); }
OBJ primAnalogRead(int c, OBJ *a)         { (void) c; (void) a; return int2obj(0); }
void primAnalogWrite(OBJ *a)              { (void) a; }
OBJ primDigitalRead(int c, OBJ *a)        { (void) c; (void) a; return falseObj; }
void primDigitalWrite(OBJ *a)             { (void) a; }
void primDigitalSet(int p, int f)         { (void) p; (void) f; }
OBJ primButtonA(OBJ *args)                { (void) args; return falseObj; }
OBJ primButtonB(OBJ *args)                { (void) args; return falseObj; }
void primSetUserLED(OBJ *args)            { (void) args; }

OBJ primI2cExists(int c, OBJ *a)          { (void) c; (void) a; return falseObj; }
OBJ primI2cGet(OBJ *a)                    { (void) a; return int2obj(0); }
OBJ primI2cSet(OBJ *a)                    { (void) a; return falseObj; }
OBJ primSPISend(OBJ *a)                   { (void) a; return falseObj; }
OBJ primSPIRecv(OBJ *a)                   { (void) a; return int2obj(0); }

OBJ primMBDisplay(int c, OBJ *a)          { (void) c; (void) a; return falseObj; }
OBJ primMBDisplayOff(int c, OBJ *a)       { (void) c; (void) a; return falseObj; }
OBJ primMBEnableDisplay(int c, OBJ *a)    { (void) c; (void) a; return falseObj; }
OBJ primMBPlot(int c, OBJ *a)             { (void) c; (void) a; return falseObj; }
OBJ primMBUnplot(int c, OBJ *a)           { (void) c; (void) a; return falseObj; }
OBJ primMBDrawShape(int c, OBJ *a)        { (void) c; (void) a; return falseObj; }
OBJ primMBShapeForLetter(int c, OBJ *a)   { (void) c; (void) a; return int2obj(0); }
OBJ primMBTiltX(int c, OBJ *a)            { (void) c; (void) a; return int2obj(0); }
OBJ primMBTiltY(int c, OBJ *a)            { (void) c; (void) a; return int2obj(0); }
OBJ primMBTiltZ(int c, OBJ *a)            { (void) c; (void) a; return int2obj(0); }
OBJ primMBTemp(int c, OBJ *a)             { (void) c; (void) a; return int2obj(0); }
OBJ primNeoPixelSend(int c, OBJ *a)       { (void) c; (void) a; return falseObj; }
OBJ primNeoPixelSetPin(int c, OBJ *a)     { (void) c; (void) a; return falseObj; }
OBJ primDeferUpdates(int c, OBJ *a)       { (void) c; (void) a; return falseObj; }
OBJ primResumeUpdates(int c, OBJ *a)      { (void) c; (void) a; return falseObj; }

// ---------- Primitive-set stubs ---------------------------------------------
//
// primsInit() in runtime.c calls every addXxxPrims() unconditionally. The
// three sets we DO want from the core VM (vars/data/misc) come from
// vm/varPrims.c, vm/dataPrims.c and vm/miscPrims.c. All the other set
// registrations are stubbed here so primsInit() runs cleanly.
//
// After primsInit() returns, main() calls addCustomHostPrims() (defined in
// custom_prims.c) which installs our application-specific blocks.

void addIOPrims(void)        {}
void addSensorPrims(void)    {}
void addSerialPrims(void)    {}
void addDisplayPrims(void)   {}
void addFilePrims(void)      {}
void addNetPrims(void)       {}
void addBLEPrims(void)       {} // intentionally empty — custom_prims.c takes this slot
void addRadioPrims(void)     {}
void addTFTPrims(void)       {}
void addHIDPrims(void)       {}
void addCameraPrims(void)    {}
void addOneWirePrims(void)   {}
void addEncoderPrims(void)   {}
void addSDCardPrims(void)    {}

// Called once before main(); not strictly required by the VM, but lets us
// initialize the clock origin so seconds()/microsecs() start at zero.
__attribute__((constructor))
static void embed_platform_ctor(void) {
    embed_initTimers();
}
