#pragma once

#include "..\Common\DeviceResources.h"
#include "..\LibretroHost.h"

namespace Qemu_Libretro_UWP
{
	struct LibretroFrameConstants
	{
		DirectX::XMFLOAT4 scaleOffset;
	};

	class LibretroFrameRenderer
	{
	public:
		LibretroFrameRenderer(
			const std::shared_ptr<DX::DeviceResources>& deviceResources,
			LibretroHost* host);

		void CreateWindowSizeDependentResources();
		void CreateDeviceDependentResources();
		void ReleaseDeviceDependentResources();
		bool Render();

	private:
		void EnsureFrameTexture(const LibretroFrameSnapshot& frame);
		void UploadFrameTexture(const LibretroFrameSnapshot& frame);
		LibretroFrameConstants BuildConstants(const LibretroFrameSnapshot& frame) const;

		std::shared_ptr<DX::DeviceResources> m_deviceResources;
		LibretroHost* m_host;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> m_frameTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_frameTextureView;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
		unsigned m_textureWidth;
		unsigned m_textureHeight;
		bool m_loadingComplete;
	};
}

