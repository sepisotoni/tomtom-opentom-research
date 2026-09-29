#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "nano-X.h"
#include <barcelona/Barc_Battery.h>
#include "info_anim/info_anim.h"

#define LOGICAL_W 320
#define LOGICAL_H 240
#define ATLAS_CELL_W 100
#define ATLAS_CELL_H 170
#define DIGIT_W 70
#define DIGIT_H 156
#define DIGIT_Y 42
#define FRAME_TIMEOUT_MS 1000

typedef struct {
    int width;
    int height;
    unsigned char *pixels;
} GlyphAtlas;

enum WeatherCondition {
    WEATHER_UNKNOWN,
    WEATHER_SUNNY,
    WEATHER_CLOUDY,
    WEATHER_PARTLY_CLOUDY,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_STORM
};

enum WeatherAlert {
    WEATHER_ALERT_NONE,
    WEATHER_ALERT_HEAT,
    WEATHER_ALERT_COLD,
    WEATHER_ALERT_HEAVY_RAIN,
    WEATHER_ALERT_STORM,
    WEATHER_ALERT_SNOW
};

typedef struct {
    int available;
    int noteworthy;
    enum WeatherCondition condition;
    int temperature_c;
    enum WeatherAlert alert;
    int precipitation_probability;
} WeatherDisplay;

static unsigned short logical_pixels[LOGICAL_W * LOGICAL_H];

#define BLUE_RED 32
#define BLUE_GREEN 88
#define BLUE_BLUE 210
#define NIGHT_SCALE 72

#define FACE_BLUE_OUTLINE 0
#define FACE_AQUA_WAVE 1
#define FACE_LAVENDER 2
#define FACE_SUNSET 3
#define FACE_WEATHER 4
#define FACE_NUMERALS_DUO 5
#define FACE_FONT_ROBOTO 6
#define FACE_FONT_UBUNTU 7
#define FACE_FONT_NUNITO 8
#define FACE_COUNT 9
#define PREVIEW_CONFIG "/mnt/sdcard/opentom/preview-gallery/watchface.cfg"
#define CURRENT_FACE_FILE "/mnt/sdcard/opentom/preview-gallery/current_face"
#define PREVIEW_CONFIG_MAX_LINE 256

static int current_night_mode;
static int available_face_count = FACE_NUMERALS_DUO + 1;
static int current_face = FACE_BLUE_OUTLINE;
static int config_12hour = 1;
static int config_show_ampm = 1;
static int config_stacked = 0;
static int cycle_start = FACE_BLUE_OUTLINE;
static int cycle_count;
static int configured_default_face;
static int configured_cycle_start;
static int configured_cycle_count;
static char artwork_dir[160] = "/mnt/sdcard/opentom/preview-gallery";
static char configured_atlas_paths[6][256];
static volatile sig_atomic_t face_change_requested;
static volatile sig_atomic_t info_panel_requested;
static time_t last_face_selection_poll;
static time_t last_weather_poll;
static InfoAnim info_animation;
static int last_animation_progress = -1;
static WeatherDisplay weather_display = {
    0, 0, WEATHER_UNKNOWN, 0, WEATHER_ALERT_NONE, 0
};

static int
read_device_weather(WeatherDisplay *weather)
{
    struct sockaddr_in address;
    struct timeval timeout;
    char reply[96];
    int fd;
    int count;
    int condition;
    int temperature;
    int alert;
    int precipitation;
    int noteworthy;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(18743);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        send(fd, "WEATHER_STATUS\n", 15, 0) != 15) {
        close(fd);
        return 0;
    }
    count = (int)recv(fd, reply, sizeof(reply) - 1, 0);
    close(fd);
    if (count <= 0)
        return 0;
    reply[count] = '\0';
    if (strcmp(reply, "OK WEATHER NONE\n") == 0) {
        if (weather->available || weather->noteworthy) {
            weather->available = 0;
            weather->noteworthy = 0;
            weather->condition = WEATHER_UNKNOWN;
            weather->temperature_c = 0;
            weather->alert = WEATHER_ALERT_NONE;
            weather->precipitation_probability = 0;
            return 1;
        }
        return 0;
    }
    if (sscanf(reply, "OK WEATHER %d %d %d %d %d",
               &condition, &temperature, &alert, &precipitation,
               &noteworthy) != 5 ||
        condition < WEATHER_UNKNOWN || condition > WEATHER_STORM ||
        temperature < -100 || temperature > 100 ||
        alert < WEATHER_ALERT_NONE || alert > 5 ||
        precipitation < 0 || precipitation > 100 ||
        (noteworthy != 0 && noteworthy != 1))
        return 0;
    if (weather->available &&
        weather->condition == (enum WeatherCondition)condition &&
        weather->temperature_c == temperature &&
        weather->alert == (enum WeatherAlert)alert &&
        weather->precipitation_probability == precipitation &&
        weather->noteworthy == noteworthy)
        return 0;
    weather->available = 1;
    weather->condition = (enum WeatherCondition)condition;
    weather->temperature_c = temperature;
    weather->alert = (enum WeatherAlert)alert;
    weather->precipitation_probability = precipitation;
    weather->noteworthy = noteworthy;
    return 1;
}

static int
is_font_face(int face)
{
    return face >= FACE_FONT_ROBOTO && face <= FACE_FONT_NUNITO;
}

static unsigned long
now_milliseconds(void)
{
    struct timeval now;

    if (gettimeofday(&now, NULL) != 0) {
        perror("cannot read animation clock");
        return 0;
    }
    return (unsigned long)now.tv_sec * 1000UL +
        (unsigned long)(now.tv_usec / 1000);
}

static void
load_configuration(void)
{
    FILE *file = fopen(PREVIEW_CONFIG, "r");
    char line[PREVIEW_CONFIG_MAX_LINE];
    if (file == NULL)
        return;
    while (fgets(line, sizeof(line), file) != NULL) {
        char key[48];
        char value[64];

        if (sscanf(line, "%47[^=]=%63s", key, value) != 2)
            continue;
        if (strcmp(key, "time_format") == 0) {
            config_12hour = strcmp(value, "24") != 0;
        } else if (strcmp(key, "show_ampm") == 0) {
            config_show_ampm = atoi(value) != 0;
        } else if (strcmp(key, "layout") == 0) {
            config_stacked = strcmp(value, "stacked") == 0;
        } else if (strcmp(key, "default_face") == 0) {
            configured_default_face = atoi(value);
        } else if (strcmp(key, "cycle_start") == 0) {
            configured_cycle_start = atoi(value);
        } else if (strcmp(key, "cycle_count") == 0) {
            configured_cycle_count = atoi(value);
        } else if (strcmp(key, "artwork_dir") == 0) {
            if (strlen(value) >= sizeof(artwork_dir)) {
                fprintf(stderr, "Preview artwork directory is too long\n");
                fclose(file);
                return;
            }
            strcpy(artwork_dir, value);
        } else if (strcmp(key, "atlas_outline") == 0) {
            strncpy(configured_atlas_paths[0], value,
                    sizeof(configured_atlas_paths[0]) - 1);
        } else if (strcmp(key, "atlas_solid") == 0) {
            strncpy(configured_atlas_paths[1], value,
                    sizeof(configured_atlas_paths[1]) - 1);
        } else if (strcmp(key, "atlas_rounded") == 0) {
            strncpy(configured_atlas_paths[2], value,
                    sizeof(configured_atlas_paths[2]) - 1);
        } else if (strcmp(key, "atlas_roboto") == 0) {
            strncpy(configured_atlas_paths[3], value,
                    sizeof(configured_atlas_paths[3]) - 1);
        } else if (strcmp(key, "atlas_ubuntu") == 0) {
            strncpy(configured_atlas_paths[4], value,
                    sizeof(configured_atlas_paths[4]) - 1);
        } else if (strcmp(key, "atlas_nunito") == 0) {
            strncpy(configured_atlas_paths[5], value,
                    sizeof(configured_atlas_paths[5]) - 1);
        }
    }
    if (fclose(file) != 0)
        perror(PREVIEW_CONFIG);
}

static void
finalize_configuration(void)
{
    int requested_start = configured_cycle_start;
    int requested_count = configured_cycle_count;
    int requested_default = configured_default_face;

    if (requested_count == 0) {
        requested_start = FACE_BLUE_OUTLINE;
        requested_count = available_face_count;
    }
    if (requested_start < 0 ||
        requested_start >= available_face_count ||
        requested_count < 1 ||
        requested_count > available_face_count - requested_start) {
        fprintf(stderr, "Invalid face-cycle range in %s; using all faces\n",
                PREVIEW_CONFIG);
        requested_start = FACE_BLUE_OUTLINE;
        requested_count = available_face_count;
    }
    cycle_start = requested_start;
    cycle_count = requested_count;
    if (requested_default < cycle_start ||
        requested_default >= cycle_start + cycle_count) {
        fprintf(stderr, "Default face is outside the configured cycle; "
                "starting at face %d\n", cycle_start);
        requested_default = cycle_start;
    }
    current_face = requested_default;
}

static void
resolve_atlas_path(char *destination, size_t size,
                   const char *configured, const char *fallback)
{
    int written;

    if (configured[0] == '/') {
        written = snprintf(destination, size, "%s", configured);
    } else {
        written = snprintf(destination, size, "%s/%s",
                           artwork_dir,
                           configured[0] != '\0' ? configured : fallback);
    }
    if (written < 0 || (size_t)written >= size) {
        fprintf(stderr, "Preview atlas path is too long\n");
        destination[0] = '\0';
    }
}

static void persist_requested_face(void);

static void
advance_face(void)
{
    if (cycle_count < 1) {
        current_face = (current_face + 1) % available_face_count;
        persist_requested_face();
        return;
    }
    if (current_face < cycle_start ||
        current_face >= cycle_start + cycle_count) {
        current_face = cycle_start;
        persist_requested_face();
        return;
    }
    current_face++;
    if (current_face >= cycle_start + cycle_count)
        current_face = cycle_start;
    persist_requested_face();
}

static int
read_requested_face(void)
{
    FILE *file = fopen(CURRENT_FACE_FILE, "r");
    char line[16];
    char *end;
    long requested_face;

    if (file == NULL)
        return -1;
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return -1;
    }
    fclose(file);

    requested_face = strtol(line, &end, 10);
    if (end == line)
        return -1;
    while (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')
        ++end;
    if (*end != '\0' || requested_face < FACE_BLUE_OUTLINE ||
        requested_face >= available_face_count)
        return -1;
    return (int)requested_face;
}

static void
persist_requested_face(void)
{
    const char *temporary_path =
        "/mnt/sdcard/opentom/preview-gallery/current_face.tmp";
    FILE *file = fopen(temporary_path, "w");
    int write_failed;

    if (file == NULL) {
        perror("cannot save current face");
        return;
    }
    write_failed = fprintf(file, "%d\n", current_face) < 0;
    if (fclose(file) != 0)
        write_failed = 1;
    if (write_failed) {
        perror("cannot save current face");
        remove(temporary_path);
        return;
    }
    if (rename(temporary_path, CURRENT_FACE_FILE) != 0) {
        perror("cannot activate current face");
        remove(temporary_path);
    }
}

static void
request_next_face(int signal_number)
{
    (void)signal_number;
    face_change_requested = 1;
}

static void
request_info_panel(int signal_number)
{
    (void)signal_number;
    info_panel_requested = 1;
}

static unsigned short
color565(unsigned int red, unsigned int green, unsigned int blue)
{
    return (unsigned short)(((red >> 3) << 11) |
                            ((green >> 2) << 5) |
                            (blue >> 3));
}

static GR_COLOR
fade_color_to_black(GR_COLOR color, int alpha)
{
    unsigned int red = color & 255U;
    unsigned int green = (color >> 8) & 255U;
    unsigned int blue = (color >> 16) & 255U;
    unsigned short blended =
        ia_blend565(0, color565(red, green, blue), alpha);

    red = ((blended >> 11) & 31U) * 255U / 31U;
    green = ((blended >> 5) & 63U) * 255U / 63U;
    blue = (blended & 31U) * 255U / 31U;
    return GR_RGB(red, green, blue);
}

static unsigned short
face_background(int night_mode)
{
    (void)night_mode;
    return color565(0, 0, 0);
}

static int
read_pgm_token(FILE *fp, char *token, size_t token_size)
{
    int c;
    size_t used = 0;

    do {
        c = fgetc(fp);
        if (c == '#') {
            do {
                c = fgetc(fp);
            } while (c != '\n' && c != EOF);
        }
    } while (c != EOF &&
             (c == ' ' || c == '\t' || c == '\r' || c == '\n'));

    if (c == EOF)
        return 0;
    do {
        if (used + 1 < token_size)
            token[used++] = (char)c;
        c = fgetc(fp);
    } while (c != EOF &&
             c != ' ' && c != '\t' && c != '\r' && c != '\n');
    token[used] = '\0';
    return used != 0;
}

static int
load_atlas(const char *path, GlyphAtlas *atlas)
{
    FILE *fp;
    char token[32];
    size_t count;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        perror(path);
        return 0;
    }
    if (!read_pgm_token(fp, token, sizeof(token)) ||
        strcmp(token, "P5") != 0 ||
        !read_pgm_token(fp, token, sizeof(token))) {
        fclose(fp);
        fprintf(stderr, "%s: expected binary PGM (P5)\n", path);
        return 0;
    }
    atlas->width = atoi(token);
    if (!read_pgm_token(fp, token, sizeof(token))) {
        fclose(fp);
        return 0;
    }
    atlas->height = atoi(token);
    if (!read_pgm_token(fp, token, sizeof(token)) || atoi(token) != 255 ||
        atlas->width != ATLAS_CELL_W * 10 ||
        atlas->height != ATLAS_CELL_H) {
        fclose(fp);
        fprintf(stderr, "%s: expected 1000x170 8-bit PGM atlas\n", path);
        return 0;
    }
    count = (size_t)atlas->width * (size_t)atlas->height;
    atlas->pixels = (unsigned char *)malloc(count);
    if (atlas->pixels == NULL) {
        fclose(fp);
        perror("malloc");
        return 0;
    }
    if (fread(atlas->pixels, 1, count, fp) != count) {
        fclose(fp);
        free(atlas->pixels);
        atlas->pixels = NULL;
        fprintf(stderr, "%s: truncated PGM atlas\n", path);
        return 0;
    }
    fclose(fp);
    return 1;
}

static unsigned char
sample_mask(const GlyphAtlas *atlas, int digit, int x, int y)
{
    int sx;
    int sy;

    if (digit < 0 || digit > 9 || x < 0 || x >= DIGIT_W ||
        y < 0 || y >= DIGIT_H)
        return 0;
    sx = digit * ATLAS_CELL_W + x * ATLAS_CELL_W / DIGIT_W;
    sy = y * ATLAS_CELL_H / DIGIT_H;
    return atlas->pixels[(size_t)sy * atlas->width + sx];
}

static unsigned int
sample_numeral_mask(const GlyphAtlas *rounded_atlas,
                    const GlyphAtlas *fallback_atlas,
                    int digit, int x, int y)
{
    if (rounded_atlas != NULL && rounded_atlas->pixels != NULL)
        return sample_mask(rounded_atlas, digit, x, y);
    return sample_mask(fallback_atlas, digit, x, y);
}

static void
face_rgb(int face, int minute_digit, unsigned int *red,
         unsigned int *green, unsigned int *blue)
{
    switch (face) {
    case FACE_AQUA_WAVE:
        *red = minute_digit ? 20 : 0;
        *green = minute_digit ? 180 : 245;
        *blue = 255;
        break;
    case FACE_LAVENDER:
        *red = minute_digit ? 218 : 232;
        *green = minute_digit ? 196 : 218;
        *blue = minute_digit ? 236 : 242;
        break;
    case FACE_SUNSET:
        *red = 255;
        *green = minute_digit ? 125 : 115;
        *blue = minute_digit ? 215 : 65;
        break;
    case FACE_NUMERALS_DUO:
        *red = minute_digit ? 20 : 255;
        *green = minute_digit ? 48 : 138;
        *blue = minute_digit ? 88 : 36;
        break;
    case FACE_WEATHER:
        *red = minute_digit ? 147 : 96;
        *green = minute_digit ? 197 : 165;
        *blue = minute_digit ? 253 : 250;
        break;
    case FACE_FONT_ROBOTO:
        *red = minute_digit ? 67 : 75;
        *green = minute_digit ? 185 : 225;
        *blue = minute_digit ? 255 : 250;
        break;
    case FACE_FONT_UBUNTU:
        *red = minute_digit ? 168 : 255;
        *green = minute_digit ? 131 : 177;
        *blue = minute_digit ? 255 : 82;
        break;
    case FACE_FONT_NUNITO:
        *red = minute_digit ? 82 : 151;
        *green = minute_digit ? 218 : 244;
        *blue = minute_digit ? 172 : 126;
        break;
    default:
        *red = BLUE_RED;
        *green = BLUE_GREEN;
        *blue = BLUE_BLUE;
        break;
    }
}

static unsigned short
face_color565(int face, int minute_digit, int night_mode)
{
    unsigned int red;
    unsigned int green;
    unsigned int blue;
    unsigned int scale = night_mode ? NIGHT_SCALE : 100;

    face_rgb(face, minute_digit, &red, &green, &blue);
    red = red * scale / 100;
    green = green * scale / 100;
    blue = blue * scale / 100;
    return color565(red, green, blue);
}

static void
blend_color_at(int x, int y, unsigned short color, unsigned int alpha)
{
    unsigned short destination;
    unsigned int inverse;
    unsigned int red;
    unsigned int green;
    unsigned int blue;

    if (x < 0 || x >= LOGICAL_W || y < 0 || y >= LOGICAL_H || alpha == 0)
        return;
    if (alpha > 255U)
        alpha = 255U;
    if (alpha == 255U) {
        logical_pixels[y * LOGICAL_W + x] = color;
        return;
    }
    destination = logical_pixels[y * LOGICAL_W + x];
    inverse = 255U - alpha;
    red = (((destination >> 11) & 31U) * inverse +
           ((color >> 11) & 31U) * alpha + 127U) / 255U;
    green = (((destination >> 5) & 63U) * inverse +
             ((color >> 5) & 63U) * alpha + 127U) / 255U;
    blue = ((destination & 31U) * inverse +
            (color & 31U) * alpha + 127U) / 255U;
    logical_pixels[y * LOGICAL_W + x] =
        (unsigned short)((red << 11) | (green << 5) | blue);
}

static void
draw_digit(const GlyphAtlas *outline_atlas, const GlyphAtlas *solid_atlas,
           const GlyphAtlas *rounded_atlas, const GlyphAtlas *roboto_atlas,
           const GlyphAtlas *ubuntu_atlas, const GlyphAtlas *nunito_atlas,
           int digit, int x, int y, int width, int height,
           unsigned int opacity, int face, int minute_digit)
{
    const GlyphAtlas *atlas = outline_atlas;
    unsigned short color;
    int px;
    int py;

    if (face == FACE_FONT_ROBOTO && roboto_atlas != NULL &&
        roboto_atlas->pixels != NULL)
        atlas = roboto_atlas;
    else if (face == FACE_FONT_UBUNTU && ubuntu_atlas != NULL &&
             ubuntu_atlas->pixels != NULL)
        atlas = ubuntu_atlas;
    else if (face == FACE_FONT_NUNITO && nunito_atlas != NULL &&
             nunito_atlas->pixels != NULL)
        atlas = nunito_atlas;
    else if (face == FACE_AQUA_WAVE || face == FACE_LAVENDER ||
             face == FACE_SUNSET)
        atlas = solid_atlas;
    if (width <= 0 || height <= 0 || opacity == 0)
        return;
    color = face_color565(face, minute_digit, current_night_mode);
    {
        int source_y = 0;
        int source_y_remainder = 0;

        for (py = 0; py < height; ++py) {
            int source_x = 0;
            int source_x_remainder = 0;

            for (px = 0; px < width; ++px) {
                unsigned int alpha;

                if (face == FACE_NUMERALS_DUO)
                    alpha = sample_numeral_mask(rounded_atlas, solid_atlas,
                                                digit, source_x, source_y);
                else
                    alpha = sample_mask(atlas, digit, source_x, source_y);
                alpha = alpha * opacity / 255U;
                blend_color_at(x + px, y + py, color, alpha);
                source_x_remainder += DIGIT_W;
                while (source_x_remainder >= width) {
                    source_x_remainder -= width;
                    ++source_x;
                }
            }
            source_y_remainder += DIGIT_H;
            while (source_y_remainder >= height) {
                source_y_remainder -= height;
                ++source_y;
            }
        }
    }
}

static int
battery_level_from_voltage(int voltage_mv)
{
    if (voltage_mv <= 3500)
        return 0;
    if (voltage_mv >= 4150)
        return 100;
    return (voltage_mv - 3500) * 100 / 650;
}

static void
draw_battery(int level, int charging, int night_mode, int face)
{
    unsigned short outline = face_color565(face, 0, night_mode);
    unsigned short interior = face_background(night_mode);
    unsigned short fill = color565(88U, 145U, 232U);
    int x = 282;
    int y = 8;
    int row;
    int col;
    int fill_width;

    if (night_mode)
        fill = color565(88U * NIGHT_SCALE / 100,
                        145U * NIGHT_SCALE / 100,
                        232U * NIGHT_SCALE / 100);
    if (level < 0)
        level = 0;
    if (level > 100)
        level = 100;
    fill_width = 17 * level / 100;

    for (row = 0; row < 13; ++row) {
        for (col = 0; col < 25; ++col) {
            int border = row == 0 || row == 12 || col == 0 || col == 24;
            unsigned short color = border ? outline : interior;

            if (!border && level > 0 && col >= 3 &&
                col < 3 + fill_width && row >= 3 && row <= 9)
                color = fill;
            blend_color_at(x + col, y + row, color, 255U);
        }
    }
    for (row = 4; row <= 8; ++row)
        blend_color_at(x + 25, y + row, outline, 255U);

    if (charging) {
        static const unsigned char bolt[7][5] = {
            {0, 0, 1, 0, 0}, {0, 0, 1, 0, 0},
            {0, 1, 1, 0, 0}, {0, 0, 1, 0, 0},
            {0, 0, 1, 0, 0}, {0, 1, 1, 1, 0},
            {0, 0, 1, 0, 0}
        };
        int by;
        int bx;

        for (by = 0; by < 7; ++by) {
            for (bx = 0; bx < 5; ++bx) {
                if (bolt[by][bx])
                    blend_color_at(x + 9 + bx, y + 3 + by,
                                   outline, 255U);
            }
        }
    }
}

static void
put_text(GR_DRAW_ID drawable, GR_GC_ID gc, GR_FONT_ID font,
         const char *text, int x, int baseline, GR_COLOR color)
{
    GrSetGCForeground(gc, color);
    GrSetGCFont(gc, font);
    GrText(drawable, gc, x, baseline, (void *)text, -1, GR_TFBASELINE);
}

static void
draw_live_details(GR_WINDOW_ID window, GR_WINDOW_ID pixmap, GR_GC_ID gc,
                  GR_FONT_ID detail_font, int second, int colon_on,
                  int show_colon, int night_mode, int face,
                  int animation_progress)
{
    static const int circle_half_width[9] = {
        0, 2, 3, 3, 4, 3, 3, 2, 0
    };
    static const unsigned char digits[10][5] = {
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7},
        {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
        {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7},
        {7, 5, 7, 1, 7}
    };
    int i;

    if (face == FACE_NUMERALS_DUO) {
        char seconds[3];
        GR_COLOR color = GR_RGB(255, 138, 36);
        int weather_mode =
            weather_display.available && animation_progress > 0;

        snprintf(seconds, sizeof(seconds), "%02d", second);
        if (night_mode)
            color = GR_RGB(255U * NIGHT_SCALE / 100U,
                           138U * NIGHT_SCALE / 100U,
                           36U * NIGHT_SCALE / 100U);
        if (weather_mode) {
            GrCopyArea(window, gc, 12, 156, 76, 24, pixmap, 12, 156, 0);
            put_text(window, gc, detail_font, seconds, 12, 176, color);
            return;
        }
        GrCopyArea(window, gc, 284, 214, 36, 26, pixmap, 284, 214, 0);
        for (i = 0; i < 2; ++i) {
            int digit = i == 0 ? second / 10 : second % 10;
            int row;
            int col;

            GrSetGCForeground(gc, color);
            for (row = 0; row < 5; ++row) {
                for (col = 0; col < 3; ++col) {
                    if ((digits[digit][row] >> (2 - col)) & 1)
                        GrFillRect(window, gc, 296 + i * 8 + col * 2,
                                   218 + row * 2, 2, 2);
                }
            }
        }
        return;
    }
    /* Keep small seconds markers from covering the enlarged transition time. */
    if (animation_progress > 0 && face != FACE_WEATHER)
        return;
    GrCopyArea(window, gc, 145, 95, 30, 52, pixmap, 145, 95, 0);
    GrCopyArea(window, gc, 284, 214, 36, 26, pixmap, 284, 214, 0);
    if (show_colon && colon_on) {
        int dot;

        {
            unsigned int red;
            unsigned int green;
            unsigned int blue;
            face_rgb(face, 0, &red, &green, &blue);
            if (night_mode) {
                red = red * NIGHT_SCALE / 100;
                green = green * NIGHT_SCALE / 100;
                blue = blue * NIGHT_SCALE / 100;
            }
            GrSetGCForeground(gc, GR_RGB(red, green, blue));
        }
        for (dot = 0; dot < 2; ++dot) {
            int center_y = dot == 0 ? 104 : 137;
            int row;

            for (row = -4; row <= 4; ++row) {
                int half_width = circle_half_width[row + 4];

                GrFillRect(window, gc, 160 - half_width,
                           center_y + row, half_width * 2 + 1, 1);
            }
        }
    }
    if (face == FACE_WEATHER)
        return;
    {
        unsigned int red;
        unsigned int green;
        unsigned int blue;
        face_rgb(face, 1, &red, &green, &blue);
        red = red * 65U / 100U;
        green = green * 65U / 100U;
        blue = blue * 65U / 100U;
        if (night_mode) {
            red = red * NIGHT_SCALE / 100;
            green = green * NIGHT_SCALE / 100;
            blue = blue * NIGHT_SCALE / 100;
        }
        GrSetGCForeground(gc, GR_RGB(red, green, blue));
    }
    for (i = 0; i < 2; ++i) {
        int digit = i == 0 ? second / 10 : second % 10;
        int row;
        int col;

        for (row = 0; row < 5; ++row) {
            for (col = 0; col < 3; ++col) {
                if ((digits[digit][row] >> (2 - col)) & 1)
                    GrFillRect(window, gc, 296 + i * 8 + col * 2,
                               218 + row * 2, 2, 2);
            }
        }
    }
}

static GR_COLOR
face_text_color(int face, int night_mode)
{
    unsigned int red;
    unsigned int green;
    unsigned int blue;

    face_rgb(face, 0, &red, &green, &blue);
    red = red * 70U / 100U;
    green = green * 70U / 100U;
    blue = blue * 70U / 100U;
    if (night_mode) {
        red = red * NIGHT_SCALE / 100;
        green = green * NIGHT_SCALE / 100;
        blue = blue * NIGHT_SCALE / 100;
    }
    return GR_RGB(red, green, blue);
}

static void
draw_weather_mask(GR_DRAW_ID drawable, GR_GC_ID gc,
                  const unsigned short *rows, int row_count,
                  int x, int y, int scale, GR_COLOR color)
{
    int row;
    int col;

    GrSetGCForeground(gc, color);
    for (row = 0; row < row_count; ++row) {
        for (col = 0; col < 16; ++col) {
            if (rows[row] & (1U << (15 - col)))
                GrFillRect(drawable, gc, x + col * scale, y + row * scale,
                           scale, scale);
        }
    }
}

static void
draw_weather_icon(GR_DRAW_ID drawable, GR_GC_ID gc,
                  enum WeatherCondition condition, int x, int y,
                  int scale, int night_mode, int opacity)
{
    static const unsigned short sun_rows[16] = {
        0x0180, 0x0180, 0x0180, 0x0180,
        0x03c0, 0x07e0, 0x1ff8, 0x3ffc,
        0x3ffc, 0x1ff8, 0x07e0, 0x03c0,
        0x0180, 0x0180, 0x0180, 0x0180
    };
    static const unsigned short cloud_rows[12] = {
        0x0000, 0x0000, 0x0180, 0x03c0,
        0x07e0, 0x1ff8, 0x3ffc, 0x7ffe,
        0x7ffe, 0xffff, 0xffff, 0x0000
    };
    static const unsigned short bolt_rows[12] = {
        0x0000, 0x0000, 0x0000, 0x0180,
        0x0380, 0x0700, 0x0fe0, 0x01c0,
        0x0380, 0x0700, 0x0e00, 0x0c00
    };
    GR_COLOR sun = GR_RGB(255, 190, 74);
    GR_COLOR cloud = GR_RGB(145, 206, 232);
    GR_COLOR rain = GR_RGB(65, 190, 245);
    GR_COLOR snow = GR_RGB(220, 242, 255);
    GR_COLOR bolt = GR_RGB(255, 221, 77);

    if (night_mode) {
        sun = GR_RGB(179, 133, 52);
        cloud = GR_RGB(102, 144, 162);
        rain = GR_RGB(45, 133, 171);
        snow = GR_RGB(154, 169, 179);
        bolt = GR_RGB(179, 155, 54);
    }
    sun = fade_color_to_black(sun, opacity);
    cloud = fade_color_to_black(cloud, opacity);
    rain = fade_color_to_black(rain, opacity);
    snow = fade_color_to_black(snow, opacity);
    bolt = fade_color_to_black(bolt, opacity);

    if (condition == WEATHER_SUNNY ||
        condition == WEATHER_PARTLY_CLOUDY)
        draw_weather_mask(drawable, gc, sun_rows, 16, x, y, scale, sun);

    if (condition == WEATHER_CLOUDY ||
        condition == WEATHER_PARTLY_CLOUDY ||
        condition == WEATHER_RAIN ||
        condition == WEATHER_SNOW ||
        condition == WEATHER_STORM ||
        condition == WEATHER_UNKNOWN)
        draw_weather_mask(drawable, gc, cloud_rows, 12,
                          x + 12 * scale, y + 10 * scale, scale, cloud);

    if (condition == WEATHER_RAIN || condition == WEATHER_STORM) {
        GR_COLOR color = condition == WEATHER_STORM ? bolt : rain;
        int drop;
        GrSetGCForeground(gc, color);
        if (condition == WEATHER_STORM) {
            draw_weather_mask(drawable, gc, bolt_rows, 12,
                              x + 18 * scale, y + 17 * scale,
                              scale, bolt);
        } else {
            for (drop = 0; drop < 3; ++drop)
                GrFillRect(drawable, gc, x + (4 + drop * 5) * scale,
                           y + 21 * scale, scale, 4 * scale);
        }
    } else if (condition == WEATHER_SNOW) {
        int flake;
        GrSetGCForeground(gc, snow);
        for (flake = 0; flake < 3; ++flake) {
            int fx = x + (4 + flake * 5) * scale;
            int fy = y + 22 * scale;
            GrFillRect(drawable, gc, fx, fy - scale, scale, 3 * scale);
            GrFillRect(drawable, gc, fx - scale, fy, 3 * scale, scale);
        }
    }
}

static GR_COLOR
weather_attribution_color(int night_mode)
{
    return night_mode ? GR_RGB(72, 84, 96) : GR_RGB(104, 118, 132);
}

static void
draw_weather_widget(GR_DRAW_ID drawable, GR_GC_ID gc, GR_FONT_ID font,
                    int night_mode, const WeatherDisplay *weather)
{
    char details[32];
    GR_COLOR text = face_text_color(FACE_WEATHER, night_mode);
    int icon_x = 12;
    int icon_y = 190;

    if (weather->available && !weather->noteworthy)
        return;

    draw_weather_icon(drawable, gc,
                      weather->available ? weather->condition :
                      WEATHER_UNKNOWN,
                      icon_x, icon_y, 2, night_mode, 255);
    if (!weather->available) {
        int badge_x = 36;
        int badge_y = 190;

        GrSetGCForeground(gc, GR_RGB(255, 255, 255));
        GrFillRect(drawable, gc, badge_x + 5, badge_y + 2, 2, 5);
        GrFillRect(drawable, gc, badge_x + 5, badge_y + 9, 2, 2);
        return;
    }

    put_text(drawable, gc, font, "WEATHER ALERT", 88, 209, text);
    if (weather->precipitation_probability >= 50) {
        snprintf(details, sizeof(details), "%d C  %d%% PRECIP",
                 weather->temperature_c,
                 weather->precipitation_probability);
    } else if (weather->alert == WEATHER_ALERT_HEAT) {
        snprintf(details, sizeof(details), "%d C  VERY HOT",
                 weather->temperature_c);
    } else if (weather->alert == WEATHER_ALERT_COLD) {
        snprintf(details, sizeof(details), "%d C  VERY COLD",
                 weather->temperature_c);
    } else if (weather->alert == WEATHER_ALERT_STORM) {
        snprintf(details, sizeof(details), "%d C  STORM",
                 weather->temperature_c);
    } else if (weather->alert == WEATHER_ALERT_SNOW) {
        snprintf(details, sizeof(details), "%d C  SNOW",
                 weather->temperature_c);
    } else if (weather->alert == WEATHER_ALERT_HEAVY_RAIN) {
        snprintf(details, sizeof(details), "%d C  HEAVY RAIN",
                 weather->temperature_c);
    } else {
        snprintf(details, sizeof(details), "%d C  ALERT",
                 weather->temperature_c);
    }
    put_text(drawable, gc, font,
             "Source: Includes weather data from Google", 88, 190,
             weather_attribution_color(night_mode));
    put_text(drawable, gc, font, details, 88, 227, text);
}

static void
draw_numduo_info(GR_DRAW_ID drawable, GR_GC_ID gc, GR_FONT_ID font,
                 int battery_level, int battery_charging, int night_mode,
                 int panel_alpha)
{
    char details[32];
    const char *condition = "WEATHER";
    GR_COLOR text = fade_color_to_black(
        face_text_color(FACE_NUMERALS_DUO, night_mode), panel_alpha);
    GR_COLOR outline = GR_RGB(70, 104, 132);
    GR_COLOR fill;
    int fill_width;
    int weather_mode = weather_display.available;
    int battery_y = weather_mode ? 211 : 70;

    if (night_mode)
        outline = GR_RGB(50, 75, 96);
    outline = fade_color_to_black(outline, panel_alpha);
    if (weather_mode) {
        switch (weather_display.condition) {
        case WEATHER_SUNNY:
            condition = "SUNNY";
            break;
        case WEATHER_CLOUDY:
            condition = "CLOUDY";
            break;
        case WEATHER_PARTLY_CLOUDY:
            condition = "PARTLY CLOUDY";
            break;
        case WEATHER_RAIN:
            condition = "RAIN";
            break;
        case WEATHER_SNOW:
            condition = "SNOW";
            break;
        case WEATHER_STORM:
            condition = "STORM";
            break;
        default:
            break;
        }
        draw_weather_icon(drawable, gc, weather_display.condition,
                          112, 34, 1, night_mode, panel_alpha);
        put_text(drawable, gc, font, condition, 12, 55, text);
        snprintf(details, sizeof(details), "%d C",
                 weather_display.temperature_c);
        put_text(drawable, gc, font, details, 12, 75, text);
        if (weather_display.precipitation_probability >= 50) {
            snprintf(details, sizeof(details), "%d%% PRECIP",
                     weather_display.precipitation_probability);
        } else {
            strcpy(details, "WEATHER ALERT");
        }
        put_text(drawable, gc, font, details, 12, 95, text);
        put_text(drawable, gc, font, "Source: Includes", 12, 115,
                 fade_color_to_black(weather_attribution_color(night_mode),
                                     panel_alpha));
        put_text(drawable, gc, font, "weather data from", 12, 127,
                 fade_color_to_black(weather_attribution_color(night_mode),
                                     panel_alpha));
        put_text(drawable, gc, font, "Google", 12, 139,
                 fade_color_to_black(weather_attribution_color(night_mode),
                                     panel_alpha));
        put_text(drawable, gc, font, "SECONDS", 12, 161, text);
        put_text(drawable, gc, font, "BATTERY", 12, 199, text);
    } else {
        put_text(drawable, gc, font, "BATTERY", 12, 58, text);
        put_text(drawable, gc, font, "SECONDS", 12, 119, text);
    }

    if (battery_level < 0)
        battery_level = 0;
    if (battery_level > 100)
        battery_level = 100;
    fill_width = 102 * battery_level / 100;
    if (battery_charging)
        fill = GR_RGB(102, 225, 214);
    else if (battery_level < 20)
        fill = GR_RGB(255, 112, 96);
    else
        fill = GR_RGB(136, 220, 40);
    if (night_mode && battery_charging)
        fill = GR_RGB(102U * NIGHT_SCALE / 100U,
                      225U * NIGHT_SCALE / 100U,
                      214U * NIGHT_SCALE / 100U);
    else if (night_mode && battery_level < 20)
        fill = GR_RGB(255U * NIGHT_SCALE / 100U,
                      112U * NIGHT_SCALE / 100U,
                      96U * NIGHT_SCALE / 100U);
    else if (night_mode)
        fill = GR_RGB(136U * NIGHT_SCALE / 100U,
                      220U * NIGHT_SCALE / 100U,
                      40U * NIGHT_SCALE / 100U);
    fill = fade_color_to_black(fill, panel_alpha);

    GrSetGCForeground(gc, outline);
    GrRect(drawable, gc, 12, battery_y, 108, 19);
    GrFillRect(drawable, gc, 121, battery_y + 5, 4, 9);
    if (fill_width > 0) {
        GrSetGCForeground(gc, fill);
        GrFillRect(drawable, gc, 15, battery_y + 3, fill_width, 13);
    }
    if (battery_charging) {
        GrSetGCForeground(gc, fade_color_to_black(
            GR_RGB(255, 255, 255), panel_alpha));
        GrFillRect(drawable, gc, 62, battery_y + 5, 3, 8);
        GrFillRect(drawable, gc, 59, battery_y + 8, 9, 3);
    }
}

static void
draw_font_weather_info(GR_DRAW_ID drawable, GR_GC_ID gc, GR_FONT_ID font,
                       int face, int night_mode,
                       const WeatherDisplay *weather, int panel_alpha)
{
    const char *condition = "WEATHER";
    char temperature[16];
    GR_COLOR text =
        fade_color_to_black(face_text_color(face, night_mode), panel_alpha);

    switch (weather->condition) {
    case WEATHER_SUNNY:
        condition = "SUNNY";
        break;
    case WEATHER_CLOUDY:
        condition = "CLOUDY";
        break;
    case WEATHER_PARTLY_CLOUDY:
        condition = "PARTLY CLOUDY";
        break;
    case WEATHER_RAIN:
        condition = "RAIN";
        break;
    case WEATHER_SNOW:
        condition = "SNOW";
        break;
    case WEATHER_STORM:
        condition = "STORM";
        break;
    default:
        break;
    }
    snprintf(temperature, sizeof(temperature), "%d C",
             weather->temperature_c);
    draw_weather_icon(drawable, gc, weather->condition, 112, 48, 1,
                      night_mode, panel_alpha);
    put_text(drawable, gc, font, condition, 12, 94, text);
    put_text(drawable, gc, font, temperature, 12, 118, text);
    if (weather->precipitation_probability >= 50) {
        snprintf(temperature, sizeof(temperature), "%d%% PRECIP",
                 weather->precipitation_probability);
        put_text(drawable, gc, font, temperature, 12, 145, text);
    } else if (weather->noteworthy) {
        put_text(drawable, gc, font, "WEATHER ALERT", 12, 145, text);
    }
    put_text(drawable, gc, font, "Source: Includes", 12, 166,
             fade_color_to_black(weather_attribution_color(night_mode),
                                 panel_alpha));
    put_text(drawable, gc, font, "weather data from", 12, 178,
             fade_color_to_black(weather_attribution_color(night_mode),
                                 panel_alpha));
    put_text(drawable, gc, font, "Google", 12, 190,
             fade_color_to_black(weather_attribution_color(night_mode),
                                 panel_alpha));
}

static void
draw_frame(const GlyphAtlas *outline_atlas, const GlyphAtlas *solid_atlas,
           const GlyphAtlas *rounded_atlas, const GlyphAtlas *roboto_atlas,
           const GlyphAtlas *ubuntu_atlas, const GlyphAtlas *nunito_atlas,
           GR_WINDOW_ID window, GR_WINDOW_ID pixmap, GR_GC_ID gc,
           GR_FONT_ID detail_font, unsigned short *output,
           int width, int height, const int digits[4],
           const struct tm *local, int battery_level, int battery_charging,
           int face, int animation_progress)
{
    const IaLayout *layout;
    IaRect digit_rect;
    GR_SIZE text_width;
    GR_SIZE text_height;
    GR_SIZE text_base;
    GR_COLOR date_color;
    char date[32];
    char ampm[3];
    int center_date_dx;
    int center_date_alpha;
    int left_date_dx;
    int left_date_alpha;
    int panel_alpha;
    int scale_num;
    int scale_den;
    int content_w;
    int content_h;
    int offset_x;
    int offset_y;
    int i;
    int x;
    int y;
    int font_face = is_font_face(face);
    int hour_only = local->tm_min == 0;
    int numduo_has_info =
        weather_display.available && animation_progress > 0;
    int night_mode = local->tm_hour >= 22 || local->tm_hour < 7;
    int layout_kind = face == FACE_NUMERALS_DUO ? IA_KIND_NUMERALS :
        (font_face ? IA_KIND_FONT : IA_KIND_GENERIC);

    current_night_mode = night_mode;
    panel_alpha = ia_panel_alpha(animation_progress);
    for (i = 0; i < LOGICAL_W * LOGICAL_H; ++i)
        logical_pixels[i] = face_background(night_mode);
    layout = ia_layout_for(layout_kind, config_stacked, hour_only);
    for (x = 0; x < layout->n; ++x) {
        ia_digit_rect(layout, x, animation_progress, &digit_rect);
        draw_digit(outline_atlas, solid_atlas, rounded_atlas,
                   roboto_atlas, ubuntu_atlas, nunito_atlas, digits[x],
                   digit_rect.x, digit_rect.y, digit_rect.w, digit_rect.h,
                   255U, face, !hour_only && x >= 2);
    }
    if (numduo_has_info) {
        int divider_height = LOGICAL_H * ia_divider(animation_progress) /
            IA_ONE;
        unsigned short divider = color565(14, 30, 40);

        for (i = 0; i < divider_height; ++i)
            logical_pixels[i * LOGICAL_W + 159] = divider;
    }
    if (face != FACE_NUMERALS_DUO)
        draw_battery(battery_level, battery_charging, night_mode, face);

    if (width * LOGICAL_H <= height * LOGICAL_W) {
        scale_num = width;
        scale_den = LOGICAL_W;
    } else {
        scale_num = height;
        scale_den = LOGICAL_H;
    }
    content_w = LOGICAL_W * scale_num / scale_den;
    content_h = LOGICAL_H * scale_num / scale_den;
    offset_x = (width - content_w) / 2;
    offset_y = (height - content_h) / 2;
    if (width == LOGICAL_W && height == LOGICAL_H) {
        GrArea(pixmap, gc, 0, 0, width, height, logical_pixels,
               MWPF_TRUECOLOR565);
    } else {
        for (y = 0; y < content_h; ++y) {
            int source_y = y * LOGICAL_H / content_h;
            for (x = 0; x < content_w; ++x) {
                int source_x = x * LOGICAL_W / content_w;
                output[(size_t)(offset_y + y) * width + offset_x + x] =
                    logical_pixels[source_y * LOGICAL_W + source_x];
            }
        }
        GrArea(pixmap, gc, 0, 0, width, height, output,
               MWPF_TRUECOLOR565);
    }
    if (font_face && numduo_has_info)
        draw_font_weather_info(pixmap, gc, detail_font, face,
                               night_mode, &weather_display, panel_alpha);
    if (face == FACE_NUMERALS_DUO && numduo_has_info)
        draw_numduo_info(pixmap, gc, detail_font, battery_level,
                         battery_charging, night_mode, panel_alpha);

    strftime(date, sizeof(date), "%a %d %b", local);
    strcpy(ampm, local->tm_hour >= 12 ? "PM" : "AM");
    GrGetGCTextSize(gc, date, -1, GR_TFBASELINE,
                    &text_width, &text_height, &text_base);
    ia_date(animation_progress, &center_date_dx, &center_date_alpha,
            &left_date_dx, &left_date_alpha);
    date_color = face_text_color(face, night_mode);
    if (center_date_alpha > 0)
        put_text(pixmap, gc, detail_font, date,
                 offset_x + (content_w - (int)text_width) / 2 +
                    center_date_dx * scale_num / scale_den,
                 offset_y + (face == FACE_NUMERALS_DUO ? 27 : 31) *
                    scale_num / scale_den,
                 fade_color_to_black(date_color, center_date_alpha));
    if (left_date_alpha > 0)
        put_text(pixmap, gc, detail_font, date,
                 offset_x + 12 * scale_num / scale_den +
                    left_date_dx * scale_num / scale_den,
                 offset_y + 31 * scale_num / scale_den,
                 fade_color_to_black(date_color, left_date_alpha));
    if (config_12hour && config_show_ampm) {
        GrGetGCTextSize(gc, ampm, -1, GR_TFBASELINE,
                        &text_width, &text_height, &text_base);
        put_text(pixmap, gc, detail_font, ampm,
                 offset_x + content_w - (int)text_width -
                    12 * scale_num / scale_den,
                 offset_y + 40 * scale_num / scale_den,
                 face_text_color(face, night_mode));
    }
    if (face == FACE_WEATHER)
        draw_weather_widget(pixmap, gc, detail_font, night_mode,
                            &weather_display);
    GrCopyArea(window, gc, 0, 0, width, height, pixmap, 0, 0, 0);
}

static int
read_digits(const struct tm *local, int digits[4])
{
    int hour = local->tm_hour % 12;
    if (!config_12hour || current_face == FACE_NUMERALS_DUO) {
        hour = local->tm_hour;
    } else if (hour == 0) {
        hour = 12;
    }
    digits[0] = hour / 10;
    digits[1] = hour % 10;
    digits[2] = local->tm_min / 10;
    digits[3] = local->tm_min % 10;
    return 1;
}

int
main(int argc, char **argv)
{
    GlyphAtlas outline_atlas;
    GlyphAtlas solid_atlas;
    GlyphAtlas rounded_atlas;
    GlyphAtlas roboto_atlas;
    GlyphAtlas ubuntu_atlas;
    GlyphAtlas nunito_atlas;
    GR_SCREEN_INFO screen;
    GR_WINDOW_INFO info;
    GR_WINDOW_ID window;
    GR_WINDOW_ID pixmap;
    GR_GC_ID gc;
    GR_FONT_ID font;
    GR_EVENT event;
    int digits[4];
    int have_time = 0;
    int width;
    int height;
    int i;
    int last_second = -1;
    int last_colon = -1;
    int last_day = -1;
    int last_month = -1;
    int last_year = -1;
    int last_night = -1;
    int last_hour_only = -1;
    int last_face = -1;
    int battery_fd;
    int battery_level = -1;
    int battery_charging = 0;
    time_t last_battery_check = 0;
    unsigned short *frame_buffer;
    char atlas_paths[6][320];
    int arg_start;

    if (argc != 1 && (argc < 3 || argc > 7)) {
        fprintf(stderr, "usage: %s digit-atlas-outline.pgm "
                "digit-atlas-solid.pgm [digit-atlas-rounded.pgm "
                "[digit-atlas-roboto.pgm [digit-atlas-ubuntu.pgm "
                "[digit-atlas-nunito.pgm]]]]]\n",
                argv[0]);
        return 2;
    }
    memset(&outline_atlas, 0, sizeof(outline_atlas));
    memset(&solid_atlas, 0, sizeof(solid_atlas));
    memset(&rounded_atlas, 0, sizeof(rounded_atlas));
    memset(&roboto_atlas, 0, sizeof(roboto_atlas));
    memset(&ubuntu_atlas, 0, sizeof(ubuntu_atlas));
    memset(&nunito_atlas, 0, sizeof(nunito_atlas));
    load_configuration();
    resolve_atlas_path(atlas_paths[0], sizeof(atlas_paths[0]),
                       configured_atlas_paths[0], "digit-atlas.pgm");
    resolve_atlas_path(atlas_paths[1], sizeof(atlas_paths[1]),
                       configured_atlas_paths[1], "digit-atlas-solid.pgm");
    resolve_atlas_path(atlas_paths[2], sizeof(atlas_paths[2]),
                       configured_atlas_paths[2],
                       "digit-atlas-numerals.pgm");
    resolve_atlas_path(atlas_paths[3], sizeof(atlas_paths[3]),
                       configured_atlas_paths[3], "digit-atlas-roboto.pgm");
    resolve_atlas_path(atlas_paths[4], sizeof(atlas_paths[4]),
                       configured_atlas_paths[4], "digit-atlas-ubuntu.pgm");
    resolve_atlas_path(atlas_paths[5], sizeof(atlas_paths[5]),
                       configured_atlas_paths[5], "digit-atlas-nunito.pgm");
    arg_start = argc == 1 ? 0 : 1;
    if (argc == 1) {
        if (!load_atlas(atlas_paths[0], &outline_atlas))
            return 1;
    } else if (!load_atlas(argv[arg_start++], &outline_atlas)) {
        return 1;
    }
    if (argc == 1) {
        if (!load_atlas(atlas_paths[1], &solid_atlas)) {
            free(outline_atlas.pixels);
            return 1;
        }
    } else if (!load_atlas(argv[arg_start++], &solid_atlas)) {
        free(outline_atlas.pixels);
        return 1;
    }
    if ((argc == 1 && atlas_paths[2][0] != '\0' &&
         !load_atlas(atlas_paths[2], &rounded_atlas)) ||
        (argc >= 4 && !load_atlas(argv[arg_start++], &rounded_atlas))) {
        free(outline_atlas.pixels);
        free(solid_atlas.pixels);
        return 1;
    }
    if ((argc == 1 && atlas_paths[3][0] != '\0' &&
         !load_atlas(atlas_paths[3], &roboto_atlas)) ||
        (argc >= 5 && !load_atlas(argv[arg_start++], &roboto_atlas))) {
        free(outline_atlas.pixels);
        free(solid_atlas.pixels);
        free(rounded_atlas.pixels);
        return 1;
    }
    if ((argc == 1 && atlas_paths[4][0] != '\0' &&
         !load_atlas(atlas_paths[4], &ubuntu_atlas)) ||
        (argc >= 6 && !load_atlas(argv[arg_start++], &ubuntu_atlas))) {
        free(outline_atlas.pixels);
        free(solid_atlas.pixels);
        free(rounded_atlas.pixels);
        free(roboto_atlas.pixels);
        return 1;
    }
    if ((argc == 1 && atlas_paths[5][0] != '\0' &&
         !load_atlas(atlas_paths[5], &nunito_atlas)) ||
        (argc >= 7 && !load_atlas(argv[arg_start++], &nunito_atlas))) {
        free(outline_atlas.pixels);
        free(solid_atlas.pixels);
        free(rounded_atlas.pixels);
        free(roboto_atlas.pixels);
        free(ubuntu_atlas.pixels);
        return 1;
    }
    if (roboto_atlas.pixels != NULL)
        available_face_count = FACE_FONT_ROBOTO + 1;
    if (ubuntu_atlas.pixels != NULL)
        available_face_count = FACE_FONT_UBUNTU + 1;
    if (nunito_atlas.pixels != NULL)
        available_face_count = FACE_FONT_NUNITO + 1;
    finalize_configuration();
    if (GrOpen() < 0) {
        fprintf(stderr, "cannot connect to Nano-X\n");
        free(outline_atlas.pixels);
        free(solid_atlas.pixels);
        free(rounded_atlas.pixels);
        free(roboto_atlas.pixels);
        free(ubuntu_atlas.pixels);
        free(nunito_atlas.pixels);
        return 1;
    }
    memset(&screen, 0, sizeof(screen));
    GrGetScreenInfo(&screen);
    fprintf(stderr, "NANOX_SCREEN=%dx%d BPP=%d\n",
            (int)screen.cols, (int)screen.rows, screen.bpp);
    window = GrNewWindowEx(
        GR_WM_PROPS_NODECORATE | GR_WM_PROPS_NOAUTOMOVE |
        GR_WM_PROPS_NOAUTORESIZE | GR_WM_PROPS_NORESIZE,
        "TomTom Outline Watchface", GR_ROOT_WINDOW_ID,
        0, 0, screen.cols, screen.rows, GR_RGB(0, 0, 0));
    gc = GrNewGC();
    font = GrCreateFontEx(GR_FONT_SYSTEM_FIXED, 12, 0, NULL);
    if (window == 0 || gc == 0 || font == 0) {
        fprintf(stderr, "Nano-X resource creation failed\n");
        GrClose();
        free(outline_atlas.pixels);
        free(solid_atlas.pixels);
        free(rounded_atlas.pixels);
        free(roboto_atlas.pixels);
        free(ubuntu_atlas.pixels);
        free(nunito_atlas.pixels);
        return 1;
    }
    signal(SIGUSR1, request_next_face);
    signal(SIGUSR2, request_info_panel);
    GrGetWindowInfo(window, &info);
    width = info.width;
    height = info.height;
    fprintf(stderr, "NANOX_WINDOW=%dx%d\n", width, height);
    frame_buffer = NULL;
    if (width != LOGICAL_W || height != LOGICAL_H)
        frame_buffer = (unsigned short *)calloc((size_t)width * height,
                                                 sizeof(*frame_buffer));
    if ((width != LOGICAL_W || height != LOGICAL_H) &&
        frame_buffer == NULL) {
        perror("calloc");
        GrDestroyWindow(window);
        GrDestroyGC(gc);
        GrClose();
        free(outline_atlas.pixels);
        free(solid_atlas.pixels);
        free(rounded_atlas.pixels);
        free(roboto_atlas.pixels);
        free(ubuntu_atlas.pixels);
        free(nunito_atlas.pixels);
        return 1;
    }
    pixmap = GrNewPixmap(width, height, NULL);
    if (pixmap == 0) {
        fprintf(stderr, "Nano-X backbuffer creation failed\n");
        free(frame_buffer);
        GrClose();
        free(outline_atlas.pixels);
        free(solid_atlas.pixels);
        free(rounded_atlas.pixels);
        free(roboto_atlas.pixels);
        free(ubuntu_atlas.pixels);
        free(nunito_atlas.pixels);
        return 1;
    }
    GrSelectEvents(window, GR_EVENT_MASK_EXPOSURE |
                   GR_EVENT_MASK_BUTTON_DOWN |
                   GR_EVENT_MASK_CLOSE_REQ);
    GrMapWindow(window);

    battery_fd = open("/dev/battery", O_RDONLY);
    if (battery_fd < 0)
        perror("cannot open /dev/battery");
    ia_init(&info_animation);

    for (;;) {
        unsigned long animation_time;
        time_t current_time;
        struct tm *local;
        int next_digits[4];
        int next_hour_only;
        int colon_on;
        int night_mode;
        int digits_changed = 0;
        int redraw_base;
        int redraw_details;
        int battery_changed = 0;
        int weather_changed = 0;
        int animation_changed;
        int animation_progress;
        int requested_face;

        animation_time = now_milliseconds();
        ia_tick(&info_animation, animation_time);
        GrGetNextEventTimeout(
            &event,
            ia_active(&info_animation, animation_time) ?
                33L : FRAME_TIMEOUT_MS);
        if (event.type == GR_EVENT_TYPE_CLOSE_REQ)
            break;
        current_time = time(NULL);
        if (current_time != last_face_selection_poll) {
            last_face_selection_poll = current_time;
            requested_face = read_requested_face();
            if (requested_face >= 0)
                current_face = requested_face;
        }
        if (current_time - last_weather_poll >= 15) {
            last_weather_poll = current_time;
            weather_changed = read_device_weather(&weather_display);
        }
        if (info_panel_requested) {
            info_panel_requested = 0;
            animation_time = now_milliseconds();
            ia_tap(&info_animation, animation_time);
        } else if (event.type == GR_EVENT_TYPE_BUTTON_DOWN) {
            animation_time = now_milliseconds();
            ia_request(&info_animation, 0, animation_time);
            advance_face();
        } else if (face_change_requested) {
            face_change_requested = 0;
            advance_face();
        }
        animation_time = now_milliseconds();
        ia_tick(&info_animation, animation_time);
        animation_progress = ia_progress(&info_animation, animation_time);
        animation_changed = animation_progress != last_animation_progress;
        local = localtime(&current_time);
        if (local == NULL)
            continue;
        read_digits(local, next_digits);
        next_hour_only = local->tm_min == 0;
        if (!have_time) {
            for (i = 0; i < 4; ++i) {
                digits[i] = next_digits[i];
            }
            have_time = 1;
            digits_changed = 1;
        } else {
            for (i = 0; i < 4; ++i) {
                if (digits[i] != next_digits[i])
                    digits_changed = 1;
            }
            if (last_hour_only != next_hour_only)
                digits_changed = 1;
            if (digits_changed) {
                memcpy(digits, next_digits, sizeof(digits));
            }
        }
        night_mode = local->tm_hour >= 22 || local->tm_hour < 7;
        if (battery_fd >= 0 && current_time - last_battery_check >= 60) {
            BATTERY_STATUS status;

            last_battery_check = current_time;
            if (ioctl(battery_fd, IOR_BATTERY_STATUS, &status) < 0) {
                perror("cannot read /dev/battery");
                close(battery_fd);
                battery_fd = -1;
                battery_changed = battery_level >= 0;
                battery_level = -1;
                battery_charging = 0;
            } else {
                int level = battery_level_from_voltage(
                    status.u16BatteryVoltage);
                int charging =
                    status.u8ChargeStatus == CHARGE_STATE_CHARGING;

                battery_changed = level != battery_level ||
                    charging != battery_charging;
                battery_level = level;
                battery_charging = charging;
            }
        }
        colon_on = (local->tm_sec % 2) == 0;
        redraw_base = event.type == GR_EVENT_TYPE_EXPOSURE ||
                      digits_changed ||
                      current_face != last_face ||
                      local->tm_mday != last_day ||
                      local->tm_mon != last_month ||
                      local->tm_year != last_year ||
                      night_mode != last_night ||
                      battery_changed ||
                      weather_changed ||
                      animation_changed;
        redraw_details = redraw_base ||
                         local->tm_sec != last_second ||
                         colon_on != last_colon;

        if (redraw_base) {
            draw_frame(&outline_atlas, &solid_atlas, &rounded_atlas,
                       &roboto_atlas, &ubuntu_atlas, &nunito_atlas,
                       window, pixmap, gc, font, frame_buffer,
                       width, height, digits, local,
                       battery_level, battery_charging,
                       current_face, animation_progress);
            last_day = local->tm_mday;
            last_month = local->tm_mon;
            last_year = local->tm_year;
            last_night = night_mode;
            last_hour_only = next_hour_only;
            last_face = current_face;
            last_animation_progress = animation_progress;
        }
        if (redraw_details) {
            draw_live_details(window, pixmap, gc, font, local->tm_sec,
                              colon_on,
                              local->tm_min != 0 && !config_stacked &&
                                  !is_font_face(current_face),
                              night_mode, current_face, animation_progress);
            last_second = local->tm_sec;
            last_colon = colon_on;
        }
    }

    GrDestroyWindow(pixmap);
    GrDestroyWindow(window);
    GrDestroyGC(gc);
    GrClose();
    if (battery_fd >= 0)
        close(battery_fd);
    free(frame_buffer);
    free(outline_atlas.pixels);
    free(solid_atlas.pixels);
    free(rounded_atlas.pixels);
    free(roboto_atlas.pixels);
    free(ubuntu_atlas.pixels);
    free(nunito_atlas.pixels);
    return 0;
}
