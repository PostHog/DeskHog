/**
 * @file lv_conf.h
 * Configuration file for v9.3.0-dev
 *
 * Generated based on migration from a v8.3 config for ESP32.
 */

/*
 * Copy this file as `lv_conf.h`
 * 1. simply next to `lvgl` folder
 * 2. or to any other place and
 * - define `LV_CONF_INCLUDE_SIMPLE`;
 * - add the path as an include path.
 */

/* clang-format off */
#if 1 /* Set this to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

/* If you need to include anything here, do it inside the `__ASSEMBLY__` guard */
#if  0 && defined(__ASSEMBLY__)
#include "my_include.h"
#endif

// Added for ESP32 IRAM attributes
#ifdef ESP32
#include "esp_attr.h"
#endif


/*====================
   COLOR SETTINGS
 *====================*/

/** Color depth: 1 (I1), 8 (L8), 16 (RGB565), 24 (RGB888), 32 (XRGB8888) */
#define LV_COLOR_DEPTH 16 // Migrated from old config (was 16)

/* Note: LV_COLOR_SCREEN_TRANSP and LV_COLOR_CHROMA_KEY from v9.0.0 are not present in v9.3 template. */
/* May need alternative configuration if screen transparency or chroma keying is required. */

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/

/** Possible values
 * - LV_STDLIB_BUILTIN:     LVGL's built in implementation
 * - LV_STDLIB_CLIB:        Standard C functions, like malloc, strlen, etc
 * - LV_STDLIB_MICROPYTHON: MicroPython implementation
 * - LV_STDLIB_RTTHREAD:    RT-Thread implementation
 * - LV_STDLIB_CUSTOM:      Implement the functions externally
 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN // Use built-in, assumes ESP PSRAM handled by underlying malloc if enabled

/** Possible values
 * - LV_STDLIB_BUILTIN:     LVGL's built in implementation
 * - LV_STDLIB_CLIB:        Standard C functions, like malloc, strlen, etc
 * - LV_STDLIB_MICROPYTHON: MicroPython implementation
 * - LV_STDLIB_RTTHREAD:    RT-Thread implementation
 * - LV_STDLIB_CUSTOM:      Implement the functions externally
 */
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN // Default

/** Possible values
 * - LV_STDLIB_BUILTIN:     LVGL's built in implementation
 * - LV_STDLIB_CLIB:        Standard C functions, like malloc, strlen, etc
 * - LV_STDLIB_MICROPYTHON: MicroPython implementation
 * - LV_STDLIB_RTTHREAD:    RT-Thread implementation
 * - LV_STDLIB_CUSTOM:      Implement the functions externally
 */
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN // Default

#define LV_STDINT_INCLUDE       <stdint.h>    // Default
#define LV_STDDEF_INCLUDE       <stddef.h>    // Default
#define LV_STDBOOL_INCLUDE      <stdbool.h>   // Default
#define LV_INTTYPES_INCLUDE     <inttypes.h>  // Default
#define LV_LIMITS_INCLUDE       <limits.h>    // Default
#define LV_STDARG_INCLUDE       <stdarg.h>    // Default

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    /** Size of memory available for `lv_malloc()` in bytes (>= 2kB) */
    #define LV_MEM_SIZE (32 * 1024U)          /**< [bytes] - Migrated from old config (was 32k) */

    /** Size of the memory expand for `lv_malloc()` in bytes */
    #define LV_MEM_POOL_EXPAND_SIZE 0         // Default

    /** Set an address for the memory pool instead of allocating it as a normal array. Can be in external SRAM too. */
    #define LV_MEM_ADR 0     /**< 0: unused*/ // Migrated from old config (was 0)
    /* Instead of an address give a memory allocator that will be called to get a memory pool for LVGL. E.g. my_malloc */
    #if LV_MEM_ADR == 0
        #undef LV_MEM_POOL_INCLUDE // Keep undefined unless LV_MEM_ADR is set
        #undef LV_MEM_POOL_ALLOC   // Keep undefined unless LV_MEM_ADR is set
    #endif
#endif  /*LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN*/

/* Note: LV_MEMCPY_MEMSET_STD from v9.0.0 is not present. Assumed to use stdlib based on LV_USE_STDLIB_* */

/*====================
   HAL SETTINGS
 *====================*/

/** Default display refresh, input device read and animation step period. */
#define LV_DEF_REFR_PERIOD  60      /**< [ms] - Migrated from old config (was 60) */

/* Note: LV_INDEV_DEF_READ_PERIOD from v9.0.0 is not present. Input handling might be configured differently. */
/* Note: LV_TICK_CUSTOM from v9.0.0 is not present. Tick source usually handled in platform integration. */

/** Default Dots Per Inch. Used to initialize default sizes such as widgets sized, style paddings.
 * (Not so important, you can adjust it to modify default sizes and spaces.) */
#define LV_DPI_DEF 130              /**< [px/inch] - Migrated from old config (was 130) */

/*=================
 * OPERATING SYSTEM
 *=================*/
/** Select operating system to use. Possible options:
 * - LV_OS_NONE
 * - LV_OS_PTHREAD
 * - LV_OS_FREERTOS
 * - LV_OS_CMSIS_RTOS2
 * - LV_OS_RTTHREAD
 * - LV_OS_WINDOWS
 * - LV_OS_MQX
 * - LV_OS_SDL2
 * - LV_OS_CUSTOM */
#define LV_USE_OS   LV_OS_NONE // Migrated from old config (assumed default was NONE)

#if LV_USE_OS == LV_OS_CUSTOM
    #define LV_OS_CUSTOM_INCLUDE <stdint.h> // Default
#endif
#if LV_USE_OS == LV_OS_FREERTOS
    /*
     * Unblocking an RTOS task with a direct notification is 45% faster and uses less RAM
     * than unblocking a task using an intermediary object such as a binary semaphore.
     * RTOS task notifications can only be used when there is only one task that can be the recipient of the event.
     */
    #define LV_USE_FREERTOS_TASK_NOTIFY 1 // Default
#endif

/*========================
 * RENDERING CONFIGURATION
 *========================*/

/** Align stride of all layers and images to this bytes */
#define LV_DRAW_BUF_STRIDE_ALIGN                1 // Default

/** Align start address of draw_buf addresses to this bytes*/
#define LV_DRAW_BUF_ALIGN                       4 // Default

/** Using matrix for transformations.
 * Requirements:
 * - `LV_USE_MATRIX = 1`.
 * - Rendering engine needs to support 3x3 matrix transformations. */
#define LV_DRAW_TRANSFORM_USE_MATRIX            0 // Default

/** The target buffer size for simple layer chunks. */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE    (24 * 1024)    /**< [bytes]*/ // Default

/* Limit the max allocated memory for simple and transformed layers. */
#define LV_DRAW_LAYER_MAX_MEMORY 0  /**< No limit by default [bytes]*/ // Default

/** Stack size of drawing thread. */
#define LV_DRAW_THREAD_STACK_SIZE    (8 * 1024)         /**< [bytes]*/ // Default

#define LV_USE_DRAW_SW 1 // Enable Software rendering
#if LV_USE_DRAW_SW == 1
    /* Selectively disable color format support */
    #define LV_DRAW_SW_SUPPORT_RGB565       1 // Keep enabled for LV_COLOR_DEPTH 16
    #define LV_DRAW_SW_SUPPORT_RGB565A8     0 // Default
    #define LV_DRAW_SW_SUPPORT_RGB888       0 // Default
    #define LV_DRAW_SW_SUPPORT_XRGB8888     0 // Default
    #define LV_DRAW_SW_SUPPORT_ARGB8888     1 // Default
    #define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0 // Default
    #define LV_DRAW_SW_SUPPORT_L8           1 // Default
    #define LV_DRAW_SW_SUPPORT_AL88         1 // Default
    #define LV_DRAW_SW_SUPPORT_A8           1 // Default
    #define LV_DRAW_SW_SUPPORT_I1           1 // Default

    #define LV_DRAW_SW_I1_LUM_THRESHOLD 127 // Default

    /** Set number of draw units. (> 1 requires LV_USE_OS) */
    #define LV_DRAW_SW_DRAW_UNIT_CNT    1 // Default

    /** Use Arm-2D to accelerate software (sw) rendering. */
    #define LV_USE_DRAW_ARM2D_SYNC      0 // Migrated from old config (was 0) - IMPORTANT for non-Arm

    /** Enable native helium assembly to be compiled. */
    #define LV_USE_NATIVE_HELIUM_ASM    0 // Migrated from old config (was 0) - IMPORTANT for non-Arm (Xtensa)

    /** Enable complex rendering */
    #define LV_DRAW_SW_COMPLEX          1 // Migrated from old config (LV_DRAW_COMPLEX was 1)

    #if LV_DRAW_SW_COMPLEX == 1
        /** Allow buffering some shadow calculation. */
        #define LV_DRAW_SW_SHADOW_CACHE_SIZE 0 // Migrated from old config (LV_SHADOW_CACHE_SIZE was 0)

        /** Set number of maximally-cached circle data. */
        #define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4 // Default
    #endif

    /* Use architecture specific assembly optimizations */
    #define  LV_USE_DRAW_SW_ASM     LV_DRAW_SW_ASM_NONE // Default, ensure no ARM/Helium asm used

    #if LV_USE_DRAW_SW_ASM == LV_DRAW_SW_ASM_CUSTOM
        #define  LV_DRAW_SW_ASM_CUSTOM_INCLUDE "" // Default
    #endif

    /** Enable drawing complex gradients in software: linear at an angle, radial or conical */
    #define LV_USE_DRAW_SW_COMPLEX_GRADIENTS    0 // Default

#endif

/* Disable other GPUs */
#define LV_USE_NEMA_GFX         0
#define LV_USE_DRAW_VGLITE      0
#define LV_USE_PXP              0
#define LV_USE_DRAW_PXP         0 // Explicitly disable PXP drawing
#define LV_USE_ROTATE_PXP       0 // Explicitly disable PXP rotation
#define LV_USE_DRAW_G2D         0
#define LV_USE_DRAW_DAVE2D      0
#define LV_USE_DRAW_SDL         0
#define LV_USE_DRAW_VG_LITE     0 // Note: Different from LV_USE_DRAW_VGLITE
#define LV_USE_DRAW_DMA2D       0
#define LV_USE_DRAW_OPENGLES    0

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/

/*-------------
 * Logging
 *-----------*/
#define LV_USE_LOG 0 // Migrated from old config (was 0)
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN  // Default if log enabled
    #define LV_LOG_PRINTF 0                 // Default if log enabled
    //#define LV_LOG_PRINT_CB                 // Default if log enabled
    #define LV_LOG_USE_TIMESTAMP 1          // Default if log enabled
    #define LV_LOG_USE_FILE_LINE 1          // Default if log enabled
    #define LV_LOG_TRACE_MEM        1       // Default if log enabled
    #define LV_LOG_TRACE_TIMER      1       // Default if log enabled
    #define LV_LOG_TRACE_INDEV      1       // Default if log enabled
    #define LV_LOG_TRACE_DISP_REFR  1       // Default if log enabled
    #define LV_LOG_TRACE_EVENT      1       // Default if log enabled
    #define LV_LOG_TRACE_OBJ_CREATE 1       // Default if log enabled
    #define LV_LOG_TRACE_LAYOUT     1       // Default if log enabled
    #define LV_LOG_TRACE_ANIM       1       // Default if log enabled
    #define LV_LOG_TRACE_CACHE      1       // Default if log enabled
#endif  /*LV_USE_LOG*/

/*-------------
 * Asserts
 *-----------*/
#define LV_USE_ASSERT_NULL          1   // Migrated from old config (was 1)
#define LV_USE_ASSERT_MALLOC        1   // Migrated from old config (was 1)
#define LV_USE_ASSERT_STYLE         0   // Migrated from old config (was 0)
#define LV_USE_ASSERT_MEM_INTEGRITY 0   // Migrated from old config (was 0)
#define LV_USE_ASSERT_OBJ           0   // Migrated from old config (was 0)

#define LV_ASSERT_HANDLER_INCLUDE <stdint.h> // Migrated from old config
#define LV_ASSERT_HANDLER while(1);           // Migrated from old config

/*-------------
 * Debug
 *-----------*/
#define LV_USE_REFR_DEBUG 0 // Migrated from old config (was 0)
#define LV_USE_LAYER_DEBUG 0 // Default
#define LV_USE_PARALLEL_DRAW_DEBUG 0 // Default

/*-------------
 * Others
 *-----------*/
#define LV_ENABLE_GLOBAL_CUSTOM 0 // Default
#if LV_ENABLE_GLOBAL_CUSTOM
    #define LV_GLOBAL_CUSTOM_INCLUDE <stdint.h> // Default
#endif

#define LV_CACHE_DEF_SIZE       0 // Migrated from old config (LV_IMG_CACHE_DEF_SIZE was 0)

#define LV_IMAGE_HEADER_CACHE_DEF_CNT 0 // Default

#define LV_GRADIENT_MAX_STOPS   2 // Default

#define LV_COLOR_MIX_ROUND_OFS  0 // Default

#define LV_OBJ_STYLE_CACHE      0 // Default

#define LV_USE_OBJ_ID           0 // Default
#define LV_USE_OBJ_NAME         0 // Default
#define LV_OBJ_ID_AUTO_ASSIGN   LV_USE_OBJ_ID // Default
#define LV_USE_OBJ_ID_BUILTIN   1 // Default

#define LV_USE_OBJ_PROPERTY 0 // Default
#define LV_USE_OBJ_PROPERTY_NAME 1 // Default

#define LV_USE_VG_LITE_THORVG  0 // Default

#define LV_USE_GESTURE_RECOGNITION 0 // Default

/* Note: LV_SPRINTF_CUSTOM, LV_SPRINTF_USE_FLOAT, LV_USE_USER_DATA, LV_ENABLE_GC from v9.0.0 are not present. */
/* Note: LV_USE_PERF_MONITOR, LV_USE_MEM_MONITOR from v9.0.0 are now under LV_USE_SYSMON */

/*=====================
 * COMPILER SETTINGS
 *====================*/

#define LV_BIG_ENDIAN_SYSTEM 0 // Migrated from old config (was 0)

#ifdef ESP32
  #define LV_ATTRIBUTE_TICK_INC IRAM_ATTR // Migrated from old config (ESP32 specific)
  #define LV_ATTRIBUTE_FAST_MEM IRAM_ATTR // Migrated from old config (ESP32 specific)
#else
  #define LV_ATTRIBUTE_TICK_INC             // Default empty
  #define LV_ATTRIBUTE_FAST_MEM             // Default empty
#endif

#define LV_ATTRIBUTE_TIMER_HANDLER          // Migrated from old config (was empty)
#define LV_ATTRIBUTE_FLUSH_READY            // Migrated from old config (was empty)

#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 1       // Default

#define LV_ATTRIBUTE_MEM_ALIGN              // Migrated from old config (was empty)

#define LV_ATTRIBUTE_LARGE_CONST            // Migrated from old config (was empty)

#define LV_ATTRIBUTE_LARGE_RAM_ARRAY        // Migrated from old config (was empty)

#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning // Migrated from old config

#define LV_ATTRIBUTE_EXTERN_DATA            // Default

#define LV_USE_FLOAT            0           // Keep disabled as per old config not using float printf

#define LV_USE_MATRIX           0           // Keep disabled (requires float)

#ifndef LV_USE_PRIVATE_API
    #define LV_USE_PRIVATE_API  0           // Default
#endif

/* Note: LV_USE_LARGE_COORD from v9.0.0 is not present */

/*==================
 * FONT USAGE
 *===================*/

#define LV_FONT_MONTSERRAT_8  0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_10 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_12 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_14 1 // ENABLE Montserrat 14 data
#define LV_FONT_MONTSERRAT_16 0 // Migrated from old config (was 1)
#define LV_FONT_MONTSERRAT_18 0 // Migrated from old config (was 1)
#define LV_FONT_MONTSERRAT_20 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_22 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_24 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_26 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_28 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_30 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_32 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_34 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_36 0 // Migrated from old config (was 1)
#define LV_FONT_MONTSERRAT_38 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_40 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_42 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_44 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_46 0 // Migrated from old config (was 0)
#define LV_FONT_MONTSERRAT_48 0 // Migrated from old config (was 0)

#define LV_FONT_MONTSERRAT_28_COMPRESSED 0  // Migrated from old config (was 0)
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0  // Migrated from old config (was 0)
#define LV_FONT_SIMSUN_14_CJK            0  // New option, default 0
#define LV_FONT_SIMSUN_16_CJK            0  // Migrated from old config (was 0)

#define LV_FONT_UNSCII_8  0 // Migrated from old config (was 0)
#define LV_FONT_UNSCII_16 0 // Migrated from old config (was 0)

#define LV_FONT_CUSTOM_DECLARE // Ensure this is an empty define, and any previous #include "fonts/fonts.h" is removed

#define LV_FONT_DEFAULT &lv_font_montserrat_14 // Set default to enabled Montserrat 14

#define LV_FONT_FMT_TXT_LARGE 0 // Migrated from old config (was 0)

#define LV_USE_FONT_COMPRESSED 0 // Migrated from old config (was 0)

#define LV_USE_FONT_PLACEHOLDER 1 // Default

/* Note: LV_USE_FONT_SUBPX from v9.0.0 is not present. */

/*=================
 * TEXT SETTINGS
 *=================*/

#define LV_TXT_ENC LV_TXT_ENC_UTF8 // Migrated from old config (was UTF8)

#define LV_TXT_BREAK_CHARS " ,.;:-_" // Migrated from old config (was " ,.;:-_")

#define LV_TXT_LINE_BREAK_LONG_LEN 0 // Migrated from old config (was 0)

#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3 // Migrated from old config (was 3)

#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3 // Migrated from old config (was 3)

#define LV_USE_BIDI 0 // Migrated from old config (was 0)
#if LV_USE_BIDI
    #define LV_BIDI_BASE_DIR_DEF LV_BASE_DIR_AUTO // Default if BIDI enabled
#endif

#define LV_USE_ARABIC_PERSIAN_CHARS 0 // Migrated from old config (was 0)

#define LV_TXT_COLOR_CMD "#" // Migrated from old config (was "#")

/*==================
 * WIDGETS
 *================*/
#define LV_WIDGETS_HAS_DEFAULT_VALUE  1 // Default

#define LV_USE_ANIMIMG    1 // Migrated from old config (was 1)
#define LV_USE_ARC        1 // Migrated from old config (was 0)
#define LV_USE_BAR        0 // Migrated from old config (was 0)
#define LV_USE_BUTTON     0 // Migrated from old config (was 0, mapped from BTN)
#define LV_USE_BUTTONMATRIX 1 // Enable for animation support
#define LV_USE_CALENDAR   0 // Migrated from old config (was 0)
#if LV_USE_CALENDAR
    #define LV_CALENDAR_WEEK_STARTS_MONDAY 0 // Default
    #if LV_CALENDAR_WEEK_STARTS_MONDAY
        #define LV_CALENDAR_DEFAULT_DAY_NAMES {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"} // Default
    #else
        #define LV_CALENDAR_DEFAULT_DAY_NAMES {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"} // Default
    #endif
    #define LV_CALENDAR_DEFAULT_MONTH_NAMES {"January", "February", "March",  "April", "May",  "June", "July", "August", "September", "October", "November", "December"} // Default
    #define LV_USE_CALENDAR_HEADER_ARROW 1 // Default
    #define LV_USE_CALENDAR_HEADER_DROPDOWN 1 // Default
    #define LV_USE_CALENDAR_CHINESE 0 // Default
#endif  /*LV_USE_CALENDAR*/

#define LV_USE_CANVAS     1 // Migrated from old config (was 1)
#define LV_USE_CHART      1 // Migrated from old config (was 1)
#define LV_USE_CHECKBOX   0 // Migrated from old config (was 0)
#define LV_USE_DROPDOWN   0 // Migrated from old config (was 0)
#define LV_USE_IMAGE      1 // Migrated from old config (was 1, mapped from IMG)
#define LV_USE_IMAGEBUTTON 0 // Migrated from old config (was 0, mapped from IMGBTN)
#define LV_USE_KEYBOARD   0 // Migrated from old config (was 0)

#define LV_USE_LABEL      1 // Migrated from old config (was 1)
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION 0   // Migrated from old config (was 0)
    #define LV_LABEL_LONG_TXT_HINT 1    // Migrated from old config (was 1)
    #define LV_LABEL_WAIT_CHAR_COUNT 3  // Default
#endif

#define LV_USE_LED        0 // Migrated from old config (was 0)
#define LV_USE_LINE       1 // Migrated from old config (was 1)
#define LV_USE_LIST       0 // Migrated from old config (was 0)
#define LV_USE_LOTTIE     0 // Default (requires ThorVG)
#define LV_USE_MENU       0 // Migrated from old config (was 0)
#define LV_USE_MSGBOX     0 // Migrated from old config (was 0)
#define LV_USE_ROLLER     0 // Migrated from old config (was 0)
/* Note: LV_ROLLER_INF_PAGES from v9.0.0 is not present */
#define LV_USE_SCALE      0 // Default (new widget, disable unless needed)
#define LV_USE_SLIDER     0 // Migrated from old config (was 0)

#define LV_USE_SPAN       0 // Migrated from old config (was 1)
#if LV_USE_SPAN
    #define LV_SPAN_SNIPPET_STACK_SIZE 64 // Migrated from old config (was 64)
#endif

#define LV_USE_SPINBOX    0 // Migrated from old config (was 0)
#define LV_USE_SPINNER    0 // Migrated from old config (was 0)
#define LV_USE_SWITCH     0 // Migrated from old config (was 0)
#define LV_USE_TABLE      1 // Migrated from old config (was 1)
#define LV_USE_TABVIEW    0 // Migrated from old config (was 0)

#define LV_USE_TEXTAREA   0 // Migrated from old config (was 0)
#if LV_USE_TEXTAREA != 0
    #define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1500    /**< [ms] */ // Migrated from old config
#endif

#define LV_USE_TILEVIEW   0 // Migrated from old config (was 0)
#define LV_USE_WIN        0 // Migrated from old config (was 0)

#define LV_USE_3DTEXTURE  0 // Default (new widget)

/*==================
 * THEMES
 *==================*/
#define LV_USE_THEME_DEFAULT 1 // Migrated from old config (was 1)
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 0           // Migrated from old config (was 0)
    #define LV_THEME_DEFAULT_GROW 1           // Migrated from old config (was 1)
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80 // Migrated from old config (was 80)
#endif /*LV_USE_THEME_DEFAULT*/

#define LV_USE_THEME_SIMPLE 0 // Migrated from old config (was THEME_BASIC 1)
#define LV_USE_THEME_MONO 0   // Migrated from old config (was 1)

/*==================
 * LAYOUTS
 *==================*/
#define LV_USE_FLEX 1 // Migrated from old config (was 1)
#define LV_USE_GRID 1 // Migrated from old config (was 1)

/*====================
 * 3RD PARTS LIBRARIES
 *====================*/
#define LV_FS_DEFAULT_DRIVER_LETTER '\0' // Default

/* Filesystem drivers - keep disabled unless specifically needed */
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_FS_MEMFS 0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0
#define LV_USE_FS_ARDUINO_SD 0
#define LV_USE_FS_UEFI 0

/* Image decoders - keep disabled unless specifically needed */
#define LV_USE_LODEPNG 0
#define LV_USE_LIBPNG 0
#define LV_USE_BMP 0
#define LV_USE_TJPGD 0
#define LV_USE_LIBJPEG_TURBO 0
#define LV_USE_GIF 0
#define LV_BIN_DECODER_RAM_LOAD 0
#define LV_USE_RLE 0

/* QR code library */
#define LV_USE_QRCODE 1 // Migrated from old config (was 1)

/* Barcode code library */
#define LV_USE_BARCODE 0 // Default

/* Font libraries */
#define LV_USE_FREETYPE 0 // Default
#define LV_USE_TINY_TTF 0 // Disable TinyTTF since we're using LVGL-compatible precompiled fonts
#if LV_USE_TINY_TTF
    #define LV_TINY_TTF_FILE_SUPPORT 0 // Migrated from old config (was 0)
    #define LV_TINY_TTF_CACHE_GLYPH_CNT 256 // Default

    #define LV_TINY_TTF_CACHE_SIZE 512 // Migrated from old config (was 512)
#endif

/* Other libraries */
#define LV_USE_RLOTTIE 0 // Default
#define LV_USE_VECTOR_GRAPHIC  0 // Default
#define LV_USE_THORVG_INTERNAL 0 // Default
#define LV_USE_THORVG_EXTERNAL 0 // Default
#define LV_USE_LZ4_INTERNAL  0 // Default
#define LV_USE_LZ4_EXTERNAL  0 // Default
#define LV_USE_SVG 0 // Default
#define LV_USE_FFMPEG 0 // Default

/*==================
 * OTHERS
 *==================*/
#define LV_USE_SNAPSHOT 0 // Default
#define LV_USE_SYSMON   0 // Keep disabled (old perf/mem monitors were 0)
#if LV_USE_SYSMON
    #define LV_SYSMON_GET_IDLE lv_os_get_idle_percent // Default
    #define LV_USE_PERF_MONITOR 0 // Default
    #define LV_USE_MEM_MONITOR 0 // Default
#endif /*LV_USE_SYSMON*/

#define LV_USE_PROFILER 0 // Default
#define LV_USE_MONKEY 0 // Default
#define LV_USE_GRIDNAV 0 // Default
#define LV_USE_FRAGMENT 0 // Default
#define LV_USE_IMGFONT 0 // Default
#define LV_USE_OBSERVER 1 // Default
#define LV_USE_IME_PINYIN 0 // Default
#define LV_USE_FILE_EXPLORER 0 // Default
#define LV_USE_FONT_MANAGER 0 // Default
#define LV_USE_TEST 0 // Default

/*==================
 * DEVICES
 *==================*/
/* Keep all external device drivers disabled for standard ESP32 Arduino use */
#define LV_USE_SDL              0
#define LV_USE_X11              0
#define LV_USE_WAYLAND          0
#define LV_USE_LINUX_FBDEV      0
#define LV_USE_NUTTX            0
#define LV_USE_LINUX_DRM        0
#define LV_USE_TFT_ESPI         0 // Set to 1 ONLY if using TFT_eSPI display driver integration via LVGL
#define LV_USE_EVDEV            0
#define LV_USE_LIBINPUT         0
#define LV_USE_ST7735           0
#define LV_USE_ST7789           0
#define LV_USE_ST7796           0
#define LV_USE_ILI9341          0
#define LV_USE_GENERIC_MIPI     0
#define LV_USE_RENESAS_GLCDC    0
#define LV_USE_ST_LTDC          0
#define LV_USE_WINDOWS          0
#define LV_USE_UEFI             0
#define LV_USE_OPENGLES         0
#define LV_USE_QNX              0

/*==================
* EXAMPLES
*==================*/
#define LV_BUILD_EXAMPLES 0 // Migrated from old config (was 1)

/*===================
 * DEMO USAGE
 ====================*/
/* Keep demos disabled unless needed */
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_RENDER 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0
#define LV_USE_DEMO_VECTOR_GRAPHIC  0
#define LV_USE_DEMO_FLEX_LAYOUT     0
#define LV_USE_DEMO_MULTILANG       0
#define LV_USE_DEMO_TRANSFORM       0
#define LV_USE_DEMO_SCROLL          0
#define LV_USE_DEMO_EBIKE           0
#define LV_USE_DEMO_HIGH_RES        0
#define LV_USE_DEMO_SMARTWATCH      0

/*--END OF LV_CONF_H--*/

#endif /*LV_CONF_H*/

#endif /*End of "Content enable"*/