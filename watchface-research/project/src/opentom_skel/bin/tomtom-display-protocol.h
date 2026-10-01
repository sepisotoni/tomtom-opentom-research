#ifndef TOMTOM_DISPLAY_PROTOCOL_H
#define TOMTOM_DISPLAY_PROTOCOL_H

#define TOMTOM_DISPLAY_HEADER_SIZE 22
#define TOMTOM_DISPLAY_ACK_SIZE 9
#define TOMTOM_DISPLAY_WIDTH 320
#define TOMTOM_DISPLAY_HEIGHT 240
#define TOMTOM_DISPLAY_BYTES_PER_PIXEL 2
#define TOMTOM_DISPLAY_FRAME_BYTES \
    (TOMTOM_DISPLAY_WIDTH * TOMTOM_DISPLAY_HEIGHT * \
     TOMTOM_DISPLAY_BYTES_PER_PIXEL)
#define TOMTOM_DISPLAY_FORMAT_RGB565_LE 1
#define TOMTOM_DISPLAY_VERSION 1
#define TOMTOM_DISPLAY_MESSAGE_FRAME 1

#define TOMTOM_DISPLAY_ACK_OK 0
#define TOMTOM_DISPLAY_ACK_INVALID 1
#define TOMTOM_DISPLAY_ACK_RATE_LIMIT 2
#define TOMTOM_DISPLAY_ACK_FRAMEBUFFER 3

#ifdef __cplusplus
extern "C" {
#endif

struct tomtom_display_frame {
    unsigned long sequence;
};

unsigned int tomtom_display_read_u16(const unsigned char *bytes);
unsigned long tomtom_display_read_u32(const unsigned char *bytes);
void tomtom_display_write_u16(unsigned char *bytes, unsigned int value);
void tomtom_display_write_u32(unsigned char *bytes, unsigned long value);
void tomtom_display_make_header(unsigned char *bytes,
                                unsigned long sequence);
int tomtom_display_parse_header(const unsigned char *bytes,
                                unsigned long length,
                                struct tomtom_display_frame *frame);
void tomtom_display_make_ack(unsigned char *bytes, unsigned long sequence,
                             unsigned int status);

#ifdef __cplusplus
}
#endif

#endif
