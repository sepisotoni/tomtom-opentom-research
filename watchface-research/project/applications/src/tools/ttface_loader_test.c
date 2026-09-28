#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ttface_loader.h"

#define TEST_PATH "/tmp/ttface-loader-test.rgb565"

static int area_called;
static int area_format;

void
GrArea(GR_DRAW_ID id, GR_GC_ID gc, GR_COORD x, GR_COORD y,
       GR_SIZE width, GR_SIZE height, void *pixels, int pixtype)
{
    (void)id;
    (void)gc;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)pixels;
    area_called = 1;
    area_format = pixtype;
}

static void
write_test_image(size_t size)
{
    FILE *file = fopen(TEST_PATH, "wb");
    size_t i;
    assert(file != NULL);
    for (i = 0; i < size; i += 2) {
        assert(fputc(0x34, file) != EOF);
        if (i + 1 < size)
            assert(fputc(0x12, file) != EOF);
    }
    assert(fclose(file) == 0);
}

int
main(void)
{
    unsigned short *pixels;
    TTFaceElement element;
    TTDeviceState state;
    TTFaceManifest manifest;
    const char *sample_json =
        "{\"manifest_version\": 1, \"face_name\": \"Cyberpunk Test\", "
        "\"renderer_id\": \"opentom.builtin.hydro-aqua.v1\", "
        "\"elements\": [{\"type\": \"digital_time\", \"x\": 50, \"y\": 50, \"rule\": \"battery_low\"}, "
        "{\"type\": \"status_icon\", \"x\": 280, \"y\": 10, \"rule\": \"unknown_rule\"}]}";

    pixels = (unsigned short *)malloc(
        (size_t)TTFACE_RAW_BG_PIXELS * sizeof(*pixels));
    assert(pixels != NULL);

    write_test_image(TTFACE_RAW_BG_SIZE);
    assert(ttface_load_rgb565_file(TEST_PATH, pixels,
                                   TTFACE_RAW_BG_PIXELS));
    assert(pixels[0] == 0x1234);
    assert(pixels[TTFACE_RAW_BG_PIXELS - 1] == 0x1234);
    assert(!ttface_load_rgb565_file(TEST_PATH, pixels,
                                    TTFACE_RAW_BG_PIXELS - 1));

    write_test_image(TTFACE_RAW_BG_SIZE - 1);
    assert(!ttface_load_rgb565_file(TEST_PATH, pixels,
                                    TTFACE_RAW_BG_PIXELS));
    write_test_image(TTFACE_RAW_BG_SIZE + 1);
    assert(!ttface_load_rgb565_file(TEST_PATH, pixels,
                                    TTFACE_RAW_BG_PIXELS));

    element.rule_id = TTFACE_RULE_BATTERY_LOW;
    state.battery_percent = 20;
    assert(ttface_is_element_visible(&element, &state));
    state.battery_percent = 21;
    assert(!ttface_is_element_visible(&element, &state));
    assert(!ttface_is_element_visible(&element, NULL));

    /* Test unknown / invalid rule ID fails closed (returns 0) */
    element.rule_id = 9999;
    assert(!ttface_is_element_visible(&element, &state));

    /* Test manifest JSON parsing and unknown rule handling */
    assert(ttface_parse_manifest_json(sample_json, &manifest));
    assert(strcmp(manifest.name, "Cyberpunk Test") == 0);
    assert(strcmp(manifest.renderer_id,
                  TTFACE_RENDERER_HYDRO_AQUA) == 0);
    assert(ttface_legacy_renderer_face_index(manifest.renderer_id) == 1);
    assert(ttface_legacy_renderer_face_index("unknown.renderer") == -2);
    assert(manifest.element_count == 2);
    assert(manifest.elements[0].type == TTFACE_EL_DIGITAL_TIME);
    assert(manifest.elements[0].rule_id == TTFACE_RULE_BATTERY_LOW);
    assert(manifest.elements[1].rule_id == TTFACE_RULE_UNKNOWN);
    assert(!ttface_is_element_visible(&manifest.elements[1], &state));

    ttface_blit_rgb565(1, 2, pixels);
    assert(area_called);
    assert(area_format == MWPF_TRUECOLOR565);

    assert(remove(TEST_PATH) == 0);
    free(pixels);
    puts("ttface loader tests passed");
    return 0;
}
