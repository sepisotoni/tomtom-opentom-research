#include "../src/opentom_skel/bin/tomtom-display-protocol.h"

#include <stdio.h>
#include <string.h>

static int
expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

int
main(void)
{
    unsigned char header[TOMTOM_DISPLAY_HEADER_SIZE];
    unsigned char acknowledgement[TOMTOM_DISPLAY_ACK_SIZE];
    struct tomtom_display_frame frame;
    int passed = 1;

    memset(header, 0, sizeof(header));
    header[0] = 'T';
    header[1] = 'T';
    header[2] = 'D';
    header[3] = 'P';
    header[4] = TOMTOM_DISPLAY_VERSION;
    header[5] = TOMTOM_DISPLAY_MESSAGE_FRAME;
    tomtom_display_write_u16(header + 6, TOMTOM_DISPLAY_HEADER_SIZE);
    tomtom_display_write_u16(header + 8, TOMTOM_DISPLAY_WIDTH);
    tomtom_display_write_u16(header + 10, TOMTOM_DISPLAY_HEIGHT);
    tomtom_display_write_u16(header + 12,
                              TOMTOM_DISPLAY_FORMAT_RGB565_LE);
    tomtom_display_write_u32(header + 14, 0x78563412UL);
    tomtom_display_write_u32(header + 18, TOMTOM_DISPLAY_FRAME_BYTES);

    passed &= expect(tomtom_display_parse_header(
                         header, sizeof(header), &frame),
                     "accept the supported RGB565 frame header");
    passed &= expect(frame.sequence == 0x78563412UL,
                     "decode sequence number little-endian");
    passed &= expect(!tomtom_display_parse_header(
                         header, sizeof(header) - 1, &frame),
                     "reject a short header");
    passed &= expect(!tomtom_display_parse_header(
                         header, sizeof(header) + 1, &frame),
                     "reject a long header");

    header[0] = 'X';
    passed &= expect(!tomtom_display_parse_header(
                         header, sizeof(header), &frame),
                     "reject invalid magic");
    header[0] = 'T';
    header[4] = TOMTOM_DISPLAY_VERSION + 1;
    passed &= expect(!tomtom_display_parse_header(
                         header, sizeof(header), &frame),
                     "reject unsupported version");
    header[4] = TOMTOM_DISPLAY_VERSION;
    tomtom_display_write_u16(header + 8, TOMTOM_DISPLAY_WIDTH - 1);
    passed &= expect(!tomtom_display_parse_header(
                         header, sizeof(header), &frame),
                     "reject unsupported width");
    tomtom_display_write_u16(header + 8, TOMTOM_DISPLAY_WIDTH);
    tomtom_display_write_u16(header + 12, 2);
    passed &= expect(!tomtom_display_parse_header(
                         header, sizeof(header), &frame),
                     "reject unsupported pixel format");
    tomtom_display_write_u16(header + 12,
                             TOMTOM_DISPLAY_FORMAT_RGB565_LE);
    tomtom_display_write_u32(header + 18,
                             TOMTOM_DISPLAY_FRAME_BYTES - 1);
    passed &= expect(!tomtom_display_parse_header(
                         header, sizeof(header), &frame),
                     "reject truncated frame payload size");
    tomtom_display_write_u32(header + 18, TOMTOM_DISPLAY_FRAME_BYTES);
    passed &= expect(!tomtom_display_parse_header(
                         NULL, sizeof(header), &frame),
                     "reject a null header");
    passed &= expect(!tomtom_display_parse_header(
                         header, sizeof(header), NULL),
                     "reject a null output frame");

    tomtom_display_make_ack(acknowledgement, 0x78563412UL,
                            TOMTOM_DISPLAY_ACK_RATE_LIMIT);
    passed &= expect(acknowledgement[0] == 'T' &&
                     acknowledgement[1] == 'T' &&
                     acknowledgement[2] == 'A' &&
                     acknowledgement[3] == '1',
                     "encode ACK magic");
    passed &= expect(tomtom_display_read_u32(acknowledgement + 4) ==
                         0x78563412UL,
                     "encode ACK sequence");
    passed &= expect(acknowledgement[8] ==
                         TOMTOM_DISPLAY_ACK_RATE_LIMIT,
                     "encode ACK status");

    if (!passed)
        return 1;
    puts("display frame protocol: 12 checks passed");
    return 0;
}
