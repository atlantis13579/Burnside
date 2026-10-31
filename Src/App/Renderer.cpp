#include "Renderer.h"
#include <d3dcompiler.h>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cstddef>
#include <stdexcept>

namespace Burnside
{
using Microsoft::WRL::ComPtr;
bool Renderer::Initialize(HWND window, bool warp)
{
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow = window;
    desc.SampleDesc.Count = 1; desc.Windowed = TRUE;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, levels, 1, D3D11_SDK_VERSION, &desc, &m_SwapChain, &m_Device, &level, &m_Context);
    if (FAILED(hr) && !warp) return Initialize(window, true);
    if (FAILED(hr)) { m_Error = "Unable to create a Direct3D 11 device"; return false; }
    ComPtr<IDXGIFactory> factory;
    if (SUCCEEDED(m_SwapChain->GetParent(IID_PPV_ARGS(&factory)))) factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    const char* shader = R"(
struct Vertex { float3 p : POSITION; float4 c : COLOR; float3 n : NORMAL;
    float2 uv : TEXCOORD0; float2 material : TEXCOORD1; float clipW : TEXCOORD2; };
struct Pixel { float4 p : SV_POSITION; float4 c : COLOR; float3 n : NORMAL;
    float2 uv : TEXCOORD0; float2 material : TEXCOORD1; };
Pixel VS(Vertex v) {
    Pixel o; o.p=float4(v.p*v.clipW,v.clipW); o.c=v.c; o.n=v.n; o.uv=v.uv; o.material=v.material; return o;
}
float4 PS(Pixel p) : SV_TARGET {
    float kind=p.material.x, selected=p.material.y;
    if (kind > 3.5) {
        float halo=exp(-1.8*dot(p.uv*float2(0.9,1.1),p.uv*float2(0.9,1.1)));
        float3 bg=lerp(float3(0.024,0.034,0.054),float3(0.065,0.086,0.125),halo);
        return float4(bg,1);
    }
    if (kind > 2.5) {
        float r=dot(p.uv,p.uv);
        return float4(0.005,0.009,0.018,0.48*exp(-3.8*r)*saturate((1-r)*5));
    }
    float alpha=1, edge=0;
    if (kind > 0.5) {
        float radius=kind > 1.5 ? 0.36 : 0.16;
        float2 q=abs(p.uv)-(1-radius);
        float sdf=length(max(q,0))+min(max(q.x,q.y),0)-radius;
        float aa=max(fwidth(sdf),0.002);
        alpha=saturate(0.5-sdf/aa);
        clip(alpha-0.01);
        edge=1-smoothstep(0.025,0.07,-sdf);
    }
    float3 n=normalize(kind > 1.5 ? float3(p.uv*0.28,1) : p.n);
    if (n.z < 0) n=-n;
    float3 key=normalize(float3(-0.45,0.75,1.1));
    float3 fill=normalize(float3(0.8,0.1,0.45));
    float diffuse=0.36+0.48*saturate(dot(n,key))+0.16*saturate(dot(n,fill));
    float spec=pow(saturate(dot(n,normalize(key+float3(0,0,1)))),kind < 0.5 ? 38 : 70);
    float fresnel=pow(1-saturate(n.z),3);
    float3 color=pow(p.c.rgb,2.2)*diffuse;
    color += spec*(kind < 0.5 ? 0.025 : 0.020)+fresnel*float3(0.004,0.006,0.010);
    if (kind > 0.5) {
        color *= 1-0.15*edge;
        color += 0.008*(1-p.uv.y)*saturate(n.z);
    }
    if (selected > 0.5) {
        float3 accent=float3(1,1,1);
        color=lerp(color,pow(accent,2.2),kind > 0.5 ? edge*0.9 : 0.16);
        color += pow(accent,2.2)*0.012;
    }
    return float4(pow(saturate(color),1.0/2.2),alpha);
}
)";
    ComPtr<ID3DBlob> vs, ps, errors;
    if (FAILED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vs, &errors)) ||
        FAILED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &ps, &errors)))
    { m_Error = "Scene shader compilation failed"; return false; }
    D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,offsetof(Vertex,Color),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(Vertex,Normal),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(Vertex,U),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(Vertex,Material),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32_FLOAT,0,offsetof(Vertex,ClipW),D3D11_INPUT_PER_VERTEX_DATA,0}};
    if (FAILED(m_Device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&m_VS)) ||
        FAILED(m_Device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&m_PS)) ||
        FAILED(m_Device->CreateInputLayout(elements,6,vs->GetBufferPointer(),vs->GetBufferSize(),&m_Layout)))
    { m_Error = "Scene shader creation failed"; return false; }
    for (UINT samples : {4u,2u})
    {
        UINT colorQuality=0, depthQuality=0;
        if (SUCCEEDED(m_Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,samples,&colorQuality)) &&
            SUCCEEDED(m_Device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT,samples,&depthQuality)) && colorQuality && depthQuality)
        { m_Samples=samples; break; }
    }
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE; raster.MultisampleEnable = TRUE; raster.ScissorEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable=TRUE;
    blend.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA; blend.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE; blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_Device->CreateRasterizerState(&raster,&m_Rasterizer)) || FAILED(m_Device->CreateDepthStencilState(&depth,&m_DepthState)) ||
        FAILED(m_Device->CreateBlendState(&blend,&m_Blend)) ||
        !m_Gui.Init(m_Device.Get(),m_Context.Get())) { m_Error = "Renderer state initialization failed"; return false; }
    RECT client{}; GetClientRect(window,&client); return Resize(client.right,client.bottom);
}
bool Renderer::Resize(int width, int height)
{
    if (!m_SwapChain || width <= 0 || height <= 0) return true;
    m_Context->OMSetRenderTargets(0,nullptr,nullptr);
    m_Target.Reset(); m_BackBuffer.Reset(); m_SceneTarget.Reset(); m_SceneColor.Reset(); m_DepthView.Reset(); m_Depth.Reset();
    m_CompositeTarget.Reset(); m_Composite.Reset(); m_Readback.Reset();
    if (FAILED(m_SwapChain->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0))) { m_Error = "Swap chain resize failed"; return false; }
    if (FAILED(m_SwapChain->GetBuffer(0,IID_PPV_ARGS(&m_BackBuffer))) ||
        FAILED(m_Device->CreateRenderTargetView(m_BackBuffer.Get(),nullptr,&m_Target))) return false;
    D3D11_TEXTURE2D_DESC desc{}; desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.SampleDesc.Count = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(m_Device->CreateTexture2D(&desc,nullptr,&m_Composite)) ||
        FAILED(m_Device->CreateRenderTargetView(m_Composite.Get(),nullptr,&m_CompositeTarget))) return false;
    desc.SampleDesc.Count = m_Samples; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(m_Device->CreateTexture2D(&desc,nullptr,&m_SceneColor)) ||
        FAILED(m_Device->CreateRenderTargetView(m_SceneColor.Get(),nullptr,&m_SceneTarget))) return false;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(m_Device->CreateTexture2D(&desc,nullptr,&m_Depth)) ||
        FAILED(m_Device->CreateDepthStencilView(m_Depth.Get(),nullptr,&m_DepthView))) return false;
    m_Width = width; m_Height = height; return true;
}
bool Renderer::Draw(const SceneFrame& scene)
{
    const float clear[] = {0.035f,0.048f,0.073f,1};
    m_Context->OMSetRenderTargets(1,m_SceneTarget.GetAddressOf(),m_DepthView.Get());
    m_Context->ClearRenderTargetView(m_SceneTarget.Get(),clear);
    m_Context->ClearDepthStencilView(m_DepthView.Get(),D3D11_CLEAR_DEPTH,1,0);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(m_Width),static_cast<float>(m_Height),0,1};
    m_Context->RSSetViewports(1,&viewport); m_Context->RSSetState(m_Rasterizer.Get());
    D3D11_RECT scissor{scene.ViewportLeft,0,scene.ViewportRight,m_Height}; m_Context->RSSetScissorRects(1,&scissor);
    m_Context->OMSetDepthStencilState(m_DepthState.Get(),0); m_Context->OMSetBlendState(m_Blend.Get(),nullptr,0xffffffff);
    if (!scene.Vertices.empty())
    {
        const UINT count = static_cast<UINT>(scene.Vertices.size());
        if (count > m_Capacity)
        {
            m_Buffer.Reset(); m_Capacity = count + 1024;
            D3D11_BUFFER_DESC desc{}; desc.ByteWidth = m_Capacity * sizeof(Vertex);
            desc.Usage = D3D11_USAGE_DYNAMIC; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(m_Device->CreateBuffer(&desc,nullptr,&m_Buffer))) return false;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_Context->Map(m_Buffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
        std::memcpy(mapped.pData,scene.Vertices.data(),count*sizeof(Vertex)); m_Context->Unmap(m_Buffer.Get(),0);
        const UINT stride = sizeof(Vertex), offset = 0;
        m_Context->IASetInputLayout(m_Layout.Get()); m_Context->IASetVertexBuffers(0,1,m_Buffer.GetAddressOf(),&stride,&offset);
        m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_Context->VSSetShader(m_VS.Get(),nullptr,0); m_Context->PSSetShader(m_PS.Get(),nullptr,0);
        m_Context->Draw(count,0);
    }
    m_Context->OMSetRenderTargets(0,nullptr,nullptr);
    if (m_Samples > 1) m_Context->ResolveSubresource(m_Composite.Get(),0,m_SceneColor.Get(),0,DXGI_FORMAT_R8G8B8A8_UNORM);
    else m_Context->CopyResource(m_Composite.Get(),m_SceneColor.Get());
    m_Context->OMSetRenderTargets(1,m_CompositeTarget.GetAddressOf(),nullptr);
    m_Gui.Draw(m_Width,m_Height);
    m_Context->OMSetRenderTargets(0,nullptr,nullptr);
    m_Context->CopyResource(m_BackBuffer.Get(),m_Composite.Get());
    const HRESULT hr = m_SwapChain->Present(1,0);
    if (FAILED(hr)) { m_Error = "Direct3D present failed (device lost)"; return false; }
    return true;
}
bool Renderer::SaveBMP(const std::wstring& path)
{
    std::vector<uint8_t> pixels;
    if (!ReadPixels(pixels)) return false;
    BITMAPFILEHEADER header{}; BITMAPINFOHEADER info{};
    header.bfType = 0x4d42; header.bfOffBits = sizeof(header)+sizeof(info); header.bfSize = header.bfOffBits+m_Width*m_Height*4;
    info.biSize = sizeof(info); info.biWidth = m_Width; info.biHeight = -m_Height; info.biPlanes = 1; info.biBitCount = 32;
    std::ofstream out(std::filesystem::path(path),std::ios::binary);
    out.write(reinterpret_cast<char*>(&header),sizeof(header)); out.write(reinterpret_cast<char*>(&info),sizeof(info));
    std::vector<uint8_t> row(m_Width*4);
    for (int y = 0; y < m_Height; ++y)
    {
        const auto* source = pixels.data()+y*m_Width*4;
        for (int x = 0; x < m_Width; ++x)
        { row[x*4] = source[x*4+2]; row[x*4+1] = source[x*4+1]; row[x*4+2] = source[x*4]; row[x*4+3] = 255; }
        out.write(reinterpret_cast<const char*>(row.data()),row.size());
    }
    return static_cast<bool>(out);
}
bool Renderer::ReadPixels(std::vector<uint8_t>& pixels)
{
    if (!m_Readback)
    {
        D3D11_TEXTURE2D_DESC desc{}; m_Composite->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if (FAILED(m_Device->CreateTexture2D(&desc,nullptr,&m_Readback))) return false;
    }
    m_Context->CopyResource(m_Readback.Get(),m_Composite.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_Context->Map(m_Readback.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;
    pixels.resize(static_cast<size_t>(m_Width)*m_Height*4);
    for (int y=0; y<m_Height; ++y)
        std::memcpy(pixels.data()+y*m_Width*4,static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch,m_Width*4);
    m_Context->Unmap(m_Readback.Get(),0); return true;
}
uint64_t Renderer::UIHash(int leftWidth, int rightWidth)
{
    std::vector<uint8_t> pixels;
    if (!ReadPixels(pixels)) throw std::runtime_error("UI readback failed");
    uint64_t hash=1469598103934665603ull;
    for (int y=0; y<m_Height; ++y)
        for (int x=0; x<m_Width; ++x)
            if (x<leftWidth || x>=m_Width-rightWidth)
                for (int c=0; c<4; ++c) { hash^=pixels[(y*m_Width+x)*4+c]; hash*=1099511628211ull; }
    return hash;
}
}
