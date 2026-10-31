#include "Feedback.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace Burnside
{
namespace
{
constexpr float Pi=3.14159265358979323846f;
Vec3 Pixel(Vec3 p, int width, int height) { return {(p.X+1)*width*0.5f,(p.Y+1)*height*0.5f,0}; }
void Line(Vec3 a, Vec3 b, float thickness, uint32_t color)
{
    imguiDrawLine(a.X,a.Y,b.X,b.Y,thickness,color);
}
bool Inside(Vec3 p, const SceneFrame& scene, int height, float margin=5)
{
    return p.X>scene.ViewportLeft+margin && p.X<scene.ViewportRight-margin && p.Y>margin && p.Y<height-margin;
}
void DrawHint(const TurnHint& hint, bool right, const SceneFrame& scene, int width, int height)
{
    const uint32_t color=right ? imguiRGBA(255,191,96) : imguiRGBA(102,225,255);
    const uint32_t shadow=imguiRGBA(7,14,24,230);
    std::array<Vec3,17> path;
    for (size_t i=0; i<path.size(); ++i) path[i]=Pixel(hint.Path[i],width,height);
    Vec3 tangent=path.back()-path[path.size()-2];
    const float length=std::sqrt(tangent.X*tangent.X+tangent.Y*tangent.Y);
    if (length<0.001f) return;
    tangent=tangent*(1.0f/length);
    const Vec3 normal{-tangent.Y,tangent.X,0},tip=path.back();
    const Vec3 wingA=tip-tangent*13+normal*7,wingB=tip-tangent*13-normal*7;
    for (int pass=0; pass<2; ++pass)
    {
        const float thickness=pass ? 3.0f : 7.0f;
        const uint32_t ink=pass ? color : shadow;
        for (size_t i=1; i<path.size(); ++i)
            if (Inside(path[i-1],scene,height) && Inside(path[i],scene,height)) Line(path[i-1],path[i],thickness,ink);
        if (Inside(tip,scene,height) && Inside(wingA,scene,height) && Inside(wingB,scene,height))
        { Line(wingA,tip,thickness,ink); Line(wingB,tip,thickness,ink); }
    }
    if (!Inside(tip,scene,height,34)) return;
    Vec3 badge=tip+tangent*36;
    badge.X=std::clamp(badge.X,scene.ViewportLeft+37.0f,scene.ViewportRight-37.0f);
    badge.Y=std::clamp(badge.Y,36.0f,height-36.0f);
    imguiDrawRoundedRect(badge.X-32,badge.Y-14,64,28,6,shadow);
    const float x=badge.X-25,y=badge.Y-10;
    imguiDrawRoundedRect(x,y,15,21,5,imguiRGBA(191,207,226));
    imguiDrawRoundedRect(x+1,y+1,13,19,4,imguiRGBA(22,31,44));
    imguiDrawRect(x+(right ? 8 : 2),y+11,5,7,color);
    imguiDrawLine(x+7.5f,y+11,x+7.5f,y+19,1,imguiRGBA(191,207,226));
    imguiDrawText(static_cast<int>(badge.X-5),static_cast<int>(badge.Y-5),IMGUI_ALIGN_LEFT,right ? "RMB" : "LMB",color);
}
void DrawCompletion(const CompletionEffect& completion, const SceneFrame& scene, int height)
{
    const float age=completion.Age();
    const float fade=std::clamp((CompletionEffect::Duration-age)/0.65f,0.0f,1.0f);
    const float centerX=(scene.ViewportLeft+scene.ViewportRight)*0.5f;
    const float centerY=height*0.53f,area=static_cast<float>(scene.ViewportRight-scene.ViewportLeft);
    const uint32_t palette[]={0xff6bdfff,0xffffdc73,0xffa4ed8b,0xffb38aff,0xffebacff};
    for (int i=0; i<84; ++i)
    {
        const float phase=i*2.39996323f,speed=0.6f+(i%13)/13.0f;
        const float radius=std::min(area,static_cast<float>(height))*0.24f;
        const Vec3 position{centerX+std::cos(phase)*radius*(0.12f+age*speed),
                            centerY+std::sin(phase)*radius*(0.12f+age*speed)+150*age-115*age*age,0};
        const float spin=phase+age*(i%2 ? 4 : -5);
        const Vec3 edge{std::cos(spin)*(3+i%4),std::sin(spin)*(3+i%4),0};
        const uint32_t color=(palette[i%5]&0xffffff)|(static_cast<uint32_t>(fade*230)<<24);
        if (Inside(position-edge,scene,height,14) && Inside(position+edge,scene,height,14)) Line(position-edge,position+edge,3,color);
    }
    const float y=height-128+8*std::sin(std::min(age,0.35f)/0.35f*Pi*0.5f);
    const auto alpha=static_cast<unsigned char>(fade*255);
    imguiDrawRoundedRect(centerX-166,y-27,332,64,12,imguiRGBA(8,19,30,static_cast<unsigned char>(fade*235)));
    imguiDrawLine(centerX-142,y+27,centerX+142,y+27,2,imguiRGBA(255,216,107,alpha));
    imguiDrawText(static_cast<int>(centerX),static_cast<int>(y+6),IMGUI_ALIGN_CENTER,"CONGRATULATIONS!",imguiRGBA(255,226,139,alpha));
    imguiDrawText(static_cast<int>(centerX),static_cast<int>(y-14),IMGUI_ALIGN_CENTER,"CUBE SOLVED",imguiRGBA(224,242,255,alpha));
}
}
void DrawFeedback(const SceneFrame& scene, const CompletionEffect& completion, int width, int height, bool showHints)
{
    if (showHints)
        for (size_t i=0; i<scene.TurnHints.size(); ++i) DrawHint(scene.TurnHints[i],i==1,scene,width,height);
    if (completion.Active()) DrawCompletion(completion,scene,height);
}
}
