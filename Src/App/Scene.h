#pragma once
#include "Core/Timeline.h"
#include <array>

namespace Burnside
{
struct Vec3
{
    float X = 0, Y = 0, Z = 0;
    Vec3 operator+(Vec3 v) const { return {X + v.X, Y + v.Y, Z + v.Z}; }
    Vec3 operator-(Vec3 v) const { return {X - v.X, Y - v.Y, Z - v.Z}; }
    Vec3 operator*(float s) const { return {X * s, Y * s, Z * s}; }
    float& operator[](int axis) { return axis == 0 ? X : axis == 1 ? Y : Z; }
    float operator[](int axis) const { return axis == 0 ? X : axis == 1 ? Y : Z; }
};
struct Vertex
{
    Vec3 Position;
    uint32_t Color;
    Vec3 Normal{0,0,1};
    float U = 0, V = 0;
    float Material = 0;
    float Highlight = 0;
    float ClipW = 1;
};
struct ViewSettings
{
    float Yaw = -0.58f, Pitch = 0.40f, Zoom = 0.8f;
    float VerticalFov = 35.0f;
};
struct ProjectedPoint { Vec3 Position; float ClipW = 1; };
struct TurnHint
{
    Move Rotation;
    std::array<Vec3,17> Path;
};
struct SceneFrame
{
    std::vector<Vertex> Vertices;
    int HoveredSticker = -1;
    size_t VisibleStickers = 0;
    size_t RenderedCubies = 0;
    int ViewportLeft = 0, ViewportRight = 0;
    std::vector<Vec3> StickerCenters;
    std::optional<Move> HoverMove;
    std::vector<TurnHint> TurnHints;
};

uint32_t FaceColor(int face);
ProjectedPoint ProjectPoint(Vec3 point, const ViewSettings& view, int width, int height, int left, int right);
SceneFrame BuildScene(const Timeline& timeline, const ViewSettings& view,
                      int width, int height, int left, int right, int mouseX, int mouseY, bool control = false);
Move ClickMove(const Layout& layout, size_t sticker, const ViewSettings& view, bool control, bool clockwise);
}
