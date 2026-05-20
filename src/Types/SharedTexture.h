#pragma once

struct SharedTexture {
	ID3D12Resource* native = nullptr;
	ID3D11Texture2D* shared = nullptr;
};