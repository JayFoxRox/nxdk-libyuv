/*
 *  Copyright (c) 2011 The LibYuv project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "libyuv/planar_functions.h"

#include <string.h>

#include "libyuv/cpu_id.h"
#include "row.h"

namespace libyuv {

#if defined(__ARM_NEON__) && !defined(COVERAGE_ENABLED)
#define HAS_SPLITUV_NEON
// Reads 16 pairs of UV and write even values to dst_u and odd to dst_v
// Alignment requirement: 16 bytes for pointers, and multiple of 16 pixels.
static void SplitUV_NEON(const uint8* src_uv,
                         uint8* dst_u, uint8* dst_v, int pix) {
  asm volatile (
    "1:                                        \n"
    "vld2.u8    {q0,q1}, [%0]!                 \n"  // load 16 pairs of UV
    "vst1.u8    {q0}, [%1]!                    \n"  // store U
    "vst1.u8    {q1}, [%2]!                    \n"  // Store V
    "subs       %3, %3, #16                    \n"  // 16 processed per loop
    "bhi        1b                             \n"
    : "+r"(src_uv),
      "+r"(dst_u),
      "+r"(dst_v),
      "+r"(pix)             // Output registers
    :                       // Input registers
    : "memory", "cc", "q0", "q1" // Clobber List
  );
}

#elif (defined(WIN32) || defined(__x86_64__) || defined(__i386__)) \
    && !defined(COVERAGE_ENABLED) && !defined(TARGET_IPHONE_SIMULATOR)

#if defined(WIN32) && !defined(COVERAGE_ENABLED)
#define HAS_SPLITUV_SSE2
__declspec(naked)
static void SplitUV_SSE2(const uint8* src_uv,
                         uint8* dst_u, uint8* dst_v, int pix) {
  __asm {
    push       edi
    mov        eax, [esp + 4 + 4]    // src_uv
    mov        edx, [esp + 4 + 8]    // dst_u
    mov        edi, [esp + 4 + 12]   // dst_v
    mov        ecx, [esp + 4 + 16]   // pix
    pcmpeqb    xmm5, xmm5            // generate mask 0x00ff00ff
    psrlw      xmm5, 8

  convertloop:
    movdqa     xmm0, [eax]
    movdqa     xmm1, [eax + 16]
    lea        eax,  [eax + 32]
    movdqa     xmm2, xmm0
    movdqa     xmm3, xmm1
    pand       xmm0, xmm5   // even bytes
    pand       xmm1, xmm5
    packuswb   xmm0, xmm1
    movdqa     [edx], xmm0
    lea        edx, [edx + 16]
    psrlw      xmm2, 8      // odd bytes
    psrlw      xmm3, 8
    packuswb   xmm2, xmm3
    movdqa     [edi], xmm2
    lea        edi, [edi + 16]
    sub        ecx, 16
    ja         convertloop
    pop        edi
    ret
  }
}

#elif (defined(__x86_64__) || defined(__i386__)) && \
    !defined(COVERAGE_ENABLED) && !defined(TARGET_IPHONE_SIMULATOR)
#define HAS_SPLITUV_SSE2
static void SplitUV_SSE2(const uint8* src_uv,
                         uint8* dst_u, uint8* dst_v, int pix) {
 asm volatile (
  "pcmpeqb    %%xmm5,%%xmm5                    \n"
  "psrlw      $0x8,%%xmm5                      \n"
"1:                                            \n"
  "movdqa     (%0),%%xmm0                      \n"
  "movdqa     0x10(%0),%%xmm1                  \n"
  "lea        0x20(%0),%0                      \n"
  "movdqa     %%xmm0,%%xmm2                    \n"
  "movdqa     %%xmm1,%%xmm3                    \n"
  "pand       %%xmm5,%%xmm0                    \n"
  "pand       %%xmm5,%%xmm1                    \n"
  "packuswb   %%xmm1,%%xmm0                    \n"
  "movdqa     %%xmm0,(%1)                      \n"
  "lea        0x10(%1),%1                      \n"
  "psrlw      $0x8,%%xmm2                      \n"
  "psrlw      $0x8,%%xmm3                      \n"
  "packuswb   %%xmm3,%%xmm2                    \n"
  "movdqa     %%xmm2,(%2)                      \n"
  "lea        0x10(%2),%2                      \n"
  "sub        $0x10,%3                         \n"
  "ja         1b                               \n"
  : "+r"(src_uv),     // %0
    "+r"(dst_u),      // %1
    "+r"(dst_v),      // %2
    "+r"(pix)         // %3
  :
  : "memory", "cc"
#if defined(__SSE2__)
    , "xmm0", "xmm1", "xmm2", "xmm3", "xmm5"
#endif
);
}
#endif
#endif

static void SplitUV_C(const uint8* src_uv,
                      uint8* dst_u, uint8* dst_v, int pix) {
  // Copy a row of UV.
  for (int x = 0; x < pix; ++x) {
    dst_u[0] = src_uv[0];
    dst_v[0] = src_uv[1];
    src_uv += 2;
    dst_u += 1;
    dst_v += 1;
  }
}

static void I420CopyPlane(const uint8* src_y, int src_stride_y,
                          uint8* dst_y, int dst_stride_y,
                          int width, int height) {
  // Copy plane
  for (int y = 0; y < height; ++y) {
    memcpy(dst_y, src_y, width);
    src_y += src_stride_y;
    dst_y += dst_stride_y;
  }
}

// Copy I420 with optional flipping
int I420Copy(const uint8* src_y, int src_stride_y,
             const uint8* src_u, int src_stride_u,
             const uint8* src_v, int src_stride_v,
             uint8* dst_y, int dst_stride_y,
             uint8* dst_u, int dst_stride_u,
             uint8* dst_v, int dst_stride_v,
             int width, int height) {
  if (!src_y || !src_u || !src_v ||
      !dst_y || !dst_u || !dst_v ||
      width <= 0 || height == 0) {
    return -1;
  }

  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    int halfheight = (height + 1) >> 1;
    src_y = src_y + (height - 1) * src_stride_y;
    src_u = src_u + (halfheight - 1) * src_stride_u;
    src_v = src_v + (halfheight - 1) * src_stride_v;
    src_stride_y = -src_stride_y;
    src_stride_u = -src_stride_u;
    src_stride_v = -src_stride_v;
  }

  int halfwidth = (width + 1) >> 1;
  int halfheight = (height + 1) >> 1;
  I420CopyPlane(src_y, src_stride_y, dst_y, dst_stride_y, width, height);
  I420CopyPlane(src_u, src_stride_u, dst_u, dst_stride_u, halfwidth, halfheight);
  I420CopyPlane(src_v, src_stride_v, dst_v, dst_stride_v, halfwidth, halfheight);
  return 0;
}

int I420Mirror(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height) {
  if (src_y == NULL || src_u == NULL || src_v == NULL ||
      dst_y == NULL || dst_u == NULL || dst_v == NULL)
    return -1;

  // Only accepts positive dimensions
  if (height < 0 || width < 0 ||  src_stride_y < 0 || src_stride_u < 0 ||
      src_stride_v < 0 || dst_stride_y < 0 || dst_stride_u < 0 ||
      dst_stride_v < 0)
    return -1;

  int indO = 0;
  int indS  = 0;
  int wind, hind;
  uint8 tmp_val;
  // Will swap two values per iteration
  const int half_width = (width + 1) >> 1;

  // Y
  for (wind = 0; wind < half_width; ++wind) {
   for (hind = 0; hind < height; ++hind) {
     indO = hind * src_stride_y + wind;
     indS = hind * dst_stride_y + (width - wind - 1);
     tmp_val = src_y[indO];
     dst_y[indO] = src_y[indS];
     dst_y[indS] = tmp_val;
    }
  }

  const int half_height = (height + 1) >> 1;
  const int half_uv_width = (width + 1) >> 1;

  for (wind = 0; wind < half_uv_width; ++wind) {
   for (hind = 0; hind < half_height; ++hind) {
     // U
     indO = hind * dst_stride_u + wind;
     indS = hind * dst_stride_u + (half_uv_width - wind - 1);
     tmp_val = src_u[indO];
     dst_u[indO] = src_u[indS];
     dst_u[indS] = tmp_val;
     // V
     indO = hind * dst_stride_v + wind;
     indS = hind * dst_stride_v + (half_uv_width - wind - 1);
     tmp_val = src_v[indO];
     dst_v[indO] = src_v[indS];
     dst_v[indS] = tmp_val;
   }
  }
  return 0;
}

// SetRows32 writes 'count' bytes using a 32 bit value repeated

#if defined(__ARM_NEON__) && !defined(COVERAGE_ENABLED)
#define HAS_SETROW_NEON
static void SetRow32_NEON(uint8* dst, uint32 v32, int count) {
  asm volatile (
    "vdup.u32   q0, %2                         \n"  // duplicate 4 ints
    "1:                                        \n"
    "vst1.u32   {q0}, [%0]!                    \n"  // store
    "subs       %1, %1, #16                    \n"  // 16 processed per loop
    "bhi        1b                             \n"
  : "+r"(dst),  // %0
    "+r"(count) // %1
  : "r"(v32)    // %2
  : "q0", "memory", "cc"
  );
}

#elif defined(WIN32) && !defined(COVERAGE_ENABLED)
#define HAS_SETROW_SSE2
__declspec(naked)
static void SetRow32_SSE2(uint8* dst, uint32 v32, int count) {
  __asm {
    mov        eax, [esp + 4]    // dst
    movd       xmm5, [esp + 8]   // v32
    mov        ecx, [esp + 12]   // count
    pshufd     xmm5, xmm5, 0

  convertloop:
    movdqa     [eax], xmm5
    lea        eax, [eax + 16]
    sub        ecx, 16
    ja         convertloop
    ret
  }
}

#elif (defined(__x86_64__) || defined(__i386__)) && \
    !defined(COVERAGE_ENABLED) && !defined(TARGET_IPHONE_SIMULATOR)

#define HAS_SETROW_SSE2
static void SetRow32_SSE2(uint8* dst, uint32 v32, int count) {
  asm volatile (
  "movd       %2, %%xmm5                       \n"
  "pshufd     $0x0,%%xmm5,%%xmm5               \n"
"1:                                            \n"
  "movdqa     %%xmm5,(%0)                      \n"
  "lea        0x10(%0),%0                      \n"
  "sub        $0x10,%1                         \n"
  "ja         1b                               \n"
  : "+r"(dst),  // %0
    "+r"(count) // %1
  : "r"(v32)    // %2
  : "memory", "cc"
#if defined(__SSE2__)
    , "xmm5"
#endif
);
}
#endif

static void SetRow8_C(uint8* dst, uint32 v8, int count) {
  memset(dst, v8, count);
}

static void I420SetPlane(uint8* dst_y, int dst_stride_y,
                         int width, int height,
                         int value) {
  void (*SetRow)(uint8* dst, uint32 value, int pix);
#if defined(HAS_SETROW_NEON)
  if (TestCpuFlag(kCpuHasNEON) &&
      (width % 16 == 0) &&
      IS_ALIGNED(dst_y, 16) && (dst_stride_y % 16 == 0)) {
    SetRow = SetRow32_NEON;
  } else
#elif defined(HAS_SETROW_SSE2)
  if (TestCpuFlag(kCpuHasSSE2) &&
      (width % 16 == 0) &&
      IS_ALIGNED(dst_y, 16) && (dst_stride_y % 16 == 0)) {
    SetRow = SetRow32_SSE2;
  } else
#endif
  {
    SetRow = SetRow8_C;
  }

  uint32 v32 = value | (value << 8) | (value << 16) | (value << 24);
  // Set plane
  for (int y = 0; y < height; ++y) {
    SetRow(dst_y, v32, width);
    dst_y += dst_stride_y;
  }
}

// Draw a rectangle into I420
int I420Rect(uint8* dst_y, int dst_stride_y,
             uint8* dst_u, int dst_stride_u,
             uint8* dst_v, int dst_stride_v,
             int x, int y,
             int width, int height,
             int value_y, int value_u, int value_v) {
  if (!dst_y || !dst_u || !dst_v ||
      width <= 0 || height == 0 ||
      x < 0 || y < 0 ||
      value_y < 0 || value_y > 255 ||
      value_u < 0 || value_u > 255 ||
      value_v < 0 || value_v > 255) {
    return -1;
  }
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    int halfheight = (height + 1) >> 1;
    dst_y = dst_y + (height - 1) * dst_stride_y;
    dst_u = dst_u + (halfheight - 1) * dst_stride_u;
    dst_v = dst_v + (halfheight - 1) * dst_stride_v;
    dst_stride_y = -dst_stride_y;
    dst_stride_u = -dst_stride_u;
    dst_stride_v = -dst_stride_v;
  }

  int halfwidth = (width + 1) >> 1;
  int halfheight = (height + 1) >> 1;
  uint8* start_y = dst_y + y * dst_stride_y + x;
  uint8* start_u = dst_u + (y / 2) * dst_stride_u + (x / 2);
  uint8* start_v = dst_v + (y / 2) * dst_stride_v + (x / 2);

  I420SetPlane(start_y, dst_stride_y, width, height, value_y);
  I420SetPlane(start_u, dst_stride_u, halfwidth, halfheight, value_u);
  I420SetPlane(start_v, dst_stride_v, halfwidth, halfheight, value_v);
  return 0;
}

// Helper function to copy yuv data without scaling.  Used
// by our jpeg conversion callbacks to incrementally fill a yuv image.
int I422ToI420(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    src_y = src_y + (height - 1) * src_stride_y;
    src_u = src_u + (height - 1) * src_stride_u;
    src_v = src_v + (height - 1) * src_stride_v;
    src_stride_y = -src_stride_y;
    src_stride_u = -src_stride_u;
    src_stride_v = -src_stride_v;
  }

  // Copy Y plane
  I420CopyPlane(src_y, src_stride_y, dst_y, dst_stride_y, width, height);

  // SubSample UV planes.
  int x, y;
  int halfwidth = (width + 1) >> 1;
  for (y = 0; y < height; y += 2) {
    const uint8* u0 = src_u;
    const uint8* u1 = src_u + src_stride_u;
    if ((y + 1) >= height) {
      u1 = u0;
    }
    for (x = 0; x < halfwidth; ++x) {
      dst_u[x] = (u0[x] + u1[x] + 1) >> 1;
    }
    src_u += src_stride_u * 2;
    dst_u += dst_stride_u;
  }
  for (y = 0; y < height; y += 2) {
    const uint8* v0 = src_v;
    const uint8* v1 = src_v + src_stride_v;
    if ((y + 1) >= height) {
      v1 = v0;
    }
    for (x = 0; x < halfwidth; ++x) {
      dst_v[x] = (v0[x] + v1[x] + 1) >> 1;
    }
    src_v += src_stride_v * 2;
    dst_v += dst_stride_v;
  }
  return 0;
}

static void I420CopyPlane2(const uint8* src, int src_stride_0, int src_stride_1,
                           uint8* dst, int dst_stride_frame,
                           int width, int height) {
  // Copy plane
  for (int y = 0; y < height; y += 2) {
    memcpy(dst, src, width);
    src += src_stride_0;
    dst += dst_stride_frame;
    memcpy(dst, src, width);
    src += src_stride_1;
    dst += dst_stride_frame;
  }
}

// Support converting from FOURCC_M420
// Useful for bandwidth constrained transports like USB 1.0 and 2.0 and for
// easy conversion to I420.
// M420 format description:
// M420 is row biplanar 420: 2 rows of Y and 1 row of VU.
// Chroma is half width / half height. (420)
// src_stride_m420 is row planar.  Normally this will be the width in pixels.
//   The UV plane is half width, but 2 values, so src_stride_m420 applies to
//   this as well as the two Y planes.
static int X420ToI420(const uint8* src_y,
                      int src_stride_y0, int src_stride_y1,
                      const uint8* src_uv, int src_stride_uv,
                      uint8* dst_y, int dst_stride_y,
                      uint8* dst_u, int dst_stride_u,
                      uint8* dst_v, int dst_stride_v,
                      int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    int halfheight = (height + 1) >> 1;
    dst_y = dst_y + (height - 1) * dst_stride_y;
    dst_u = dst_u + (halfheight - 1) * dst_stride_u;
    dst_v = dst_v + (halfheight - 1) * dst_stride_v;
    dst_stride_y = -dst_stride_y;
    dst_stride_u = -dst_stride_u;
    dst_stride_v = -dst_stride_v;
  }

  int halfwidth = (width + 1) >> 1;
  void (*SplitUV)(const uint8* src_uv, uint8* dst_u, uint8* dst_v, int pix);
#if defined(HAS_SPLITUV_NEON)
  if (TestCpuFlag(kCpuHasNEON) &&
      (halfwidth % 16 == 0) &&
      IS_ALIGNED(src_uv, 16) && (src_stride_uv % 16 == 0) &&
      IS_ALIGNED(dst_u, 16) && (dst_stride_u % 16 == 0) &&
      IS_ALIGNED(dst_v, 16) && (dst_stride_v % 16 == 0)) {
    SplitUV = SplitUV_NEON;
  } else
#elif defined(HAS_SPLITUV_SSE2)
  if (TestCpuFlag(kCpuHasSSE2) &&
      (halfwidth % 16 == 0) &&
      IS_ALIGNED(src_uv, 16) && (src_stride_uv % 16 == 0) &&
      IS_ALIGNED(dst_u, 16) && (dst_stride_u % 16 == 0) &&
      IS_ALIGNED(dst_v, 16) && (dst_stride_v % 16 == 0)) {
    SplitUV = SplitUV_SSE2;
  } else
#endif
  {
    SplitUV = SplitUV_C;
  }

  I420CopyPlane2(src_y, src_stride_y0, src_stride_y1, dst_y, dst_stride_y,
                 width, height);

  int halfheight = (height + 1) >> 1;
  for (int y = 0; y < halfheight; ++y) {
    // Copy a row of UV.
    SplitUV(src_uv, dst_u, dst_v, halfwidth);
    dst_u += dst_stride_u;
    dst_v += dst_stride_v;
    src_uv += src_stride_uv;
  }
  return 0;
}

// Convert M420 to I420.
int M420ToI420(const uint8* src_m420, int src_stride_m420,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height) {
  return X420ToI420(src_m420, src_stride_m420, src_stride_m420 * 2,
                    src_m420 + src_stride_m420 * 2, src_stride_m420 * 3,
                    dst_y, dst_stride_y,
                    dst_u, dst_stride_u,
                    dst_v, dst_stride_v,
                    width, height);
}

// Convert NV12 to I420.
int NV12ToI420(const uint8* src_y, int src_stride_y,
               const uint8* src_uv, int src_stride_uv,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height) {
  return X420ToI420(src_y, src_stride_y, src_stride_y,
                    src_uv, src_stride_uv,
                    dst_y, dst_stride_y,
                    dst_u, dst_stride_u,
                    dst_v, dst_stride_v,
                    width, height);
}

// Convert NV12 to I420.  Deprecated.
int NV12ToI420(const uint8* src_y,
               const uint8* src_uv,
               int src_stride_frame,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height) {
  return X420ToI420(src_y, src_stride_frame, src_stride_frame,
                    src_uv, src_stride_frame,
                    dst_y, dst_stride_y,
                    dst_u, dst_stride_u,
                    dst_v, dst_stride_v,
                    width, height);
}

#if defined(WIN32) && !defined(COVERAGE_ENABLED)
#define HAS_SPLITYUY2_SSE2
__declspec(naked)
static void SplitYUY2_SSE2(const uint8* src_yuy2,
                           uint8* dst_y, uint8* dst_u, uint8* dst_v, int pix) {
  __asm {
    push       esi
    push       edi
    mov        eax, [esp + 8 + 4]    // src_yuy2
    mov        edx, [esp + 8 + 8]    // dst_y
    mov        esi, [esp + 8 + 12]   // dst_u
    mov        edi, [esp + 8 + 16]   // dst_v
    mov        ecx, [esp + 8 + 20]   // pix
    pcmpeqb    xmm5, xmm5            // generate mask 0x00ff00ff
    psrlw      xmm5, 8

  convertloop:
    movdqa     xmm0, [eax]
    movdqa     xmm1, [eax + 16]
    lea        eax,  [eax + 32]
    movdqa     xmm2, xmm0
    movdqa     xmm3, xmm1
    pand       xmm2, xmm5   // even bytes are Y
    pand       xmm3, xmm5
    packuswb   xmm2, xmm3
    movdqa     [edx], xmm2
    lea        edx, [edx + 16]
    psrlw      xmm0, 8      // YUYV -> UVUV
    psrlw      xmm1, 8
    packuswb   xmm0, xmm1
    movdqa     xmm1, xmm0
    pand       xmm0, xmm5  // U
    packuswb   xmm0, xmm0
    movq       qword ptr [esi], xmm0
    lea        esi, [esi + 8]
    psrlw      xmm1, 8     // V
    packuswb   xmm1, xmm1
    movq       qword ptr [edi], xmm1
    lea        edi, [edi + 8]
    sub        ecx, 16
    ja         convertloop

    pop        edi
    pop        esi
    ret
  }
}

#elif (defined(__x86_64__) || defined(__i386__)) && \
    !defined(COVERAGE_ENABLED) && !defined(TARGET_IPHONE_SIMULATOR)
#define HAS_SPLITYUY2_SSE2
static void SplitYUY2_SSE2(const uint8* src_yuy2, uint8* dst_y,
                           uint8* dst_u, uint8* dst_v, int pix) {
  asm volatile (
  "pcmpeqb    %%xmm5,%%xmm5                    \n"
  "psrlw      $0x8,%%xmm5                      \n"
"1:                                            \n"
  "movdqa     (%0),%%xmm0                      \n"
  "movdqa     0x10(%0),%%xmm1                  \n"
  "lea        0x20(%0),%0                      \n"
  "movdqa     %%xmm0,%%xmm2                    \n"
  "movdqa     %%xmm1,%%xmm3                    \n"
  "pand       %%xmm5,%%xmm2                    \n"
  "pand       %%xmm5,%%xmm3                    \n"
  "packuswb   %%xmm3,%%xmm2                    \n"
  "movdqa     %%xmm2,(%1)                      \n"
  "lea        0x10(%1),%1                      \n"
  "psrlw      $0x8,%%xmm0                      \n"
  "psrlw      $0x8,%%xmm1                      \n"
  "packuswb   %%xmm1,%%xmm0                    \n"
  "movdqa     %%xmm0,%%xmm1                    \n"
  "pand       %%xmm5,%%xmm0                    \n"
  "packuswb   %%xmm0,%%xmm0                    \n"
  "movq       %%xmm0,(%2)                      \n"
  "lea        0x8(%2),%2                       \n"
  "psrlw      $0x8,%%xmm1                      \n"
  "packuswb   %%xmm1,%%xmm1                    \n"
  "movq       %%xmm1,(%3)                      \n"
  "lea        0x8(%3),%3                       \n"
  "sub        $0x10,%4                         \n"
  "ja         1b                               \n"
  : "+r"(src_yuy2),    // %0
    "+r"(dst_y),       // %1
    "+r"(dst_u),       // %2
    "+r"(dst_v),       // %3
    "+r"(pix)          // %4
  :
  : "memory", "cc"
#if defined(__SSE2__)
    , "xmm0", "xmm1", "xmm2", "xmm3", "xmm5"
#endif
);
}
#endif

static void SplitYUY2_C(const uint8* src_yuy2,
                        uint8* dst_y, uint8* dst_u, uint8* dst_v, int pix) {
  // Copy a row of YUY2.
  for (int x = 0; x < pix; x += 2) {
    dst_y[0] = src_yuy2[0];
    dst_y[1] = src_yuy2[2];
    dst_u[0] = src_yuy2[1];
    dst_v[0] = src_yuy2[3];
    src_yuy2 += 4;
    dst_y += 2;
    dst_u += 1;
    dst_v += 1;
  }
}

// Convert Q420 to I420.
// Format is rows of YY/YUYV
int Q420ToI420(const uint8* src_y, int src_stride_y,
               const uint8* src_yuy2, int src_stride_yuy2,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    int halfheight = (height + 1) >> 1;
    dst_y = dst_y + (height - 1) * dst_stride_y;
    dst_u = dst_u + (halfheight - 1) * dst_stride_u;
    dst_v = dst_v + (halfheight - 1) * dst_stride_v;
    dst_stride_y = -dst_stride_y;
    dst_stride_u = -dst_stride_u;
    dst_stride_v = -dst_stride_v;
  }
  void (*SplitYUY2)(const uint8* src_yuy2,
                    uint8* dst_y, uint8* dst_u, uint8* dst_v, int pix);
#if defined(HAS_SPLITYUY2_SSE2)
  if (TestCpuFlag(kCpuHasSSE2) &&
      (width % 16 == 0) &&
      IS_ALIGNED(src_yuy2, 16) && (src_stride_yuy2 % 16 == 0) &&
      IS_ALIGNED(dst_y, 16) && (dst_stride_y % 16 == 0) &&
      IS_ALIGNED(dst_u, 8) && (dst_stride_u % 8 == 0) &&
      IS_ALIGNED(dst_v, 8) && (dst_stride_v % 8 == 0)) {
    SplitYUY2 = SplitYUY2_SSE2;
  } else
#endif
  {
    SplitYUY2 = SplitYUY2_C;
  }
  for (int y = 0; y < height; y += 2) {
    memcpy(dst_y, src_y, width);
    dst_y += dst_stride_y;
    src_y += src_stride_y;

    // Copy a row of YUY2.
    SplitYUY2(src_yuy2, dst_y, dst_u, dst_v, width);
    dst_y += dst_stride_y;
    dst_u += dst_stride_u;
    dst_v += dst_stride_v;
    src_yuy2 += src_stride_yuy2;
  }
  return 0;
}

#if defined(WIN32) && !defined(COVERAGE_ENABLED)
#define HAS_YUY2TOI420ROW_SSE2
__declspec(naked)
void YUY2ToI420RowY_SSE2(const uint8* src_yuy2,
                         uint8* dst_y, int pix) {
  __asm {
    mov        eax, [esp + 4]    // src_yuy2
    mov        edx, [esp + 8]    // dst_y
    mov        ecx, [esp + 12]   // pix
    pcmpeqb    xmm5, xmm5        // generate mask 0x00ff00ff
    psrlw      xmm5, 8

  convertloop:
    movdqa     xmm0, [eax]
    movdqa     xmm1, [eax + 16]
    lea        eax,  [eax + 32]
    pand       xmm0, xmm5   // even bytes are Y
    pand       xmm1, xmm5
    packuswb   xmm0, xmm1
    movdqa     [edx], xmm0
    lea        edx, [edx + 16]
    sub        ecx, 16
    ja         convertloop
    ret
  }
}

__declspec(naked)
void YUY2ToI420RowUV_SSE2(const uint8* src_yuy2, int stride_yuy2,
                          uint8* dst_u, uint8* dst_y, int pix) {
  __asm {
    push       esi
    push       edi
    mov        eax, [esp + 8 + 4]    // src_yuy2
    mov        esi, [esp + 8 + 8]    // stride_yuy2
    mov        edx, [esp + 8 + 12]   // dst_u
    mov        edi, [esp + 8 + 16]   // dst_v
    mov        ecx, [esp + 8 + 20]   // pix
    pcmpeqb    xmm5, xmm5            // generate mask 0x00ff00ff
    psrlw      xmm5, 8

  convertloop:
    movdqa     xmm0, [eax]
    movdqa     xmm1, [eax + 16]
    movdqa     xmm2, [eax + esi]
    movdqa     xmm3, [eax + esi + 16]
    lea        eax,  [eax + 32]
    pavgb      xmm0, xmm2
    pavgb      xmm1, xmm3
    psrlw      xmm0, 8      // YUYV -> UVUV
    psrlw      xmm1, 8
    packuswb   xmm0, xmm1
    movdqa     xmm1, xmm0
    pand       xmm0, xmm5  // U
    packuswb   xmm0, xmm0
    movq       qword ptr [edx], xmm0
    lea        edx, [edx + 8]
    psrlw      xmm1, 8     // V
    packuswb   xmm1, xmm1
    movq       qword ptr [edi], xmm1
    lea        edi, [edi + 8]
    sub        ecx, 16
    ja         convertloop

    pop        edi
    pop        esi
    ret
  }
}

#define HAS_UYVYTOI420ROW_SSE2
__declspec(naked)
void UYVYToI420RowY_SSE2(const uint8* src_uyvy,
                         uint8* dst_y, int pix) {
  __asm {
    mov        eax, [esp + 4]    // src_uyvy
    mov        edx, [esp + 8]    // dst_y
    mov        ecx, [esp + 12]   // pix

  convertloop:
    movdqa     xmm0, [eax]
    movdqa     xmm1, [eax + 16]
    lea        eax,  [eax + 32]
    psrlw      xmm0, 8    // odd bytes are Y
    psrlw      xmm1, 8
    packuswb   xmm0, xmm1
    movdqa     [edx], xmm0
    lea        edx, [edx + 16]
    sub        ecx, 16
    ja         convertloop
    ret
  }
}

__declspec(naked)
void UYVYToI420RowUV_SSE2(const uint8* src_uyvy, int stride_uyvy,
                          uint8* dst_u, uint8* dst_y, int pix) {
  __asm {
    push       esi
    push       edi
    mov        eax, [esp + 8 + 4]    // src_yuy2
    mov        esi, [esp + 8 + 8]    // stride_yuy2
    mov        edx, [esp + 8 + 12]   // dst_u
    mov        edi, [esp + 8 + 16]   // dst_v
    mov        ecx, [esp + 8 + 20]   // pix
    pcmpeqb    xmm5, xmm5            // generate mask 0x00ff00ff
    psrlw      xmm5, 8

  convertloop:
    movdqa     xmm0, [eax]
    movdqa     xmm1, [eax + 16]
    movdqa     xmm2, [eax + esi]
    movdqa     xmm3, [eax + esi + 16]
    lea        eax,  [eax + 32]
    pavgb      xmm0, xmm2
    pavgb      xmm1, xmm3
    pand       xmm0, xmm5   // UYVY -> UVUV
    pand       xmm1, xmm5
    packuswb   xmm0, xmm1
    movdqa     xmm1, xmm0
    pand       xmm0, xmm5  // U
    packuswb   xmm0, xmm0
    movq       qword ptr [edx], xmm0
    lea        edx, [edx + 8]
    psrlw      xmm1, 8     // V
    packuswb   xmm1, xmm1
    movq       qword ptr [edi], xmm1
    lea        edi, [edi + 8]
    sub        ecx, 16
    ja         convertloop

    pop        edi
    pop        esi
    ret
  }
}

#elif (defined(__x86_64__) || defined(__i386__)) && \
    !defined(COVERAGE_ENABLED) && !defined(TARGET_IPHONE_SIMULATOR)

#define HAS_YUY2TOI420ROW_SSE2
static void YUY2ToI420RowY_SSE2(const uint8* src_yuy2,
                                uint8* dst_y, int pix) {
  asm volatile (
  "pcmpeqb    %%xmm5,%%xmm5                    \n"
  "psrlw      $0x8,%%xmm5                      \n"
"1:                                            \n"
  "movdqa     (%0),%%xmm0                      \n"
  "movdqa     0x10(%0),%%xmm1                  \n"
  "lea        0x20(%0),%0                      \n"
  "pand       %%xmm5,%%xmm0                    \n"
  "pand       %%xmm5,%%xmm1                    \n"
  "packuswb   %%xmm1,%%xmm0                    \n"
  "movdqa     %%xmm0,(%1)                      \n"
  "lea        0x10(%1),%1                      \n"
  "sub        $0x10,%2                         \n"
  "ja         1b                               \n"
  : "+r"(src_yuy2),  // %0
    "+r"(dst_y),     // %1
    "+r"(pix)        // %2
  :
  : "memory", "cc"
#if defined(__SSE2__)
    , "xmm0", "xmm1", "xmm5"
#endif
);
}

static void YUY2ToI420RowUV_SSE2(const uint8* src_yuy2, int stride_yuy2,
                                 uint8* dst_u, uint8* dst_y, int pix) {
  asm volatile (
  "pcmpeqb    %%xmm5,%%xmm5                    \n"
  "psrlw      $0x8,%%xmm5                      \n"
"1:                                            \n"
  "movdqa     (%0),%%xmm0                      \n"
  "movdqa     0x10(%0),%%xmm1                  \n"
  "movdqa     (%0,%4,1),%%xmm2                 \n"
  "movdqa     0x10(%0,%4,1),%%xmm3             \n"
  "lea        0x20(%0),%0                      \n"
  "pavgb      %%xmm2,%%xmm0                    \n"
  "pavgb      %%xmm3,%%xmm1                    \n"
  "psrlw      $0x8,%%xmm0                      \n"
  "psrlw      $0x8,%%xmm1                      \n"
  "packuswb   %%xmm1,%%xmm0                    \n"
  "movdqa     %%xmm0,%%xmm1                    \n"
  "pand       %%xmm5,%%xmm0                    \n"
  "packuswb   %%xmm0,%%xmm0                    \n"
  "movq       %%xmm0,(%1)                      \n"
  "lea        0x8(%1),%1                       \n"
  "psrlw      $0x8,%%xmm1                      \n"
  "packuswb   %%xmm1,%%xmm1                    \n"
  "movq       %%xmm1,(%2)                      \n"
  "lea        0x8(%2),%2                       \n"
  "sub        $0x10,%3                         \n"
  "ja         1b                               \n"
  : "+r"(src_yuy2),    // %0
    "+r"(dst_u),       // %1
    "+r"(dst_y),       // %2
    "+r"(pix)          // %3
  : "r"(static_cast<intptr_t>(stride_yuy2))  // %4
  : "memory", "cc"
#if defined(__SSE2__)
    , "xmm0", "xmm1", "xmm2", "xmm3", "xmm5"
#endif
);
}
#define HAS_UYVYTOI420ROW_SSE2
static void UYVYToI420RowY_SSE2(const uint8* src_uyvy,
                                uint8* dst_y, int pix) {
  asm volatile (
"1:                                            \n"
  "movdqa     (%0),%%xmm0                      \n"
  "movdqa     0x10(%0),%%xmm1                  \n"
  "lea        0x20(%0),%0                      \n"
  "psrlw      $0x8,%%xmm0                      \n"
  "psrlw      $0x8,%%xmm1                      \n"
  "packuswb   %%xmm1,%%xmm0                    \n"
  "movdqa     %%xmm0,(%1)                      \n"
  "lea        0x10(%1),%1                      \n"
  "sub        $0x10,%2                         \n"
  "ja         1b                               \n"
  : "+r"(src_uyvy),  // %0
    "+r"(dst_y),     // %1
    "+r"(pix)        // %2
  :
  : "memory", "cc"
#if defined(__SSE2__)
    , "xmm0", "xmm1"
#endif
);
}

static void UYVYToI420RowUV_SSE2(const uint8* src_uyvy, int stride_uyvy,
                                 uint8* dst_u, uint8* dst_y, int pix) {
  asm volatile (
  "pcmpeqb    %%xmm5,%%xmm5                    \n"
  "psrlw      $0x8,%%xmm5                      \n"
"1:                                            \n"
  "movdqa     (%0),%%xmm0                      \n"
  "movdqa     0x10(%0),%%xmm1                  \n"
  "movdqa     (%0,%4,1),%%xmm2                 \n"
  "movdqa     0x10(%0,%4,1),%%xmm3             \n"
  "lea        0x20(%0),%0                      \n"
  "pavgb      %%xmm2,%%xmm0                    \n"
  "pavgb      %%xmm3,%%xmm1                    \n"
  "pand       %%xmm5,%%xmm0                    \n"
  "pand       %%xmm5,%%xmm1                    \n"
  "packuswb   %%xmm1,%%xmm0                    \n"
  "movdqa     %%xmm0,%%xmm1                    \n"
  "pand       %%xmm5,%%xmm0                    \n"
  "packuswb   %%xmm0,%%xmm0                    \n"
  "movq       %%xmm0,(%1)                      \n"
  "lea        0x8(%1),%1                       \n"
  "psrlw      $0x8,%%xmm1                      \n"
  "packuswb   %%xmm1,%%xmm1                    \n"
  "movq       %%xmm1,(%2)                      \n"
  "lea        0x8(%2),%2                       \n"
  "sub        $0x10,%3                         \n"
  "ja         1b                               \n"
  : "+r"(src_uyvy),    // %0
    "+r"(dst_u),       // %1
    "+r"(dst_y),       // %2
    "+r"(pix)          // %3
  : "r"(static_cast<intptr_t>(stride_uyvy))  // %4
  : "memory", "cc"
#if defined(__SSE2__)
    , "xmm0", "xmm1", "xmm2", "xmm3", "xmm5"
#endif
);
}
#endif

// Filter 2 rows of YUY2 UV's (422) into U and V (420)
void YUY2ToI420RowUV_C(const uint8* src_yuy2, int src_stride_yuy2,
                       uint8* dst_u, uint8* dst_v, int pix) {
  // Output a row of UV values, filtering 2 rows of YUY2
  for (int x = 0; x < pix; x += 2) {
    dst_u[0] = (src_yuy2[1] + src_yuy2[src_stride_yuy2 + 1] + 1) >> 1;
    dst_v[0] = (src_yuy2[3] + src_yuy2[src_stride_yuy2 + 3] + 1) >> 1;
    src_yuy2 += 4;
    dst_u += 1;
    dst_v += 1;
  }
}

void YUY2ToI420RowY_C(const uint8* src_yuy2,
                      uint8* dst_y, int pix) {
  // Copy a row of yuy2 Y values
  for (int x = 0; x < pix; ++x) {
    dst_y[0] = src_yuy2[0];
    src_yuy2 += 2;
    dst_y += 1;
  }
}

void UYVYToI420RowUV_C(const uint8* src_uyvy, int src_stride_uyvy,
                       uint8* dst_u, uint8* dst_v, int pix) {
  // Copy a row of uyvy UV values
  for (int x = 0; x < pix; x += 2) {
    dst_u[0] = (src_uyvy[0] + src_uyvy[src_stride_uyvy + 0] + 1) >> 1;
    dst_v[0] = (src_uyvy[2] + src_uyvy[src_stride_uyvy + 2] + 1) >> 1;
    src_uyvy += 4;
    dst_u += 1;
    dst_v += 1;
  }
}

void UYVYToI420RowY_C(const uint8* src_uyvy,
                      uint8* dst_y, int pix) {
  // Copy a row of uyvy Y values
  for (int x = 0; x < pix; ++x) {
    dst_y[0] = src_uyvy[1];
    src_uyvy += 2;
    dst_y += 1;
  }
}

// Convert YUY2 to I420.
int YUY2ToI420(const uint8* src_yuy2, int src_stride_yuy2,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    src_yuy2 = src_yuy2 + (height - 1) * src_stride_yuy2;
    src_stride_yuy2 = -src_stride_yuy2;
  }
  void (*YUY2ToI420RowUV)(const uint8* src_yuy2, int src_stride_yuy2,
                          uint8* dst_u, uint8* dst_v, int pix);
  void (*YUY2ToI420RowY)(const uint8* src_yuy2,
                         uint8* dst_y, int pix);
#if defined(HAS_YUY2TOI420ROW_SSE2)
  if (TestCpuFlag(kCpuHasSSE2) &&
      (width % 16 == 0) &&
      IS_ALIGNED(src_yuy2, 16) && (src_stride_yuy2 % 16 == 0) &&
      IS_ALIGNED(dst_y, 16) && (dst_stride_y % 16 == 0) &&
      IS_ALIGNED(dst_u, 8) && (dst_stride_u % 8 == 0) &&
      IS_ALIGNED(dst_v, 8) && (dst_stride_v % 8 == 0)) {
    YUY2ToI420RowY = YUY2ToI420RowY_SSE2;
    YUY2ToI420RowUV = YUY2ToI420RowUV_SSE2;
  } else
#endif
  {
    YUY2ToI420RowY = YUY2ToI420RowY_C;
    YUY2ToI420RowUV = YUY2ToI420RowUV_C;
  }
  for (int y = 0; y < height; ++y) {
    if ((y & 1) == 0) {
      if (y >= (height - 1) ) {  // last chroma on odd height clamp height
        src_stride_yuy2 = 0;
      }
      YUY2ToI420RowUV(src_yuy2, src_stride_yuy2, dst_u, dst_v, width);
      dst_u += dst_stride_u;
      dst_v += dst_stride_v;
    }
    YUY2ToI420RowY(src_yuy2, dst_y, width);
    dst_y += dst_stride_y;
    src_yuy2 += src_stride_yuy2;
  }
  return 0;
}

// Convert UYVY to I420.
int UYVYToI420(const uint8* src_uyvy, int src_stride_uyvy,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    src_uyvy = src_uyvy + (height - 1) * src_stride_uyvy;
    src_stride_uyvy = -src_stride_uyvy;
  }
  void (*UYVYToI420RowUV)(const uint8* src_uyvy, int src_stride_uyvy,
                          uint8* dst_u, uint8* dst_v, int pix);
  void (*UYVYToI420RowY)(const uint8* src_uyvy,
                         uint8* dst_y, int pix);
#if defined(HAS_UYVYTOI420ROW_SSE2)
  if (TestCpuFlag(kCpuHasSSE2) &&
      (width % 16 == 0) &&
      IS_ALIGNED(src_uyvy, 16) && (src_stride_uyvy % 16 == 0) &&
      IS_ALIGNED(dst_y, 16) && (dst_stride_y % 16 == 0) &&
      IS_ALIGNED(dst_u, 8) && (dst_stride_u % 8 == 0) &&
      IS_ALIGNED(dst_v, 8) && (dst_stride_v % 8 == 0)) {
    UYVYToI420RowY = UYVYToI420RowY_SSE2;
    UYVYToI420RowUV = UYVYToI420RowUV_SSE2;
  } else
#endif
  {
    UYVYToI420RowY = UYVYToI420RowY_C;
    UYVYToI420RowUV = UYVYToI420RowUV_C;
  }
  for (int y = 0; y < height; ++y) {
    if ((y & 1) == 0) {
      if (y >= (height - 1) ) {  // last chroma on odd height clamp height
        src_stride_uyvy = 0;
      }
      UYVYToI420RowUV(src_uyvy, src_stride_uyvy, dst_u, dst_v, width);
      dst_u += dst_stride_u;
      dst_v += dst_stride_v;
    }
    UYVYToI420RowY(src_uyvy, dst_y, width);
    dst_y += dst_stride_y;
    src_uyvy += src_stride_uyvy;
  }
  return 0;
}

// Convert I420 to ARGB.
int I420ToARGB(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    dst_argb = dst_argb + (height - 1) * dst_stride_argb;
    dst_stride_argb = -dst_stride_argb;
  }
  void (*FastConvertYUVToARGBRow)(const uint8* y_buf,
                                  const uint8* u_buf,
                                  const uint8* v_buf,
                                  uint8* rgb_buf,
                                  int width);
#if defined(HAS_FASTCONVERTYUVTOARGBROW_SSSE3)
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 8 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    FastConvertYUVToARGBRow = FastConvertYUVToARGBRow_SSSE3;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOARGBROW_SSE2)
#error foo
  if (TestCpuFlag(kCpuHasSSE2) &&
      (width % 4 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    FastConvertYUVToARGBRow = FastConvertYUVToARGBRow4_SSE2;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOARGBROW_SSE2)
  if (TestCpuFlag(kCpuHasSSE2) &&
      (width % 2 == 0)) {
    FastConvertYUVToARGBRow = FastConvertYUVToARGBRow_SSE2;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOARGBROW_MMX)
  if (width % 2 == 0) {
    FastConvertYUVToARGBRow = FastConvertYUVToARGBRow_MMX;
  } else
#endif
  {
    FastConvertYUVToARGBRow = FastConvertYUVToARGBRow_C;
  }
  for (int y = 0; y < height; ++y) {
    FastConvertYUVToARGBRow(src_y, src_u, src_v, dst_argb, width);
    dst_argb += dst_stride_argb;
    src_y += src_stride_y;
    if (y & 1) {
      src_u += src_stride_u;
      src_v += src_stride_v;
    }
  }
  // MMX used for FastConvertYUVToARGBRow requires an emms instruction.
  EMMS(); 
  return 0;
}
  
#include "jfr_planar_functions.cc"   
 
// Convert I420 to BGRA.
int I420ToBGRA(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {  
    height = -height; 
    dst_argb = dst_argb + (height - 1) * dst_stride_argb; 
    dst_stride_argb = -dst_stride_argb; 
  }
  void (*FastConvertYUVToBGRARow)(const uint8* y_buf, 
                                   const uint8* u_buf, 
                                   const uint8* v_buf, 
                                   uint8* rgb_buf,
                                   int width);
#if defined(HAS_FASTCONVERTYUVTOBGRAROW_SSSE3)
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 8 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    FastConvertYUVToBGRARow = FastConvertYUVToBGRARow_SSSE3;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOBGRAROW_SSE2)
  if (TestCpuFlag(kCpuHasSSE2) &&
      (width % 2 == 0)) {
    FastConvertYUVToBGRARow = FastConvertYUVToBGRARow_SSE2;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOBGRAROW_MMX)
  if (width % 2 == 0) {
    FastConvertYUVToBGRARow = FastConvertYUVToBGRARow_MMX;
  } else
#endif
  {
    FastConvertYUVToBGRARow = FastConvertYUVToBGRARow_C;
  }
  for (int y = 0; y < height; ++y) {
    FastConvertYUVToBGRARow(src_y, src_u, src_v, dst_argb, width);
    dst_argb += dst_stride_argb;
    src_y += src_stride_y;
    if (y & 1) {
      src_u += src_stride_u;
      src_v += src_stride_v;
    }
  }
  EMMS();
  return 0;
}

// Convert I420 to ABGR.
int I420ToABGR(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    dst_argb = dst_argb + (height - 1) * dst_stride_argb;
    dst_stride_argb = -dst_stride_argb;
  }
  void (*FastConvertYUVToABGRRow)(const uint8* y_buf,
                                   const uint8* u_buf,
                                   const uint8* v_buf,
                                   uint8* rgb_buf,
                                   int width);
#if defined(HAS_FASTCONVERTYUVTOABGRROW_SSSE3)
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 8 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    FastConvertYUVToABGRRow = FastConvertYUVToABGRRow_SSSE3;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOABGRROW_SSE2)
  if (TestCpuFlag(kCpuHasSSE2) &&
      (width % 2 == 0)) {
    FastConvertYUVToABGRRow = FastConvertYUVToABGRRow_SSE2;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOABGRROW_MMX)
  if (width % 2 == 0) {
    FastConvertYUVToABGRRow = FastConvertYUVToABGRRow_MMX;
  } else
#endif
  {
    FastConvertYUVToABGRRow = FastConvertYUVToABGRRow_C;
  }
  for (int y = 0; y < height; ++y) {
    FastConvertYUVToABGRRow(src_y, src_u, src_v, dst_argb, width);
    dst_argb += dst_stride_argb;
    src_y += src_stride_y;
    if (y & 1) {
      src_u += src_stride_u;
      src_v += src_stride_v;
    }
  }
  EMMS();
  return 0;
}

// Convert I422 to ARGB.
int I422ToARGB(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    dst_argb = dst_argb + (height - 1) * dst_stride_argb;
    dst_stride_argb = -dst_stride_argb;
  }
  void (*FastConvertYUVToARGBRow)(const uint8* y_buf,
                                   const uint8* u_buf,
                                   const uint8* v_buf,
                                   uint8* rgb_buf,
                                   int width);
#if defined(HAS_FASTCONVERTYUVTOARGBROW_SSSE3)
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 8 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    FastConvertYUVToARGBRow = FastConvertYUVToARGBRow_SSSE3;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOARGBROW_SSE2)
  if (TestCpuFlag(kCpuHasSSE2) &&
      (width % 2 == 0)) {
    FastConvertYUVToARGBRow = FastConvertYUVToARGBRow_SSE2;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOARGBROW_MMX)
  if (width % 2 == 0) {
    FastConvertYUVToARGBRow = FastConvertYUVToARGBRow_MMX;
  } else
#endif
  {
    FastConvertYUVToARGBRow = FastConvertYUVToARGBRow_C;
  }
  for (int y = 0; y < height; ++y) {
    FastConvertYUVToARGBRow(src_y, src_u, src_v, dst_argb, width);
    dst_argb += dst_stride_argb;
    src_y += src_stride_y;
    src_u += src_stride_u;
    src_v += src_stride_v;
  }
  // MMX used for FastConvertYUVToARGBRow requires an emms instruction.
  EMMS();
  return 0;
}

// Convert I444 to ARGB.
int I444ToARGB(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    dst_argb = dst_argb + (height - 1) * dst_stride_argb;
    dst_stride_argb = -dst_stride_argb;
  }
  void (*FastConvertYUV444ToARGBRow)(const uint8* y_buf,
                                      const uint8* u_buf,
                                      const uint8* v_buf,
                                      uint8* rgb_buf,
                                      int width);
#if defined(HAS_FASTCONVERTYUV444TOARGBROW_SSSE3)
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 8 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    FastConvertYUV444ToARGBRow = FastConvertYUV444ToARGBRow_SSSE3;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOARGBROW_SSE2)
  if (TestCpuFlag(kCpuHasSSE2)) {
    FastConvertYUV444ToARGBRow = FastConvertYUV444ToARGBRow_SSE2;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOARGBROW_MMX)
    FastConvertYUV444ToARGBRow = FastConvertYUV444ToARGBRow_MMX;
#else
  {
    FastConvertYUV444ToARGBRow = FastConvertYUV444ToARGBRow_C;
  }
#endif
  for (int y = 0; y < height; ++y) {
    FastConvertYUV444ToARGBRow(src_y, src_u, src_v, dst_argb, width);
    dst_argb += dst_stride_argb;
    src_y += src_stride_y;
    src_u += src_stride_u;
    src_v += src_stride_v;
  }
  // MMX used for FastConvertYUVToARGBRow requires an emms instruction.
  EMMS();
  return 0;
}

// Convert I400 to ARGB.
int I400ToARGB_Reference(const uint8* src_y, int src_stride_y,
                         uint8* dst_argb, int dst_stride_argb,
                         int width, int height) {
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    dst_argb = dst_argb + (height - 1) * dst_stride_argb;
    dst_stride_argb = -dst_stride_argb;
  }
  void (*FastConvertYToARGBRow)(const uint8* y_buf,
                                 uint8* rgb_buf,
                                 int width);
#if defined(HAS_FASTCONVERTYTOARGBROW_SSE2)
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 8 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    FastConvertYToARGBRow = FastConvertYToARGBRow_SSE2;
  } else
#endif
#if defined(HAS_FASTCONVERTYUVTOARGBROW_MMX)
  if (width % 2 == 0) {
    FastConvertYToARGBRow = FastConvertYToARGBRow_MMX;
  } else
#endif
  {
    FastConvertYToARGBRow = FastConvertYToARGBRow_C;
  }
  for (int y = 0; y < height; ++y) {
    FastConvertYToARGBRow(src_y, dst_argb, width);
    dst_argb += dst_stride_argb;
    src_y += src_stride_y;
  }
  // MMX used for FastConvertYUVToARGBRow requires an emms instruction.
  EMMS();
  return 0;
}

// Convert I400 to ARGB.
int I400ToARGB(const uint8* src_y, int src_stride_y,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height) {
  if (height < 0) {
    height = -height;
    src_y = src_y + (height - 1) * src_stride_y;
    src_stride_y = -src_stride_y;
  }
  void (*I400ToARGBRow)(const uint8* src_y, uint8* dst_argb, int pix);
#if defined(HAS_I400TOARGBROW_SSE2)
  if (TestCpuFlag(kCpuHasSSE2) &&
      (width % 8 == 0) &&
      IS_ALIGNED(src_y, 8) && (src_stride_y % 8 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    I400ToARGBRow = I400ToARGBRow_SSE2;
  } else
#endif
  {
    I400ToARGBRow = I400ToARGBRow_C;
  }

  for (int y = 0; y < height; ++y) {
    I400ToARGBRow(src_y, dst_argb, width);
    src_y += src_stride_y;
    dst_argb += dst_stride_argb;
  }
  return 0;
}

int ABGRToARGB(const uint8* src_abgr, int src_stride_abgr,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height) {
  if (height < 0) {
    height = -height;
    src_abgr = src_abgr + (height - 1) * src_stride_abgr;
    src_stride_abgr = -src_stride_abgr;
  }
void (*ABGRToARGBRow)(const uint8* src_abgr, uint8* dst_argb, int pix);
#if defined(HAS_ABGRTOARGBROW_SSSE3)
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 4 == 0) &&
      IS_ALIGNED(src_abgr, 16) && (src_stride_abgr % 16 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    ABGRToARGBRow = ABGRToARGBRow_SSSE3;
  } else
#endif
  {
    ABGRToARGBRow = ABGRToARGBRow_C;
  }

  for (int y = 0; y < height; ++y) {
    ABGRToARGBRow(src_abgr, dst_argb, width);
    src_abgr += src_stride_abgr;
    dst_argb += dst_stride_argb;
  }
  return 0;
}

// Convert BGRA to ARGB.
int BGRAToARGB(const uint8* src_bgra, int src_stride_bgra,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height) {
  if (height < 0) {
    height = -height;
    src_bgra = src_bgra + (height - 1) * src_stride_bgra;
    src_stride_bgra = -src_stride_bgra;
  }
  void (*BGRAToARGBRow)(const uint8* src_bgra, uint8* dst_argb, int pix);
#if defined(HAS_BGRATOARGBROW_SSSE3)
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 4 == 0) &&
      IS_ALIGNED(src_bgra, 16) && (src_stride_bgra % 16 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    BGRAToARGBRow = BGRAToARGBRow_SSSE3;
  } else
#endif
  {
    BGRAToARGBRow = BGRAToARGBRow_C;
  }

  for (int y = 0; y < height; ++y) {
    BGRAToARGBRow(src_bgra, dst_argb, width);
    src_bgra += src_stride_bgra;
    dst_argb += dst_stride_argb;
  }
  return 0;
}

// Convert ARGB to I400.
int ARGBToI400(const uint8* src_argb, int src_stride_argb,
               uint8* dst_y, int dst_stride_y,
               int width, int height) {
  if (height < 0) {
    height = -height;
    src_argb = src_argb + (height - 1) * src_stride_argb;
    src_stride_argb = -src_stride_argb;
  }
void (*ARGBToYRow)(const uint8* src_argb, uint8* dst_y, int pix);
#if defined(HAS_ARGBTOYROW_SSSE3) 
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 4 == 0) &&
      IS_ALIGNED(src_argb, 16) && (src_stride_argb % 16 == 0) &&
      IS_ALIGNED(dst_y, 16) && (dst_stride_y % 16 == 0)) {
    ARGBToYRow = ARGBToYRow_SSSE3;
  } else
#endif
  {
    ARGBToYRow = ARGBToYRow_C;
  }

  for (int y = 0; y < height; ++y) {
    ARGBToYRow(src_argb, dst_y, width);
    src_argb += src_stride_argb;
    dst_y += dst_stride_y;
  }
  return 0;
}


// Convert RAW to ARGB.
int RAWToARGB(const uint8* src_raw, int src_stride_raw,
              uint8* dst_argb, int dst_stride_argb,
              int width, int height) {
  if (height < 0) {
    height = -height;
    src_raw = src_raw + (height - 1) * src_stride_raw;
    src_stride_raw = -src_stride_raw;
  }
  void (*RAWToARGBRow)(const uint8* src_raw, uint8* dst_argb, int pix);
#if defined(HAS_RAWTOARGBROW_SSSE3)
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 16 == 0) &&
      IS_ALIGNED(src_raw, 16) && (src_stride_raw % 16 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    RAWToARGBRow = RAWToARGBRow_SSSE3;
  } else
#endif
  {
    RAWToARGBRow = RAWToARGBRow_C;
  }

  for (int y = 0; y < height; ++y) {
    RAWToARGBRow(src_raw, dst_argb, width);
    src_raw += src_stride_raw;
    dst_argb += dst_stride_argb;
  }
  return 0;
}

// Convert BG24 to ARGB.
int BG24ToARGB(const uint8* src_bg24, int src_stride_bg24,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height) {
  if (height < 0) {
    height = -height;
    src_bg24 = src_bg24 + (height - 1) * src_stride_bg24;
    src_stride_bg24 = -src_stride_bg24;
  }
  void (*BG24ToARGBRow)(const uint8* src_bg24, uint8* dst_argb, int pix);
#if defined(HAS_BG24TOARGBROW_SSSE3)
  if (TestCpuFlag(kCpuHasSSSE3) &&
      (width % 16 == 0) &&
      IS_ALIGNED(src_bg24, 16) && (src_stride_bg24 % 16 == 0) &&
      IS_ALIGNED(dst_argb, 16) && (dst_stride_argb % 16 == 0)) {
    BG24ToARGBRow = BG24ToARGBRow_SSSE3;
  } else
#endif
  {
    BG24ToARGBRow = BG24ToARGBRow_C;
  }

  for (int y = 0; y < height; ++y) {
    BG24ToARGBRow(src_bg24, dst_argb, width);
    src_bg24 += src_stride_bg24;
    dst_argb += dst_stride_argb;
  }
  return 0;
}

}  // namespace libyuv

