#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_input.h"

#include <stdbool.h>
#include <multimodalinput/oh_key_code.h>

enum
{
    XRDP_KEYSYM_BACKSPACE = 65288,
    XRDP_KEYSYM_TAB = 65289,
    XRDP_KEYSYM_RETURN = 65293,
    XRDP_KEYSYM_ESCAPE = 65307,
    XRDP_KEYSYM_HOME = 65360,
    XRDP_KEYSYM_LEFT = 65361,
    XRDP_KEYSYM_UP = 65362,
    XRDP_KEYSYM_RIGHT = 65363,
    XRDP_KEYSYM_DOWN = 65364,
    XRDP_KEYSYM_PAGE_UP = 65365,
    XRDP_KEYSYM_PAGE_DOWN = 65366,
    XRDP_KEYSYM_END = 65367,
    XRDP_KEYSYM_INSERT = 65379,
    XRDP_KEYSYM_DELETE = 65535,
    XRDP_KEYSYM_SHIFT_LEFT = 65505,
    XRDP_KEYSYM_SHIFT_RIGHT = 65506,
    XRDP_KEYSYM_CTRL_LEFT = 65507,
    XRDP_KEYSYM_CTRL_RIGHT = 65508,
    XRDP_KEYSYM_CAPS_LOCK = 65509,
    XRDP_KEYSYM_ALT_LEFT = 65513,
    XRDP_KEYSYM_ALT_RIGHT = 65514,
    XRDP_KEYSYM_META_LEFT = 65511,
    XRDP_KEYSYM_META_RIGHT = 65512,
    XRDP_KEYSYM_SUPER_LEFT = 65515,
    XRDP_KEYSYM_SUPER_RIGHT = 65516,
    XRDP_KEYSYM_F1 = 65470
};

static int
ohos_input_is_extended_key(const struct xrdp_ohos_input_event *event)
{
    return event != 0 && (event->param4 & 0x0100L) != 0;
}

static int
ohos_input_map_scancode(long scancode, int extended)
{
    switch (scancode)
    {
        case 1: return KEYCODE_ESCAPE;
        case 2: return KEYCODE_1;
        case 3: return KEYCODE_2;
        case 4: return KEYCODE_3;
        case 5: return KEYCODE_4;
        case 6: return KEYCODE_5;
        case 7: return KEYCODE_6;
        case 8: return KEYCODE_7;
        case 9: return KEYCODE_8;
        case 10: return KEYCODE_9;
        case 11: return KEYCODE_0;
        case 12: return KEYCODE_MINUS;
        case 13: return KEYCODE_EQUALS;
        case 14: return KEYCODE_DEL;
        case 15: return KEYCODE_TAB;
        case 16: return KEYCODE_Q;
        case 17: return KEYCODE_W;
        case 18: return KEYCODE_E;
        case 19: return KEYCODE_R;
        case 20: return KEYCODE_T;
        case 21: return KEYCODE_Y;
        case 22: return KEYCODE_U;
        case 23: return KEYCODE_I;
        case 24: return KEYCODE_O;
        case 25: return KEYCODE_P;
        case 26: return KEYCODE_LEFT_BRACKET;
        case 27: return KEYCODE_RIGHT_BRACKET;
        case 28: return extended ? KEYCODE_NUMPAD_ENTER : KEYCODE_ENTER;
        case 29: return extended ? KEYCODE_CTRL_RIGHT : KEYCODE_CTRL_LEFT;
        case 30: return KEYCODE_A;
        case 31: return KEYCODE_S;
        case 32: return KEYCODE_D;
        case 33: return KEYCODE_F;
        case 34: return KEYCODE_G;
        case 35: return KEYCODE_H;
        case 36: return KEYCODE_J;
        case 37: return KEYCODE_K;
        case 38: return KEYCODE_L;
        case 39: return KEYCODE_SEMICOLON;
        case 40: return KEYCODE_APOSTROPHE;
        case 41: return KEYCODE_GRAVE;
        case 42: return KEYCODE_SHIFT_LEFT;
        case 43: return KEYCODE_BACKSLASH;
        case 44: return KEYCODE_Z;
        case 45: return KEYCODE_X;
        case 46: return KEYCODE_C;
        case 47: return KEYCODE_V;
        case 48: return KEYCODE_B;
        case 49: return KEYCODE_N;
        case 50: return KEYCODE_M;
        case 51: return KEYCODE_COMMA;
        case 52: return KEYCODE_PERIOD;
        case 53: return extended ? KEYCODE_NUMPAD_DIVIDE : KEYCODE_SLASH;
        case 54: return KEYCODE_SHIFT_RIGHT;
        case 55: return KEYCODE_NUMPAD_MULTIPLY;
        case 56: return extended ? KEYCODE_ALT_RIGHT : KEYCODE_ALT_LEFT;
        case 57: return KEYCODE_SPACE;
        case 58: return KEYCODE_CAPS_LOCK;
        case 59: return KEYCODE_F1;
        case 60: return KEYCODE_F2;
        case 61: return KEYCODE_F3;
        case 62: return KEYCODE_F4;
        case 63: return KEYCODE_F5;
        case 64: return KEYCODE_F6;
        case 65: return KEYCODE_F7;
        case 66: return KEYCODE_F8;
        case 67: return KEYCODE_F9;
        case 68: return KEYCODE_F10;
        case 69: return KEYCODE_NUM_LOCK;
        case 70: return KEYCODE_SCROLL_LOCK;
        case 71: return extended ? KEYCODE_MOVE_HOME : KEYCODE_NUMPAD_7;
        case 72: return extended ? KEYCODE_DPAD_UP : KEYCODE_NUMPAD_8;
        case 73: return extended ? KEYCODE_PAGE_UP : KEYCODE_NUMPAD_9;
        case 74: return KEYCODE_NUMPAD_SUBTRACT;
        case 75: return extended ? KEYCODE_DPAD_LEFT : KEYCODE_NUMPAD_4;
        case 76: return KEYCODE_NUMPAD_5;
        case 77: return extended ? KEYCODE_DPAD_RIGHT : KEYCODE_NUMPAD_6;
        case 78: return KEYCODE_NUMPAD_ADD;
        case 79: return extended ? KEYCODE_MOVE_END : KEYCODE_NUMPAD_1;
        case 80: return extended ? KEYCODE_DPAD_DOWN : KEYCODE_NUMPAD_2;
        case 81: return extended ? KEYCODE_PAGE_DOWN : KEYCODE_NUMPAD_3;
        case 82: return extended ? KEYCODE_INSERT : KEYCODE_NUMPAD_0;
        case 83: return extended ? KEYCODE_FORWARD_DEL : KEYCODE_NUMPAD_DOT;
        case 87: return KEYCODE_F11;
        case 88: return KEYCODE_F12;
        case 91: return KEYCODE_META_LEFT;
        case 92: return KEYCODE_META_RIGHT;
        case 93: return KEYCODE_MENU;
        default: return -1;
    }
}

static int
ohos_input_map_keysym(long keysym)
{
    if (keysym >= 'a' && keysym <= 'z')
    {
        return KEYCODE_A + (int)(keysym - 'a');
    }
    if (keysym >= 'A' && keysym <= 'Z')
    {
        return KEYCODE_A + (int)(keysym - 'A');
    }
    if (keysym >= '0' && keysym <= '9')
    {
        return keysym == '0' ? KEYCODE_0 : KEYCODE_1 + (int)(keysym - '1');
    }

    switch (keysym)
    {
        case ' ': return KEYCODE_SPACE;
        case '-': return KEYCODE_MINUS;
        case '=': return KEYCODE_EQUALS;
        case '[': return KEYCODE_LEFT_BRACKET;
        case ']': return KEYCODE_RIGHT_BRACKET;
        case '\\': return KEYCODE_BACKSLASH;
        case ';': return KEYCODE_SEMICOLON;
        case '\'': return KEYCODE_APOSTROPHE;
        case ',': return KEYCODE_COMMA;
        case '.': return KEYCODE_PERIOD;
        case '/': return KEYCODE_SLASH;
        case '`': return KEYCODE_GRAVE;
        case XRDP_KEYSYM_BACKSPACE: return KEYCODE_DEL;
        case XRDP_KEYSYM_TAB: return KEYCODE_TAB;
        case XRDP_KEYSYM_RETURN: return KEYCODE_ENTER;
        case XRDP_KEYSYM_ESCAPE: return KEYCODE_ESCAPE;
        case XRDP_KEYSYM_HOME: return KEYCODE_MOVE_HOME;
        case XRDP_KEYSYM_LEFT: return KEYCODE_DPAD_LEFT;
        case XRDP_KEYSYM_UP: return KEYCODE_DPAD_UP;
        case XRDP_KEYSYM_RIGHT: return KEYCODE_DPAD_RIGHT;
        case XRDP_KEYSYM_DOWN: return KEYCODE_DPAD_DOWN;
        case XRDP_KEYSYM_PAGE_UP: return KEYCODE_PAGE_UP;
        case XRDP_KEYSYM_PAGE_DOWN: return KEYCODE_PAGE_DOWN;
        case XRDP_KEYSYM_END: return KEYCODE_MOVE_END;
        case XRDP_KEYSYM_INSERT: return KEYCODE_INSERT;
        case XRDP_KEYSYM_DELETE: return KEYCODE_FORWARD_DEL;
        case XRDP_KEYSYM_SHIFT_LEFT: return KEYCODE_SHIFT_LEFT;
        case XRDP_KEYSYM_SHIFT_RIGHT: return KEYCODE_SHIFT_RIGHT;
        case XRDP_KEYSYM_CTRL_LEFT: return KEYCODE_CTRL_LEFT;
        case XRDP_KEYSYM_CTRL_RIGHT: return KEYCODE_CTRL_RIGHT;
        case XRDP_KEYSYM_CAPS_LOCK: return KEYCODE_CAPS_LOCK;
        case XRDP_KEYSYM_ALT_LEFT: return KEYCODE_ALT_LEFT;
        case XRDP_KEYSYM_ALT_RIGHT: return KEYCODE_ALT_RIGHT;
        case XRDP_KEYSYM_META_LEFT:
        case XRDP_KEYSYM_SUPER_LEFT: return KEYCODE_META_LEFT;
        case XRDP_KEYSYM_META_RIGHT:
        case XRDP_KEYSYM_SUPER_RIGHT: return KEYCODE_META_RIGHT;
        default:
            if (keysym >= XRDP_KEYSYM_F1 && keysym < XRDP_KEYSYM_F1 + 12)
            {
                return KEYCODE_F1 + (int)(keysym - XRDP_KEYSYM_F1);
            }
            return -1;
    }
}

int
ohos_input_map_key(const struct xrdp_ohos_input_event *event)
{
    int by_scancode;

    if (event == 0)
    {
        return -1;
    }

    by_scancode = ohos_input_map_scancode(event->param3,
                                          ohos_input_is_extended_key(event));
    if (by_scancode >= 0)
    {
        return by_scancode;
    }
    return ohos_input_map_keysym(event->param2);
}
