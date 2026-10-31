#pragma once
#include "Scene.h"
#include "ImguiRenderDX11.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace Burnside
{
class Renderer
{
public:
    bool Initialize(HWND window, bool warp = false);
    bool Resize(int width, int height);
    bool Draw(const SceneFrame& scene);
    bool SaveBMP(const std::wstring& path);
    uint64_t UIHash(int leftWidth, int rightWidth);
    const std::string& Error() const { return m_Error; }
    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    UINT SampleCount() const { return m_Samples; }
private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_Context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_Target;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_BackBuffer;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Composite;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_CompositeTarget;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Readback;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_SceneColor;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_SceneTarget;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Depth;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthView;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_VS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_PS;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_Layout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_Buffer;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_Rasterizer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_DepthState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_Blend;
    ImguiRenderDX11 m_Gui;
    UINT m_Capacity = 0;
    UINT m_Samples = 1;
    int m_Width = 1, m_Height = 1;
    std::string m_Error;
    bool ReadPixels(std::vector<uint8_t>& pixels);
};
}
