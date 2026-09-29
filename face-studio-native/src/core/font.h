// Embedded 5x7 bitmap font used for deterministic previews on every platform.
// Text is scaled to approximate a requested pixel font size, so previews are an
// approximation of the device fonts, not a pixel-exact reproduction.
#pragma once
#include <string>

#include "image.h"

namespace tt {

int text_width(const std::string& text, int font_size);
int text_height(int font_size);
// (x, y) is the top-left corner of the text box (like PIL's draw.text anchor).
void draw_text(Image& img, int x, int y, const std::string& text, Rgb color, int font_size, bool bold = true);

}  // namespace tt
