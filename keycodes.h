#pragma once

namespace mgo
{

namespace key
{

// This is not meant to be exhaustive

constexpr int None = -1;

constexpr int EQUALS    = 61;
constexpr int MINUS     = 45;
constexpr int BACKSLASH = 92;
constexpr int PIPE      = 124;
constexpr int ENTER     = 10;
constexpr int UP        = 259;
constexpr int DOWN      = 258;
constexpr int LEFT      = 260;
constexpr int RIGHT     = 261;
constexpr int COMMA     = 44;
constexpr int FULLSTOP  = 46;
constexpr int LBRACKET  = 91; // [
constexpr int RBRACKET  = 93; // ]
constexpr int ASTERISK  = 42;
constexpr int ESC       = 27;
constexpr int BACKSPACE = 8;
constexpr int DELETE    = 127;
constexpr int SPACE     = 32;

constexpr int F1  = 265;
constexpr int F2  = 266;
constexpr int F3  = 267;
constexpr int F4  = 268;
constexpr int F5  = 269;
constexpr int F6  = 270;
constexpr int F7  = 271;
constexpr int F8  = 272;
constexpr int F9  = 273;
constexpr int F10 = 274;
constexpr int F11 = 275;
constexpr int F12 = 276;

constexpr int ZERO  = 48;
constexpr int ONE   = 49;
constexpr int TWO   = 50;
constexpr int THREE = 51;
constexpr int FOUR  = 52;
constexpr int FIVE  = 53;
constexpr int SIX   = 54;
constexpr int SEVEN = 55;
constexpr int EIGHT = 56;
constexpr int NINE  = 57;

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

// Axis 1 leader-prefixed keys ( 4096 = bit 12 is set )
constexpr int a1_z = z            + 4096;  // axis1 zero
constexpr int a1_m = m            + 4096;  // axis1 memorise
constexpr int a1_g = g            + 4096;  // axis1 go to number
constexpr int a1_s = s            + 4096;  // axis1 manual set
constexpr int a1_f = f            + 4096;  // fast return on axis 1
constexpr int a1_1 = ONE          + 4096;  // axis1 speed 1
constexpr int a1_2 = TWO          + 4096;  // axis1 speed 2
constexpr int a1_3 = THREE        + 4096;  // axis1 speed 3
constexpr int a1_4 = FOUR         + 4096;  // axis1 speed 4
constexpr int a1_5 = FIVE         + 4096;  // axis1 speed 5
constexpr int a1_ENTER = ENTER    + 4096;  // axis1 return to memory
constexpr int a1_MINUS = MINUS    + 4096;  // axis1 speed decrease
constexpr int a1_EQUALS = EQUALS  + 4096;  // axis1 speed increase

// Axis 1 leader-prefixed keys ( 8192 = bit 13 is set )
constexpr int a2_z = z            + 8192;  // axis2 zero
constexpr int a2_m = m            + 8192;  // axis2 memorise
constexpr int a2_g = g            + 8192;  // axis2 go to number
constexpr int a2_s = s            + 8192;  // axis2 manual set
constexpr int a2_f = f            + 8192;  // fast return on axis 2
constexpr int a2_1 = ONE          + 8192;  // axis2 speed 1
constexpr int a2_2 = TWO          + 8192;  // axis2 speed 2
constexpr int a2_3 = THREE        + 8192;  // axis2 speed 3
constexpr int a2_4 = FOUR         + 8192;  // axis2 speed 4
constexpr int a2_5 = FIVE         + 8192;  // axis2 speed 5
constexpr int a2_ENTER = ENTER    + 8192;  // axis2 return to memory
constexpr int a2_MINUS = MINUS    + 8192;  // axis2 speed decrease
constexpr int a2_EQUALS = EQUALS  + 8192;  // axis2 speed increase


// Functions, leader is F2
constexpr int f2h = 7000; // help
constexpr int f2s = 7001; // setup
constexpr int f2t = 7002; // threading
constexpr int f2p = 7003; // taPer
constexpr int f2r = 7004; // X retraction mode
constexpr int f2o = 7005; // radius mode


// Random others
constexpr int CtrlQ = key::q | 0x10000; // quit

} // namespace key
} // namespace mgo

