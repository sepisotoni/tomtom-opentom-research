// 320x240 pixel-art canvas with bounded undo/redo (port of studio/core/canvas.py).
#pragma once
#include <deque>
#include <string>

#include "image.h"

namespace tt {

class CanvasModel {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 240;
    static constexpr size_t kMaxHistory = 30;

    explicit CanvasModel(const std::string& bg_hex = "#000000");

    const Image& image() const { return image_; }
    Image& mutable_image() { return image_; }
    void set_image(const Image& img);  // replaces the bitmap (must be 320x240)

    void push_history();
    bool undo();
    bool redo();
    void clear(const std::string& fill_hex = "#000000");
    void draw_pixel(int x, int y, const std::string& hex, int size = 1);
    void flood_fill(int x, int y, const std::string& hex);
    std::string get_pixel_color(int x, int y) const;

    // mode: "contain" | "cover" | "stretch" | anything else = custom position.
    bool import_background(const RgbaImage& src, const std::string& mode, int pos_x, int pos_y, std::string& error);

private:
    std::string bg_hex_;
    Image image_;
    std::deque<Image> history_;
    std::vector<Image> redo_;
};

}  // namespace tt
