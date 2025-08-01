#pragma once

#define ANSI_ESC '\x1b'

#define ANSI_MAX_NUM_PARAMS 16
#define ANSI_FINAL_SGR 'm'

#define ANSI_FINAL_CUU 'A'
#define ANSI_FINAL_CUD 'B'
#define ANSI_FINAL_CUF 'C'
#define ANSI_FINAL_CUB 'D'
#define ANSI_FINAL_EL 'K'
#define ANSI_FINAL_DCH 'P'
#define ANSI_FINAL_ICH '@'
#define ANSI_FINAL_ECH 'X'
#define ANSI_FINAL_ECH 'X'
#define ANSI_FINAL_CUP 'H'
#define ANSI_FINAL_HVP 'f'
#define ANSI_FINAL_ED 'J'
#define ANSI_FINAL_DECSET 'h'
#define ANSI_FINAL_DECRST 'l'
#define ANSI_FINAL_DA 'c'
#define ANSI_FINAL_CHA 'G'
#define ANSI_FINAL_DECSTBM 'r'
#define ANSI_FINAL_VPA 'd'

#define ANSI_DA_VT320 "63"
#define ANSI_DA_ANSI "22"
#define ANSI_DA_RESP "\x1b[?" ANSI_DA_VT320 ";" ANSI_DA_ANSI "c"

// u
// p

#define ANSI_C1_RI 'M'
