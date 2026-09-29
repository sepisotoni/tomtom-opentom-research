#include "canvas.h"

#include <algorithm>
#include <cmath>

namespace tt {

CanvasModel::CanvasModel(const std::string& bg_hex)
    : bg_hex_(bg_hex), image_(kWidth, kHeight, hex_to_rgb(bg_hex, Rgb{0, 0, 0})) {
    push_history();
}

void CanvasModel::set_image(const Image& img) {
    if (img.w == kWidth && img.h == kHeight) image_ = img;
}

void CanvasModel::push_history() {
    history_.push_back(image_);
    if (history_.size() > kMaxHistory) history_.pop_front();
    redo_.clear();
}

bool CanvasModel::undo() {
    if (history_.size() > 1) {
        redo_.push_back(image_);
        history_.pop_back();
        image_ = history_.back();
        return true;
    }
    return false;
}

bool CanvasModel::redo() {
    if (redo_.empty()) return false;
    Image item = std::move(redo_.back());
    redo_.pop_back();
    history_.push_back(item);
    image_ = std::move(item);
    return true;
}

void CanvasModel::clear(const std::string& fill_hex) {
    push_history();
    image_ = Image(kWidth, kHeight, hex_to_rgb(fill_hex, Rgb{0, 0, 0}));
}

void CanvasModel::draw_pixel(int x, int y, const std::string& hex, int size) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
    int half = size / 2;
    image_.fill_rect(std::max(0, x - half), std::max(0, y - half), std::min(kWidth - 1, x + half),
                     std::min(kHeight - 1, y + half), hex_to_rgb(hex, Rgb{0, 0, 0}));
}

void CanvasModel::flood_fill(int sx, int sy, const std::string& hex) {
    if (sx < 0 || sx >= kWidth || sy < 0 || sy >= kHeight) return;
    Rgb target = image_.get(sx, sy);
    Rgb fill = hex_to_rgb(hex, Rgb{0, 0, 0});
    if (target == fill) return;
    push_history();
    std::vector<uint8_t> visited(static_cast<size_t>(kWidth) * kHeight, 0);
    std::vector<std::pair<int, int>> stack;
    stack.emplace_back(sx, sy);
    while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();
        size_t idx = static_cast<size_t>(y) * kWidth + x;
        if (visited[idx]) continue;
        visited[idx] = 1;
        if (image_.get(x, y) != target) continue;
        image_.set(x, y, fill);
        if (x + 1 < kWidth) stack.emplace_back(x + 1, y);
        if (x > 0) stack.emplace_back(x - 1, y);
        if (y + 1 < kHeight) stack.emplace_back(x, y + 1);
        if (y > 0) stack.emplace_back(x, y - 1);
    }
}

std::string CanvasModel::get_pixel_color(int x, int y) const {
    if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) return rgb_to_hex(image_.get(x, y));
    return "#000000";
}

bool CanvasModel::import_background(const RgbaImage& src, const std::string& mode, int pos_x, int pos_y,
                                    std::string& error) {
    if (src.w <= 0 || src.h <= 0 || src.px.size() != static_cast<size_t>(src.w) * src.h * 4) {
        error = "Image has no pixels.";
        return false;
    }
    Image canvas(kWidth, kHeight, hex_to_rgb(bg_hex_, Rgb{0, 0, 0}));
    const double sw = src.w, sh = src.h;
    auto place_scaled = [&](int nw, int nh, int ox, int oy) {
        if (nw <= 0 || nh <= 0) {
            error = "Image aspect ratio is too extreme to fit the canvas.";
            return false;
        }
        paste_over(canvas, resize_lanczos(src, nw, nh), ox, oy);
        return true;
    };
    auto floor_half = [](int v) { return v >= 0 ? v / 2 : -((1 - v) / 2); };  // Python v // 2
    bool ok = true;
    if (mode == "stretch") {
        ok = place_scaled(kWidth, kHeight, 0, 0);
    } else if (mode == "contain" || mode == "cover") {
        double ratio = mode == "contain" ? std::min(kWidth / sw, kHeight / sh) : std::max(kWidth / sw, kHeight / sh);
        int nw = static_cast<int>(sw * ratio), nh = static_cast<int>(sh * ratio);
        ok = place_scaled(nw, nh, floor_half(kWidth - nw), floor_half(kHeight - nh));
    } else {  // custom position, unscaled
        PremulImage p = premultiply(src);
        paste_over(canvas, p, pos_x, pos_y);
    }
    if (!ok) return false;  // nothing was modified
    push_history();          // snapshot the pre-import bitmap, like the reference
    image_ = std::move(canvas);
    return true;
}

}  // namespace tt
