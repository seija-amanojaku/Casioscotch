// mostly stolen from the SDL backend, but this time it's gint!!!!
#include "data_win.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

// TODO: platform-agnostic way to write to the display (classpad?)
#include <gint/drivers/r61524.h>
#include <gint/keyboard.h>
#include <gint/kmalloc.h>
#include <gint/display.h>
#include <gint/clock.h>
#include <gint/prof.h>
#include <gint/rtc.h>
#include <gint/dma.h>

#include <fxlibc/printf.h>

#include <fxCGIO/fxCGIO.h>
// TODO
#include "runner_keyboard.h"
#include "runner.h"
#include "input_recording.h"
#include "debug_overlay.h"

#include "sw_renderer.h"
#include "overlay_file_system.h" // TODO: have our own FS (based around Yatis' fugue PR, the calculator's BFile interface is basically 
                                 // as slow as molasses)
#if defined(USE_OPENAL)
#include "al_audio_system.h"
#elif defined(USE_MINIAUDIO)
#include "ma_audio_system.h"
#endif
#include "noop_audio_system.h"
#include "stb_ds.h"
#include "stb_image_write.h"

#include "utils.h"
#include "profiler.h"

// ===[ COMMAND LINE ARGUMENTS ]===
typedef struct {
    int key;
    // We need this dummy value, think that the ds_map is like a Java HashMap NOT a HashSet
    // (Which is funny, because in Java HashSets are backed by HashMaps lol)
    bool value;
} FrameSetEntry;

typedef struct {
    const char* dataWinPath;
    const char* saveFolder; // null = default to the directory containing dataWinPath
    const char* screenshotPattern;
    FrameSetEntry* screenshotFrames;
    const char* screenshotSurfacesPattern;
    FrameSetEntry* screenshotSurfacesFrames;
    FrameSetEntry* dumpFrames;
    FrameSetEntry* dumpJsonFrames;
    const char* dumpJsonFilePattern;
    StringBooleanEntry* varReadsToBeTraced;
    StringBooleanEntry* varWritesToBeTraced;
    StringBooleanEntry* functionCallsToBeTraced;
    StringBooleanEntry* alarmsToBeTraced;
    StringBooleanEntry* instanceLifecyclesToBeTraced;
    StringBooleanEntry* eventsToBeTraced;
    StringBooleanEntry* collisionsToBeTraced;
    StringBooleanEntry* opcodesToBeTraced;
    StringBooleanEntry* stackToBeTraced;
    StringBooleanEntry* disassemble;
    StringBooleanEntry* tilesToBeTraced;
    bool alwaysLogUnknownFunctions;
    bool alwaysLogStubbedFunctions;
    bool headless;
    bool traceFrames;
    bool printRooms;
    bool printDeclaredFunctions;
    int exitAtFrame;
    int traceBytecodeAfterFrame;
    double speedMultiplier;
    double fastForwardSpeed;
    int seed;
    bool hasSeed;
    bool debug;
    bool traceEventInherited;
    const char* recordInputsPath;
    const char* playbackInputsPath;
    const char* renderer;
    YoYoOperatingSystem osType;
    bool lazyRooms;
    StringBooleanEntry* eagerRooms; // stb_ds string-keyed set of room names
    int profilerFramesBetween; // 0 = disabled
#ifdef ENABLE_VM_OPCODE_PROFILER
    bool opcodeProfiler;
#endif
} CommandLineArgs;

static int fbWidth, fbHeight;
//static SDL_Surface* scr;
//static bool useSWRend;

typedef struct { const char* name; YoYoOperatingSystem value; } OsTypeNameEntry;

static const OsTypeNameEntry OS_TYPE_NAMES[] = {
    {"unknown",       OS_UNKNOWN},
    {"windows",       OS_WINDOWS},
    {"win32",         OS_WINDOWS},
    {"macosx",        OS_MACOSX},
    {"macos",         OS_MACOSX},
    {"psp",           OS_PSP},
    {"ios",           OS_IOS},
    {"android",       OS_ANDROID},
    {"symbian",       OS_SYMBIAN},
    {"linux",         OS_LINUX},
    {"winphone",      OS_WINPHONE},
    {"tizen",         OS_TIZEN},
    {"win8native",    OS_WIN8NATIVE},
    {"wiiu",          OS_WIIU},
    {"3ds",           OS_3DS},
    {"psvita",        OS_PSVITA},
    {"bb10",          OS_BB10},
    {"ps4",           OS_PS4},
    {"xboxone",       OS_XBOXONE},
    {"ps3",           OS_PS3},
    {"xbox360",       OS_XBOX360},
    {"uwp",           OS_UWP},
    {"amazon",        OS_AMAZON},
    {"switch",        OS_SWITCH},
};
#define OS_TYPE_NAMES_COUNT (sizeof(OS_TYPE_NAMES)/sizeof(OS_TYPE_NAMES[0]))

static bool parseOsTypeArg(const char* s, YoYoOperatingSystem* out) {
    forEach(const OsTypeNameEntry, entry, OS_TYPE_NAMES, OS_TYPE_NAMES_COUNT) {
        if (strcmp(s, entry->name) == 0) {
            *out = entry->value;
            return true;
        }
    }
    return false;
}

static void printOsTypeNames(FILE* out) {
    forEachIndexed(const OsTypeNameEntry, entry, i, OS_TYPE_NAMES, OS_TYPE_NAMES_COUNT) {
        fprintf(out, "%s%s", i > 0 ? ", " : "", entry->name);
    }
}

static void parseCommandLineArgs(CommandLineArgs* args, int argc, char* argv[]) {
    memset(args, 0, sizeof(CommandLineArgs));

    // TODO: maybe have an UI part to parse these
#if 0
    static struct option longOptions[] = {
        {"screenshot",          required_argument, nullptr, 's'},
        {"screenshot-at-frame", required_argument, nullptr, 'f'},
        {"screenshot-surfaces", required_argument, nullptr, 'U'},
        {"screenshot-surfaces-at-frame", required_argument, nullptr, 'V'},
        {"headless",            no_argument,       nullptr, 'h'},
        {"print-rooms", no_argument,               nullptr, 'r'},
        {"print-declared-functions", no_argument,  nullptr, 'p'},
        {"trace-variable-reads", required_argument,  nullptr, 'R'},
        {"trace-variable-writes", required_argument, nullptr, 'W'},
        {"trace-function-calls", required_argument,         nullptr, 'c'},
        {"trace-alarms", required_argument,         nullptr, 'a'},
        {"trace-instance-lifecycles", required_argument,         nullptr, 'l'},
        {"trace-events", required_argument,         nullptr, 'e'},
        {"trace-collisions", required_argument,     nullptr, 'C'},
        {"trace-event-inherited", no_argument, nullptr, 'E'},
        {"trace-tiles", required_argument, nullptr, 'T'},
        {"trace-opcodes", required_argument,       nullptr, 'o'},
        {"trace-stack", required_argument,         nullptr, 'S'},
        {"trace-frames", no_argument, nullptr, 'k'},
        {"always-log-unknown-functions", no_argument, nullptr, 'y'},
        {"always-log-stubbed-functions", no_argument, nullptr, 'Y'},
        {"exit-at-frame", required_argument, nullptr, 'x'},
        {"trace-bytecode-after-frame", required_argument, nullptr, 'F'},
        {"dump-frame", required_argument, nullptr, 'd'},
        {"dump-frame-json", required_argument, nullptr, 'j'},
        {"dump-frame-json-file", required_argument, nullptr, 'J'},
        {"speed", required_argument, nullptr, 'M'},
        {"fast-forward-speed", required_argument, nullptr, 'X'},
        {"seed", required_argument, nullptr, 'Z'},
        {"debug", no_argument, nullptr, 'D'},
        {"disassemble", required_argument, nullptr, 'A'},
        {"record-inputs", required_argument, nullptr, 'I'},
        {"playback-inputs", required_argument, nullptr, 'P'},
        {"renderer", required_argument, nullptr, 'g'},
        {"lazy-rooms", no_argument, nullptr, 'z'},
        {"eager-room", required_argument, nullptr, 'G'},
        {"os-type", required_argument, nullptr, 'O'},
        {"profile-gml-scripts", required_argument, nullptr, 'q'},
        {"save-folder", required_argument, nullptr, 'B'},
#ifdef ENABLE_VM_OPCODE_PROFILER
        {"profile-opcodes", no_argument, nullptr, 'Q'},
#endif
        {nullptr,               0,                 nullptr,  0 }
    };
#endif

    args->screenshotFrames = nullptr;
    args->exitAtFrame = -1;
    args->traceBytecodeAfterFrame = 0;
    args->speedMultiplier = 1.0;
    args->fastForwardSpeed = 0.0;
    args->renderer = "software";
    args->osType = OS_WINDOWS; // I mean, the OS calls itself CASIO**WIN** :troll:
    args->profilerFramesBetween = 0;

#if 0
    int opt;
    while ((opt = getopt_long(argc, argv, "", longOptions, nullptr)) != -1) {
        switch (opt) {
            case 's':
                args->screenshotPattern = optarg;
                break;
            case 'f': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s'\n", optarg);
                    exit(1);
                }

                hmput(args->screenshotFrames, (int) frame, true);
                break;
            }
            case 'U':
                args->screenshotSurfacesPattern = optarg;
                break;
            case 'V': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --screenshot-surfaces-at-frame\n", optarg);
                    exit(1);
                }
                hmput(args->screenshotSurfacesFrames, (int) frame, true);
                break;
            }
            case 'h':
                args->headless = true;
                break;
            case 'r':
                args->printRooms = true;
                break;
            case 'p':
                args->printDeclaredFunctions = true;
                break;
            case 'R':
                shput(args->varReadsToBeTraced, optarg, true);
                break;
            case 'W':
                shput(args->varWritesToBeTraced, optarg, true);
                break;
            case 'c':
                shput(args->functionCallsToBeTraced, optarg, true);
                break;
            case 'a':
                shput(args->alarmsToBeTraced, optarg, true);
                break;
            case 'l':
                shput(args->instanceLifecyclesToBeTraced, optarg, true);
                break;
            case 'e':
                shput(args->eventsToBeTraced, optarg, true);
                break;
            case 'C':
                shput(args->collisionsToBeTraced, optarg, true);
                break;
            case 'o':
                shput(args->opcodesToBeTraced, optarg, true);
                break;
            case 'S':
                shput(args->stackToBeTraced, optarg, true);
                break;
            case 'k':
                args->traceFrames = true;
                break;
            case 'y':
                args->alwaysLogUnknownFunctions = true;
                break;
            case 'Y':
                args->alwaysLogStubbedFunctions = true;
                break;
            case 'x': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --exit-at-frame\n", optarg);
                    exit(1);
                }
                args->exitAtFrame = (int) frame;
                break;
            }
            case 'F': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --trace-bytecode-after-frame\n", optarg);
                    exit(1);
                }
                args->traceBytecodeAfterFrame = (int) frame;
                break;
            }
            case 'd': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --dump-frame\n", optarg);
                    exit(1);
                }
                hmput(args->dumpFrames, (int) frame, true);
                break;
            }
            case 'j': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --dump-frame-json\n", optarg);
                    exit(1);
                }
                hmput(args->dumpJsonFrames, (int) frame, true);
                break;
            }
            case 'J':
                args->dumpJsonFilePattern = optarg;
                break;
            case 'M': {
                char* endPtr;
                double speed = strtod(optarg, &endPtr);
                if (*endPtr != '\0' || speed <= 0.0) {
                    fprintf(stderr, "Error: Invalid speed multiplier '%s' for --speed (must be > 0)\n", optarg);
                    exit(1);
                }
                args->speedMultiplier = speed;
                break;
            }
            case 'X': {
                char* endPtr;
                double speed = strtod(optarg, &endPtr);
                if (*endPtr != '\0' || speed <= 0.0) {
                    fprintf(stderr, "Error: Invalid speed '%s' for --fast-forward-speed (must be > 0)\n", optarg);
                    exit(1);
                }
                args->fastForwardSpeed = speed;
                break;
            }
            case 'D':
                args->debug = true;
                break;
            case 'g':
                args->renderer = optarg;
                break;
            case 'z':
                args->lazyRooms = true;
                break;
            case 'G':
                shput(args->eagerRooms, optarg, true);
                break;
            case 'A':
                shput(args->disassemble, optarg, true);
                break;
            case 'T':
                shput(args->tilesToBeTraced, optarg, true);
                break;
            case 'E':
                args->traceEventInherited = true;
                break;
            case 'Z': {
                char* endPtr;
                long seedVal = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0') {
                    fprintf(stderr, "Error: Invalid seed value '%s' for --seed\n", optarg);
                    exit(1);
                }
                args->seed = (int) seedVal;
                args->hasSeed = true;
                break;
            }
            case 'I':
                args->recordInputsPath = optarg;
                break;
            case 'P':
                args->playbackInputsPath = optarg;
                break;
            case 'q': {
                char* endPtr;
                long framesBetween = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || framesBetween <= 0) {
                    fprintf(stderr, "Error: Invalid frame count '%s' for --profile-gml-scripts (must be > 0)\n", optarg);
                    exit(1);
                }
                args->profilerFramesBetween = (int) framesBetween;
                break;
            }
            case 'B':
                args->saveFolder = optarg;
                break;
#ifdef ENABLE_VM_OPCODE_PROFILER
            case 'Q':
                args->opcodeProfiler = true;
                break;
#endif
            case 'O':
                if (!parseOsTypeArg(optarg, &args->osType)) {
                    fprintf(stderr, "Error: Invalid --os-type value '%s' (expected: ", optarg);
                    printOsTypeNames(stderr);
                    fprintf(stderr, ")\n");
                    exit(1);
                }
                break;
            default:
                fprintf(stderr, "Usage: %s "
#ifdef ENABLE_SW_RENDERER
                        "[--headless] "
#endif
                        "[--screenshot=PATTERN] [--screenshot-at-frame=N ...] <path to data.win or game.unx>\n", argv[0]);
                exit(1);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Usage: %s "
#ifdef ENABLE_SW_RENDERER
                "[--headless] "
#endif
                "[--screenshot=PATTERN] [--screenshot-at-frame=N ...] <path to data.win or game.unx>\n", argv[0]);
        exit(1);
    }

    if (args->headless) {
#ifdef ENABLE_SW_RENDERER
        args->renderer = "software";
        fprintf(stderr, "Warning: forcing software rendering in headless mode!\n");
#else
        fprintf(stderr, "Error: headless mode requires the software renderer, but it is not enabled!\n");
        exit(1);
#endif
    }
#endif

    //args->dataWinPath = argv[optind];
    args->dataWinPath = "/data.win"; // TODO: copy over my Undertale
}

static void freeCommandLineArgs(CommandLineArgs* args) {
    hmfree(args->screenshotFrames);
    hmfree(args->screenshotSurfacesFrames);
    hmfree(args->dumpFrames);
    hmfree(args->dumpJsonFrames);
    shfree(args->varReadsToBeTraced);
    shfree(args->varWritesToBeTraced);
    shfree(args->functionCallsToBeTraced);
    shfree(args->alarmsToBeTraced);
    shfree(args->instanceLifecyclesToBeTraced);
    shfree(args->eventsToBeTraced);
    shfree(args->collisionsToBeTraced);
    shfree(args->opcodesToBeTraced);
    shfree(args->stackToBeTraced);
    shfree(args->disassemble);
    shfree(args->tilesToBeTraced);
}

// ===[ KEYBOARD INPUT ]===

static int32_t GintKeyToGml(int keycode) {

    switch (keycode)
    {
        case KEY_UP: return 'W';
        case KEY_DOWN: return 'S';
        case KEY_LEFT: return 'A';
        case KEY_RIGHT: return 'D';
        default: return -1;     // TODO
    }
}

static InputRecording* globalInputRecording = nullptr;

#if defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define BUTTERSCOTCH_HAS_ASAN 1
    #endif
#endif
#if defined(__SANITIZE_ADDRESS__)
    #define BUTTERSCOTCH_HAS_ASAN 1
#endif

#if BUTTERSCOTCH_HAS_ASAN
void __asan_set_death_callback(void (*callback)(void));
#endif

static volatile sig_atomic_t crashSaveInProgress = 0;

static void saveRecordingOnCrash(void) {
    if (crashSaveInProgress) return;
    crashSaveInProgress = 1;
    if (globalInputRecording != nullptr && globalInputRecording->isRecording) {
        InputRecording_save(globalInputRecording);
    }
}

static void crashSignalHandler(int sig) {
    saveRecordingOnCrash();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void installCrashHandlers(void) {
#if BUTTERSCOTCH_HAS_ASAN
    __asan_set_death_callback(saveRecordingOnCrash);
#endif
    signal(SIGSEGV, crashSignalHandler);
    signal(SIGABRT, crashSignalHandler);
#ifdef SIGBUS
    signal(SIGBUS,  crashSignalHandler);
#endif
    signal(SIGFPE,  crashSignalHandler);
    signal(SIGILL,  crashSignalHandler);
}


static void setGintWindowTitle(void* window, const char* title) {
    (void) window;
    (void) title;
}
static bool getGintWindowSize(void *window, int32_t *outW, int32_t *outH) {
    (void)window;
    if (outW == nullptr || outH == nullptr) return false;
    *outW = DWIDTH;
    *outH = DHEIGHT;
    return true;
}
static void setGintWindowSize(void *window, int32_t width, int32_t height) {
    (void) window;
    (void) width;
    (void) height;
}
static bool getGintWindowFocus(void *window) {
    (void)window;
    return true;
}

static int lastW, lastH;
void Runner_setNextFrame(uint16_t* framebuffer, int width, int height)
{
#ifndef ENABLE_STDOUT
    // The LCD controller lives there
    volatile color_t *DISPLAY = (volatile color_t *) 0xB4000000;

    int offX = width < DWIDTH ? (DWIDTH - width)>>1 : 0;
    int offY = height < DHEIGHT ? (DHEIGHT - height)>>1 : 0;
    r61524_start_frame(offX, width-1+offX, offY, height-1+offY); // this surely will not cause issues at high resolutions
    
    // Check how much we can transfer per block
    if (!(((uintptr_t) framebuffer) & 0b11))
    {
        dma_transfer_sync(1, DMA_4B, (width * height)>>1, framebuffer, DMA_INC, DISPLAY, DMA_FIXED);
    }
    else
    {
        dma_transfer_sync(1, DMA_2B, width * height, framebuffer, DMA_INC, DISPLAY, DMA_FIXED);
    }
#endif ENABLE_STDOUT
    lastW = width;
    lastH = height;
}

void saveInputRecording() {
    // Save input recording if active, then free
    if (globalInputRecording != nullptr) {
        if (globalInputRecording->isRecording) {
            InputRecording_save(globalInputRecording);
        }
        InputRecording_free(globalInputRecording);
        globalInputRecording = nullptr;
    }
}

static void GDESTRUCTOR press_any_key_to_end() {
#ifdef ENABLE_STDOUT
    printf("Press any key to exit...\n");
    fflush(stdout);
#endif
    getkey();
}

// ===[ MAIN ]===
static bool shouldExit = false;
int main(int argc, char* argv[]) {
    kmalloc_arena_t external_ram = {
        .name = GINT_ERAM_ARENA, .is_default = true,
        .start  = (void *) (GINT_ERAM_START), 
        .end    = (void *) (GINT_ERAM_START + GINT_ERAM_SIZE)
    };
#ifdef ENABLE_STDOUT
    __printf_enable_fp();
    fxCGIO_init();
#endif

    kmalloc_init_arena(&external_ram, true);
    kmalloc_add_arena(&external_ram);

    CommandLineArgs args;
    parseCommandLineArgs(&args, argc, argv);

    printf("Loading the file %s...\n", args.dataWinPath);
    fflush(stderr);

    DataWin* dataWin = DataWin_parse(
        args.dataWinPath,
        (DataWinParserOptions) {
            .parseGen8 = true,
            .parseOptn = true,
            .parseLang = true,
            .parseExtn = false,
            .parseSond = true,
            .parseAgrp = true,
            .parseSprt = true,
            .parseBgnd = true,
            .parsePath = true,
            .parseScpt = true,
            .parseGlob = true,
            .parseTmln = true,
            .parseObjt = true,
            .parseRoom = true,
            .parseTpag = true,
            .parseCode = true,
            .parseVari = true,
            .parseFunc = true,
            .parseStrg = true,
            .parseTxtr = true,

            // lol
            .parseAudo = false,
            .parseFont = false,
            .parseShdr = false,

            .skipLoadingPreciseMasksForNonPreciseSprites = true,
            .lazyLoadRooms = true,
            .eagerlyLoadedRooms = false
        }
    );

    Gen8* gen8 = &dataWin->gen8;
    printf("Loaded \"%s\" (%d) successfully! [Bytecode Version %u / GameMaker version %u.%u.%u.%u]\n", gen8->name, gen8->gameID, gen8->bytecodeVersion, dataWin->detectedFormat.major, dataWin->detectedFormat.minor, dataWin->detectedFormat.release, dataWin->detectedFormat.build);

    {
        // This is gint bitch! Get your sensitive ass back to Linux!
        //struct mallinfo2 mi = mallinfo2();
        kmalloc_gint_stats_t *mi = kmalloc_get_gint_stats(kmalloc_get_arena(GINT_ERAM_ARENA));
        printf("Memory after data.win parsing: used=%zu bytes (%.1f KB)\n", mi->used_memory, mi->used_memory / 1024.0f);
#ifdef ENABLE_STDOUT
        char *line = NULL;
        size_t size;
        getline(&line, &size, stdin);
        free(line);
#endif
    }

    // Initialize VM
    VMContext* vm = VM_create(dataWin);

    Profiler_setEnabled(&vm->profiler, false); // lol no

    if (args.hasSeed) {
        srand((unsigned int) args.seed);
        vm->hasFixedSeed = true;
        printf("Using fixed RNG seed: %d\n", args.seed);
    }


    // Initialize the file system
    char* dataWinDir = nullptr;
    {
        const char* lastSlash = strrchr(args.dataWinPath, '/');
        const char* lastBackslash = strrchr(args.dataWinPath, '\\');
        if (lastBackslash != nullptr && (lastSlash == nullptr || lastBackslash > lastSlash))
            lastSlash = lastBackslash;
        if (lastSlash != nullptr) {
            size_t len = (size_t) (lastSlash - args.dataWinPath + 1);
            dataWinDir = safeMalloc(len + 1);
            memcpy(dataWinDir, args.dataWinPath, len);
            dataWinDir[len] = '\0';
        } else {
            dataWinDir = safeStrdup("./");
        }
    }
    const char* savePath = args.saveFolder != nullptr ? args.saveFolder : dataWinDir;
    OverlayFileSystem* overlayFs = OverlayFileSystem_create(dataWinDir, savePath);
    free(dataWinDir);

    int reqW = (int) gen8->defaultWindowWidth;
    int reqH = (int) gen8->defaultWindowHeight;
    fbWidth = reqW;
    fbHeight = reqH;

    // There are no such things as 'video modes' in Ba Sing Se

    // Initialize the renderer
    Renderer *renderer = SWRenderer_create();

    // Initialize the audio system
    AudioSystem* audioSystem;
    if (args.headless) {
        audioSystem = (AudioSystem*) NoopAudioSystem_create();
    } else {
#if defined(USE_OPENAL)
        audioSystem = (AudioSystem*) AlAudioSystem_create();
#elif defined(USE_MINIAUDIO)
        audioSystem = (AudioSystem*) MaAudioSystem_create();
#else
        audioSystem = (AudioSystem*) NoopAudioSystem_create();
#endif
    }

    // Initialize the runner
    Runner* runner = Runner_create(dataWin, vm, renderer, (FileSystem*) overlayFs, audioSystem);
    runner->debugMode = args.debug;
    runner->osType = args.osType;
    runner->setWindowTitle = setGintWindowTitle;
    runner->getWindowSize = getGintWindowSize;
    runner->setWindowSize = setGintWindowSize;
    runner->windowHasFocus = getGintWindowFocus;
    runner->nativeWindow = (void*)0xDEADBEEF;

    // Set up input recording/playback (both can be active: playback then continue recording)
    if (args.playbackInputsPath != nullptr) {
        globalInputRecording = InputRecording_createPlayer(args.playbackInputsPath, args.recordInputsPath);
    } else if (args.recordInputsPath != nullptr) {
        globalInputRecording = InputRecording_createRecorder(args.recordInputsPath);
    }
    if (globalInputRecording != nullptr) {
        installCrashHandlers();
    }
    // TODO: very, very suspicious code
#if 0
    shcopyFromTo(args.varReadsToBeTraced, runner->vmContext->varReadsToBeTraced);
    shcopyFromTo(args.varWritesToBeTraced, runner->vmContext->varWritesToBeTraced);
    shcopyFromTo(args.functionCallsToBeTraced, runner->vmContext->functionCallsToBeTraced);
    shcopyFromTo(args.alarmsToBeTraced, runner->vmContext->alarmsToBeTraced);
    shcopyFromTo(args.instanceLifecyclesToBeTraced, runner->vmContext->instanceLifecyclesToBeTraced);
    shcopyFromTo(args.eventsToBeTraced, runner->vmContext->eventsToBeTraced);
    shcopyFromTo(args.collisionsToBeTraced, runner->vmContext->collisionsToBeTraced);
    shcopyFromTo(args.opcodesToBeTraced, runner->vmContext->opcodesToBeTraced);
    shcopyFromTo(args.stackToBeTraced, runner->vmContext->stackToBeTraced);
    shcopyFromTo(args.tilesToBeTraced, runner->vmContext->tilesToBeTraced);
    runner->vmContext->traceBytecodeAfterFrame = args.traceBytecodeAfterFrame;
    runner->vmContext->alwaysLogUnknownFunctions = args.alwaysLogUnknownFunctions;
    runner->vmContext->alwaysLogStubbedFunctions = args.alwaysLogStubbedFunctions;
    runner->vmContext->traceEventInherited = args.traceEventInherited;
#endif

    // Initialize the first room and fire Game Start / Room Start events
    Runner_initFirstRoom(runner);

    // Main loop
    bool debugPaused = false;
    bool debugShowCollisionMasks = false;
    double lastFrameTime = (rtc_ticks()/128.0f);
    //SDL_Event e;

    prof_init();
    prof_t frametime = prof_make();
    while (!runner->shouldExit && !shouldExit) {
        // Clear last frame's pressed/released state, then poll new input events
        uint32_t total_frametime = prof_time(frametime);

        frametime = prof_make();
        prof_enter(frametime);
        RunnerKeyboard_beginFrame(runner->keyboard);
        RunnerGamepad_beginFrame(runner->gamepads);

        // TODO: read keyboard
        volatile int one = 1; // ONE DOLLAR!
        cleareventflips();
        pollevent();
        if (keydown(KEY_MENU))
        {
            shouldExit = true;
        }
#ifdef ENABLE_STDOUT
        kmalloc_gint_stats_t *mi = kmalloc_get_gint_stats(kmalloc_get_arena(GINT_ERAM_ARENA));
        printf("used=%zu bytes (%.1f KB) (%d x %d) | %d us\n", mi->used_memory, mi->used_memory / 1024.0f, lastW, lastH, total_frametime);
#endif

        // Poll every keycode, regardless of if it's real or not!!!!!!
        for (int i = 0; i < 0x100; i++)
        {
            if (keypressed(i))
                RunnerKeyboard_onKeyDown(runner->keyboard, GintKeyToGml(i));
            if (keyreleased(i))
                RunnerKeyboard_onKeyUp(runner->keyboard, GintKeyToGml(i));
        }

        // Process input recording/playback (must happen after SDL_PollEvents, before Runner_step)
        InputRecording_processFrame(globalInputRecording, runner->keyboard, runner->frameCount);

        // Debug key bindings
        if (runner->debugMode) {
            // Pause
            if (RunnerKeyboard_checkPressed(runner->keyboard, 'P')) {
                debugPaused = !debugPaused;
                fprintf(stderr, "Debug: %s\n", debugPaused ? "Paused" : "Resumed");
            }

            // Go to next room
            if (RunnerKeyboard_checkPressed(runner->keyboard, VK_PAGEUP)) {
                DataWin* dw = runner->dataWin;
                if ((int32_t) dw->gen8.roomOrderCount > runner->currentRoomOrderPosition + 1) {
                    int32_t nextIdx = dw->gen8.roomOrder[runner->currentRoomOrderPosition + 1];
                    runner->pendingRoom = nextIdx;
                    runner->audioSystem->vtable->stopAll(runner->audioSystem);
                    fprintf(stderr, "Debug: Going to next room -> %s\n", dw->room.rooms[nextIdx].name);
                }
            }

            // Go to previous room
            if (RunnerKeyboard_checkPressed(runner->keyboard, VK_PAGEDOWN)) {
                DataWin* dw = runner->dataWin;
                if (runner->currentRoomOrderPosition > 0) {
                    int32_t prevIdx = dw->gen8.roomOrder[runner->currentRoomOrderPosition - 1];
                    runner->pendingRoom = prevIdx;
                    runner->audioSystem->vtable->stopAll(runner->audioSystem);
                    fprintf(stderr, "Debug: Going to previous room -> %s\n", dw->room.rooms[prevIdx].name);
                }
            }

            // Dump runner state to console
            if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F12)) {
                fprintf(stderr, "Debug: Dumping runner state at frame %d\n", runner->frameCount);
                Runner_dumpState(runner);
            }

            if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F11)) {
                fprintf(stderr, "Debug: Dumping runner state at frame %d\n", runner->frameCount);
                char* json = Runner_dumpStateJson(runner);

                if (args.dumpJsonFilePattern != nullptr) {
                    char filename[512];
                    snprintf(filename, sizeof(filename), args.dumpJsonFilePattern, runner->frameCount);
                    FILE* f = fopen(filename, "w");
                    if (f != nullptr) {
                        fwrite(json, 1, strlen(json), f);
                        fputc('\n', f);
                        fclose(f);
                        printf("JSON dump saved: %s\n", filename);
                    } else {
                        fprintf(stderr, "Error: Could not write JSON dump to '%s'\n", filename);
                    }
                } else {
                    printf("%s\n", json);
                }

                free(json);
            }

            // Toggle the collision mask debug overlay
            if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F2)) {
                debugShowCollisionMasks = !debugShowCollisionMasks;
                fprintf(stderr, "Debug: Collision mask overlay %s!\n", debugShowCollisionMasks ? "enabled" : "disabled");
            }

            // Reset global interact state because I HATE when I get stuck while moving through rooms
            if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F10)) {
                int32_t interactVarId = shget(runner->vmContext->globalVarNameMap, "interact");

                runner->vmContext->globalVars[interactVarId] = RValue_makeInt32(0);
                printf("Changed global.interact [%d] value!\n", interactVarId);
            }
        }

        // Run the game step if the game is paused
        bool shouldStep = true;
        if (runner->debugMode && debugPaused) {
            shouldStep = RunnerKeyboard_checkPressed(runner->keyboard, 'O');
            if (shouldStep) fprintf(stderr, "Debug: Frame advance (frame %d)\n", runner->frameCount);
        }

        double frameStartTime = 0;

        if (shouldStep) {
            if (args.traceFrames) {
                frameStartTime = (rtc_ticks()/128.0f);
                fprintf(stderr, "Frame %d (Start)\n", runner->frameCount);
            }

            // Run one game step (Begin Step, Keyboard, Alarms, Step, End Step, room transitions)
            Runner_step(runner);

            if (args.profilerFramesBetween > 0 && runner->frameCount > 0 && runner->frameCount % args.profilerFramesBetween == 0) {
                char* profilerReport = Profiler_createReport(vm->profiler, 20, args.profilerFramesBetween);
                if (profilerReport != nullptr) {
                    fprintf(stderr, "%s\n", profilerReport);
                    free(profilerReport);
                }
                Profiler_reset(vm->profiler);
            }

            // Update audio system (gain fading, cleanup ended sounds)
            float dt = (float) ((rtc_ticks()/128.0f) - lastFrameTime);
            if (0.0f > dt) dt = 0.0f;
            if (dt > 0.1f) dt = 0.1f; // cap delta to avoid huge fades on lag spikes
            runner->audioSystem->vtable->update(runner->audioSystem, dt);

            // Dump full runner state if this frame was requested
            if (hmget(args.dumpFrames, runner->frameCount)) {
                Runner_dumpState(runner);
            }

            // Dump runner state as JSON if this frame was requested
            if (hmget(args.dumpJsonFrames, runner->frameCount)) {
                char* json = Runner_dumpStateJson(runner);
                if (args.dumpJsonFilePattern != nullptr) {
                    char filename[512];
                    snprintf(filename, sizeof(filename), args.dumpJsonFilePattern, runner->frameCount);
                    FILE* f = fopen(filename, "w");
                    if (f != nullptr) {
                        fwrite(json, 1, strlen(json), f);
                        fputc('\n', f);
                        fclose(f);
                        printf("JSON dump saved: %s\n", filename);
                    } else {
                        fprintf(stderr, "Error: Could not write JSON dump to '%s'\n", filename);
                    }
                } else {
                    printf("%s\n", json);
                }
                free(json);
            }
        }

        // Clear the default framebuffer (window background) to black

        SWRenderer_clearFrameBuffer(renderer, 0);

        // The application surface (FBO) is sized to defaultWindowWidth x defaultWindowHeight.
        // It is a bit hard to understand, but here's how it works:
        // The Port X/Port Y controls the position of the game viewport within the application surface.
        // The Port W/Port H controls the size of the game viewport within the application surface.
        // Think of it like if you had an image (or... well, a framebuffer) and you are "pasting" it over the application surface.
        // And the Port W/Port H are scaled by the window size too (set by the GEN8 chunk)
        float displayScaleX;
        float displayScaleY;

        Runner_computeViewDisplayScale(runner, reqW, reqH, &displayScaleX, &displayScaleY);

        renderer->vtable->beginFrame(renderer, reqW, reqH, fbWidth, fbHeight);

        // Clear FBO with room background color
        if (runner->drawBackgroundColor) {
            SWRenderer_clearFrameBuffer(renderer, runner->backgroundColor);
        } else {
            SWRenderer_clearFrameBuffer(renderer, 0);
        }

        Runner_drawViews(runner, reqW, reqH, displayScaleX, displayScaleY, debugShowCollisionMasks);
        renderer->vtable->endFrameInit(renderer);
        Runner_drawPost(runner, fbWidth, fbHeight);
        renderer->vtable->endFrameEnd(renderer);
        Runner_drawGUI(runner, fbWidth, fbHeight, reqW, reqH);

        if (shouldStep && args.traceFrames) {
            double frameElapsedMs = ((rtc_ticks()/128.0f) - frameStartTime) * 1000.0;
            fprintf(stderr, "Frame %d (End, %.2f ms)\n", runner->frameCount, frameElapsedMs);
        }

        // TODO: Only swap when there isn't a room change to match the original runner.
#if 0 
        if (runner->pendingRoom == -1) {
            if(!args.headless) {
                if(!useSWRend)
                    SDL_GL_SwapBuffers();
                else {
                    SDL_BlitSurface(nextFb, NULL, scr, NULL);
                    SDL_Flip(scr);
                }
            }
        }
#endif
        Runner_handlePendingRoomChange(runner);

        // Limit frame rate to room speed (skip in headless mode for max speed!!)
        if (!args.headless && runner->currentRoom->speed > 0) {
            static bool fastForwardActive = false;
            static bool fastForwardTabPrev = false;
            bool fastForwardTabNow = RunnerKeyboard_checkPressed(runner->keyboard, '\t');
            if (args.fastForwardSpeed > 0.0 && fastForwardTabNow && !fastForwardTabPrev) {
                fastForwardActive = !fastForwardActive;
                lastFrameTime = (rtc_ticks()/1000.0f);
            }
            fastForwardTabPrev = fastForwardTabNow;
            double effectiveSpeed = (args.fastForwardSpeed > 0.0 && fastForwardActive) ? args.fastForwardSpeed : args.speedMultiplier;
            double targetFrameTime = 1.0 / (runner->currentRoom->speed * effectiveSpeed);
            double nextFrameTime = lastFrameTime + targetFrameTime;
            // Sleep for most of the remaining time, then spin-wait for precision
            double remaining = nextFrameTime - (rtc_ticks()/128.0f);
            if (remaining > 0.002) {
                // TODO: make sure that this is correct
                //sleep_us((long) (remaining * 1e12));
            }
            lastFrameTime = nextFrameTime;
        } else {
            lastFrameTime = (rtc_ticks()/128.0f);
        }
        prof_leave(frametime);
    }

    saveInputRecording();

    // Cleanup
    runner->audioSystem->vtable->destroy(runner->audioSystem);
    runner->audioSystem = nullptr;
    renderer->vtable->destroy(renderer);


    Runner_free(runner);
    OverlayFileSystem_destroy(overlayFs);
    VM_free(vm);
    DataWin_free(dataWin);

    freeCommandLineArgs(&args);

    dclear(C_GREEN);
    dupdate();
    getkey();
    return 0;
}

