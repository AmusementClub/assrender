#ifndef _RENDER_H_
#define _RENDER_H_

#include "assrender.h"

#define _r(c) (( (c) >> 24))
#define _g(c) ((((c) >> 16) & 0xFF))
#define _b(c) ((((c) >> 8)  & 0xFF))
#define _a(c) (( (c)        & 0xFF))

#define div256(x)   (((x + 128)   >> 8))
#define div65536(x) (((x + 32768) >> 16))
#define div255(x)   ((div256(x + div256(x))))
#define div65535(x) ((div65536(x + div65536(x))))

#define blend(srcA, srcC, dstC) \
    ((div255(srcA * srcC + (255 - srcA) * dstC)))
#define blend2(src1A, src1C, src2A, src2C, dstC) \
    ((div255(((src1A * src1C + src2A * src2C + (510 - src1A - src2A) * dstC + 1) >> 1))))
#define blend4(src1A, src1C, src2A, src2C, src3A, src3C, src4A, src4C, dstC) \
    ((div255(((src1A * src1C + src2A * src2C + src3A * src3C + src4A * src4C + (1020 - src1A - src2A - src3A - src4A) * dstC + 2) >> 2))))
#define scale(srcA, srcC, dstC) \
    ((srcA * srcC + (255 - srcA) * dstC))
#define dblend(srcA, srcC, dstA, dstC, outA) \
    (((srcA * srcC * 255 + dstA * dstC * (255 - srcA) + (outA >> 1)) / outA))

void FillMatrix(ConversionMatrix* matrix, matrix_type mt);

void make_sub_img(ASS_Image* img, uint8_t** sub_img, uint32_t width, int bits_per_pixel, int rgb, ConversionMatrix *mx);
void make_sub_img16(ASS_Image* img, uint8_t** sub_img, uint32_t width, int bits_per_pixel, int rgb, ConversionMatrix* mx);

void apply_bgr_(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_rgba(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_rgb(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_rgb48(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_rgb64(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_yuy2(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_yv12(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_yv16(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_yv24(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_y8(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_yuv420(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_yuv422(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_yuv444(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_y(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);
void apply_yv411(uint8_t** sub_img, uint8_t** data, int32_t* pitch, uint32_t width, uint32_t height);

const VSFrameRef* VS_CC assrender_get_frame_vs(int n, int activationReason, void** instanceData, void** frameData, VSFrameContext* frameCtx, VSCore* core, const VSAPI* vsapi);

#endif
