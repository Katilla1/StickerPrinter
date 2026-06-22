#ifndef UI_H
#define UI_H

#include "LGFX_Config.h"
#include "ble_printer.h"
#include "print_logic.h"
#include "wifi_sender.h"

#define SCREEN_W   320
#define SCREEN_H   480

// High-Res Canvas (2x Zoom)
#define HR_W 640
#define HR_H 560
#define HR_BYTES ((HR_W * HR_H) / 8)
#define COLOR_BYTES (((SCREEN_W * CANVAS_H) + 1) / 2)

#define TOPBAR_Y     0
#define TOPBAR_H    38
#define CANVAS_Y    38
#define CANVAS_H   336
#define TOOLBAR_Y  374
#define TOOLBAR_H   44
#define BOTBAR_Y   418
#define BOTBAR_H    62

enum Tool { TOOL_PEN=0, TOOL_BRUSH, TOOL_INK, TOOL_ERASER };
enum OutputMode { OUTPUT_PRINTER=0, OUTPUT_EMAIL };

class UI {
public:
    void begin();
    void update();
    void clearCanvas();
    void printAndSend();
    void uploadAndSend();
    void calibrate();
    void toggleTheme();
    void toggleZoom();

private:
    uint16_t getThemeFG();
    uint16_t getThemeBar();
    uint16_t getViewW() const;
    uint16_t getViewH() const;
    int16_t getViewX() const;
    int16_t getViewY() const;
    void clampView();

    StickerPad_LGFX tft;
    LGFX_Sprite canvas = LGFX_Sprite(&tft);
    
    // High-Res Bitmaps (1-bit)
    uint8_t* hrBuffer = nullptr;
    uint8_t* colorBuffer = nullptr;
    uint32_t* changeLog = nullptr;
    static const uint16_t MAX_COMMANDS = 64;
    static const uint16_t MAX_CHANGES = 8000;
    uint16_t commandStart[MAX_COMMANDS] = {0};
    uint16_t commandLen[MAX_COMMANDS] = {0};
    uint16_t commandCount = 0;
    uint16_t historyPos = 0;
    uint16_t changeCount = 0;
    uint16_t activeCommandStart = 0;
    bool recordingCommand = false;
    bool commandOverflow = false;
    
    uint16_t calData[8];
    bool isCalibrated = false;
    bool darkMode = false;
    uint8_t zoomLevel = 0;
    int16_t viewX = 0;
    int16_t viewY = 0;
    
    // State
    uint8_t  curSize   = 0;
    uint8_t  curColor  = 0;
    Tool     curTool   = TOOL_PEN;
    OutputMode outputMode = OUTPUT_PRINTER;
    bool     smoothOn  = true;
    bool     panMode   = false;
    bool     isDrawing = false;
    bool     isPanning = false;
    bool     showHelpScreen = false;
    bool     showSizeMenu = false;
    bool     showColorMenu = false;
    uint32_t lastUITap = 0;
    uint32_t lastRelayPoll = 0;
    int16_t  prevX = -1, prevY = -1;
    int16_t  panLastX = -1, panLastY = -1;

    // Smoothing
    float smoothX = 0, smoothY = 0;
    const float ALPHA = 0.35f;
    static const uint8_t HIST = 4;
    float hx[HIST] = {0}, hy[HIST] = {0};
    uint8_t hLen = 0;

    void drawUI();
    void drawTopBar();
    void drawToolbar();
    void drawBottomBar();
    void drawSizeMenu();
    void drawColorMenu();
    void drawHelpScreen();
    void drawStatus(const char* message, uint16_t bg);
    void handleUITap(int16_t sx, int16_t sy);
    
    bool getPixelHR(int16_t x, int16_t y) const;
    void setPixelHR(int16_t x, int16_t y, bool black);
    uint8_t getColorPixel(int16_t x, int16_t y) const;
    void setColorPixel(int16_t x, int16_t y, uint8_t colorIndex);
    void setColorPixelRaw(uint32_t idx, uint8_t colorIndex);
    void clearColorBuffer();
    void drawColorOverlay();
    bool hasColorOutput() const;
    void screenToHR(int16_t sx, int16_t sy, int16_t& hx, int16_t& hy) const;
    uint8_t getScreenRadius(uint8_t hrRad) const;
    void drawSegment(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
    void catmullRomDraw(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float p3x, float p3y);
    void resetStroke(float x, float y);
    void addStrokePoint(float x, float y);
    void finishStroke();
    void startPan(int16_t sx, int16_t sy);
    void updatePan(int16_t sx, int16_t sy);
    void finishPan();
    void syncCanvasFromHR();
    void beginUndoCommand();
    void endUndoCommand();
    void recordPixelChange(uint32_t idx, bool oldValue, bool newValue);
    void setPixelHRRaw(uint32_t idx, bool black);
    void applyCommand(uint16_t commandIndex, bool redoCommand);
    void undo();
    void redo();
    void zoomIn();
    void zoomOut();
    void toggleOutputMode();
    void recordColorChange(uint32_t idx, uint8_t oldColor, uint8_t newColor);
    void pollRelayQueue();
};

#endif
