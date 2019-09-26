/*
 * Copiright (C) 2019 Santiago León O.
 */
char *xkb_keycode_names[KEY_CNT] = {0};

void init_xkb_keycode_names () {
     xkb_keycode_names[ 1 ] = "ESC";
     xkb_keycode_names[ 2 ] = "AE01";
     xkb_keycode_names[ 3 ] = "AE02";
     xkb_keycode_names[ 4 ] = "AE03";
     xkb_keycode_names[ 5 ] = "AE04";
     xkb_keycode_names[ 6 ] = "AE05";
     xkb_keycode_names[ 7 ] = "AE06";
     xkb_keycode_names[ 8 ] = "AE07";
     xkb_keycode_names[ 9 ] = "AE08";
     xkb_keycode_names[ 10 ] = "AE09";
     xkb_keycode_names[ 11 ] = "AE10";
     xkb_keycode_names[ 12 ] = "AE11";
     xkb_keycode_names[ 13 ] = "AE12";
     xkb_keycode_names[ 14 ] = "BKSP";
     xkb_keycode_names[ 15 ] = "TAB";
     xkb_keycode_names[ 16 ] = "AD01";
     xkb_keycode_names[ 17 ] = "AD02";
     xkb_keycode_names[ 18 ] = "AD03";
     xkb_keycode_names[ 19 ] = "AD04";
     xkb_keycode_names[ 20 ] = "AD05";
     xkb_keycode_names[ 21 ] = "AD06";
     xkb_keycode_names[ 22 ] = "AD07";
     xkb_keycode_names[ 23 ] = "AD08";
     xkb_keycode_names[ 24 ] = "AD09";
     xkb_keycode_names[ 25 ] = "AD10";
     xkb_keycode_names[ 26 ] = "AD11";
     xkb_keycode_names[ 27 ] = "AD12";
     xkb_keycode_names[ 28 ] = "RTRN";
     xkb_keycode_names[ 29 ] = "LCTL";
     xkb_keycode_names[ 30 ] = "AC01";
     xkb_keycode_names[ 31 ] = "AC02";
     xkb_keycode_names[ 32 ] = "AC03";
     xkb_keycode_names[ 33 ] = "AC04";
     xkb_keycode_names[ 34 ] = "AC05";
     xkb_keycode_names[ 35 ] = "AC06";
     xkb_keycode_names[ 36 ] = "AC07";
     xkb_keycode_names[ 37 ] = "AC08";
     xkb_keycode_names[ 38 ] = "AC09";
     xkb_keycode_names[ 39 ] = "AC10";
     xkb_keycode_names[ 40 ] = "AC11";
     xkb_keycode_names[ 41 ] = "TLDE";
     xkb_keycode_names[ 42 ] = "LFSH";
     xkb_keycode_names[ 43 ] = "BKSL";
     xkb_keycode_names[ 44 ] = "AB01";
     xkb_keycode_names[ 45 ] = "AB02";
     xkb_keycode_names[ 46 ] = "AB03";
     xkb_keycode_names[ 47 ] = "AB04";
     xkb_keycode_names[ 48 ] = "AB05";
     xkb_keycode_names[ 49 ] = "AB06";
     xkb_keycode_names[ 50 ] = "AB07";
     xkb_keycode_names[ 51 ] = "AB08";
     xkb_keycode_names[ 52 ] = "AB09";
     xkb_keycode_names[ 53 ] = "AB10";
     xkb_keycode_names[ 54 ] = "RTSH";
     xkb_keycode_names[ 55 ] = "KPMU";
     xkb_keycode_names[ 56 ] = "LALT";
     xkb_keycode_names[ 57 ] = "SPCE";
     xkb_keycode_names[ 58 ] = "CAPS";
     xkb_keycode_names[ 59 ] = "FK01";
     xkb_keycode_names[ 60 ] = "FK02";
     xkb_keycode_names[ 61 ] = "FK03";
     xkb_keycode_names[ 62 ] = "FK04";
     xkb_keycode_names[ 63 ] = "FK05";
     xkb_keycode_names[ 64 ] = "FK06";
     xkb_keycode_names[ 65 ] = "FK07";
     xkb_keycode_names[ 66 ] = "FK08";
     xkb_keycode_names[ 67 ] = "FK09";
     xkb_keycode_names[ 68 ] = "FK10";
     xkb_keycode_names[ 69 ] = "NMLK";
     xkb_keycode_names[ 70 ] = "SCLK";
     xkb_keycode_names[ 71 ] = "KP7";
     xkb_keycode_names[ 72 ] = "KP8";
     xkb_keycode_names[ 73 ] = "KP9";
     xkb_keycode_names[ 74 ] = "KPSU";
     xkb_keycode_names[ 75 ] = "KP4";
     xkb_keycode_names[ 76 ] = "KP5";
     xkb_keycode_names[ 77 ] = "KP6";
     xkb_keycode_names[ 78 ] = "KPAD";
     xkb_keycode_names[ 79 ] = "KP1";
     xkb_keycode_names[ 80 ] = "KP2";
     xkb_keycode_names[ 81 ] = "KP3";
     xkb_keycode_names[ 82 ] = "KP0";
     xkb_keycode_names[ 83 ] = "KPDL";
     xkb_keycode_names[ 84 ] = "LVL3";
     xkb_keycode_names[ 86 ] = "LSGT";
     xkb_keycode_names[ 87 ] = "FK11";
     xkb_keycode_names[ 88 ] = "FK12";
     xkb_keycode_names[ 90 ] = "KATA";
     xkb_keycode_names[ 91 ] = "HIRA";
     xkb_keycode_names[ 92 ] = "HENK";
     xkb_keycode_names[ 93 ] = "HKTG";
     xkb_keycode_names[ 94 ] = "MUHE";
     xkb_keycode_names[ 96 ] = "KPEN";
     xkb_keycode_names[ 97 ] = "RCTL";
     xkb_keycode_names[ 98 ] = "KPDV";
     xkb_keycode_names[ 99 ] = "PRSC";
     xkb_keycode_names[ 100 ] = "RALT";
     xkb_keycode_names[ 101 ] = "LNFD";
     xkb_keycode_names[ 102 ] = "HOME";
     xkb_keycode_names[ 103 ] = "UP";
     xkb_keycode_names[ 104 ] = "PGUP";
     xkb_keycode_names[ 105 ] = "LEFT";
     xkb_keycode_names[ 106 ] = "RGHT";
     xkb_keycode_names[ 107 ] = "END";
     xkb_keycode_names[ 108 ] = "DOWN";
     xkb_keycode_names[ 109 ] = "PGDN";
     xkb_keycode_names[ 110 ] = "INS";
     xkb_keycode_names[ 111 ] = "DELE";
     xkb_keycode_names[ 113 ] = "MUTE";
     xkb_keycode_names[ 114 ] = "VOL-";
     xkb_keycode_names[ 115 ] = "VOL+";
     xkb_keycode_names[ 116 ] = "POWR";
     xkb_keycode_names[ 117 ] = "KPEQ";
     xkb_keycode_names[ 118 ] = "I126";
     xkb_keycode_names[ 119 ] = "PAUS";
     xkb_keycode_names[ 120 ] = "I128";
     xkb_keycode_names[ 121 ] = "I129";
     xkb_keycode_names[ 122 ] = "HNGL";
     xkb_keycode_names[ 123 ] = "HJCV";
     xkb_keycode_names[ 125 ] = "LWIN";
     xkb_keycode_names[ 126 ] = "RWIN";
     xkb_keycode_names[ 127 ] = "COMP";
     xkb_keycode_names[ 128 ] = "STOP";
     xkb_keycode_names[ 129 ] = "AGAI";
     xkb_keycode_names[ 130 ] = "PROP";
     xkb_keycode_names[ 131 ] = "UNDO";
     xkb_keycode_names[ 132 ] = "FRNT";
     xkb_keycode_names[ 133 ] = "COPY";
     xkb_keycode_names[ 134 ] = "OPEN";
     xkb_keycode_names[ 135 ] = "PAST";
     xkb_keycode_names[ 136 ] = "FIND";
     xkb_keycode_names[ 137 ] = "CUT";
     xkb_keycode_names[ 138 ] = "HELP";
     xkb_keycode_names[ 139 ] = "I147";
     xkb_keycode_names[ 140 ] = "I148";
     xkb_keycode_names[ 142 ] = "I150";
     xkb_keycode_names[ 143 ] = "I151";
     xkb_keycode_names[ 144 ] = "I152";
     xkb_keycode_names[ 145 ] = "I153";
     xkb_keycode_names[ 147 ] = "I155";
     xkb_keycode_names[ 148 ] = "I156";
     xkb_keycode_names[ 149 ] = "I157";
     xkb_keycode_names[ 150 ] = "I158";
     xkb_keycode_names[ 151 ] = "I159";
     xkb_keycode_names[ 152 ] = "I160";
     xkb_keycode_names[ 153 ] = "I161";
     xkb_keycode_names[ 154 ] = "I162";
     xkb_keycode_names[ 155 ] = "I163";
     xkb_keycode_names[ 156 ] = "I164";
     xkb_keycode_names[ 157 ] = "I165";
     xkb_keycode_names[ 158 ] = "I166";
     xkb_keycode_names[ 159 ] = "I167";
     xkb_keycode_names[ 161 ] = "I169";
     xkb_keycode_names[ 162 ] = "I170";
     xkb_keycode_names[ 163 ] = "I171";
     xkb_keycode_names[ 164 ] = "I172";
     xkb_keycode_names[ 165 ] = "I173";
     xkb_keycode_names[ 166 ] = "I174";
     xkb_keycode_names[ 167 ] = "I175";
     xkb_keycode_names[ 168 ] = "I176";
     xkb_keycode_names[ 169 ] = "I177";
     xkb_keycode_names[ 171 ] = "I179";
     xkb_keycode_names[ 172 ] = "I180";
     xkb_keycode_names[ 173 ] = "I181";
     xkb_keycode_names[ 174 ] = "I182";
     xkb_keycode_names[ 177 ] = "I185";
     xkb_keycode_names[ 178 ] = "I186";
     xkb_keycode_names[ 179 ] = "I187";
     xkb_keycode_names[ 180 ] = "I188";
     xkb_keycode_names[ 181 ] = "I189";
     xkb_keycode_names[ 182 ] = "I190";
     xkb_keycode_names[ 183 ] = "FK13";
     xkb_keycode_names[ 184 ] = "FK14";
     xkb_keycode_names[ 185 ] = "FK15";
     xkb_keycode_names[ 186 ] = "FK16";
     xkb_keycode_names[ 187 ] = "FK17";
     xkb_keycode_names[ 188 ] = "FK18";
     xkb_keycode_names[ 190 ] = "FK20";
     xkb_keycode_names[ 191 ] = "FK21";
     xkb_keycode_names[ 192 ] = "FK22";
     xkb_keycode_names[ 193 ] = "FK23";
     xkb_keycode_names[ 195 ] = "MDSW";
     xkb_keycode_names[ 196 ] = "ALT";
     xkb_keycode_names[ 197 ] = "META";
     xkb_keycode_names[ 198 ] = "SUPR";
     xkb_keycode_names[ 199 ] = "HYPR";
     xkb_keycode_names[ 200 ] = "I208";
     xkb_keycode_names[ 201 ] = "I209";
     xkb_keycode_names[ 202 ] = "I210";
     xkb_keycode_names[ 203 ] = "I211";
     xkb_keycode_names[ 204 ] = "I212";
     xkb_keycode_names[ 205 ] = "I213";
     xkb_keycode_names[ 206 ] = "I214";
     xkb_keycode_names[ 207 ] = "I215";
     xkb_keycode_names[ 208 ] = "I216";
     xkb_keycode_names[ 210 ] = "I218";
     xkb_keycode_names[ 212 ] = "I220";
     xkb_keycode_names[ 215 ] = "I223";
     xkb_keycode_names[ 216 ] = "I224";
     xkb_keycode_names[ 217 ] = "I225";
     xkb_keycode_names[ 218 ] = "I226";
     xkb_keycode_names[ 219 ] = "I227";
     xkb_keycode_names[ 220 ] = "I228";
     xkb_keycode_names[ 221 ] = "I229";
     xkb_keycode_names[ 223 ] = "I231";
     xkb_keycode_names[ 224 ] = "I232";
     xkb_keycode_names[ 225 ] = "I233";
     xkb_keycode_names[ 226 ] = "I234";
     xkb_keycode_names[ 227 ] = "I235";
     xkb_keycode_names[ 228 ] = "I236";
     xkb_keycode_names[ 229 ] = "I237";
     xkb_keycode_names[ 230 ] = "I238";
     xkb_keycode_names[ 231 ] = "I239";
     xkb_keycode_names[ 232 ] = "I240";
     xkb_keycode_names[ 233 ] = "I241";
     xkb_keycode_names[ 234 ] = "I242";
     xkb_keycode_names[ 235 ] = "I243";
     xkb_keycode_names[ 236 ] = "I244";
     xkb_keycode_names[ 237 ] = "I245";
     xkb_keycode_names[ 238 ] = "I246";
}
