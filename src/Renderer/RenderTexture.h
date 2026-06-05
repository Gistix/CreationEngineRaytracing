#pragma once

struct RenderTexture
{
	nvrhi::TextureHandle handle = nullptr;
	HANDLE sharedHandle = nullptr;
	winrt::com_ptr<ID3D12Resource> d3d12Resource = nullptr;
	winrt::com_ptr<ID3D11Texture2D> d3d11Texture = nullptr;
};