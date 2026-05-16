// small_vm_platform.c — Platform stubs for the MicroBlocks VM, GDExtension flavor.
//
// This is a stripped-down sibling of thirdparty/smallvm/example/embed_platform.c.
// The example file targets a standalone executable that talks to the
// MicroBlocks IDE over a pseudo-terminal and persists scripts to a file on
// disk. Inside a Godot GDExtension we don't need either of those — the host
// hands us pre-compiled bytecode in a PackedByteArray, so we only need:
//
//   * timing functions the VM uses (microsecs/millisecs/...)
//   * a stand-in for the file-backed code store, served by an in-memory
//     pointer that the host can swap before each restoreScripts() call
//   * stubs for every hardware function the VM forward-declares but the
//     embedded host doesn't use
//
// Compiled with -D GNUBLOCKS so the VM takes its "desktop simulator" code
// paths (no Arduino HAL, USE_CODE_FILE persistence model, our own
// ideConnected()).

#define _DEFAULT_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "mem.h"
#include "interp.h"
#include "persist.h"

#include "small_vm_platform.h"

// ---------- Timing -----------------------------------------------------------

static long startSecs = 0;

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

void handleMicosecondClockWrap(void) {}

void delay(unsigned long ms) {
    if (ms == 0) return;
    usleep((useconds_t) ms * 1000);
}

// ---------- IDE transport: not used --------------------------------------
//
// We never speak the IDE protocol in the GDExtension build. Reading returns
// "no bytes available"; sending pretends to have written. ideConnected()
// is required because runtime.c only compiles its own implementation when
// GNUBLOCKS is *not* defined.

int recvBytes(uint8 *buf, int count)   { (void) buf; (void) count; return 0; }
int sendBytes(uint8 *buf, int s, int e) { (void) buf; return e - s; }
void restartSerial(void) {}

int ideConnected(void) { return 0; }

// ---------- In-memory bytecode buffer ---------------------------------------
//
// persist.c's RAM_CODE_STORE configuration drives the VM through this
// interface: it calls initCodeFile(flash, HALF_SPACE) at restoreScripts()
// time to populate the in-memory flash buffer. We satisfy it from a pointer
// the host has set, instead of a file on disk.
//
// Format expected: a `ublockscode.bin`-style byte stream — i.e. a 4-byte
// 'S' cycle header followed by persistent records (the same thing the
// linux+pi VM writes to disk while the IDE is attached).

static const uint8_t *embed_bytecode_data = NULL;
static int            embed_bytecode_size = 0;

void embed_set_bytecode(const uint8_t *data, int size) {
    embed_bytecode_data = data;
    embed_bytecode_size = (size > 0) ? size : 0;
}

int initCodeFile(uint8 *flash, int flashByteCount) {
    if (!embed_bytecode_data || embed_bytecode_size <= 0) {
        return 0;
    }
    int n = (embed_bytecode_size < flashByteCount) ? embed_bytecode_size
                                                   : flashByteCount;
    memcpy(flash, embed_bytecode_data, n);
    return n;
}

void initFileSystem(void) {}

// The VM still writes back to the code store as scripts run (e.g. when
// chunks are added). We ignore those writes — the host owns the bytecode.
void writeCodeFile(uint8 *code, int byteCount) { (void) code; (void) byteCount; }
void writeCodeFileWord(int word)               { (void) word; }
void clearCodeFile(int cycleCount)             { (void) cycleCount; }

// File primitives the VM expects on storage-bearing boards. We don't
// support file I/O from scripts.
void createFile(const char *fileName)   { (void) fileName; }
void deleteFile(const char *fileName)   { (void) fileName; }
int  fileExists(const char *fileName)   { (void) fileName; return 0; }
int  hasStartupSnapshot(void)           { return 0; }
void snapshotCodeToFile(char *n, int b) { (void) n; (void) b; }
void loadCodeSnapshot(char *fileName)   { (void) fileName; }

void processFileMessage(int msgType, int dataSize, char *data) {
    (void) msgType; (void) dataSize; (void) data;
}

// ---------- Board / I/O stubs -----------------------------------------------

const char *boardType(void)               { return "Godot"; }
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

// ---------- I/O primitive stubs ---------------------------------------------

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
// three sets we DO want from the core VM (vars/data/misc) come from the
// VM's own sources. Every other set registration is stubbed here so
// primsInit() runs cleanly. small_vm.cpp then calls addCustomHostPrims()
// after primsInit() to install our Godot-aware blocks.

void addIOPrims(void)        {}
void addSensorPrims(void)    {}
void addSerialPrims(void)    {}
void addDisplayPrims(void)   {}
void addFilePrims(void)      {}
void addNetPrims(void)       {}
void addBLEPrims(void)       {}  // overwritten by addCustomHostPrims()
void addRadioPrims(void)     {}
void addTFTPrims(void)       {}
void addHIDPrims(void)       {}
void addCameraPrims(void)    {}
void addOneWirePrims(void)   {}
void addEncoderPrims(void)   {}
void addSDCardPrims(void)    {}

// Initialize the clock origin so seconds()/microsecs() start near zero.
// Called as an early ctor, before any GDExtension setup runs.
__attribute__((constructor))
static void small_vm_platform_ctor(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    startSecs = now.tv_sec;
}
