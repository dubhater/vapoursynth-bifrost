#include <stdio.h>
#include <windows.h>
#include "avisynth.h"

enum BlendDirection { bdNext, bdPrev, bdBoth };

class Bifrost : public GenericVideoFilter {
   int rt;
   int offset;
   float scenelumathresh;
   bool interlaced, conservativemask;
   float relativeframediff;
   PClip child2;

   public:
   Bifrost(PClip _child, PClip _child2, float _scenelumathresh,int _variation,bool _conservativemask,bool _interlaced, IScriptEnvironment* env) :
      GenericVideoFilter(_child), child2(_child2), scenelumathresh(_scenelumathresh), rt(_variation), conservativemask(_conservativemask) {
         relativeframediff  = 1.2f;

         if ((!vi.IsYV12()) || (!child2->GetVideoInfo().IsYV12()))
            env->ThrowError("Bifrost: YV12 video required");

         offset = _interlaced ? 2 : 1;

      }

   PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
   float FrameDiff(PVideoFrame &f1, PVideoFrame &f2);
   void MakeMask(PVideoFrame &srcp, PVideoFrame &srcc, PVideoFrame &srcn, PVideoFrame &dst);
   void ProcessMask(PVideoFrame &dst); 
   void ApplyMask(PVideoFrame &srcp, PVideoFrame &srcc, PVideoFrame &srcn, PVideoFrame &dst, BlendDirection blenddirection);
};

void Bifrost::ApplyMask(PVideoFrame &srcp, PVideoFrame &srcc, PVideoFrame &srcn, PVideoFrame &dst, BlendDirection blenddirection) {
   const unsigned char *srcc_u = srcc->GetReadPtr(PLANAR_U);
   const unsigned char *srcc_v = srcc->GetReadPtr(PLANAR_V);

   const unsigned char *srcp_u = srcp->GetReadPtr(PLANAR_U);
   const unsigned char *srcp_v = srcp->GetReadPtr(PLANAR_V);

   const unsigned char *srcn_u = srcn->GetReadPtr(PLANAR_U);
   const unsigned char *srcn_v = srcn->GetReadPtr(PLANAR_V);

   unsigned char *dst_u = dst->GetWritePtr(PLANAR_U);
   unsigned char *dst_v = dst->GetWritePtr(PLANAR_V);

   int rowsize_uv = srcc->GetRowSize(PLANAR_U);
   int height_uv = srcc->GetHeight(PLANAR_U);

   int srcc_pitch_uv = srcc->GetPitch(PLANAR_U);
   int srcp_pitch_uv = srcp->GetPitch(PLANAR_U);
   int srcn_pitch_uv = srcn->GetPitch(PLANAR_U);
   int dst_pitch_uv = dst->GetPitch(PLANAR_U);


   for (int y=0; y < height_uv; y++) {
      if (blenddirection == bdNext) {
         for (int x=0; x < rowsize_uv; x++) {
            if (dst_v[x]) {
               dst_u[x] = (srcc_u[x]+srcn_u[x]+1) >> 1;
               dst_v[x] = (srcc_v[x]+srcn_v[x]+1) >> 1;
            } else {
               dst_u[x] = srcc_u[x];
               dst_v[x] = srcc_v[x];
            }
         }
      } else if (blenddirection == bdPrev) {
         for (int x=0; x < rowsize_uv; x++) {
            if (dst_v[x]) {
               dst_u[x] = (srcc_u[x]+srcp_u[x]+1) >> 1;
               dst_v[x] = (srcc_v[x]+srcp_v[x]+1) >> 1;
            } else {
               dst_u[x] = srcc_u[x];
               dst_v[x] = srcc_v[x];
            }
         }
      } else if (blenddirection == bdBoth) {
         for (int x=0; x < rowsize_uv; x++) {
            if (dst_v[x]) {
               dst_u[x] = (2*srcc_u[x]+srcp_u[x]+srcn_u[x]+3) >> 2;
               dst_v[x] = (2*srcc_v[x]+srcp_v[x]+srcn_v[x]+3) >> 2;
            } else {
               dst_u[x] = srcc_u[x];
               dst_v[x] = srcc_v[x];
            }
         }
      }

      dst_u+=dst_pitch_uv;
      dst_v+=dst_pitch_uv;
      srcc_u+=srcc_pitch_uv;
      srcc_v+=srcc_pitch_uv;
      srcn_u+=srcn_pitch_uv;
      srcn_v+=srcn_pitch_uv;
      srcp_u+=srcp_pitch_uv;
      srcp_v+=srcp_pitch_uv;
   }
}

void Bifrost::ProcessMask(PVideoFrame &dst) {
   unsigned char *dst_u = dst->GetWritePtr(PLANAR_U);
   unsigned char *dst_v = dst->GetWritePtr(PLANAR_V);

   int dst_pitch_uv = dst->GetPitch(PLANAR_U);
   int rowsize_uv = dst->GetRowSize(PLANAR_U);
   int height_uv = dst->GetHeight(PLANAR_U);


   //denoise mask, remove marked pixels with no horizontal marked neighbors
   for (int y=0; y < height_uv; y++) {
      dst_v[0] = dst_u[0] && dst_u[1];
      for (int x=1; x < rowsize_uv - 1; x++)
         dst_v[x] = dst_u[x] && (dst_u[x-1] || dst_u[x+1]);
      dst_v[rowsize_uv - 1] = dst_u[rowsize_uv - 1] && dst_u[rowsize_uv - 2];

      dst_u+=dst_pitch_uv;
      dst_v+=dst_pitch_uv;
   }

   //expand mask vertically
   if (!conservativemask) {
      dst_v = dst->GetWritePtr(PLANAR_V);

      for (int x=0; x < rowsize_uv; x++)
         dst_v[x] = dst_v[x] || dst_v[x+dst_pitch_uv];	

      dst_v+=dst_pitch_uv;

      for (int y=1; y < height_uv - 1; y++) {
         for (int x=0; x < rowsize_uv; x++)
            dst_v[x] = dst_v[x] || (dst_v[x+dst_pitch_uv] && dst_v[x-dst_pitch_uv]);

         dst_v+=dst_pitch_uv;
      }

      for (int x=0; x < rowsize_uv; x++)
         dst_v[x] = dst_v[x] || dst_v[x-dst_pitch_uv];
   }

}

void Bifrost::MakeMask(PVideoFrame &srcp, PVideoFrame &srcc, PVideoFrame &srcn, PVideoFrame &dst) {
   const unsigned char *srcc_u = srcc->GetReadPtr(PLANAR_U);
   const unsigned char *srcc_v = srcc->GetReadPtr(PLANAR_V);

   const unsigned char *srcp_u = srcp->GetReadPtr(PLANAR_U);
   const unsigned char *srcp_v = srcp->GetReadPtr(PLANAR_V);

   const unsigned char *srcn_u = srcn->GetReadPtr(PLANAR_U);
   const unsigned char *srcn_v = srcn->GetReadPtr(PLANAR_V);

   unsigned char *dst_u = dst->GetWritePtr(PLANAR_U);
   unsigned char *dst_v = dst->GetWritePtr(PLANAR_V);

   int rowsize_uv = srcc->GetRowSize(PLANAR_U);
   int height_uv = srcc->GetHeight(PLANAR_U);

   int srcc_pitch_uv = srcc->GetPitch(PLANAR_U);
   int srcp_pitch_uv = srcp->GetPitch(PLANAR_U);
   int srcn_pitch_uv = srcn->GetPitch(PLANAR_U);
   int dst_pitch_uv = dst->GetPitch(PLANAR_U);

   for (int y = 0; y < height_uv; y++) {  
      for (int x=0; x < rowsize_uv; x++) {
         unsigned char up = srcp_u[x];
         unsigned char uc = srcc_u[x];
         unsigned char un = srcn_u[x];

         unsigned char vp = srcp_v[x];
         unsigned char vc = srcc_v[x];
         unsigned char vn = srcn_v[x];

         int ucup = uc-up;
         int ucun = uc-un;

         int vcvp = vc-vp;
         int vcvn = vc-vn;

         dst_u[x] = (((ucup+rt) & (ucun+rt)) < 0)
            || (((-ucup+rt) & (-ucun+rt)) < 0)
            || (((vcvp+rt) & (vcvn+rt)) < 0) 
            || (((-vcvp+rt) & (-vcvn+rt)) < 0);
      }

      dst_u+=dst_pitch_uv;
      dst_v+=dst_pitch_uv;
      srcc_u+=srcc_pitch_uv;
      srcc_v+=srcc_pitch_uv;
      srcp_u+=srcp_pitch_uv;
      srcp_v+=srcp_pitch_uv;
      srcn_u+=srcn_pitch_uv;
      srcn_v+=srcn_pitch_uv;
   }
}

float Bifrost::FrameDiff(PVideoFrame &f1, PVideoFrame &f2) {

   const unsigned char *f1_y = f1->GetReadPtr(PLANAR_Y);
   const unsigned char *f2_y = f2->GetReadPtr(PLANAR_Y);

   int f1_pitch_y = f1->GetPitch(PLANAR_Y);
   int f2_pitch_y = f2->GetPitch(PLANAR_Y);

   int rowsize_y = f1->GetRowSize(PLANAR_Y);
   int height_y = f1->GetHeight(PLANAR_Y);

   int diff = 0;

   for (int y = 0; y < height_y; y++) {  
      for (int x=0; x < rowsize_y; x++)
         diff+=abs(f1_y[x]-f2_y[x]);

      f1_y+=f1_pitch_y;
      f2_y+=f2_pitch_y;
   }

   return (float)diff/(vi.width*vi.height);
}

PVideoFrame __stdcall Bifrost::GetFrame(int n, IScriptEnvironment* env) {
   PVideoFrame srcpp = child->GetFrame(max(n - offset * 2, 0), env);
   PVideoFrame srcp = child->GetFrame(max(n - offset, 0), env);
   PVideoFrame srcc = child->GetFrame(n, env);
   PVideoFrame srcn = child->GetFrame(min(n + offset, vi.num_frames - 1), env);
   PVideoFrame srcnn = child->GetFrame(min(n + offset * 2, vi.num_frames - 1), env);

   float ldprev = FrameDiff(srcp, srcc);
   float ldnext = FrameDiff(srcc, srcn);
   float ldprevprev = 0.0f;
   float ldnextnext= 0.0f;

   //too much movevement in both directions?
   if (ldnext > scenelumathresh && ldprev > scenelumathresh)
      return child2->GetFrame(n, env);

   if (ldnext > scenelumathresh) {
      ldprevprev = FrameDiff(srcpp, srcp);
   } else if (ldprev > scenelumathresh) {
      ldnextnext = FrameDiff(srcn, srcnn);
   }

   //two consecutive frames in one direction to generate mask?
   if (ldnext > scenelumathresh && ldprevprev > scenelumathresh || ldprev > scenelumathresh && ldnextnext > scenelumathresh)
      return child2->GetFrame(n, env);

   PVideoFrame dst = env->NewVideoFrame(vi);

   const unsigned char *srcc_y = srcc->GetReadPtr(PLANAR_Y);
   unsigned char *dst_y = dst->GetWritePtr(PLANAR_Y);
   int rowsize_y = srcc->GetRowSize(PLANAR_Y);
   int height_y = srcc->GetHeight(PLANAR_Y);
   int srcc_pitch_y = srcc->GetPitch(PLANAR_Y);
   int dst_pitch_y = dst->GetPitch(PLANAR_Y);

   env->BitBlt(dst_y, dst_pitch_y, srcc_y, srcc_pitch_y, rowsize_y, height_y);

   //generate mask from right side of scenechange
   if (ldnext > scenelumathresh) {
      MakeMask(srcpp, srcp, srcc, dst);
   } else if (ldprev > scenelumathresh) {
      MakeMask(srcc, srcn, srcnn, dst);
   } else {
      MakeMask(srcp, srcc, srcn, dst);
   }

   //denoise and expand mask
   ProcessMask(dst);

   //determine direction to blend in
   if (ldprev > ldnext*relativeframediff)
      ApplyMask(srcp, srcc, srcn, dst, bdNext);
   else if (ldnext > ldprev*relativeframediff)
      ApplyMask(srcp, srcc, srcn, dst, bdPrev);
   else
      ApplyMask(srcp, srcc, srcn, dst, bdBoth);

   return dst;
}

AVSValue __cdecl Create_Bifrost(AVSValue args, void* user_data, IScriptEnvironment* env)
{
   PClip InClip;
   if (!args[1].IsClip())
      InClip=args[0].AsClip();
   else
      InClip=args[1].AsClip();

   if (args[5].AsBool(true)) {
      InClip=env->Invoke("SeparateFields",InClip).AsClip();
      return env->Invoke("Weave",new Bifrost(env->Invoke("SeparateFields",args[0].AsClip()).AsClip(),InClip,(float)args[2].AsFloat(3.0),args[3].AsInt(5),args[4].AsBool(false),true,env));
   } else {
      return new Bifrost(args[0].AsClip(),InClip,(float)args[2].AsFloat(1.5),args[3].AsInt(5),args[4].AsBool(false),false,env);
   }
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* env)
{
   env->AddFunction("Bifrost", "c[altclip]c[scenelumathresh]f[variation]i[conservativemask]b[interlaced]b", Create_Bifrost, 0);
   return 0;
};

