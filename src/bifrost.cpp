#include <stdint.h>
//#include <stdio.h>
#include <windows.h>
#include "avisynth.h"

enum BlendDirection { bdNext, bdPrev, bdBoth };

class Bifrost : public GenericVideoFilter {
   int variation;
   int offset;
   float scenelumathresh;
   bool interlaced, conservativemask;
   int block_width, block_height, block_width_uv, block_height_uv;
   int blocks_x, blocks_y;
   float relativeframediff;
   PClip child2;

   public:
      Bifrost(PClip _child, PClip _child2, float _scenelumathresh, int _variation, bool _conservativemask, bool _interlaced, int _block_width, int _block_height, IScriptEnvironment* env)
      : GenericVideoFilter(_child)
      , child2(_child2)
      , scenelumathresh(_scenelumathresh)
      , variation(_variation)
      , conservativemask(_conservativemask)
      , block_width(_block_width)
      , block_height(_block_height) {

         relativeframediff  = 1.2f;

         VideoInfo vi2 = child2->GetVideoInfo();

         if ((!vi.IsYV12()) || (!vi2.IsYV12())) {
            env->ThrowError("Bifrost 2.0: YV12 video required.");
         }

         if (vi.width != vi2.width || vi.height != vi2.height || vi.num_frames != vi2.num_frames) {
            env->ThrowError("Bifrost 2.0: The two clips must have the same dimensions and length.");
         }

         if (block_width % 2 || block_height % 2) {
            env->ThrowError("Bifrost 2.0: The block dimensions must be multiples of 2, due to the chroma subsampling.");
         }

         offset = _interlaced ? 2 : 1;

         block_width_uv = block_width >> 1;
         block_height_uv = block_height >> 1;

         blocks_x = vi.width / block_width;
         blocks_y = vi.height / block_height;
      }

      PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
};


void applyBlockRainbowMask(const uint8_t *srcp_u, const uint8_t *srcp_v,
                           const uint8_t *srcc_u, const uint8_t *srcc_v,
                           const uint8_t *srcn_u, const uint8_t *srcn_v,
                           uint8_t *dst_u, uint8_t *dst_v,
                           int block_width_uv, int block_height_uv,
                           int srcp_stride_uv, int srcc_stride_uv, int srcn_stride_uv, int dst_stride_uv,
                           BlendDirection blenddirection) {

   for (int y = 0; y < block_height_uv; y++) {
      if (blenddirection == bdNext) {
         for (int x = 0; x < block_width_uv; x++) {
            if (dst_v[x]) {
               dst_u[x] = (srcc_u[x]+srcn_u[x]+1) >> 1;
               dst_v[x] = (srcc_v[x]+srcn_v[x]+1) >> 1;
            } else {
               dst_u[x] = srcc_u[x];
               dst_v[x] = srcc_v[x];
            }
         }
      } else if (blenddirection == bdPrev) {
         for (int x = 0; x < block_width_uv; x++) {
            if (dst_v[x]) {
               dst_u[x] = (srcc_u[x]+srcp_u[x]+1) >> 1;
               dst_v[x] = (srcc_v[x]+srcp_v[x]+1) >> 1;
            } else {
               dst_u[x] = srcc_u[x];
               dst_v[x] = srcc_v[x];
            }
         }
      } else if (blenddirection == bdBoth) {
         for (int x = 0; x < block_width_uv; x++) {
            if (dst_v[x]) {
               dst_u[x] = (2*srcc_u[x]+srcp_u[x]+srcn_u[x]+3) >> 2;
               dst_v[x] = (2*srcc_v[x]+srcp_v[x]+srcn_v[x]+3) >> 2;
            } else {
               dst_u[x] = srcc_u[x];
               dst_v[x] = srcc_v[x];
            }
         }
      }

      srcp_u += srcp_stride_uv;
      srcp_v += srcp_stride_uv;

      srcc_u += srcc_stride_uv;
      srcc_v += srcc_stride_uv;

      srcn_u += srcn_stride_uv;
      srcn_v += srcn_stride_uv;

      dst_u += dst_stride_uv;
      dst_v += dst_stride_uv;
   }
}


void processBlockRainbowMask(uint8_t *dst_u, uint8_t *dst_v,
                             int block_width_uv, int block_height_uv, int dst_stride_uv, bool conservativemask) {

   // Maybe needed later.
   uint8_t *tmp = dst_v;

   //denoise mask, remove marked pixels with no horizontal marked neighbors
   for (int y = 0; y < block_height_uv; y++) {
      dst_v[0] = dst_u[0] && dst_u[1];
      for (int x = 1; x < block_width_uv - 1; x++) {
         dst_v[x] = dst_u[x] && (dst_u[x-1] || dst_u[x+1]);
      }
      dst_v[block_width_uv - 1] = dst_u[block_width_uv - 1] && dst_u[block_width_uv - 2];

      dst_u += dst_stride_uv;
      dst_v += dst_stride_uv;
   }

   //expand mask vertically
   if (!conservativemask) {
      dst_v = tmp;

      for (int x = 0; x < block_width_uv; x++) {
         dst_v[x] = dst_v[x] || dst_v[x + dst_stride_uv];
      }

      dst_v += dst_stride_uv;

      for (int y = 1; y < block_height_uv - 1; y++) {
         for (int x = 0; x < block_width_uv; x++) {
            dst_v[x] = dst_v[x] || (dst_v[x + dst_stride_uv] && dst_v[x - dst_stride_uv]);
         }

         dst_v += dst_stride_uv;
      }

      for (int x = 0; x < block_width_uv; x++) {
         dst_v[x] = dst_v[x] || dst_v[x - dst_stride_uv];
      }
   }
}


void makeBlockRainbowMask(const uint8_t *srcp_u, const uint8_t *srcp_v,
                          const uint8_t *srcc_u, const uint8_t *srcc_v,
                          const uint8_t *srcn_u, const uint8_t *srcn_v,
                          uint8_t *dst_u, uint8_t *dst_v,
                          int block_width_uv, int block_height_uv,
                          int srcp_stride_uv, int srcc_stride_uv, int srcn_stride_uv, int dst_stride_uv,
                          int variation) {

   for (int y = 0; y < block_height_uv; y++) {
      for (int x = 0; x < block_width_uv; x++) {
         uint8_t up = srcp_u[x];
         uint8_t uc = srcc_u[x];
         uint8_t un = srcn_u[x];

         uint8_t vp = srcp_v[x];
         uint8_t vc = srcc_v[x];
         uint8_t vn = srcn_v[x];

         int ucup = uc-up;
         int ucun = uc-un;

         int vcvp = vc-vp;
         int vcvn = vc-vn;

         dst_u[x] = ((( ucup+variation) & ( ucun+variation)) < 0)
                 || (((-ucup+variation) & (-ucun+variation)) < 0)
                 || ((( vcvp+variation) & ( vcvn+variation)) < 0)
                 || (((-vcvp+variation) & (-vcvn+variation)) < 0);
      }

      srcp_u += srcp_stride_uv;
      srcp_v += srcp_stride_uv;

      srcc_u += srcc_stride_uv;
      srcc_v += srcc_stride_uv;

      srcn_u += srcn_stride_uv;
      srcn_v += srcn_stride_uv;

      dst_u += dst_stride_uv;
      dst_v += dst_stride_uv;
   }
}


void copyChromaBlock(uint8_t *dst_u, uint8_t *dst_v,
                     const uint8_t *src_u, const uint8_t *src_v,
                     int block_width_uv, int block_height_uv,
                     int dst_stride_uv, int src_stride_uv) {

   for (int y = 0; y < block_height_uv; y++) {
      memcpy(dst_u, src_u, block_width_uv);
      memcpy(dst_v, src_v, block_width_uv);

      dst_u += dst_stride_uv;
      dst_v += dst_stride_uv;
      src_u += src_stride_uv;
      src_v += src_stride_uv;
   }
}


float blockLumaDiff(const uint8_t *src1_y, const uint8_t *src2_y, int block_width, int block_height, int src1_stride_y, int src2_stride_y) {
   int diff = 0;

   for (int y = 0; y < block_height; y++) {
      for (int x = 0; x < block_width; x++) {
         diff += abs(src1_y[x] - src2_y[x]);
      }

      src1_y += src1_stride_y;
      src2_y += src2_stride_y;
   }

   return (float)diff / (block_width * block_height);
}


PVideoFrame __stdcall Bifrost::GetFrame(int n, IScriptEnvironment* env) {
   PVideoFrame srcpp = child->GetFrame(max(n - offset * 2, 0), env);
   PVideoFrame srcp = child->GetFrame(max(n - offset, 0), env);
   PVideoFrame srcc = child->GetFrame(n, env);
   PVideoFrame srcn = child->GetFrame(min(n + offset, vi.num_frames - 1), env);
   PVideoFrame srcnn = child->GetFrame(min(n + offset * 2, vi.num_frames - 1), env);

   PVideoFrame altsrcc = child2->GetFrame(n, env);

   PVideoFrame dst = env->NewVideoFrame(vi);

   const uint8_t *srcpp_y = srcpp->GetReadPtr(PLANAR_Y);
   const uint8_t *srcpp_u = srcpp->GetReadPtr(PLANAR_U);
   const uint8_t *srcpp_v = srcpp->GetReadPtr(PLANAR_V);

   const uint8_t *srcp_y = srcp->GetReadPtr(PLANAR_Y);
   const uint8_t *srcp_u = srcp->GetReadPtr(PLANAR_U);
   const uint8_t *srcp_v = srcp->GetReadPtr(PLANAR_V);

   const uint8_t *srcc_y = srcc->GetReadPtr(PLANAR_Y);
   const uint8_t *srcc_u = srcc->GetReadPtr(PLANAR_U);
   const uint8_t *srcc_v = srcc->GetReadPtr(PLANAR_V);

   const uint8_t *srcn_y = srcn->GetReadPtr(PLANAR_Y);
   const uint8_t *srcn_u = srcn->GetReadPtr(PLANAR_U);
   const uint8_t *srcn_v = srcn->GetReadPtr(PLANAR_V);

   const uint8_t *srcnn_y = srcnn->GetReadPtr(PLANAR_Y);
   const uint8_t *srcnn_u = srcnn->GetReadPtr(PLANAR_U);
   const uint8_t *srcnn_v = srcnn->GetReadPtr(PLANAR_V);

   const uint8_t *altsrcc_u = altsrcc->GetReadPtr(PLANAR_U);
   const uint8_t *altsrcc_v = altsrcc->GetReadPtr(PLANAR_V);

   uint8_t *dst_y = dst->GetWritePtr(PLANAR_Y);
   uint8_t *dst_u = dst->GetWritePtr(PLANAR_U);
   uint8_t *dst_v = dst->GetWritePtr(PLANAR_V);

   int srcpp_pitch_y = srcpp->GetPitch(PLANAR_Y);
   int srcpp_pitch_uv = srcpp->GetPitch(PLANAR_U);

   int srcp_pitch_y = srcp->GetPitch(PLANAR_Y);
   int srcp_pitch_uv = srcp->GetPitch(PLANAR_U);

   int srcc_pitch_y = srcc->GetPitch(PLANAR_Y);
   int srcc_pitch_uv = srcc->GetPitch(PLANAR_U);

   int srcn_pitch_y = srcn->GetPitch(PLANAR_Y);
   int srcn_pitch_uv = srcn->GetPitch(PLANAR_U);

   int srcnn_pitch_y = srcnn->GetPitch(PLANAR_Y);
   int srcnn_pitch_uv = srcnn->GetPitch(PLANAR_U);

   int altsrcc_pitch_uv = srcc->GetPitch(PLANAR_U);

   int dst_pitch_y = dst->GetPitch(PLANAR_Y);
   int dst_pitch_uv = dst->GetPitch(PLANAR_U);

   int rowsize_y = srcc->GetRowSize(PLANAR_Y);
   int height_y = srcc->GetHeight(PLANAR_Y);

   env->BitBlt(dst_y, dst_pitch_y, srcc_y, srcc_pitch_y, rowsize_y, height_y);

   for (int y = 0; y < blocks_y; y++) {
      for (int x = 0; x < blocks_x; x++) {
         float ldprev = blockLumaDiff(srcp_y + block_width*x, srcc_y + block_width*x, block_width, block_height, srcp_pitch_y, srcc_pitch_y);
         float ldnext = blockLumaDiff(srcc_y + block_width*x, srcn_y + block_width*x, block_width, block_height, srcc_pitch_y, srcn_pitch_y);
         float ldprevprev = 0.0f;
         float ldnextnext = 0.0f;

         //too much movement in both directions?
         if (ldnext > scenelumathresh && ldprev > scenelumathresh) {
            copyChromaBlock(dst_u + block_width_uv*x, dst_v + block_width_uv*x,
                            altsrcc_u + block_width_uv*x, altsrcc_v + block_width_uv*x,
                            block_width_uv, block_height_uv, dst_pitch_uv, altsrcc_pitch_uv);
            continue;
         }

         if (ldnext > scenelumathresh) {
            ldprevprev = blockLumaDiff(srcpp_y + block_width*x, srcp_y + block_width*x, block_width, block_height, srcpp_pitch_y, srcp_pitch_y);
         } else if (ldprev > scenelumathresh) {
            ldnextnext = blockLumaDiff(srcn_y + block_width*x, srcnn_y + block_width*x, block_width, block_height, srcn_pitch_y, srcnn_pitch_y);
         }

         //two consecutive frames in one direction to generate mask?
         if ((ldnext > scenelumathresh && ldprevprev > scenelumathresh) ||
             (ldprev > scenelumathresh && ldnextnext > scenelumathresh)) {
            copyChromaBlock(dst_u + block_width_uv*x, dst_v + block_width_uv*x,
                            altsrcc_u + block_width_uv*x, altsrcc_v + block_width_uv*x,
                            block_width_uv, block_height_uv, dst_pitch_uv, altsrcc_pitch_uv);
            continue;
         }

         //generate mask from correct side of scenechange
         if (ldnext > scenelumathresh) {
            makeBlockRainbowMask(srcpp_u + block_width_uv*x, srcpp_v + block_width_uv*x,
                                  srcp_u + block_width_uv*x,  srcp_v + block_width_uv*x,
                                  srcc_u + block_width_uv*x,  srcc_v + block_width_uv*x,
                                   dst_u + block_width_uv*x,   dst_v + block_width_uv*x,
                                 block_width_uv, block_height_uv,
                                 srcpp_pitch_uv, srcp_pitch_uv, srcc_pitch_uv, dst_pitch_uv,
                                 variation);
         } else if (ldprev > scenelumathresh) {
            makeBlockRainbowMask( srcc_u + block_width_uv*x,  srcc_v + block_width_uv*x,
                                  srcn_u + block_width_uv*x,  srcn_v + block_width_uv*x,
                                 srcnn_u + block_width_uv*x, srcnn_v + block_width_uv*x,
                                   dst_u + block_width_uv*x,   dst_v + block_width_uv*x,
                                 block_width_uv, block_height_uv,
                                 srcc_pitch_uv, srcn_pitch_uv, srcnn_pitch_uv, dst_pitch_uv,
                                 variation);
         } else {
            makeBlockRainbowMask(srcp_u + block_width_uv*x, srcp_v + block_width_uv*x,
                                 srcc_u + block_width_uv*x, srcc_v + block_width_uv*x,
                                 srcn_u + block_width_uv*x, srcn_v + block_width_uv*x,
                                  dst_u + block_width_uv*x,  dst_v + block_width_uv*x,
                                 block_width_uv, block_height_uv,
                                 srcp_pitch_uv, srcc_pitch_uv, srcn_pitch_uv, dst_pitch_uv,
                                 variation);
         }

         //denoise and expand mask
         processBlockRainbowMask(dst_u + block_width_uv*x, dst_v + block_width_uv*x,
                                 block_width_uv, block_height_uv, dst_pitch_uv, conservativemask);

         //determine direction to blend in
         BlendDirection direction;
         if (ldprev > ldnext*relativeframediff) {
            direction = bdNext;
         } else if (ldnext > ldprev*relativeframediff) {
            direction = bdPrev;
         } else {
            direction = bdBoth;
         }
         applyBlockRainbowMask(srcp_u + block_width_uv*x, srcp_v + block_width_uv*x,
                               srcc_u + block_width_uv*x, srcc_v + block_width_uv*x,
                               srcn_u + block_width_uv*x, srcn_v + block_width_uv*x,
                                dst_u + block_width_uv*x,  dst_v + block_width_uv*x,
                               block_width_uv, block_height_uv,
                               srcp_pitch_uv, srcc_pitch_uv, srcn_pitch_uv, dst_pitch_uv,
                               direction);

      }

      srcpp_y += block_height * srcpp_pitch_y;
      srcpp_u += block_height_uv * srcpp_pitch_uv;
      srcpp_v += block_height_uv * srcpp_pitch_uv;

      srcp_y += block_height * srcp_pitch_y;
      srcp_u += block_height_uv * srcp_pitch_uv;
      srcp_v += block_height_uv * srcp_pitch_uv;

      srcc_y += block_height * srcc_pitch_y;
      srcc_u += block_height_uv * srcc_pitch_uv;
      srcc_v += block_height_uv * srcc_pitch_uv;

      srcn_y += block_height * srcn_pitch_y;
      srcn_u += block_height_uv * srcn_pitch_uv;
      srcn_v += block_height_uv * srcn_pitch_uv;

      srcnn_y += block_height * srcnn_pitch_y;
      srcnn_u += block_height_uv * srcnn_pitch_uv;
      srcnn_v += block_height_uv * srcnn_pitch_uv;

      altsrcc_u += block_height_uv * altsrcc_pitch_uv;
      altsrcc_v += block_height_uv * altsrcc_pitch_uv;

      dst_u += block_height_uv * dst_pitch_uv;
      dst_v += block_height_uv * dst_pitch_uv;
   }

   return dst;
}

AVSValue __cdecl Create_Bifrost(AVSValue args, void* user_data, IScriptEnvironment* env)
{
   PClip InClip;
   if (!args[1].IsClip())
      InClip = args[0].AsClip();
   else
      InClip = args[1].AsClip();

   if (args[5].AsBool(true)) {
      InClip = env->Invoke("SeparateFields", InClip).AsClip();
      return env->Invoke("Weave", new Bifrost(env->Invoke("SeparateFields", args[0].AsClip()).AsClip(), InClip, (float)args[2].AsFloat(3.0), args[3].AsInt(5), args[4].AsBool(false), true, args[6].AsInt(4), args[7].AsInt(4), env));
   } else {
      return new Bifrost(args[0].AsClip(), InClip, (float)args[2].AsFloat(1.5), args[3].AsInt(5), args[4].AsBool(false), false, args[6].AsInt(4), args[7].AsInt(4), env);
   }
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* env)
{
   env->AddFunction("Bifrost20", "c[altclip]c[scenelumathresh]f[variation]i[conservativemask]b[interlaced]b[blockx]i[blocky]i", Create_Bifrost, 0);
   return 0;
};

