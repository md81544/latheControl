#pragma once

namespace mgo {

namespace key {

// This is not meant to be exhaustive

constexpr int None = -1;

constexpr int EQUALS = 61;
constexpr int MINUS = 45;
constexpr int BACKSLASH = 92;
constexpr int PIPE = 124;
constexpr int ENTER = 10;
constexpr int UP = 259;
constexpr int DOWN = 258;
constexpr int LEFT = 260;
constexpr int RIGHT = 261;
constexpr int COMMA = 44;
constexpr int FULLSTOP = 46;
constexpr int LBRACKET = 91; // [
constexpr int RBRACKET = 93; // ]
constexpr int ASTERISK = 42;
constexpr int ESC = 27;
constexpr int BACKSPACE = 8;
constexpr int DELETE = 127;
constexpr int SPACE = 32;
constexpr int COLON = 58;

constexpr int F1 = 265;
constexpr int F2 = 266;
constexpr int F3 = 267;
constexpr int F4 = 268;
constexpr int F5 = 269;
constexpr int F6 = 270;
constexpr int F7 = 271;
constexpr int F8 = 272;
constexpr int F9 = 273;
constexpr int F10 = 274;
constexpr int F11 = 275;
constexpr int F12 = 276;

constexpr int ZERO = 48;
constexpr int ONE = 49;
constexpr int TWO = 50;
constexpr int THREE = 51;
constexpr int FOUR = 52;
constexpr int FIVE = 53;
constexpr int SIX = 54;
constexpr int SEVEN = 55;
constexpr int EIGHT = 56;
constexpr int NINE = 57;

constexpr int A = 65;
constexpr int B = 66;
constexpr int C = 67;
constexpr int D = 68;
constexpr int E = 69;
constexpr int F = 70;
constexpr int G = 71;
constexpr int H = 72;
constexpr int I = 73;
constexpr int J = 74;
constexpr int K = 75;
constexpr int L = 76;
constexpr int M = 77;
constexpr int N = 78;
constexpr int O = 79;
constexpr int P = 80;
constexpr int Q = 81;
constexpr int R = 82;
constexpr int S = 83;
constexpr int T = 84;
constexpr int U = 85;
constexpr int V = 86;
constexpr int W = 87;
constexpr int X = 88;
constexpr int Y = 89;
constexpr int Z = 90;

constexpr int a = 97;
constexpr int b = 98;
constexpr int c = 99;
constexpr int d = 100;
constexpr int e = 101;
constexpr int f = 102;
constexpr int g = 103;
constexpr int h = 104;
constexpr int i = 105;
constexpr int j = 106;
constexpr int k = 107;
constexpr int l = 108;
constexpr int m = 109;
constexpr int n = 110;
constexpr int o = 111;
constexpr int p = 112;
constexpr int q = 113;
constexpr int r = 114;
constexpr int s = 115;
constexpr int t = 116;
constexpr int u = 117;
constexpr int v = 118;
constexpr int w = 119;
constexpr int x = 120;
constexpr int y = 121;
constexpr int z = 122;

// Axis 1 leader-prefixed keys ( 0x1000 = bit 12 is set )
constexpr int a1_z = z + 0x1000; // axis1 zero
constexpr int a1_m = m + 0x1000; // axis1 memorise
constexpr int a1_g = g + 0x1000; // axis1 go to number
constexpr int a1_r = r + 0x1000; // axis1 go to relative offset
constexpr int a1_s = s + 0x1000; // axis1 manual set
constexpr int a1_f = f + 0x1000; // fast return on axis 1
constexpr int a1_l = l + 0x1000; // last position on axis 1
constexpr int a1_1 = ONE + 0x1000; // axis1 speed 1
constexpr int a1_2 = TWO + 0x1000; // axis1 speed 2
constexpr int a1_3 = THREE + 0x1000; // axis1 speed 3
constexpr int a1_4 = FOUR + 0x1000; // axis1 speed 4
constexpr int a1_5 = FIVE + 0x1000; // axis1 speed 5
constexpr int a1_ENTER = ENTER + 0x1000; // axis1 return to memory
constexpr int a1_MINUS = MINUS + 0x1000; // axis1 speed decrease
constexpr int a1_EQUALS = EQUALS + 0x1000; // axis1 speed increase
constexpr int a1_FULLSTOP = '.' + 0x1000;

// Axis 2 leader-prefixed keys ( 0x2000 = bit 13 is set )
constexpr int a2_z = z + 0x2000; // axis2 zero
constexpr int a2_m = m + 0x2000; // axis2 memorise
constexpr int a2_g = g + 0x2000; // axis2 go to number
constexpr int a2_r = r + 0x2000; // axis2 go to relative offset
constexpr int a2_s = s + 0x2000; // axis2 manual set
constexpr int a2_f = f + 0x2000; // fast return on axis 2
constexpr int a2_l = l + 0x2000; // last position on axis 2
constexpr int a2_1 = ONE + 0x2000; // axis2 speed 1
constexpr int a2_2 = TWO + 0x2000; // axis2 speed 2
constexpr int a2_3 = THREE + 0x2000; // axis2 speed 3
constexpr int a2_4 = FOUR + 0x2000; // axis2 speed 4
constexpr int a2_5 = FIVE + 0x2000; // axis2 speed 5
constexpr int a2_ENTER = ENTER + 0x2000; // axis2 return to memory
constexpr int a2_MINUS = MINUS + 0x2000; // axis2 speed decrease
constexpr int a2_EQUALS = EQUALS + 0x2000; // axis2 speed increase
constexpr int a2_FULLSTOP = '.' + 0x2000;

// Axis All leader-prefixed keys ( 0x4000 = bit 14 is set )
constexpr int aAll_z = z + 0x4000; // All axis zero
constexpr int aAll_ENTER = ENTER + 0x4000; // All axis zero
constexpr int aAll_f = f + 0x4000;
constexpr int aAll_l = l + 0x4000; // last position on both axes
constexpr int aAll_FULLSTOP = '.' + 0x4000;

// Functions, leader is F2 (or colon)
constexpr int f2h = 7000; // help
constexpr int f2s = 7001; // setup
constexpr int f2t = 7002; // threading
constexpr int f2p = 7003; // taPer
constexpr int f2r = 7004; // X retraction mode
constexpr int f2o = 7005; // radius mode
constexpr int f2q = 7006; // quit (i.e. :q)
constexpr int f2m = 7007; // multipass

// Joystick specific "keys"
// Note some joystick commands just return regular keycodes.
constexpr int js_left = 8000;
constexpr int js_right = 8001;
constexpr int js_up = 8002;
constexpr int js_down = 8003;
constexpr int js_rapid_left = 8004;
constexpr int js_rapid_right = 8005;
constexpr int js_rapid_up = 8006;
constexpr int js_rapid_down = 8007;

// Random others
constexpr int CTRL = 0x10000;
constexpr int ALT = 0x100000;

constexpr int CtrlQ = key::q | CTRL; // quit
constexpr int AltW = key::w | ALT; // big nudge
constexpr int AltA = key::a | ALT; // big nudge
constexpr int AltS = key::s | ALT; // big nudge
constexpr int AltD = key::d | ALT; // big nudge
constexpr int AltLeft = key::LEFT | ALT; // rapid
constexpr int AltRight = key::RIGHT | ALT; // rapid
constexpr int AltUp = key::UP | ALT; // rapid
constexpr int AltDown = key::DOWN | ALT; // rapid

} // namespace key
} // namespace mgo
