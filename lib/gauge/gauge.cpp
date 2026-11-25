#include "gauge.h"

int gaugeFillWidth(float value, float lo, float hi, int width) {
    if (hi <= lo || width <= 0) return 0;
    float frac = (value - lo) / (hi - lo);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return (int)(frac * width);
}
