#ifndef SHARED_H
#define SHARED_H

// ========== UI COLOR PALETTE ==========
// Terminal/CRT green theme, matching the boot splash artwork.
const uint16_t UI_MAGENTA = 0x3FE8;  // Bright green - Accents
const uint16_t UI_HOTPINK = 0x1EEB;  // Mid green - Accents
const uint16_t UI_BRIGHT = 0x1EEB;   // Mid green - Highlights
const uint16_t UI_VIOLET = 0x04A8;   // Darker green - Secondary accent
const uint16_t UI_DARK = 0x0102;     // Near-black green - Dark backgrounds
const uint16_t UI_CYAN = 0x174D;     // Terminal green - body text (most-used color)
const uint16_t UI_BLACK = 0x0000;    // #000000 - Pure black
const uint16_t UI_GUNMETAL = 0x1143; // Dark green-gray - borders/dim UI
const uint16_t UI_GREEN = 0x04A8;    // Darker green
// Amber CRT accent - deliberately outside the green family so the
// currently-selected menu/list item reads clearly against both the black
// background and the green body text (UI_CYAN) around it.
const uint16_t UI_AMBER = 0xFD80;    // #FFB000 amber - selected-item highlight

const uint16_t ORANGE = UI_MAGENTA;   // Use magenta instead of orange
const uint16_t GRAY = 0x8410;
const uint16_t BLUE = UI_CYAN;
const uint16_t RED = 0xF800;
const uint16_t GREEN = UI_GREEN;
const uint16_t BLACK = 0x0000;
const uint16_t WHITE = 0xFFFF;
const uint16_t LIGHT_GRAY = 0xC618;
const uint16_t DARK_GRAY = UI_GUNMETAL;

#define TFT_DARKBLUE  0x3166
#define TFT_LIGHTBLUE UI_CYAN
#define TFTWHITE     0xFFFF
#define TFT_GRAY      0x8410
#define SELECTED_ICON_COLOR UI_AMBER

void displaySubmenu();

extern bool in_sub_menu;
extern bool feature_active;
extern bool submenu_initialized;
extern bool is_main_menu;
extern bool feature_exit_requested;


#endif // SHARED_H
