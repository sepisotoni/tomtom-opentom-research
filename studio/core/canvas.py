"""
TomTom Face Studio - Pixel Art Drawing Canvas Model & Operations
==============================================================
Manages the 320x240 pixel grid, drawing tools (pencil, eraser, fill bucket, eyedropper),
zoom/grid state, and undo/redo history stack.
"""

from typing import Tuple, List, Optional
from PIL import Image, ImageDraw


class CanvasModel:
    """Manages 320x240 pixel art bitmap and history stack."""

    WIDTH = 320
    HEIGHT = 240
    MAX_HISTORY = 30

    def __init__(self, bg_color: str = "#000000"):
        self.bg_color_hex = bg_color
        self.image = Image.new("RGB", (self.WIDTH, self.HEIGHT), bg_color)
        self.history: List[Image.Image] = []
        self.redo_stack: List[Image.Image] = []
        self.push_history()

    def push_history(self):
        """Save a snapshot of the current canvas image to the undo history stack."""
        self.history.append(self.image.copy())
        if len(self.history) > self.MAX_HISTORY:
            self.history.pop(0)
        self.redo_stack.clear()

    def undo(self) -> bool:
        """Undo the last drawing action."""
        if len(self.history) > 1:
            self.redo_stack.append(self.image.copy())
            self.history.pop()
            self.image = self.history[-1].copy()
            return True
        return False

    def redo(self) -> bool:
        """Redo the last undone action."""
        if self.redo_stack:
            item = self.redo_stack.pop()
            self.history.append(item.copy())
            self.image = item.copy()
            return True
        return False

    def clear(self, fill_color: str = "#000000"):
        """Clear canvas with target color."""
        self.push_history()
        self.image = Image.new("RGB", (self.WIDTH, self.HEIGHT), fill_color)

    def draw_pixel(self, x: int, y: int, color_hex: str, size: int = 1):
        """Draw a pixel or square brush at (x, y)."""
        if x < 0 or x >= self.WIDTH or y < 0 or y >= self.HEIGHT:
            return

        draw = ImageDraw.Draw(self.image)
        r, g, b = self._hex_to_rgb(color_hex)

        half = size // 2
        x0 = max(0, x - half)
        y0 = max(0, y - half)
        x1 = min(self.WIDTH - 1, x + half)
        y1 = min(self.HEIGHT - 1, y + half)

        draw.rectangle([x0, y0, x1, y1], fill=(r, g, b))

    def flood_fill(self, start_x: int, start_y: int, fill_color_hex: str):
        """Flood fill region starting at (start_x, start_y)."""
        if start_x < 0 or start_x >= self.WIDTH or start_y < 0 or start_y >= self.HEIGHT:
            return

        target_color = self.image.getpixel((start_x, start_y))
        fill_color = self._hex_to_rgb(fill_color_hex)

        if target_color == fill_color:
            return

        self.push_history()
        pixels = self.image.load()
        queue = [(start_x, start_y)]
        visited = set()

        while queue:
            cx, cy = queue.pop(0)
            if (cx, cy) in visited:
                continue
            visited.add((cx, cy))

            if pixels[cx, cy] == target_color:
                pixels[cx, cy] = fill_color
                for nx, ny in ((cx+1, cy), (cx-1, cy), (cx, cy+1), (cx, cy-1)):
                    if 0 <= nx < self.WIDTH and 0 <= ny < self.HEIGHT:
                        if (nx, ny) not in visited and pixels[nx, ny] == target_color:
                            queue.append((nx, ny))

    def get_pixel_color(self, x: int, y: int) -> str:
        """Return hex color string of pixel at (x, y)."""
        if 0 <= x < self.WIDTH and 0 <= y < self.HEIGHT:
            r, g, b = self.image.getpixel((x, y))
            return f"#{r:02X}{g:02X}{b:02X}"
        return "#000000"

    def import_background(self, bg_img: Image.Image, mode: str = "contain", pos_x: int = 0, pos_y: int = 0):
        """Import external PNG/JPG artwork and place onto 320x240 canvas."""
        self.push_history()
        canvas_img = Image.new("RGB", (self.WIDTH, self.HEIGHT), self.bg_color_hex)

        src = bg_img.convert("RGBA")
        sw, sh = src.size

        if mode == "stretch":
            resized = src.resize((self.WIDTH, self.HEIGHT), Image.Resampling.LANCZOS)
            canvas_img.paste(resized, (0, 0), resized)
        elif mode == "contain":
            ratio = min(self.WIDTH / float(sw), self.HEIGHT / float(sh))
            nw, nh = int(sw * ratio), int(sh * ratio)
            resized = src.resize((nw, nh), Image.Resampling.LANCZOS)
            ox = (self.WIDTH - nw) // 2
            oy = (self.HEIGHT - nh) // 2
            canvas_img.paste(resized, (ox, oy), resized)
        elif mode == "cover":
            ratio = max(self.WIDTH / float(sw), self.HEIGHT / float(sh))
            nw, nh = int(sw * ratio), int(sh * ratio)
            resized = src.resize((nw, nh), Image.Resampling.LANCZOS)
            ox = (self.WIDTH - nw) // 2
            oy = (self.HEIGHT - nh) // 2
            canvas_img.paste(resized, (ox, oy), resized)
        else:  # Custom position
            canvas_img.paste(src, (pos_x, pos_y), src)

        self.image = canvas_img.convert("RGB")

    @staticmethod
    def _hex_to_rgb(hex_str: str) -> Tuple[int, int, int]:
        hex_str = hex_str.lstrip("#")
        if len(hex_str) == 6:
            return (int(hex_str[0:2], 16), int(hex_str[2:4], 16), int(hex_str[4:6], 16))
        return (0, 0, 0)
