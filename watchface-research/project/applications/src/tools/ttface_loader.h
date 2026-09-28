/*
 * TomTom Face Package (.ttface) C Loader & Nano-X Rendering Interface
 * ===================================================================
 * Target: TomTom ONE v6 (Samsung S3C2412 ARM926EJ-S, Nano-X / Microwindows)
 * Language: Pure ANSI C (C89/C90, gcc 3.3.4 compatible)
 *
 * This header defines the contract and parsing interface for loading
 * exported .ttface bundles and rendering RGB565 raw background layers
 * and logic-gate element overlays on Nano-X.
 */

#ifndef TTFACE_LOADER_H
#define TTFACE_LOADER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nano-X.h"

#define TTFACE_CANVAS_W 320
#define TTFACE_CANVAS_H 240
#define TTFACE_RAW_BG_PIXELS (TTFACE_CANVAS_W * TTFACE_CANVAS_H)
#define TTFACE_RAW_BG_SIZE (TTFACE_RAW_BG_PIXELS * 2)

#define TTFACE_MAX_ELEMENTS 16
#define TTFACE_MAX_RULES    8
#define TTFACE_RENDERER_DECLARATIVE "ttface.elements.v1"
#define TTFACE_RENDERER_FROST_OUTLINE "opentom.builtin.frost-outline.v1"
#define TTFACE_RENDERER_HYDRO_AQUA "opentom.builtin.hydro-aqua.v1"
#define TTFACE_RENDERER_SOLID_LAVENDER "opentom.builtin.solid-lavender.v1"
#define TTFACE_RENDERER_VIVID_SUNSET "opentom.builtin.vivid-sunset.v1"
#define TTFACE_RENDERER_REAL_TELEMETRY "opentom.builtin.real-telemetry.v1"

/* Element Types */
#define TTFACE_EL_DIGITAL_TIME 1
#define TTFACE_EL_DATE         2
#define TTFACE_EL_TEXT         3
#define TTFACE_EL_STATUS_ICON  4

/* Pre-built Rule Signals */
#define TTFACE_RULE_NONE                0
#define TTFACE_RULE_BATTERY_LOW         1  /* battery_percent <= 20 */
#define TTFACE_RULE_GPS_FIX             2  /* gps_status == 1 */
#define TTFACE_RULE_WEATHER_UNAVAILABLE 3  /* weather_status == 0 */
#define TTFACE_RULE_UNKNOWN          9999  /* Unknown / invalid rule */

typedef struct {
    char id[32];
    int type;
    int x;
    int y;
    int width;
    int height;
    GR_COLOR color;
    int font_size;
    int rule_id;
    char text_label[32];
} TTFaceElement;

typedef struct {
    char name[64];
    char version[16];
    char author[64];
    char renderer_id[64];
    GR_COLOR background_fallback;
    int element_count;
    TTFaceElement elements[TTFACE_MAX_ELEMENTS];
} TTFaceManifest;

/* Hardware state structure for device-side rule evaluation */
typedef struct {
    int battery_percent;   /* 0 to 100 */
    int gps_status;        /* 0 = searching, 1 = fixed */
    int weather_status;    /* 0 = unavailable, 1 = available */
    int is_charging;       /* 0 or 1 */
} TTDeviceState;

static int
ttface_legacy_renderer_face_index(const char *renderer_id)
{
    if (renderer_id == NULL || renderer_id[0] == '\0' ||
        strcmp(renderer_id, TTFACE_RENDERER_DECLARATIVE) == 0)
        return -1;
    if (strcmp(renderer_id, TTFACE_RENDERER_FROST_OUTLINE) == 0)
        return 0;
    if (strcmp(renderer_id, TTFACE_RENDERER_HYDRO_AQUA) == 0)
        return 1;
    if (strcmp(renderer_id, TTFACE_RENDERER_SOLID_LAVENDER) == 0)
        return 2;
    if (strcmp(renderer_id, TTFACE_RENDERER_VIVID_SUNSET) == 0)
        return 3;
    if (strcmp(renderer_id, TTFACE_RENDERER_REAL_TELEMETRY) == 0)
        return 4;
    return -2;
}

/*
 * Evaluates whether an element should be rendered based on its rule ID
 * and current device telemetry.
 */
static int
ttface_is_element_visible(const TTFaceElement *el, const TTDeviceState *state)
{
    if (el == NULL || el->rule_id == TTFACE_RULE_NONE) {
        return 1;
    }
    if (state == NULL)
        return 0;

    switch (el->rule_id) {
        case TTFACE_RULE_BATTERY_LOW:
            return (state->battery_percent <= 20);
        case TTFACE_RULE_GPS_FIX:
            return (state->gps_status == 1);
        case TTFACE_RULE_WEATHER_UNAVAILABLE:
            return (state->weather_status == 0);
        default:
            return 0; /* Fail closed on unknown/invalid rule ID */
    }
}

/*
 * Convert hex color string "#RRGGBB" to GR_COLOR (GR_RGB(r, g, b)).
 */
static GR_COLOR
ttface_parse_hex_color(const char *hex_str)
{
    unsigned int r = 255, g = 255, b = 255;
    if (hex_str != NULL && hex_str[0] == '#') {
        sscanf(hex_str + 1, "%02x%02x%02x", &r, &g, &b);
    }
    return GR_RGB(r, g, b);
}

/*
 * Bounded manifest JSON parser for C89 / GCC 3.3.4.
 * Reads manifest JSON content into TTFaceManifest struct.
 */
static int
ttface_parse_manifest_json(const char *json, TTFaceManifest *manifest)
{
    const char *ptr;
    const char *elements_ptr;

    if (json == NULL || manifest == NULL)
        return 0;

    memset(manifest, 0, sizeof(*manifest));
    manifest->background_fallback = GR_RGB(0, 0, 0);

    /* Extract face_name */
    ptr = strstr(json, "\"face_name\"");
    if (ptr != NULL) {
        const char *colon = strchr(ptr, ':');
        if (colon != NULL) {
            const char *q1 = strchr(colon, '"');
            if (q1 != NULL) {
                const char *q2 = strchr(q1 + 1, '"');
                if (q2 != NULL && (size_t)(q2 - q1 - 1) < sizeof(manifest->name)) {
                    strncpy(manifest->name, q1 + 1, q2 - q1 - 1);
                    manifest->name[q2 - q1 - 1] = '\0';
                }
            }
        }
    }

    /* Read the bounded renderer identifier for packaged native faces. */
    ptr = strstr(json, "\"renderer_id\"");
    if (ptr != NULL) {
        const char *colon = strchr(ptr, ':');
        if (colon != NULL) {
            const char *q1 = strchr(colon, '"');
            if (q1 != NULL) {
                const char *q2 = strchr(q1 + 1, '"');
                if (q2 != NULL &&
                    (size_t)(q2 - q1 - 1) <
                    sizeof(manifest->renderer_id)) {
                    strncpy(manifest->renderer_id, q1 + 1,
                            q2 - q1 - 1);
                    manifest->renderer_id[q2 - q1 - 1] = '\0';
                }
            }
        }
    }

    /* Extract elements */
    elements_ptr = strstr(json, "\"elements\"");
    if (elements_ptr != NULL) {
        const char *elem_arr = strchr(elements_ptr, '[');
        if (elem_arr != NULL) {
            const char *curr = elem_arr;
            while (*curr != '\0' && *curr != ']' && manifest->element_count < TTFACE_MAX_ELEMENTS) {
                const char *obj_start = strchr(curr, '{');
                const char *obj_end;
                char buf[512];
                size_t len;
                TTFaceElement *el;

                if (obj_start == NULL)
                    break;
                obj_end = strchr(obj_start, '}');
                if (obj_end == NULL)
                    break;

                len = obj_end - obj_start + 1;
                if (len >= sizeof(buf))
                    len = sizeof(buf) - 1;
                strncpy(buf, obj_start, len);
                buf[len] = '\0';

                el = &manifest->elements[manifest->element_count];
                memset(el, 0, sizeof(*el));
                el->width = 24;
                el->height = 24;
                el->font_size = 24;
                el->color = GR_RGB(255, 255, 255);
                el->rule_id = TTFACE_RULE_NONE;

                /* Parse element type */
                if (strstr(buf, "\"digital_time\"") != NULL)
                    el->type = TTFACE_EL_DIGITAL_TIME;
                else if (strstr(buf, "\"date\"") != NULL)
                    el->type = TTFACE_EL_DATE;
                else if (strstr(buf, "\"text\"") != NULL)
                    el->type = TTFACE_EL_TEXT;
                else if (strstr(buf, "\"status_icon\"") != NULL)
                    el->type = TTFACE_EL_STATUS_ICON;
                else
                    el->type = TTFACE_EL_TEXT;

                /* Parse x, y, width, height, font_size */
                ptr = strstr(buf, "\"x\"");
                if (ptr) sscanf(ptr, "\"x\": %d", &el->x);
                ptr = strstr(buf, "\"y\"");
                if (ptr) sscanf(ptr, "\"y\": %d", &el->y);
                ptr = strstr(buf, "\"width\"");
                if (ptr) sscanf(ptr, "\"width\": %d", &el->width);
                ptr = strstr(buf, "\"height\"");
                if (ptr) sscanf(ptr, "\"height\": %d", &el->height);
                ptr = strstr(buf, "\"font_size\"");
                if (ptr) sscanf(ptr, "\"font_size\": %d", &el->font_size);

                /* Parse color hex string */
                ptr = strstr(buf, "\"color\"");
                if (ptr) {
                    const char *q1 = strchr(ptr + 7, '"');
                    if (q1) {
                        const char *q2 = strchr(q1 + 1, '"');
                        if (q2 && (q2 - q1) < 16) {
                            char color_hex[16];
                            strncpy(color_hex, q1 + 1, q2 - q1 - 1);
                            color_hex[q2 - q1 - 1] = '\0';
                            el->color = ttface_parse_hex_color(color_hex);
                        }
                    }
                }

                /* Parse rule binding */
                ptr = strstr(buf, "\"rule\"");
                if (ptr) {
                    if (strstr(ptr, "\"battery_low\""))
                        el->rule_id = TTFACE_RULE_BATTERY_LOW;
                    else if (strstr(ptr, "\"gps_fix\""))
                        el->rule_id = TTFACE_RULE_GPS_FIX;
                    else if (strstr(ptr, "\"weather_unavailable\""))
                        el->rule_id = TTFACE_RULE_WEATHER_UNAVAILABLE;
                    else if (strstr(ptr, "null") || strstr(ptr, "\"None\""))
                        el->rule_id = TTFACE_RULE_NONE;
                    else
                        el->rule_id = TTFACE_RULE_UNKNOWN; /* Unknown rule ID -> fail closed */
                }

                manifest->element_count++;
                curr = obj_end + 1;
            }
        }
    }

    return 1;
}

/*
 * Load the unpacked assets/bg.rgb565 file. ZIP extraction and manifest
 * parsing are deliberately outside this helper. The file must contain
 * exactly 320x240 little-endian RGB565 pixels.
 */
static int
ttface_load_rgb565_file(const char *path, unsigned short *pixels,
                        size_t pixel_count)
{
    FILE *file;
    unsigned char bytes[4096];
    size_t pixel_offset = 0;
    int ok = 1;

    if (path == NULL || pixels == NULL ||
        pixel_count != (size_t)TTFACE_RAW_BG_PIXELS)
        return 0;

    file = fopen(path, "rb");
    if (file == NULL)
        return 0;

    while (pixel_offset < pixel_count) {
        size_t count = pixel_count - pixel_offset;
        size_t byte_count;
        size_t i;

        if (count > sizeof(bytes) / 2)
            count = sizeof(bytes) / 2;
        byte_count = count * 2;
        if (fread(bytes, 1, byte_count, file) != byte_count) {
            ok = 0;
            break;
        }
        for (i = 0; i < count; ++i) {
            pixels[pixel_offset + i] =
                (unsigned short)((unsigned short)bytes[i * 2] |
                ((unsigned short)bytes[i * 2 + 1] << 8));
        }
        pixel_offset += count;
    }

    if (ok && (fgetc(file) != EOF || ferror(file)))
        ok = 0;
    if (fclose(file) != 0)
        ok = 0;
    return ok;
}

/*
 * Fast raw RGB565 blitter for Nano-X.
 * Draws a loaded 320x240 native RGB565 buffer onto the Nano-X drawable.
 */
static void
ttface_blit_rgb565(GR_WINDOW_ID win, GR_GC_ID gc, const unsigned short *rgb565_buf)
{
    if (rgb565_buf == NULL)
        return;
    GrArea(win, gc, 0, 0, TTFACE_CANVAS_W, TTFACE_CANVAS_H,
           (void *)rgb565_buf, MWPF_TRUECOLOR565);
}

#endif /* TTFACE_LOADER_H */
