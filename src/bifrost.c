#include <stdint.h>
//#include <stdlib.h>
//#include <string.h>

#include <vapoursynth/VapourSynth.h>
#include <vapoursynth/VSHelper.h>


enum BlendDirection {
   bdNext,
   bdPrev,
   bdBoth
};


typedef struct {
   VSNodeRef *node;
   VSNodeRef *altnode;
   float scenelumathresh;
   int variation;
   int conservativemask;
   int interlaced;

   const VSVideoInfo *vi;
   float relativeframediff;
   int offset;
} BifrostData;


static void VS_CC bifrostInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
   BifrostData *d = (BifrostData *) * instanceData;
   vsapi->setVideoInfo(d->vi, 1, node);

   d->relativeframediff = 1.2f;
   d->offset = d->interlaced ? 2 : 1;
}


static void ApplyMask(const VSFrameRef *srcp, const VSFrameRef *srcc, const VSFrameRef *srcn, VSFrameRef *dst, int blenddirection, const VSAPI *vsapi) {
   const uint8_t *srcp_u = vsapi->getReadPtr(srcp, 1);
   const uint8_t *srcp_v = vsapi->getReadPtr(srcp, 2);

   const uint8_t *srcc_u = vsapi->getReadPtr(srcc, 1);
   const uint8_t *srcc_v = vsapi->getReadPtr(srcc, 2);

   const uint8_t *srcn_u = vsapi->getReadPtr(srcn, 1);
   const uint8_t *srcn_v = vsapi->getReadPtr(srcn, 2);

   uint8_t *dst_u = vsapi->getWritePtr(dst, 1);
   uint8_t *dst_v = vsapi->getWritePtr(dst, 2);

   int rowsize_uv = vsapi->getFrameWidth(srcc, 1);
   int height_uv = vsapi->getFrameHeight(srcc, 1);

   int pitch_uv = vsapi->getStride(srcc, 1);


   for (int y = 0; y < height_uv; y++) {
      if (blenddirection == bdNext) {
         for (int x = 0; x < rowsize_uv; x++) {
            if (dst_v[x]) {
               dst_u[x] = (srcc_u[x]+srcn_u[x]+1) >> 1;
               dst_v[x] = (srcc_v[x]+srcn_v[x]+1) >> 1;
            } else {
               dst_u[x] = srcc_u[x];
               dst_v[x] = srcc_v[x];
            }
         }
      } else if (blenddirection == bdPrev) {
         for (int x = 0; x < rowsize_uv; x++) {
            if (dst_v[x]) {
               dst_u[x] = (srcc_u[x]+srcp_u[x]+1) >> 1;
               dst_v[x] = (srcc_v[x]+srcp_v[x]+1) >> 1;
            } else {
               dst_u[x] = srcc_u[x];
               dst_v[x] = srcc_v[x];
            }
         }
      } else if (blenddirection == bdBoth) {
         for (int x = 0; x < rowsize_uv; x++) {
            if (dst_v[x]) {
               dst_u[x] = (2*srcc_u[x]+srcp_u[x]+srcn_u[x]+3) >> 2;
               dst_v[x] = (2*srcc_v[x]+srcp_v[x]+srcn_v[x]+3) >> 2;
            } else {
               dst_u[x] = srcc_u[x];
               dst_v[x] = srcc_v[x];
            }
         }
      }

      srcp_u += pitch_uv;
      srcp_v += pitch_uv;

      srcc_u += pitch_uv;
      srcc_v += pitch_uv;

      srcn_u += pitch_uv;
      srcn_v += pitch_uv;

      dst_u += pitch_uv;
      dst_v += pitch_uv;
   }
}


static void ProcessMask(VSFrameRef *dst, int conservativemask, const VSAPI *vsapi) {
   uint8_t *dst_u = vsapi->getWritePtr(dst, 1);
   uint8_t *dst_v = vsapi->getWritePtr(dst, 2);

   int dst_pitch_uv = vsapi->getStride(dst, 1);
   int rowsize_uv = vsapi->getFrameWidth(dst, 1);
   int height_uv = vsapi->getFrameHeight(dst, 1);


   //denoise mask, remove marked pixels with no horizontal marked neighbors
   for (int y = 0; y < height_uv; y++) {
      dst_v[0] = dst_u[0] && dst_u[1];
      for (int x = 1; x < rowsize_uv - 1; x++) {
         dst_v[x] = dst_u[x] && (dst_u[x-1] || dst_u[x+1]);
      }
      dst_v[rowsize_uv - 1] = dst_u[rowsize_uv - 1] && dst_u[rowsize_uv - 2];

      dst_u += dst_pitch_uv;
      dst_v += dst_pitch_uv;
   }

   //expand mask vertically
   if (!conservativemask) {
      dst_v = vsapi->getWritePtr(dst, 2);

      for (int x = 0; x < rowsize_uv; x++) {
         dst_v[x] = dst_v[x] || dst_v[x+dst_pitch_uv];
      }

      dst_v += dst_pitch_uv;

      for (int y = 1; y < height_uv - 1; y++) {
         for (int x = 0; x < rowsize_uv; x++) {
            dst_v[x] = dst_v[x] || (dst_v[x+dst_pitch_uv] && dst_v[x-dst_pitch_uv]);
         }

         dst_v += dst_pitch_uv;
      }

      for (int x = 0; x < rowsize_uv; x++) {
         dst_v[x] = dst_v[x] || dst_v[x-dst_pitch_uv];
      }
   }
}


static void MakeMask(const VSFrameRef *srcp, const VSFrameRef *srcc, const VSFrameRef *srcn, VSFrameRef *dst, int variation, const VSAPI *vsapi) {
   const uint8_t *srcp_u = vsapi->getReadPtr(srcp, 1);
   const uint8_t *srcp_v = vsapi->getReadPtr(srcp, 2);

   const uint8_t *srcc_u = vsapi->getReadPtr(srcc, 1);
   const uint8_t *srcc_v = vsapi->getReadPtr(srcc, 2);

   const uint8_t *srcn_u = vsapi->getReadPtr(srcn, 1);
   const uint8_t *srcn_v = vsapi->getReadPtr(srcn, 2);

   uint8_t *dst_u = vsapi->getWritePtr(dst, 1);
   uint8_t *dst_v = vsapi->getWritePtr(dst, 2);

   int rowsize_uv = vsapi->getFrameWidth(srcc, 1);
   int height_uv = vsapi->getFrameHeight(srcc, 1);

   int pitch_uv = vsapi->getStride(srcc, 1);

   for (int y = 0; y < height_uv; y++) {  
      for (int x = 0; x < rowsize_uv; x++) {
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

      srcp_u += pitch_uv;
      srcp_v += pitch_uv;

      srcc_u += pitch_uv;
      srcc_v += pitch_uv;

      srcn_u += pitch_uv;
      srcn_v += pitch_uv;

      dst_u += pitch_uv;
      dst_v += pitch_uv;
   }
}


static float FrameDiff(const VSFrameRef *f1, const VSFrameRef *f2, const VSAPI *vsapi) {
   const uint8_t *f1_y = vsapi->getReadPtr(f1, 0);
   const uint8_t *f2_y = vsapi->getReadPtr(f2, 0);

   int pitch_y = vsapi->getStride(f1, 0);

   int rowsize_y = vsapi->getFrameWidth(f1, 0);
   int height_y = vsapi->getFrameHeight(f1, 0);

   int diff = 0;

   for (int y = 0; y < height_y; y++) {  
      for (int x = 0; x < rowsize_y; x++) {
         diff += abs(f1_y[x] - f2_y[x]);
      }

      f1_y += pitch_y;
      f2_y += pitch_y;
   }

   return (float)diff / (rowsize_y * height_y);
}


static const VSFrameRef *VS_CC bifrostGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
   BifrostData *d = (BifrostData *) * instanceData;

#define min(a, b)  (((a) < (b)) ? (a) : (b))
#define max(a, b)  (((a) > (b)) ? (a) : (b))

   if (activationReason == arInitial) {
      vsapi->requestFrameFilter(max(n - d->offset*2, 0), d->node, frameCtx);
      vsapi->requestFrameFilter(max(n - d->offset,   0), d->node, frameCtx);
      vsapi->requestFrameFilter(n, d->node, frameCtx);
      vsapi->requestFrameFilter(min(n + d->offset,   d->vi->numFrames-1), d->node, frameCtx);
      vsapi->requestFrameFilter(min(n + d->offset*2, d->vi->numFrames-1), d->node, frameCtx);
   } else if (activationReason == arAllFramesReady) {
      const VSFrameRef *srcpp = vsapi->getFrameFilter(max(n - d->offset*2, 0), d->node, frameCtx);
      const VSFrameRef *srcp  = vsapi->getFrameFilter(max(n - d->offset,   0), d->node, frameCtx);
      const VSFrameRef *srcc  = vsapi->getFrameFilter(n, d->node, frameCtx);
      const VSFrameRef *srcn  = vsapi->getFrameFilter(min(n + d->offset,   d->vi->numFrames-1), d->node, frameCtx);
      const VSFrameRef *srcnn = vsapi->getFrameFilter(min(n + d->offset*2, d->vi->numFrames-1), d->node, frameCtx);

#undef min
#undef max

      float ldprev = FrameDiff(srcp, srcc, vsapi);
      float ldnext = FrameDiff(srcc, srcn, vsapi);
      float ldprevprev = 0.0f;
      float ldnextnext = 0.0f;

      //too much movevement in both directions?
      if (ldnext > d->scenelumathresh && ldprev > d->scenelumathresh) {
         //return child2->GetFrame(n, env);
         // FIXME: should return frame n from d->altnode
         vsapi->freeFrame(srcpp);
         vsapi->freeFrame(srcp);
         vsapi->freeFrame(srcn);
         vsapi->freeFrame(srcnn);
         return srcc;
      }

      if (ldnext > d->scenelumathresh) {
         ldprevprev = FrameDiff(srcpp, srcp, vsapi);
      } else if (ldprev > d->scenelumathresh) {
         ldnextnext = FrameDiff(srcn, srcnn, vsapi);
      }

      //two consecutive frames in one direction to generate mask?
      if ((ldnext > d->scenelumathresh && ldprevprev > d->scenelumathresh) || (ldprev > d->scenelumathresh && ldnextnext > d->scenelumathresh)) {
         //return child2->GetFrame(n, env);
         // FIXME: should return frame n from d->altnode
         vsapi->freeFrame(srcpp);
         vsapi->freeFrame(srcp);
         vsapi->freeFrame(srcn);
         vsapi->freeFrame(srcnn);
         return srcc;
      }

      const VSFrameRef *planeSrc[3] = { srcc, NULL, NULL };
      const int planes[3] = { 0 };
      VSFrameRef *dst = vsapi->newVideoFrame2(d->vi->format, d->vi->width, d->vi->height, planeSrc, planes, srcc, core);

      //generate mask from right side of scenechange
      if (ldnext > d->scenelumathresh) {
         MakeMask(srcpp, srcp, srcc, dst, d->variation, vsapi);
      } else if (ldprev > d->scenelumathresh) {
         MakeMask(srcc, srcn, srcnn, dst, d->variation, vsapi);
      } else {
         MakeMask(srcp, srcc, srcn, dst, d->variation, vsapi);
      }

      //denoise and expand mask
      ProcessMask(dst, d->conservativemask, vsapi);

      //determine direction to blend in
      if (ldprev > ldnext*d->relativeframediff) {
         ApplyMask(srcp, srcc, srcn, dst, bdNext, vsapi);
      } else if (ldnext > ldprev*d->relativeframediff) {
         ApplyMask(srcp, srcc, srcn, dst, bdPrev, vsapi);
      } else {
         ApplyMask(srcp, srcc, srcn, dst, bdBoth, vsapi);
      }

      vsapi->freeFrame(srcpp);
      vsapi->freeFrame(srcp);
      vsapi->freeFrame(srcc);
      vsapi->freeFrame(srcn);
      vsapi->freeFrame(srcnn);

      return dst;
   }

   return 0;
}


static void VS_CC bifrostFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
   BifrostData *d = (BifrostData *)instanceData;

   vsapi->freeNode(d->node);
   vsapi->freeNode(d->altnode);
   free(d);
}


static void VS_CC bifrostCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
   BifrostData d;
   BifrostData *data;
   int err;

   d.variation = vsapi->propGetInt(in, "variation", 0, &err);
   if (err) {
      d.variation = 5;
   }

   d.interlaced = !!vsapi->propGetInt(in, "interlaced", 0, &err);
   if (err) {
      d.interlaced = 1;
   }

   d.scenelumathresh = (float)vsapi->propGetFloat(in, "scenelumathresh", 0, &err);
   if (err) {
      if (d.interlaced) {
         d.scenelumathresh = 3.0f;
      } else {
         d.scenelumathresh = 1.5f;
      }
   }

   d.conservativemask = !!vsapi->propGetInt(in, "conservativemask", 0, &err);

   d.node = vsapi->propGetNode(in, "clip", 0, 0);
   d.vi = vsapi->getVideoInfo(d.node);

   if (!isConstantFormat(d.vi) || d.vi->format->id != pfYUV420P8) {
      vsapi->setError(out, "Bifrost: Only constant format YUV420P8 allowed.");
      vsapi->freeNode(d.node);
      return;
   }

   d.altnode = vsapi->propGetNode(in, "altclip", 0, &err);
   if (err) {
      d.altnode = vsapi->cloneNodeRef(d.node);
   }

   // TODO: separatefields and weave right here

   data = malloc(sizeof(d));
   *data = d;

   vsapi->createFilter(in, out, "Bifrost", bifrostInit, bifrostGetFrame, bifrostFree, fmParallel, 0, data, core);
   return;
}


VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
   configFunc("com.nodame.bifrost", "bifrost", "Bifrost plugin for VapourSynth", VAPOURSYNTH_API_VERSION, 1, plugin);
   registerFunc("Bifrost",
                "clip:clip;"
                "altclip:clip:opt;"
                "scenelumathresh:float:opt;"
                "variation:int:opt;"
                "conservativemask:int:opt;"
                "interlaced:int:opt;",
                bifrostCreate, 0, plugin);
}
