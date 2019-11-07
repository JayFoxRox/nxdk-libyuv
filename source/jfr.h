// Stolen from newer libyuv, ported to MMX and unrolled

// Show uncompiled disassembly:
// reset && cat jfr.h  | clang -E - -DTEST | clang-format

// Show compiled disassembly
// reset && clang -DTEST -xc -c jfr.h -o foo.elf -g -m32 && objdump -d foo.elf

#define UNDERSCORE "_"

#define _S(x) #x
#define S(x) _S((x))

#define ARGB_TO_ARGB_0()
#define ARGB_TO_ARGB_i(i) \
    "movq    %mm1," S((i)*8) "(%ebp)     \n"
#define ARGB_TO_ARGB_n(n) \
    "lea     " S((n)*8) "(%ebp),%ebp     \n"

  //FIXME: Scaling can probably be avoided using other tables [at least for blue?]
  //       If we do this naively, we can skip the `>> 3` below, but it will clip.
  //       The saturation just won't work anymore.
  //       Maybe I just fucked with R?
#define ARGB_TO_RGB565_0() \
  "pcmpeqb   %mm5,%mm5                   \n" \
  "psrld     $0x1b,%mm5                  \n" /* 0000001F */ \
  "pcmpeqb   %mm6,%mm6                   \n" \
  "psrld     $0x1a,%mm6                  \n" \
  "pslld     $0x5,%mm6                   \n" /* 0000007E */ \
  "pcmpeqb   %mm7,%mm7                   \n" \
  "pslld     $0xb,%mm7                   \n" /* FFFFF800 */
#define ARGB_TO_RGB565_i(i) \
  "movq      %mm1,%mm0                   \n" \
  "movq      %mm1,%mm2                   \n" \
  "pslld     $0x8,%mm0                   \n" /* mm0 = AARRGGBB << 8 [= RRGGBB00 ..] */ \
  "psrld     $0x3,%mm1                   \n" /* mm1 = AARRGGBB >> 3 [= / = get 5 bits blue] */ \
  "psrld     $0x5,%mm2                   \n" /* mm2 = AARRGGBB >> 5 [= / = get 6 bits green] */ \
  "psrad     $0x10,%mm0                  \n" /* ((signed)mm0 >> 16) [= 0000RRGG; = get 5 bits red] */ \
  "pand      %mm5,%mm1                   \n" /* mm1 = B bits */ \
  "pand      %mm6,%mm2                   \n" /* mm2 = G bits */ \
  "pand      %mm7,%mm0                   \n" /* mm0 = R bits */ \
  "por       %mm2,%mm1                   \n" /* mm1 = R-B */ \
  "por       %mm1,%mm0                   \n" /* mm0 = RGB */ \
  "packssdw  %mm0,%mm0                   \n" /* --XX--YY but we need ----XXYY */ \
  "movd      %mm0," S((i)*4) "(%ebp)     \n"
#define ARGB_TO_RGB565_n(n) \
  "lea       " S((n)*4) "(%ebp),%ebp     \n"

//FIXME: Untested
#define ARGB_TO_ARGB4444_0() \
  "pcmpeqb   %mm6,%mm6                   \n" \
  "psllw     $0xc,%mm6                   \n" /* mm3:F000F000 */ \
  "movq      %mm6,%mm7                   \n" \
  "psrlw     $0x8,%mm7                   \n" /* mm4:00F000F0 */
#define ARGB_TO_ARGB4444_i(i) \
  "movq      %mm1,%mm0                   \n" /* mm0: AARRGGBB */ \
  "pand      %mm6,%mm1                   \n" /* mm1: A000G000 */ \
  "pand      %mm7,%mm0                   \n" /* mm0: 00R000B0 */ \
  "psrlq     $0x8,%mm1                   \n" /* mm1: 00A000G0 */ \
  "psrlq     $0x4,%mm0                   \n" /* mm0: 000R000B */ \
  "por       %mm1,%mm0                   \n" /* mm0: 00AR00GB */ \
  "packuswb  %mm0,%mm0                   \n" /* mm0: 00AR 00GB -> AR GB */ \
  "movd      %mm0," S((i)*4) "(%ebp)     \n"
#define ARGB_TO_ARGB4444_n(n) \
  "lea       " S((n)*4) "(%ebp),%ebp     \n"

#define ARGB_TO_ARGB1555_0() \
  "pcmpeqb   %mm4,%mm4                   \n" \
  "psrld     $0x1b,%mm4                  \n" /* 0000001F */ \
  "movq      %mm4,%mm5                   \n" \
  "pslld     $0x5,%mm5                   \n" /* 000003E0 */ \
  "movq      %mm4,%mm6                   \n" \
  "pslld     $0xa,%mm6                   \n" /* 00007C00 */ \
  "pcmpeqb   %mm7,%mm7                   \n" \
  "pslld     $0xf,%mm7                   \n" /* FFFF8000 */
#define ARGB_TO_ARGB1555_i(i) \
  "movq      %mm1,%mm0                   \n" /* Alpha bit [can possibly be ignored] */ \
  "movq      %mm1,%mm2                   \n" \
  "movq      %mm1,%mm3                   \n" \
  "psrad     $0x10,%mm0                  \n" /* mm0:  ----AARR - Alpha bit [can possibly be ignored] */ \
  "psrld     $0x3,%mm1                   \n" /* mm1: ~-AARRGGB */ \
  "psrld     $0x6,%mm2                   \n" /* mm2: ~--AARRGG */ \
  "psrld     $0x9,%mm3                   \n" /* mm3: ~---AARRG */ \
  "pand      %mm7,%mm0                   \n" /* Alpha bit [can possibly be ignored] */ \
  "pand      %mm4,%mm1                   \n" \
  "pand      %mm5,%mm2                   \n" \
  "pand      %mm6,%mm3                   \n" \
  "por       %mm1,%mm0                   \n" \
  "por       %mm3,%mm2                   \n" \
  "por       %mm2,%mm0                   \n" \
  "packssdw  %mm0,%mm0                   \n" \
  "movd      %mm0," S((i)*4) "(%ebp)     \n"
#define ARGB_TO_ARGB1555_n(n) \
  "lea       " S((n)*4) "(%ebp),%ebp     \n"


//       esi = y buf
//       edi = u buf
//       edx = v buf
//       ebp = rgb buf
//       ecx = loop iter
//
//       eax = temp
//       ebx = temp [unused - FIXME: rgb stride?]
//
//       mm1 = OUTPUT_i argb input
//       mm0 - mm3 = OUTPUT_i tmp
//       mm4 - mm7 = OUTPUT tmp
//
//FIXME: Undo some changes.. I had it at ~4ms before (without unrolling!).
//       then it was back at back at ~5ms (without unrolling!).
//       I assume the fast one was the original code?
//       (Update: it's back at 4ms when unrolled to 2/4 - can still be made faster probably..)
//
//FIXME: We could handle 2 lines at once (they share U and V)
//       We'd have Y3 and Y4 respectively, and `ebx` as rgb stride
//       We'd need to remove alpha to gain 1-2 free MMX regs
//       (so we don't destroy mm0 in OUTPUT)
//       See XXX comments
#define CONVERT_YUV_LOOP_1(i, OUTPUT) \
  /* Common for 2 lines */ \
  "movzbl " S(i) "(%edi),%eax                                \n" /* Read U */ \
  "movq   " UNDERSCORE "kCoefficientsRgbY+2048(,%eax,8),%mm0 \n" /* mm0 = ARGB[U] */ \
  "movzbl " S(i) "(%edx),%eax                                \n" /* Read V */ \
  "paddsw " UNDERSCORE "kCoefficientsRgbY+4096(,%eax,8),%mm0 \n" /* mm0 += ARGB[V] */ \
  /* Per line stuff */ \
  "movzbl " S((i)*2+0) "(%esi),%eax                          \n" /* Read Y1 */ \
  "movq   " UNDERSCORE "kCoefficientsRgbY(,%eax,8),%mm1      \n" /* mm1 = ARGB[Y1] */ \
  "movzbl " S((i)*2+1) "(%esi),%eax                          \n" /* Read Y2 */ \
  "movq   " UNDERSCORE "kCoefficientsRgbY(,%eax,8),%mm2      \n" /* mm2 = ARGB[Y2] */ \
  "paddsw %mm0,%mm1                                          \n" /* mm1 += ARGB[U,V] */ \
  "paddsw %mm0,%mm2                                          \n" /* mm2 += ARGB[U,V] */ \
  "psraw  $0x6,%mm1                                          \n" /* mm1 /= 64 */ \
  "psraw  $0x6,%mm2                                          \n" /* mm2 /= 64  */ \
  /* XXX: eax = Y3 offset? */\
  "packuswb %mm2,%mm1                                        \n" /* mm1 = ARGB[Y1,U,V] # ARGB[Y2,U,V] */ \
  OUTPUT(i) /* XXX: Couldn't touch mm0 */
  /* XXX: mm1/mm2 = Y3/Y4 handling */

#define CONVERT_YUV_LOOP_2(i, OUTPUT) CONVERT_YUV_LOOP_1(i, OUTPUT) CONVERT_YUV_LOOP_1(1+i, OUTPUT)
#define CONVERT_YUV_LOOP_4(i, OUTPUT) CONVERT_YUV_LOOP_2(i, OUTPUT) CONVERT_YUV_LOOP_2(2+i, OUTPUT)
#define CONVERT_YUV_LOOP_8(i, OUTPUT) CONVERT_YUV_LOOP_4(i, OUTPUT) CONVERT_YUV_LOOP_4(4+i, OUTPUT)
#define CONVERT_YUV_LOOP_16(i, OUTPUT) CONVERT_YUV_LOOP_8(i, OUTPUT) CONVERT_YUV_LOOP_8(8+i, OUTPUT)
#define CONVERT_YUV_LOOP_32(i, OUTPUT) CONVERT_YUV_LOOP_16(i, OUTPUT) CONVERT_YUV_LOOP_16(16+i, OUTPUT)
#define CONVERT_YUV_LOOP_64(i, OUTPUT) CONVERT_YUV_LOOP_32(i, OUTPUT) CONVERT_YUV_LOOP_32(32+i, OUTPUT)
#define CONVERT_YUV_LOOP_128(i, OUTPUT) CONVERT_YUV_LOOP_64(i, OUTPUT) CONVERT_YUV_LOOP_64(64+i, OUTPUT)

#define CONVERT_YUV_n(n, OUTPUT)  \
  OUTPUT ## _0() /* FIXME: Only do once per image, not per line */ \
  "1:                                                   \n" \
    CONVERT_YUV_LOOP_ ## n(0, OUTPUT ## _i)                 /* Typically address-logic (like `lea`) near end (for `OUTPUT_i`) */ \
    "lea    " S(n) "(%edi),%edi                         \n" /* Advance U [use `add`?] */ \
    "lea    " S(n) "(%edx),%edx                         \n" /* Advance V */ \
    "lea    " S((n)*2) "(%esi),%esi                     \n" /* Advance Y [use `add`?] */ \
    OUTPUT ## _n(n)                                         /* Typically just `lea` */ \
    "sub    $" S((n)*2) ",%ecx                          \n" \
  "ja   1b                                              \n" /*FIXME: Check for off-by-one; use `loop`? */ \
  "popa                                                 \n" \
  "ret                                                  \n"

#define CONVERT_YUV_1(OUTPUT) CONVERT_YUV_n(1, OUTPUT)
#define CONVERT_YUV_2(OUTPUT) CONVERT_YUV_n(2, OUTPUT)
#define CONVERT_YUV_4(OUTPUT) CONVERT_YUV_n(4, OUTPUT)
#define CONVERT_YUV_8(OUTPUT) CONVERT_YUV_n(8, OUTPUT)
#define CONVERT_YUV_16(OUTPUT) CONVERT_YUV_n(16, OUTPUT) // 4.089527 ms
#define CONVERT_YUV_32(OUTPUT) CONVERT_YUV_n(32, OUTPUT)
#define CONVERT_YUV_128(OUTPUT) CONVERT_YUV_n(128, OUTPUT) // 4.643765 ms

#ifdef TEST
#define OUTPUT_0() "" "" "# ------------------------- OUTPUT_0" "\n" "nop;nop;nop;nop\n" "" ""
#define OUTPUT_i(i) "" "" "# ------------------------- OUTPUT_i" #i "\n" "nop;nop;nop;nop\n" "" ""
#define OUTPUT_n(n) "" "" "# ------------------------- OUTPUT_n" #n "\n" "nop;nop;nop;nop\n" "" ""
void foo() {
asm(
CONVERT_YUV_2(OUTPUT)
);
}
#else

#define FastConvertYUVTo___Row_MMX(name) \
  void FastConvertYUVTo ## name ## Row_MMX(const uint8* y_buf, \
                                           const uint8* u_buf, \
                                           const uint8* v_buf, \
                                           uint8* rgb_buf, \
                                           int width); \
    asm( \
      ".text                                                 \n" \
      ".globl " UNDERSCORE "FastConvertYUVTo" #name "Row_MMX \n" \
      UNDERSCORE "FastConvertYUVTo" #name "Row_MMX:          \n" \
      "pusha                                                 \n" \
      "mov    0x24(%esp),%esi                                \n" /* y_buf */ \
      "mov    0x28(%esp),%edi                                \n" /* u_buf */ \
      "mov    0x2c(%esp),%edx                                \n" /* v_buf */ \
      "mov    0x30(%esp),%ebp                                \n" /* rgb_buf */ \
      "mov    0x34(%esp),%ecx                                \n" /* width */ \
      CONVERT_YUV_16(ARGB_TO_ ## name) \
    );


FastConvertYUVTo___Row_MMX(ARGB)
FastConvertYUVTo___Row_MMX(RGB565)
FastConvertYUVTo___Row_MMX(ARGB1555) // ZRGB1555 / ORGB1555 / XRGB1555 [perf!]
FastConvertYUVTo___Row_MMX(ARGB4444) // ZRGB4444 / ORGB4444 / XRGB4444 [same as ARGB4444 ?]

#endif
