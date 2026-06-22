#include "ui.h"
#include <Preferences.h>

const uint8_t SIZES[] = { 2, 4, 7, 11 };
const uint8_t SIZE_COUNT = sizeof(SIZES) / sizeof(SIZES[0]);
const uint8_t S_WHITE = 0;
const uint8_t S_BLACK = 1;

// Theme Colors
#define C_WHITE  0xFFFF
#define C_BLACK  0x0000
#define C_ACCENT 0xE40E
#define C_L_GRAY 0xEF7D
#define C_D_GRAY 0x2947
#define C_NAVY   0x1596
#define C_MID    0x7BEF
#define C_PANEL  0xF7BE

const uint16_t PALETTE[] = { C_BLACK, C_ACCENT, 0x24EA, 0x159F, 0xFD20, 0xA43F };
const uint16_t SEND_PALETTE[] = { C_WHITE, C_BLACK, C_ACCENT, 0x24EA, 0x159F, 0xFD20, 0xA43F };
const uint8_t PALETTE_COUNT = sizeof(PALETTE) / sizeof(PALETTE[0]);
const uint32_t CHANGE_IDX_MASK = 0x7FFFF;
const uint32_t CHANGE_OLD_MASK = 1UL << 19;
const uint32_t CHANGE_NEW_MASK = 1UL << 20;
const uint32_t CHANGE_OLD_COLOR_SHIFT = 21;
const uint32_t CHANGE_NEW_COLOR_SHIFT = 25;
const uint32_t CHANGE_COLOR_MASK = 0x0F;
const uint32_t CHANGE_COLOR_FLAG = 1UL << 29;

uint32_t packChange(uint32_t idx, bool oldValue, bool newValue, bool isColor, uint8_t oldColor = 0, uint8_t newColor = 0) {
    return (idx & CHANGE_IDX_MASK)
        | ((uint32_t)oldValue << 19)
        | ((uint32_t)newValue << 20)
        | ((uint32_t)(oldColor & CHANGE_COLOR_MASK) << CHANGE_OLD_COLOR_SHIFT)
        | ((uint32_t)(newColor & CHANGE_COLOR_MASK) << CHANGE_NEW_COLOR_SHIFT)
        | (isColor ? CHANGE_COLOR_FLAG : 0);
}

uint32_t unpackIdx(uint32_t packed) { return packed & CHANGE_IDX_MASK; }
bool unpackOldValue(uint32_t packed) { return (packed & CHANGE_OLD_MASK) != 0; }
bool unpackNewValue(uint32_t packed) { return (packed & CHANGE_NEW_MASK) != 0; }
bool unpackIsColor(uint32_t packed) { return (packed & CHANGE_COLOR_FLAG) != 0; }
uint8_t unpackOldColor(uint32_t packed) { return (packed >> CHANGE_OLD_COLOR_SHIFT) & CHANGE_COLOR_MASK; }
uint8_t unpackNewColor(uint32_t packed) { return (packed >> CHANGE_NEW_COLOR_SHIFT) & CHANGE_COLOR_MASK; }

uint16_t UI::getThemeFG() { return darkMode ? C_WHITE : C_BLACK; }
uint16_t UI::getThemeBar() { return darkMode ? C_NAVY : C_L_GRAY; }
uint16_t UI::getViewW() const { return HR_W >> zoomLevel; }
uint16_t UI::getViewH() const { return HR_H >> zoomLevel; }
int16_t UI::getViewX() const { return viewX; }
int16_t UI::getViewY() const { return viewY; }

void UI::clampView() {
    const int16_t maxX = HR_W - getViewW();
    const int16_t maxY = HR_H - getViewH();
    if (viewX < 0) viewX = 0;
    if (viewY < 0) viewY = 0;
    if (viewX > maxX) viewX = maxX;
    if (viewY > maxY) viewY = maxY;
}

void UI::begin() {
    tft.init();
    tft.setRotation(0);
    tft.setBrightness(180);
    tft.fillScreen(C_WHITE);

    // Canvas View Sprite. 1-bit storage keeps enough heap for undo/redo history.
    canvas.setColorDepth(1);
    canvas.createSprite(SCREEN_W, CANVAS_H);
    canvas.setPaletteColor(S_WHITE, C_WHITE);
    canvas.setPaletteColor(S_BLACK, C_BLACK);
    canvas.fillSprite(S_WHITE);

    // High-Res 1-bit Buffer (640x560)
    hrBuffer = (uint8_t*)malloc(HR_BYTES);
    if (!hrBuffer) {
        tft.fillScreen(C_BLACK);
        tft.setTextColor(C_WHITE);
        tft.setTextSize(2);
        tft.drawCentreString("CANVAS MEMORY FAILED", SCREEN_W / 2, SCREEN_H / 2 - 12);
        return;
    }
    memset(hrBuffer, 0, HR_BYTES);

    changeLog = (uint32_t*)malloc(sizeof(uint32_t) * MAX_CHANGES);
    if (!changeLog) {
        Serial.println("Undo history disabled: change log allocation failed");
    }

    colorBuffer = (uint8_t*)malloc(COLOR_BYTES);
    if (colorBuffer) {
        memset(colorBuffer, 0, COLOR_BYTES);
    } else {
        Serial.println("Color output disabled: color buffer allocation failed");
    }

    Preferences prefs;
    prefs.begin("inkpod", false);
    if (prefs.getBytes("cal", calData, 16) == 16) {
        tft.setTouchCalibrate(calData);
        isCalibrated = true;
    } else {
        calibrate();
    }
    darkMode = prefs.getBool("dark", false);
    prefs.end();

    viewX = 0;
    viewY = 0;
    clampView();
    drawUI();
}

void UI::calibrate() {
    tft.fillScreen(C_BLACK);
    tft.setTextColor(C_WHITE);
    tft.setTextSize(2);
    tft.drawCentreString("TOUCH THE CORNERS", 160, 200);
    tft.calibrateTouch(calData, C_WHITE, C_BLACK, 20);
    Preferences prefs;
    prefs.begin("inkpod", false);
    prefs.putBytes("cal", calData, 16);
    prefs.end();
    isCalibrated = true;
    drawUI();
}

void UI::toggleTheme() {
    darkMode = !darkMode;
    Preferences prefs;
    prefs.begin("inkpod", false);
    prefs.putBool("dark", darkMode);
    prefs.end();
    drawUI();
}

void UI::toggleZoom() {
    if (zoomLevel == 0) zoomIn();
    else zoomOut();
}

void UI::syncCanvasFromHR() {
    canvas.fillSprite(S_WHITE);
    const uint16_t viewW = getViewW();
    const uint16_t viewH = getViewH();
    const int16_t viewX = getViewX();
    const int16_t viewY = getViewY();
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            int hrX = viewX + ((int32_t)x * viewW) / SCREEN_W;
            int hrY = viewY + ((int32_t)y * viewH) / CANVAS_H;
            if (getPixelHR(hrX, hrY)) {
                canvas.drawPixel(x, y, S_BLACK);
            }
        }
    }
}

void UI::drawUI() {
    tft.fillScreen(darkMode ? C_D_GRAY : C_WHITE);
    drawTopBar();
    drawToolbar();
    drawBottomBar();
    canvas.pushSprite(0, CANVAS_Y);
    drawColorOverlay();
    if (showSizeMenu) drawSizeMenu();
    if (showColorMenu) drawColorMenu();
    if (showHelpScreen) drawHelpScreen();
}

void UI::drawTopBar() {
    tft.fillRect(0, TOPBAR_Y, SCREEN_W, TOPBAR_H, getThemeBar());
    tft.setTextColor(darkMode ? 0xFDAF : C_BLACK);
    tft.setTextSize(1);
    tft.setCursor(6, TOPBAR_Y + 9);
    tft.print(outputMode == OUTPUT_PRINTER ? "PRINT" : "EMAIL");
    tft.setCursor(6, TOPBAR_Y + 22);
    tft.print(panMode ? "PAN" : (zoomLevel == 0 ? "FULL" : (zoomLevel == 1 ? "2X" : "4X")));
    tft.drawCentreString("?", 50, TOPBAR_Y + 14);

    const char* labels[] = {
        outputMode == OUTPUT_PRINTER ? "P" : "E",
        "U",
        "R",
        "-",
        "+",
        "PAN",
        smoothOn ? "S" : "J",
        darkMode ? "D" : "L"
    };
    const uint16_t active[] = {
        (uint16_t)(outputMode == OUTPUT_EMAIL ? C_ACCENT : C_D_GRAY),
        (uint16_t)(historyPos ? C_D_GRAY : C_MID),
        (uint16_t)(historyPos < commandCount ? C_D_GRAY : C_MID),
        (uint16_t)(zoomLevel ? C_D_GRAY : C_MID),
        (uint16_t)(zoomLevel < 2 ? C_D_GRAY : C_MID),
        (uint16_t)((panMode && zoomLevel > 0) ? C_ACCENT : (zoomLevel > 0 ? C_D_GRAY : C_MID)),
        (uint16_t)(smoothOn ? C_ACCENT : C_D_GRAY),
        (uint16_t)(darkMode ? C_ACCENT : C_D_GRAY)
    };
    for (int i = 0; i < 8; i++) {
        int x = 60 + i * 32;
        tft.fillRoundRect(x, TOPBAR_Y + 5, 30, 28, 4, active[i]);
        tft.setTextColor(C_WHITE);
        tft.drawCentreString(labels[i], x + 15, TOPBAR_Y + 13);
    }
}

void UI::drawToolbar() {
    tft.fillRect(0, TOOLBAR_Y, SCREEN_W, TOOLBAR_H, darkMode ? 0x18C3 : C_PANEL);
    const char* labels[] = {"PEN","BRUSH","INK","ERASE"};
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (curTool == i) ? C_ACCENT : (darkMode ? C_D_GRAY : C_WHITE);
        int x = 4 + i * 48;
        tft.fillRoundRect(x, TOOLBAR_Y + 6, 45, 32, 6, bg);
        tft.setTextColor((curTool == i) ? C_WHITE : (darkMode ? 0xFDAF : C_BLACK));
        tft.setTextSize(1);
        tft.drawCentreString(labels[i], x + 22, TOOLBAR_Y + 16);
    }

    uint16_t sizeBg = showSizeMenu ? C_ACCENT : (darkMode ? C_D_GRAY : C_WHITE);
    tft.fillRoundRect(200, TOOLBAR_Y + 6, 56, 32, 6, sizeBg);
    tft.setTextColor(showSizeMenu ? C_WHITE : (darkMode ? 0xFDAF : C_BLACK));
    tft.drawCentreString("SIZE", 228, TOOLBAR_Y + 10);
    tft.fillCircle(228, TOOLBAR_Y + 29, 2 + curSize * 2, showSizeMenu ? C_WHITE : C_BLACK);

    uint16_t colorBg = showColorMenu ? C_ACCENT : (darkMode ? C_D_GRAY : C_WHITE);
    tft.fillRoundRect(260, TOOLBAR_Y + 6, 56, 32, 6, colorBg);
    tft.fillCircle(288, TOOLBAR_Y + 22, 8, PALETTE[curColor]);
    tft.drawCircle(288, TOOLBAR_Y + 22, 9, showColorMenu ? C_WHITE : C_BLACK);
}

void UI::drawBottomBar() {
    tft.fillRect(0, BOTBAR_Y, SCREEN_W, BOTBAR_H, getThemeBar());
    tft.fillRoundRect(10, BOTBAR_Y+8, 88, 44, 8, C_D_GRAY);
    tft.setTextColor(C_WHITE); tft.setTextSize(1); tft.drawCentreString("CLEAR", 54, BOTBAR_Y + 22);

    if (outputMode == OUTPUT_PRINTER) {
        BLEPrinter* printer = BLEPrinter::getInstance();
        uint16_t connColor = printer->isConnected() ? 0x24EA : C_D_GRAY;
        tft.fillRoundRect(108, BOTBAR_Y+8, 88, 44, 8, connColor);
        tft.drawCentreString(printer->isConnected() ? "READY" : "CONN", 152, BOTBAR_Y + 22);

        tft.fillRoundRect(206, BOTBAR_Y+8, 104, 44, 8, C_ACCENT);
        tft.drawCentreString("PRINT", 258, BOTBAR_Y + 22);
    } else {
        uint16_t wifiColor = WiFiSender::isConnected() ? 0x24EA : C_D_GRAY;
        tft.fillRoundRect(108, BOTBAR_Y+8, 88, 44, 8, wifiColor);
        tft.drawCentreString(WiFiSender::isConnected() ? "WIFI OK" : "WIFI", 152, BOTBAR_Y + 22);

        tft.fillRoundRect(206, BOTBAR_Y+8, 104, 44, 8, C_ACCENT);
        tft.drawCentreString("SEND", 258, BOTBAR_Y + 22);
    }
}

void UI::drawStatus(const char* message, uint16_t bg) {
    tft.fillRoundRect(36, CANVAS_Y + 142, 248, 34, 6, bg);
    tft.setTextColor(C_WHITE);
    tft.setTextSize(1);
    tft.drawCentreString(message, SCREEN_W / 2, CANVAS_Y + 154);
}

void UI::drawSizeMenu() {
    const int x = 200;
    const int y = TOOLBAR_Y - 132;
    tft.fillRoundRect(x, y, 56, 126, 6, darkMode ? C_D_GRAY : C_WHITE);
    tft.drawRoundRect(x, y, 56, 126, 6, C_MID);
    tft.setTextSize(1);
    for (int i = 0; i < SIZE_COUNT; i++) {
        int rowY = y + 6 + i * 30;
        uint16_t bg = (curSize == i) ? C_ACCENT : (darkMode ? C_NAVY : C_PANEL);
        tft.fillRoundRect(x + 6, rowY, 44, 24, 5, bg);
        tft.fillCircle(x + 28, rowY + 12, 3 + i * 3, (curSize == i) ? C_WHITE : C_BLACK);
    }
}

void UI::drawColorMenu() {
    const int x = 200;
    const int y = TOOLBAR_Y - 102;
    tft.fillRoundRect(x, y, 116, 96, 6, darkMode ? C_D_GRAY : C_WHITE);
    tft.drawRoundRect(x, y, 116, 96, 6, C_MID);
    for (int i = 0; i < PALETTE_COUNT; i++) {
        int cx = x + 22 + (i % 3) * 36;
        int cy = y + 24 + (i / 3) * 42;
        tft.fillCircle(cx, cy, 13, PALETTE[i]);
        tft.drawCircle(cx, cy, 14, (i == curColor) ? C_ACCENT : C_MID);
        if (PALETTE[i] == C_WHITE) tft.drawCircle(cx, cy, 11, C_MID);
    }
}

void UI::drawHelpScreen() {
    const uint16_t bg = darkMode ? C_NAVY : C_WHITE;
    const uint16_t fg = darkMode ? C_WHITE : C_BLACK;
    const uint16_t line = darkMode ? 0x7BEF : C_MID;

    tft.fillRect(0, 0, SCREEN_W, SCREEN_H, bg);
    tft.setTextSize(1);
    tft.setTextColor(fg);
    tft.drawCentreString("WHITE PAD BUTTONS", SCREEN_W / 2, 12);
    tft.drawFastHLine(18, 34, SCREEN_W - 36, line);

    const char* rows[] = {
        "?: this help",
        "2X/4X label: toggle PAN mode",
        "PAN: drag canvas to move view",
        "P/E: printer or email/send mode",
        "U/R: undo and redo strokes",
        "-/+: zoom canvas out or in",
        "S/J: smoothed or raw jagged input",
        "D/L: dark or light theme",
        "PEN/BRUSH/INK: black strokes",
        "ERASE: clear pixels under brush",
        "SIZE: brush diameter menu",
        "Color dot: persisted color layer",
        "CLEAR: erase the drawing",
        "CONN/READY: BLE printer link",
        "PRINT: crop and print drawing",
        "WIFI/SEND: relay upload mode"
    };

    int y = 48;
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        tft.setCursor(18, y);
        tft.print(rows[i]);
        y += 24;
    }

    tft.drawFastHLine(18, SCREEN_H - 48, SCREEN_W - 36, line);
    tft.drawCentreString("Tap anywhere to close", SCREEN_W / 2, SCREEN_H - 30);
}

bool UI::getPixelHR(int16_t x, int16_t y) const {
    if (!hrBuffer || x < 0 || x >= HR_W || y < 0 || y >= HR_H) return false;
    uint32_t idx = (y * HR_W) + x;
    return (hrBuffer[idx / 8] & (1 << (7 - (idx % 8)))) != 0;
}

void UI::setPixelHR(int16_t x, int16_t y, bool black) {
    if (!hrBuffer || x < 0 || x >= HR_W || y < 0 || y >= HR_H) return;
    uint32_t idx = (y * HR_W) + x;
    bool oldValue = (hrBuffer[idx / 8] & (1 << (7 - (idx % 8)))) != 0;
    if (oldValue == black) return;
    recordPixelChange(idx, oldValue, black);
    setPixelHRRaw(idx, black);
}

void UI::setPixelHRRaw(uint32_t idx, bool black) {
    if (!hrBuffer || idx >= (uint32_t)HR_W * HR_H) return;
    if (black) hrBuffer[idx / 8] |= (1 << (7 - (idx % 8)));
    else hrBuffer[idx / 8] &= ~(1 << (7 - (idx % 8)));
}

uint8_t UI::getColorPixel(int16_t x, int16_t y) const {
    if (!colorBuffer || x < 0 || x >= SCREEN_W || y < 0 || y >= CANVAS_H) return 0;
    uint32_t idx = (uint32_t)y * SCREEN_W + x;
    uint8_t packed = colorBuffer[idx / 2];
    return (idx & 1) ? (packed & 0x0F) : (packed >> 4);
}

void UI::setColorPixel(int16_t x, int16_t y, uint8_t colorIndex) {
    if (!colorBuffer || x < 0 || x >= SCREEN_W || y < 0 || y >= CANVAS_H) return;
    if (colorIndex > PALETTE_COUNT) colorIndex = 0;
    uint32_t idx = (uint32_t)y * SCREEN_W + x;
    uint8_t oldColor = getColorPixel(x, y);
    if (oldColor == colorIndex) return;
    recordColorChange(idx, oldColor, colorIndex);
    setColorPixelRaw(idx, colorIndex);
}

void UI::setColorPixelRaw(uint32_t idx, uint8_t colorIndex) {
    if (!colorBuffer || idx >= (uint32_t)SCREEN_W * CANVAS_H) return;
    uint8_t& packed = colorBuffer[idx / 2];
    if (idx & 1) packed = (packed & 0xF0) | (colorIndex & 0x0F);
    else packed = (packed & 0x0F) | (colorIndex << 4);
}

void UI::clearColorBuffer() {
    if (!colorBuffer) return;
    if (recordingCommand) {
        for (uint32_t idx = 0; idx < (uint32_t)SCREEN_W * CANVAS_H; idx++) {
            uint8_t oldColor = getColorPixel(idx % SCREEN_W, idx / SCREEN_W);
            if (oldColor != 0) {
                recordColorChange(idx, oldColor, 0);
            }
        }
    }
    memset(colorBuffer, 0, COLOR_BYTES);
}

void UI::drawColorOverlay() {
    if (!colorBuffer) return;
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            uint8_t colorIndex = getColorPixel(x, y);
            if (colorIndex > 1 && colorIndex <= PALETTE_COUNT) {
                tft.drawPixel(x, y + CANVAS_Y, PALETTE[colorIndex - 1]);
            }
        }
    }
}

bool UI::hasColorOutput() const {
    if (!colorBuffer) return false;
    for (uint32_t i = 0; i < COLOR_BYTES; i++) {
        uint8_t packed = colorBuffer[i];
        if ((packed >> 4) > 1 || (packed & 0x0F) > 1) return true;
    }
    return false;
}

void UI::screenToHR(int16_t sx, int16_t sy, int16_t& hx, int16_t& hy) const {
    hx = getViewX() + ((int32_t)sx * getViewW()) / SCREEN_W;
    hy = getViewY() + ((int32_t)sy * getViewH()) / CANVAS_H;
}

uint8_t UI::getScreenRadius(uint8_t hrRad) const {
    uint16_t viewW = getViewW();
    uint8_t r = ((uint32_t)hrRad * SCREEN_W + viewW - 1) / viewW;
    if (r < 2) return 2;
    if (r > 14) return 14;
    return r;
}

void UI::drawSegment(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    uint8_t hrRad = SIZES[curSize];
    bool isEraser = (curTool == TOOL_ERASER);
    uint16_t color = isEraser ? C_WHITE : PALETTE[curColor];
    if (curTool == TOOL_BRUSH && !isEraser) hrRad += 2;
    if (curTool == TOOL_INK && !isEraser) hrRad += 4;
    uint8_t screenRad = getScreenRadius(hrRad);
    
    int16_t dx = abs(x1-x0), sx = x0<x1?1:-1;
    int16_t dy = -abs(y1-y0), sy = y0<y1?1:-1;
    int16_t err = dx+dy, e2;
    int16_t cx = x0, cy = y0;

    for (;;) {
        int16_t hrX, hrY;
        screenToHR(cx, cy, hrX, hrY);

        // Draw to HR Buffer
        for (int i = -hrRad; i <= hrRad; i++) {
            for (int j = -hrRad; j <= hrRad; j++) {
                if (i*i + j*j <= hrRad*hrRad) setPixelHR(hrX+i, hrY+j, !isEraser);
            }
        }

        // Draw to Screen (Visible clipping)
        if (cx >= 0 && cx < SCREEN_W && cy >= 0 && cy < CANVAS_H) {
            uint8_t colorIndex = isEraser ? 0 : (curColor + 1);
            for (int i = -screenRad; i <= screenRad; i++) {
                for (int j = -screenRad; j <= screenRad; j++) {
                    if (i*i + j*j <= screenRad*screenRad) setColorPixel(cx + i, cy + j, colorIndex);
                }
            }
            canvas.fillCircle(cx, cy, screenRad, isEraser ? S_WHITE : S_BLACK);
            tft.fillCircle(cx, cy + CANVAS_Y, screenRad, color);
        }

        if (cx==x1 && cy==y1) break;
        e2 = 2*err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }
}

void UI::catmullRomDraw(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float p3x, float p3y) {
    const int STEPS = 8;
    int16_t lx = (int16_t)p1x, ly = (int16_t)p1y;
    for (int i = 1; i <= STEPS; i++) {
        float t = (float)i/STEPS; float t2=t*t; float t3=t2*t;
        float nx = 0.5f*((2*p1x)+(-p0x+p2x)*t+(2*p0x-5*p1x+4*p2x-p3x)*t2+(-p0x+3*p1x-3*p2x+p3x)*t3);
        float ny = 0.5f*((2*p1y)+(-p0y+p2y)*t+(2*p0y-5*p1y+4*p2y-p3y)*t2+(-p0y+3*p1y-3*p2y+p3y)*t3);
        drawSegment(lx, ly, (int16_t)nx, (int16_t)ny);
        lx = (int16_t)nx; ly = (int16_t)ny;
    }
}

void UI::resetStroke(float x, float y) {
    beginUndoCommand();
    isDrawing = true;
    smoothX = x;
    smoothY = y;
    prevX = (int16_t)x;
    prevY = (int16_t)y;
    hLen = 1;
    for (int i = 0; i < HIST; i++) {
        hx[i] = x;
        hy[i] = y;
    }
    drawSegment(prevX, prevY, prevX, prevY);
}

void UI::addStrokePoint(float x, float y) {
    if (smoothOn) {
        smoothX = ALPHA * x + (1.0f - ALPHA) * smoothX;
        smoothY = ALPHA * y + (1.0f - ALPHA) * smoothY;
        x = smoothX;
        y = smoothY;
    }

    for (int i = 0; i < HIST - 1; i++) {
        hx[i] = hx[i + 1];
        hy[i] = hy[i + 1];
    }
    hx[HIST - 1] = x;
    hy[HIST - 1] = y;
    if (hLen < HIST) hLen++;

    if (smoothOn && hLen >= HIST) {
        catmullRomDraw(hx[0], hy[0], hx[1], hy[1], hx[2], hy[2], hx[3], hy[3]);
        prevX = (int16_t)hx[2];
        prevY = (int16_t)hy[2];
    } else {
        drawSegment(prevX, prevY, (int16_t)x, (int16_t)y);
        prevX = (int16_t)x;
        prevY = (int16_t)y;
    }
}

void UI::finishStroke() {
    if (!isDrawing) return;
    if (smoothOn && prevX >= 0 && prevY >= 0) {
        drawSegment(prevX, prevY, (int16_t)smoothX, (int16_t)smoothY);
    }
    isDrawing = false;
    hLen = 0;
    endUndoCommand();
}

void UI::startPan(int16_t sx, int16_t sy) {
    isPanning = true;
    panLastX = sx;
    panLastY = sy;
}

void UI::updatePan(int16_t sx, int16_t sy) {
    if (!isPanning) {
        startPan(sx, sy);
        return;
    }

    int16_t dx = sx - panLastX;
    int16_t dy = sy - panLastY;
    panLastX = sx;
    panLastY = sy;

    viewX -= ((int32_t)dx * getViewW()) / SCREEN_W;
    viewY -= ((int32_t)dy * getViewH()) / CANVAS_H;
    clampView();
    syncCanvasFromHR();
    canvas.pushSprite(0, CANVAS_Y);
    drawColorOverlay();
}

void UI::finishPan() {
    isPanning = false;
    panLastX = -1;
    panLastY = -1;
}

void UI::handleUITap(int16_t sx, int16_t sy) {
    Serial.printf("Tap: %d, %d\n", sx, sy);
    if (showHelpScreen) {
        showHelpScreen = false;
        drawUI();
        return;
    }

    if (showColorMenu) {
        const int menuX = 200;
        const int menuY = TOOLBAR_Y - 102;
        if (sx >= menuX && sx < menuX + 116 && sy >= menuY && sy < menuY + 96) {
            int col = (sx - menuX) / 36;
            int row = (sy - menuY) / 42;
            int i = row * 3 + col;
            if (i >= 0 && i < PALETTE_COUNT) curColor = i;
        }
        showColorMenu = false;
        drawUI();
        return;
    }

    if (showSizeMenu) {
        const int menuX = 200;
        const int menuY = TOOLBAR_Y - 132;
        if (sx >= menuX && sx < menuX + 56 && sy >= menuY && sy < menuY + 126) {
            int row = sy - menuY - 6;
            if (row >= 0) {
                int i = row / 30;
                if (i >= 0 && i < SIZE_COUNT) curSize = i;
            }
        }
        showSizeMenu = false;
        drawUI();
        return;
    }

    if (sy < TOPBAR_H) {
        if (sx < 60) {
            showHelpScreen = true;
            showSizeMenu = false;
            showColorMenu = false;
            drawHelpScreen();
        } else {
            int i = (sx - 60) / 32;
            if (i == 0) toggleOutputMode();
            else if (i == 1) undo();
            else if (i == 2) redo();
            else if (i == 3) zoomOut();
            else if (i == 4) zoomIn();
            else if (i == 5 && zoomLevel > 0) { panMode = !panMode; finishPan(); drawTopBar(); }
            else if (i == 6) { smoothOn = !smoothOn; drawTopBar(); }
            else if (i == 7) toggleTheme();
        }
    } else if (sy >= TOOLBAR_Y && sy < BOTBAR_Y) {
        if (sx >= 260) {
            showColorMenu = true;
            showSizeMenu = false;
            drawUI();
        } else if (sx >= 200) {
            showSizeMenu = true;
            showColorMenu = false;
            drawUI();
        } else {
            int i = sx / 48;
            if (i >= 0 && i < 4) { curTool = (Tool)i; drawToolbar(); }
        }
    } else if (sy >= BOTBAR_Y) {
        if (sx < 105) clearCanvas();
        else if (sx < 205 && outputMode == OUTPUT_PRINTER) {
            drawStatus("CONNECTING...", C_D_GRAY);
            bool ok = BLEPrinter::getInstance()->connect();
            drawUI();
            if (!ok) drawStatus("PRINTER NOT FOUND", C_ACCENT);
            delay(450);
            drawUI();
        }
        else if (sx < 205) {
            if (!WiFiSender::isConfigured()) {
                drawStatus("CONFIGURE WIFI FIRST", C_ACCENT);
                delay(700);
                drawUI();
            } else {
                drawStatus("CONNECTING WIFI...", C_D_GRAY);
                bool ok = WiFiSender::connect();
                drawUI();
                if (!ok) {
                    char msg[40];
                    snprintf(msg, sizeof(msg), "WIFI FAIL: %.28s", WiFiSender::lastError());
                    drawStatus(msg, C_ACCENT);
                }
                else {
                    char msg[32];
                    snprintf(msg, sizeof(msg), "WIFI: %.24s", WiFiSender::currentSSID());
                    drawStatus(msg, 0x24EA);
                    delay(900);
                    snprintf(msg, sizeof(msg), "IP: %.24s", WiFiSender::localIP());
                    drawStatus(msg, 0x24EA);
                }
                delay(1100);
                drawUI();
            }
        }
        else if (outputMode == OUTPUT_PRINTER) printAndSend();
        else uploadAndSend();
    }
}

void UI::clearCanvas() {
    if (!hrBuffer) return;
    beginUndoCommand();
    for (uint32_t idx = 0; idx < (uint32_t)HR_W * HR_H; idx++) {
        if (hrBuffer[idx / 8] & (1 << (7 - (idx % 8)))) {
            recordPixelChange(idx, true, false);
        }
    }
    memset(hrBuffer, 0, HR_BYTES);
    clearColorBuffer();
    endUndoCommand();
    canvas.fillSprite(S_WHITE);
    drawUI();
}

void UI::update() {
    if (!hrBuffer) return;
    uint16_t x, y;
    if (tft.getTouch(&x, &y)) {
        if (showHelpScreen) {
            if (millis() - lastUITap > 220) { handleUITap(x, y); lastUITap = millis(); }
            return;
        }
        if (showSizeMenu || showColorMenu) {
            if (isDrawing) finishStroke();
            if (millis() - lastUITap > 220) { handleUITap(x, y); lastUITap = millis(); }
        } else if (y >= CANVAS_Y && y < TOOLBAR_Y) {
            float cx = x, cy = y - CANVAS_Y;
            if (panMode && zoomLevel > 0) {
                if (isDrawing) finishStroke();
                updatePan(x, y - CANVAS_Y);
            } else if (!isDrawing) {
                resetStroke(cx, cy);
            } else {
                addStrokePoint(cx, cy);
            }
        } else {
            if (isDrawing) finishStroke();
            if (isPanning) finishPan();
            if (millis() - lastUITap > 220) { handleUITap(x, y); lastUITap = millis(); }
        }
    } else {
        if (isDrawing) finishStroke();
        if (isPanning) finishPan();
    }

    if (outputMode == OUTPUT_EMAIL && !isDrawing && !isPanning && !showHelpScreen && !showSizeMenu && !showColorMenu) {
        pollRelayQueue();
    }
}

void UI::beginUndoCommand() {
    if (!changeLog || recordingCommand) return;
    if (historyPos < commandCount) {
        changeCount = (historyPos == 0) ? 0 : commandStart[historyPos - 1] + commandLen[historyPos - 1];
        commandCount = historyPos;
    }
    if (commandCount == MAX_COMMANDS) {
        uint16_t firstLen = commandLen[0];
        if (firstLen > 0 && firstLen < changeCount) {
            memmove(changeLog, changeLog + firstLen, sizeof(uint32_t) * (changeCount - firstLen));
        }
        changeCount -= firstLen;
        for (uint16_t i = 1; i < commandCount; i++) {
            commandStart[i - 1] = commandStart[i] - firstLen;
            commandLen[i - 1] = commandLen[i];
        }
        commandCount--;
        historyPos = commandCount;
    }
    activeCommandStart = changeCount;
    commandOverflow = false;
    recordingCommand = true;
}

void UI::endUndoCommand() {
    if (!recordingCommand) return;
    recordingCommand = false;
    uint16_t len = changeCount - activeCommandStart;
    if (commandOverflow || len == 0) {
        changeCount = activeCommandStart;
        commandOverflow = false;
        drawTopBar();
        return;
    }
    commandStart[commandCount] = activeCommandStart;
    commandLen[commandCount] = len;
    commandCount++;
    historyPos = commandCount;
    drawTopBar();
}

void UI::recordPixelChange(uint32_t idx, bool oldValue, bool newValue) {
    if (!recordingCommand || !changeLog || commandOverflow) return;
    if (changeCount >= MAX_CHANGES) {
        if (commandCount > 0) {
            uint16_t firstLen = commandLen[0];
            if (firstLen > 0 && firstLen < changeCount) {
                memmove(changeLog, changeLog + firstLen, sizeof(uint32_t) * (changeCount - firstLen));
            }
            changeCount -= firstLen;
            activeCommandStart -= firstLen;
            for (uint16_t i = 1; i < commandCount; i++) {
                commandStart[i - 1] = commandStart[i] - firstLen;
                commandLen[i - 1] = commandLen[i];
            }
            commandCount--;
            historyPos = commandCount;
        }
        if (changeCount >= MAX_CHANGES) {
            commandOverflow = true;
            return;
        }
    }
    changeLog[changeCount++] = packChange(idx, oldValue, newValue, false);
}

void UI::recordColorChange(uint32_t idx, uint8_t oldColor, uint8_t newColor) {
    if (!recordingCommand || !changeLog || commandOverflow) return;
    if (changeCount >= MAX_CHANGES) {
        if (commandCount > 0) {
            uint16_t firstLen = commandLen[0];
            if (firstLen > 0 && firstLen < changeCount) {
                memmove(changeLog, changeLog + firstLen, sizeof(uint32_t) * (changeCount - firstLen));
            }
            changeCount -= firstLen;
            activeCommandStart -= firstLen;
            for (uint16_t i = 1; i < commandCount; i++) {
                commandStart[i - 1] = commandStart[i] - firstLen;
                commandLen[i - 1] = commandLen[i];
            }
            commandCount--;
            historyPos = commandCount;
        }
        if (changeCount >= MAX_CHANGES) {
            commandOverflow = true;
            return;
        }
    }
    changeLog[changeCount++] = packChange(idx, oldColor != 0, newColor != 0, true, oldColor, newColor);
}

void UI::applyCommand(uint16_t commandIndex, bool redoCommand) {
    if (!changeLog || commandIndex >= commandCount) return;
    uint16_t start = commandStart[commandIndex];
    uint16_t len = commandLen[commandIndex];
    if (redoCommand) {
        for (uint16_t i = 0; i < len; i++) {
            uint32_t packed = changeLog[start + i];
            if (unpackIsColor(packed)) {
                setColorPixelRaw(unpackIdx(packed), unpackNewColor(packed));
            } else {
                setPixelHRRaw(unpackIdx(packed), unpackNewValue(packed));
            }
        }
    } else {
        for (int32_t i = len - 1; i >= 0; i--) {
            uint32_t packed = changeLog[start + i];
            if (unpackIsColor(packed)) {
                setColorPixelRaw(unpackIdx(packed), unpackOldColor(packed));
            } else {
                setPixelHRRaw(unpackIdx(packed), unpackOldValue(packed));
            }
        }
    }
}

void UI::undo() {
    if (recordingCommand) endUndoCommand();
    if (historyPos == 0) return;
    historyPos--;
    applyCommand(historyPos, false);
    syncCanvasFromHR();
    drawUI();
}

void UI::redo() {
    if (recordingCommand) endUndoCommand();
    if (historyPos >= commandCount) return;
    applyCommand(historyPos, true);
    historyPos++;
    syncCanvasFromHR();
    drawUI();
}

void UI::zoomIn() {
    if (zoomLevel >= 2) return;
    int16_t centerX = viewX + getViewW() / 2;
    int16_t centerY = viewY + getViewH() / 2;
    zoomLevel++;
    viewX = centerX - getViewW() / 2;
    viewY = centerY - getViewH() / 2;
    clampView();
    syncCanvasFromHR();
    drawUI();
}

void UI::zoomOut() {
    if (zoomLevel == 0) return;
    int16_t centerX = viewX + getViewW() / 2;
    int16_t centerY = viewY + getViewH() / 2;
    zoomLevel--;
    if (zoomLevel == 0) panMode = false;
    viewX = centerX - getViewW() / 2;
    viewY = centerY - getViewH() / 2;
    clampView();
    syncCanvasFromHR();
    drawUI();
}

void UI::toggleOutputMode() {
    outputMode = (outputMode == OUTPUT_PRINTER) ? OUTPUT_EMAIL : OUTPUT_PRINTER;
    drawUI();
}

void UI::printAndSend() {
    if (!hrBuffer) return;
    if (!BLEPrinter::getInstance()->isConnected()) {
        drawStatus("CONNECT PRINTER FIRST", C_ACCENT);
        delay(600);
        drawUI();
        return;
    }

    drawStatus("PRINTING HI-RES...", C_ACCENT);
    
    bool ok = PrintLogic::printHRCanvas(hrBuffer, HR_W, HR_H);
    
    drawUI();
    if (!ok) {
        drawStatus("PRINT FAILED", C_ACCENT);
        delay(600);
        drawUI();
    }
}

void UI::uploadAndSend() {
    if (!hrBuffer) return;
    if (!WiFiSender::isConfigured()) {
        drawStatus("CONFIGURE WIFI FIRST", C_ACCENT);
        delay(800);
        drawUI();
        return;
    }

    drawStatus("SENDING SKETCH...", C_ACCENT);
    bool ok = hasColorOutput()
        ? WiFiSender::sendColorSketch(colorBuffer, SEND_PALETTE, sizeof(SEND_PALETTE) / sizeof(SEND_PALETTE[0]), SCREEN_W, CANVAS_H)
        : WiFiSender::sendSketch(hrBuffer, HR_W, HR_H);
    drawUI();
    if (ok) {
        drawStatus("SENT TO RELAY", 0x24EA);
    } else {
        char msg[32];
        snprintf(msg, sizeof(msg), "SEND FAILED %d", WiFiSender::lastStatus());
        drawStatus(msg, C_ACCENT);
        delay(900);
        snprintf(msg, sizeof(msg), "HOST %.24s", WiFiSender::relayHost());
        drawStatus(msg, C_ACCENT);
    }
    delay(1200);
    drawUI();
}

void UI::pollRelayQueue() {
    if (millis() - lastRelayPoll < 5000) return;
    lastRelayPoll = millis();
    if (!WiFiSender::isConfigured()) return;

    if (!WiFiSender::isConnected() && !WiFiSender::connect()) {
        Serial.printf("[RELAY] wifi connect failed: %s\n", WiFiSender::lastError());
        return;
    }

    RelayJob job;
    if (!WiFiSender::fetchNextJob(job)) {
        if (WiFiSender::lastStatus() != 204) {
            Serial.printf("[RELAY] fetch failed status=%d\n", WiFiSender::lastStatus());
        }
        return;
    }

    if (!job.data || job.length < 9) {
        WiFiSender::freeJob(job);
        return;
    }

    BLEPrinter* printer = BLEPrinter::getInstance();
    if (!printer->isConnected() && !printer->connect()) {
        drawStatus("PRINTER OFFLINE", C_ACCENT);
        delay(500);
        drawUI();
        WiFiSender::freeJob(job);
        return;
    }

    if (job.format != 1 || job.width == 0 || job.height == 0) {
        WiFiSender::freeJob(job);
        return;
    }

    drawStatus("PRINTING RELAY JOB...", C_ACCENT);
    bool ok = PrintLogic::printCanvas(job.data + 9, job.width, job.height);
    WiFiSender::freeJob(job);

    drawUI();
    if (ok) {
        drawStatus("RELAY JOB PRINTED", 0x24EA);
    } else {
        drawStatus("RELAY PRINT FAILED", C_ACCENT);
    }
    delay(600);
    drawUI();
}
