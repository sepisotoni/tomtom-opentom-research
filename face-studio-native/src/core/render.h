// Element rendering for the editor preview and generated gallery previews.
#pragma once
#include <string>

#include "image.h"
#include "json.h"

namespace tt {

struct SimTime {
    int year = 2026, month = 1, day = 1;
    int hour = 10, minute = 8, second = 0;
    int weekday() const;  // 0 = Monday ... 6 = Sunday
};

// Draws one element (digital_time / date / text / status_icon) onto `img`.
// digital_time honours the transient "layout" key ("horizontal" or "stacked").
void render_element(Image& img, const Json& element, const SimTime& time, int frame_tick = 0);

// Canvas + every element that is visible under `state` (the editor preview).
Image compose_preview(const Image& canvas, const Json& project, const Json& state, const SimTime& time, int frame_tick);

}  // namespace tt
