# Google Maps attribution logo

`GoogleMaps_Logo_WithLightOutline_1x.png` is the official light-outline
Google Maps attribution logo downloaded from Google's [Maps Platform
attribution assets](https://developers.google.com/maps/documentation/images/Google_Maps_Attribution_Assets.zip).
It is used unchanged over the dark weather card; do not recolor, crop, or
otherwise modify the logo artwork.

The device renderer uses a generated C89 RGB565 sprite header:

```sh
python3 watchface-research/preview/convert_icon.py \
  watchface-research/preview/assets/google-maps-attribution/GoogleMaps_Logo_WithLightOutline_1x.png \
  watchface-research/preview/assets/google-maps-attribution/google_maps_logo.h \
  --symbol google_maps_logo --max-dimension 128
```

The source logo is 105x22 pixels. The font-face weather card renders it at
77x16 pixels in the bottom-left corner, preserving its aspect ratio while
keeping the attribution small. This preserves the provider attribution
without a drawn text label.
See Google's [Weather API attribution policy](https://developers.google.com/maps/documentation/weather/policies).
