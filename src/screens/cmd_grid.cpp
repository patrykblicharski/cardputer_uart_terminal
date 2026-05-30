#include "cmd_grid.h"
#include "app_state.h"
#include "display/display_ext.h"
#include "config.h"

static const char CMD_KEYS[] = "ASDFGHJKL";

bool cmdGrid_ParseResponse(const String& line) {
    if (!line.startsWith("!CMD:") || !line.endsWith("!")) return false;
    String payload = line.substring(5, line.length() - 1);

    g_session.dynamicCmdCount = 0;
    int start = 0;
    while (g_session.dynamicCmdCount < 9) {
        int comma = payload.indexOf(',', start);
        String token = (comma >= 0) ? payload.substring(start, comma)
                                    : payload.substring(start);
        token.trim();
        if (token.length()) {
            g_session.dynamicCmds[g_session.dynamicCmdCount++] = token;
        }
        if (comma < 0) break;
        start = comma + 1;
    }
    return g_session.dynamicCmdCount > 0;
}

void cmdGrid_Draw() {
    constexpr int COLS = 5;
    constexpr int ROWS = 2;
    int cellW = EXT_W / COLS;
    int cellH = CMD_GRID_H / ROWS;

    extDisplay.fillRect(0, CMD_GRID_Y, EXT_W, CMD_GRID_H, TFT_DARKGREY);
    extDisplay.drawFastHLine(0, CMD_GRID_Y, EXT_W, TFT_BLACK);
    extDisplay.setTextSize(1);

    for (int i = 0; i < g_session.dynamicCmdCount && i < COLS * ROWS; i++) {
        int col = i % COLS;
        int row = i / COLS;
        int x   = col * cellW + 2;
        int y   = CMD_GRID_Y + row * cellH + 3;

        extDisplay.setTextColor(C_AMBER, TFT_DARKGREY);
        char kbuf[3] = { CMD_KEYS[i], ':', '\0' };
        extDisplay.setCursor(x, y);
        extDisplay.print(kbuf);

        extDisplay.setTextColor(TFT_WHITE, TFT_DARKGREY);
        String lbl = g_session.dynamicCmds[i];
        if (lbl.length() > 7) lbl = lbl.substring(0, 7);
        extDisplay.print(lbl);
    }
}
