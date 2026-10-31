#include "Scene.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Burnside
{
namespace
{
constexpr float Pi = 3.14159265358979323846f;
constexpr float CubeWidth = 3.12f;
constexpr float CameraDistance = 8.5f;
float Dot(Vec3 a, Vec3 b) { return a.X*b.X+a.Y*b.Y+a.Z*b.Z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.Y*b.Z-a.Z*b.Y,a.Z*b.X-a.X*b.Z,a.X*b.Y-a.Y*b.X}; }
Vec3 Unit(Vec3 p) { return p*(1.0f/std::max(0.000001f,std::sqrt(Dot(p,p)))); }
Vec3 Camera(Vec3 p, const ViewSettings& view)
{
    const float cy=std::cos(view.Yaw), sy=std::sin(view.Yaw);
    const float cp=std::cos(view.Pitch), sp=std::sin(view.Pitch);
    const Vec3 r{cy*p.X+sy*p.Z,p.Y,-sy*p.X+cy*p.Z};
    return {r.X,cp*r.Y-sp*r.Z,sp*r.Y+cp*r.Z};
}
Vec3 Rotate(Vec3 p, const Move& move, float angle)
{
    const float a=p[move.A], b=p[move.B], c=std::cos(angle), s=std::sin(angle);
    p[move.A]=c*a-s*b; p[move.B]=s*a+c*b;
    return p;
}
float Area(Vec3 a, Vec3 b, Vec3 c) { return (b.X-a.X)*(c.Y-a.Y)-(b.Y-a.Y)*(c.X-a.X); }
bool HitTriangle(Vec3 p, const Vertex& a, const Vertex& b, const Vertex& c, float& depth, float* uv=nullptr)
{
    const float area=Area(a.Position,b.Position,c.Position);
    if (std::abs(area)<1e-10f) return false;
    const float u=Area(p,b.Position,c.Position)/area, v=Area(a.Position,p,c.Position)/area, w=1-u-v;
    if (u<0 || v<0 || w<0) return false;
    depth=u*a.Position.Z+v*b.Position.Z+w*c.Position.Z;
    if (uv)
    {
        const float denominator=u/a.ClipW+v/b.ClipW+w/c.ClipW;
        uv[0]=(u*a.U/a.ClipW+v*b.U/b.ClipW+w*c.U/c.ClipW)/denominator;
        uv[1]=(u*a.V/a.ClipW+v*b.V/b.ClipW+w*c.V/c.ClipW)/denominator;
    }
    return true;
}
struct Patch { Vec3 Points[4]; Vec3 Normals[4]; };
const std::vector<Patch>& CubieMesh()
{
    static const std::vector<Patch> mesh=[]()
    {
        std::vector<Patch> result;
        constexpr float half=0.476f, bevel=0.0625f, inner=half-bevel;
        constexpr float grid[]={-half,-inner,inner,half};
        for (int axis=0; axis<3; ++axis) for (int sign : {-1,1})
        {
            const int a=(axis+1)%3,b=(axis+2)%3;
            for (int u=0; u<3; ++u) for (int v=0; v<3; ++v)
            {
                Patch patch;
                for (int k=0; k<4; ++k)
                {
                    Vec3 p{}; p[axis]=sign*half;
                    p[a]=grid[u+(k==0 || k==3 ? 0 : 1)]; p[b]=grid[v+(k<2 ? 0 : 1)];
                    Vec3 rounded{};
                    for (int d=0; d<3; ++d) rounded[d]=std::clamp(p[d],-inner,inner);
                    patch.Normals[k]=Unit(p-rounded);
                    patch.Points[k]=rounded+patch.Normals[k]*bevel;
                }
                result.push_back(patch);
            }
        }
        return result;
    }();
    return mesh;
}
}

ProjectedPoint ProjectPoint(Vec3 point, const ViewSettings& view, int width, int height, int left, int right)
{
    const Vec3 p=Camera(point,view);
    const float usableWidth=static_cast<float>(std::max(1,width-left-right));
    const float focal=0.5f*std::min(usableWidth,height*0.82f)/std::tan(view.VerticalFov*Pi/360.0f)*view.Zoom;
    const float depth=std::max(0.1f,CameraDistance-p.Z);
    const float centerX=left+usableWidth*0.5f, centerY=height*0.47f;
    constexpr float nearPlane=0.1f,farPlane=50.0f;
    return {{(centerX+focal*p.X/depth)/width*2-1,1-(centerY-focal*p.Y/depth)/height*2,
              farPlane/(farPlane-nearPlane)-farPlane*nearPlane/((farPlane-nearPlane)*depth)},depth};
}

uint32_t FaceColor(int face)
{
    static constexpr uint32_t palette[]={0xff5655f2,0xff3198ff,0xffeee8e5,0xff4ddaf2,0xffd29136,0xff80c85c};
    return palette[face%FaceCount];
}

SceneFrame BuildScene(const Timeline& timeline, const ViewSettings& view,
                      int width, int height, int left, int right, int mouseX, int mouseY, bool control)
{
    SceneFrame frame;
    frame.ViewportLeft=left; frame.ViewportRight=width-right;
    const Cube& cube=timeline.State();
    const auto& stickers=cube.Geometry().Stickers();
    const int order=cube.Order();
    const float pitch=CubeWidth/order, center=(order-1)*0.5f;
    const auto& active=timeline.ActiveMove();
    const float t=timeline.Progress(), angle=active ? t*t*(3-2*t)*active->Turns*Pi*0.5f : 0;
    const auto centerOf=[&](const Coordinates& p) { return Vec3{(p[0]-center)*pitch,(p[1]-center)*pitch,(p[2]-center)*pitch}; };
    const auto moving=[&](const Coordinates& p) { return active && p[active->Axis]==active->Layer; };
    const auto point=[&](Vec3 p,bool animate) { return animate ? Rotate(p,*active,angle) : p; };
    const auto vertex=[&](Vec3 p,Vec3 n,uint32_t color,float u,float v,float material,bool animate)
    {
        const auto projected=ProjectPoint(point(p,animate),view,width,height,left,right);
        return Vertex{projected.Position,color,Camera(point(n,animate),view),u,v,material,0,projected.ClipW};
    };
    constexpr int indices[]={0,1,2,0,2,3};
    const auto emit=[&](const Vertex* corners) { for (int k : indices) frame.Vertices.push_back(corners[k]); };
    frame.Vertices.reserve(static_cast<size_t>(order)*order*order*324+stickers.size()*6+12);
    frame.StickerCenters.resize(stickers.size());
    const auto screenQuad=[&](float x,float y,float w,float h,float z,float material)
    {
        Vertex corners[4];
        for (int k=0; k<4; ++k)
        {
            const float u=k==0 || k==3 ? -1.0f : 1.0f,v=k<2 ? -1.0f : 1.0f;
            corners[k]={{(x+u*w)/width*2-1,1-(y-v*h)/height*2,z},0xffffffff,{0,0,1},u,v,material,0,1};
        }
        emit(corners);
    };
    screenQuad(width*0.5f,height*0.5f,width*0.5f,height*0.5f,0.9998f,4);
    const auto floor=ProjectPoint({0,-2.12f,0},view,width,height,left,right);
    screenQuad((floor.Position.X+1)*width*0.5f,(1-floor.Position.Y)*height*0.5f,
               std::min(width-left-right,height)*0.28f*view.Zoom,height*0.065f*view.Zoom,0.999f,3);
    const Vec3 mouse{mouseX*2.0f/width-1,1-mouseY*2.0f/height,0};
    const bool canPick=mouseX>left && mouseX<width-right && mouseY>=0 && mouseY<height;
    float closest=std::numeric_limits<float>::max();
    struct DrawRange { size_t First,Count; Coordinates Position; int StickerIndex; };
    std::vector<DrawRange> ranges;
    ranges.reserve(static_cast<size_t>(order)*order*order+stickers.size());
    for (int x=0; x<order; ++x) for (int y=0; y<order; ++y) for (int z=0; z<order; ++z)
    {
        const Coordinates coordinates{static_cast<int8_t>(x),static_cast<int8_t>(y),static_cast<int8_t>(z)};
        const Vec3 origin=centerOf(coordinates); const bool animate=moving(coordinates);
        const size_t first=frame.Vertices.size();
        for (const Patch& patch : CubieMesh())
        {
            Vertex corners[4];
            for (int k=0; k<4; ++k) corners[k]=vertex(origin+patch.Points[k]*pitch,patch.Normals[k],0xff302722,0,0,0,animate);
            emit(corners);
            float depth=0;
            if (canPick && (HitTriangle(mouse,corners[0],corners[1],corners[2],depth) || HitTriangle(mouse,corners[0],corners[2],corners[3],depth)) && depth<closest)
                closest=depth;
        }
        ranges.push_back({first,frame.Vertices.size()-first,coordinates,-1}); ++frame.RenderedCubies;
    }
    for (size_t i=0; i<stickers.size(); ++i)
    {
        const Sticker& s=stickers[i];
        const bool animate=moving(s.Position);
        Vec3 origin=centerOf(s.Position),n{}; n[s.Axis]=static_cast<float>(s.Sign);
        origin=origin+n*(pitch*0.482f);
        int a=-1,b=-1;
        for (int d=0; d<3; ++d) if (d!=s.Axis) { if (a<0) a=d; else b=d; }
        Vertex corners[4];
        for (int k=0; k<4; ++k)
        {
            const float u=k==0 || k==3 ? -1.0f : 1.0f,v=k<2 ? -1.0f : 1.0f;
            Vec3 p=origin; p[a]+=u*pitch*0.420f; p[b]+=v*pitch*0.420f;
            corners[k]=vertex(p,n,FaceColor(cube.Colors()[i]),u,v,1,animate);
        }
        const size_t first=frame.Vertices.size(); emit(corners);
        ranges.push_back({first,6,s.Position,static_cast<int>(i)});
        frame.StickerCenters[i]=ProjectPoint(point(origin,animate),view,width,height,left,right).Position;
        ++frame.VisibleStickers;
        float depth=0,uv[2]{};
        bool hit=canPick && (HitTriangle(mouse,corners[0],corners[1],corners[2],depth,uv) || HitTriangle(mouse,corners[0],corners[2],corners[3],depth,uv));
        if (hit)
        {
            constexpr float radius=0.16f;
            const float dx=std::max(0.0f,std::abs(uv[0])-(1-radius)),dy=std::max(0.0f,std::abs(uv[1])-(1-radius));
            hit=dx*dx+dy*dy<=radius*radius;
        }
        if (hit && depth<closest) { closest=depth; frame.HoveredSticker=static_cast<int>(i); }
    }
    if (frame.HoveredSticker>=0 && !timeline.Busy())
    {
        frame.HoverMove=ClickMove(cube.Geometry(),frame.HoveredSticker,view,control,true);
        const Sticker& selected=stickers[frame.HoveredSticker];
        Vec3 origin=centerOf(selected.Position);
        origin[selected.Axis]+=selected.Sign*pitch*0.482f;
        const auto project=[&](Vec3 p) { return ProjectPoint(p,view,width,height,left,right).Position; };
        for (const Move& move : {*frame.HoverMove,frame.HoverMove->Inverse()})
        {
            TurnHint hint; hint.Rotation=move;
            const float sign=move.Turns>0 ? 1.0f : -1.0f;
            const Vec3 delta=project(Rotate(origin,move,sign*0.02f))-project(origin);
            const float speed=std::sqrt(delta.X*delta.X*width*width+delta.Y*delta.Y*height*height)*0.5f/0.02f;
            const float arc=std::clamp(80.0f/std::max(1.0f,speed),0.20f,0.60f);
            for (size_t i=0; i<hint.Path.size(); ++i)
                hint.Path[i]=project(Rotate(origin,move,sign*arc*static_cast<float>(i)/(hint.Path.size()-1)));
            frame.TurnHints.push_back(hint);
        }
    }
    const auto& highlighted=active ? active : frame.HoverMove;
    if (highlighted)
        for (const DrawRange& range : ranges)
        {
            if (range.Position[highlighted->Axis]!=highlighted->Layer) continue;
            for (size_t i=range.First; i<range.First+range.Count; ++i)
                frame.Vertices[i].Highlight=range.StickerIndex==frame.HoveredSticker && !timeline.Busy() ? 2.0f : 1.0f;
        }
    return frame;
}

Move ClickMove(const Layout& layout,size_t sticker,const ViewSettings& view,bool control,bool clockwise)
{
    const Sticker& s=layout.Stickers().at(sticker);
    int first=-1,second=-1;
    for (int d=0; d<3; ++d) if (d!=s.Axis) { if (first<0) first=d; else second=d; }
    Vec3 bases[3]={{1,0,0},{0,1,0},{0,0,1}};
    const auto verticality=[&](int axis)
    {
        const Vec3 p=Camera(bases[axis],view);
        return std::abs(p.Y)/std::max(0.0001f,std::sqrt(p.X*p.X+p.Y*p.Y));
    };
    const int vertical=verticality(first)>=verticality(second) ? first : second;
    const int horizontal=vertical==first ? second : first;
    const int fixed=control ? horizontal : vertical,travel=control ? vertical : horizontal;
    const int a=std::min<int>(s.Axis,travel),b=std::max<int>(s.Axis,travel);
    const float middle=(layout.Order()-1)*0.5f;
    Vec3 p{s.Position[0]-middle,s.Position[1]-middle,s.Position[2]-middle}; p[s.Axis]+=0.5f*s.Sign;
    Vec3 velocity{}; velocity[a]=-p[b]; velocity[b]=p[a]; velocity=Camera(velocity,view);
    Vec3 desired=Camera(bases[travel],view);
    if ((control ? desired.Y : desired.X)<0) desired=desired*(-1);
    int direction=velocity.X*desired.X+velocity.Y*desired.Y>=0 ? 1 : -1;
    if (!clockwise) direction=-direction;
    const Move move{fixed,s.Position[fixed],a,b,direction}; layout.Validate(move); return move;
}
}
