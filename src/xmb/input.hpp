// PS3 controller -> keyboard mapping. Single source of truth (see docs/CONTROLS.md).
#pragma once
#include <SDL.h>

enum class Pad {
    LS_Left, LS_Down, LS_Right, LS_Up,
    RS_Left, RS_Down, RS_Right, RS_Up,
    Start, Select, PS,
    Square, Cross, Circle, Triangle,
    DP_Left, DP_Down, DP_Right, DP_Up,
    R1, R2, R3, L1, L2, L3
};

inline bool mapKey(SDL_Scancode sc, Pad& out) {
    switch (sc) {
        case SDL_SCANCODE_LEFT:      out = Pad::LS_Left;  return true;
        case SDL_SCANCODE_DOWN:      out = Pad::LS_Down;  return true;
        case SDL_SCANCODE_RIGHT:     out = Pad::LS_Right; return true;
        case SDL_SCANCODE_UP:        out = Pad::LS_Up;    return true;
        case SDL_SCANCODE_H:         out = Pad::RS_Left;  return true;
        case SDL_SCANCODE_J:         out = Pad::RS_Down;  return true;
        case SDL_SCANCODE_K:         out = Pad::RS_Right; return true;
        case SDL_SCANCODE_U:         out = Pad::RS_Up;    return true;
        case SDL_SCANCODE_V:         out = Pad::Start;    return true;
        case SDL_SCANCODE_SPACE:     out = Pad::Select;   return true;
        case SDL_SCANCODE_BACKSPACE: out = Pad::PS;       return true;
        case SDL_SCANCODE_A:         out = Pad::Square;   return true;
        case SDL_SCANCODE_Z:         out = Pad::Cross;    return true;
        case SDL_SCANCODE_X:         out = Pad::Circle;   return true;
        case SDL_SCANCODE_S:         out = Pad::Triangle; return true;
        case SDL_SCANCODE_KP_4:      out = Pad::DP_Left;  return true;
        case SDL_SCANCODE_KP_5:      out = Pad::DP_Down;  return true;
        case SDL_SCANCODE_KP_6:      out = Pad::DP_Right; return true;
        case SDL_SCANCODE_KP_8:      out = Pad::DP_Up;    return true;
        case SDL_SCANCODE_R:         out = Pad::R1;       return true;
        case SDL_SCANCODE_RCTRL:     out = Pad::R2;       return true;
        case SDL_SCANCODE_LCTRL:     out = Pad::R3;       return true;
        case SDL_SCANCODE_L:         out = Pad::L1;       return true;
        case SDL_SCANCODE_RSHIFT:    out = Pad::L2;       return true;
        case SDL_SCANCODE_LSHIFT:    out = Pad::L3;       return true;
        default: return false;
    }
}

inline bool isDirectional(Pad p) {
    switch (p) {
        case Pad::LS_Left: case Pad::LS_Down: case Pad::LS_Right: case Pad::LS_Up:
        case Pad::RS_Left: case Pad::RS_Down: case Pad::RS_Right: case Pad::RS_Up:
        case Pad::DP_Left: case Pad::DP_Down: case Pad::DP_Right: case Pad::DP_Up:
        case Pad::L1: case Pad::R1: case Pad::L2: case Pad::R2:
            return true;
        default: return false;
    }
}
