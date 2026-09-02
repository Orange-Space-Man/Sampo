#pragma once

namespace overlay {
    void draw();
    void drawNoitaText(float x, float y, float scale, unsigned int color, const char* text, bool rainbow);
    float noitaTextWidth(float scale, const char* text);
}
