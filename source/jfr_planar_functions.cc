//FIXME: Undefine these
#define RGB565_w 2
#define ARGB1555_w 2
#define ARGB4444_w 2
#define ARGB_w 4

#define I420To___(name) \
  int I420To ## name ## _(const uint8* src_y, int src_stride_y, \
                         const uint8* src_u, int src_stride_u, \
                         const uint8* src_v, int src_stride_v, \
                         uint8* dst_rgb, int dst_stride_rgb, \
                         int width, int height) { \
    /* Negative height means invert the image. */ \
    if (height < 0) { \
      height = -height; \
      dst_rgb = dst_rgb + (height - 1) * dst_stride_rgb; \
      dst_stride_rgb = -dst_stride_rgb; \
    } \
    void (*FastConvertYUVTo ## name ## Row)(const uint8* y_buf, \
                                            const uint8* u_buf, \
                                            const uint8* v_buf, \
                                            uint8* rgb_buf, \
                                            int width); \
    /* FIXME: Consider requirements for speedup */ \
    if (width % name ## _w == 0) { \
      FastConvertYUVTo ## name ## Row = FastConvertYUVTo ## name ## Row_MMX; \
    } else { \
      /* FIXME: FastConvertYUVTo ## name ## Row = FastConvertYUVTo ## name ## Row_C; */ \
    } \
    for (int y = 0; y < height; ++y) { \
      FastConvertYUVTo ## name ## Row(src_y, src_u, src_v, dst_rgb, width); \
      dst_rgb += dst_stride_rgb; \
      src_y += src_stride_y; \
      if (y & 1) { \
        src_u += src_stride_u; \
        src_v += src_stride_v; \
      } \
    } \
    /* MMX used for FastConvertYUVTo___Row requires an emms instruction. */ \
    /* FIXME: Only clear in MMX path */ \
    asm("emms"); \
    return 0; \
  }

I420To___(RGB565)
I420To___(ARGB1555)
I420To___(ARGB4444)
I420To___(ARGB)

#undef I420To___
