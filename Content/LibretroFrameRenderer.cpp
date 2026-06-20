#include "pch.h"
#include "LibretroFrameRenderer.h"

#include "..\Common\DirectXHelper.h"

#include <algorithm>

using namespace Qemu_Libretro_UWP;
using namespace Microsoft::WRL;
using namespace DirectX;

namespace
{
	struct FrameVertex
	{
		XMFLOAT2 position;
		XMFLOAT2 texcoord;
	};
}

LibretroFrameRenderer::LibretroFrameRenderer(
	const std::shared_ptr<DX::DeviceResources>& deviceResources,
	LibretroHost* host) :
	m_deviceResources(deviceResources),
	m_host(host),
	m_textureWidth(0),
	m_textureHeight(0),
	m_loadingComplete(false)
{
	CreateDeviceDependentResources();
}

void LibretroFrameRenderer::CreateWindowSizeDependentResources()
{
}

void LibretroFrameRenderer::CreateDeviceDependentResources()
{
	auto loadVSTask = DX::ReadDataAsync(L"LibretroFrameVertexShader.cso");
	auto loadPSTask = DX::ReadDataAsync(L"LibretroFramePixelShader.cso");

	auto createVSTask = loadVSTask.then([this](const std::vector<byte>& fileData)
	{
		DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateVertexShader(
			fileData.data(), fileData.size(), nullptr, &m_vertexShader));

		static const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateInputLayout(
			vertexDesc, ARRAYSIZE(vertexDesc), fileData.data(), fileData.size(), &m_inputLayout));
	});

	auto createPSTask = loadPSTask.then([this](const std::vector<byte>& fileData)
	{
		DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreatePixelShader(
			fileData.data(), fileData.size(), nullptr, &m_pixelShader));
	});

	(createVSTask && createPSTask).then([this]()
	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateSamplerState(&samplerDesc, &m_samplerState));

		CD3D11_BUFFER_DESC constantBufferDesc(sizeof(LibretroFrameConstants), D3D11_BIND_CONSTANT_BUFFER);
		DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBuffer(&constantBufferDesc, nullptr, &m_constantBuffer));

		static const FrameVertex vertices[] =
		{
			{ XMFLOAT2(-1.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) },
			{ XMFLOAT2(-1.0f,  1.0f), XMFLOAT2(0.0f, 0.0f) },
			{ XMFLOAT2( 1.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },
			{ XMFLOAT2( 1.0f,  1.0f), XMFLOAT2(1.0f, 0.0f) },
		};
		D3D11_SUBRESOURCE_DATA vertexData = {};
		vertexData.pSysMem = vertices;
		CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(vertices), D3D11_BIND_VERTEX_BUFFER);
		DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBuffer(&vertexBufferDesc, &vertexData, &m_vertexBuffer));

		m_loadingComplete = true;
	});
}

void LibretroFrameRenderer::ReleaseDeviceDependentResources()
{
	m_loadingComplete = false;
	m_vertexShader.Reset();
	m_pixelShader.Reset();
	m_inputLayout.Reset();
	m_vertexBuffer.Reset();
	m_frameTexture.Reset();
	m_frameTextureView.Reset();
	m_samplerState.Reset();
	m_constantBuffer.Reset();
	m_textureWidth = 0;
	m_textureHeight = 0;
}

void LibretroFrameRenderer::EnsureFrameTexture(const LibretroFrameSnapshot& frame)
{
	if (m_frameTexture && m_textureWidth == frame.width && m_textureHeight == frame.height)
	{
		return;
	}

	m_frameTexture.Reset();
	m_frameTextureView.Reset();

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width = frame.width;
	textureDesc.Height = frame.height;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateTexture2D(&textureDesc, nullptr, &m_frameTexture));
	DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateShaderResourceView(
		m_frameTexture.Get(), nullptr, &m_frameTextureView));

	m_textureWidth = frame.width;
	m_textureHeight = frame.height;
}

void LibretroFrameRenderer::UploadFrameTexture(const LibretroFrameSnapshot& frame)
{
	if (frame.pixels.empty())
	{
		return;
	}

	m_deviceResources->GetD3DDeviceContext()->UpdateSubresource1(
		m_frameTexture.Get(), 0, nullptr, frame.pixels.data(), frame.width * 4, 0, 0);
}

LibretroFrameConstants LibretroFrameRenderer::BuildConstants(const LibretroFrameSnapshot& frame) const
{
	Windows::Foundation::Size output = m_deviceResources->GetOutputSize();
	float outputWidth = output.Width > 1.0f ? output.Width : 1.0f;
	float outputHeight = output.Height > 1.0f ? output.Height : 1.0f;
	float frameAspect = static_cast<float>(frame.width) / static_cast<float>(frame.height);
	float outputAspect = outputWidth / outputHeight;

	float scaleX = 1.0f;
	float scaleY = 1.0f;
	if (outputAspect > frameAspect)
	{
		scaleX = frameAspect / outputAspect;
	}
	else
	{
		scaleY = outputAspect / frameAspect;
	}

	LibretroFrameConstants constants = {};
	constants.scaleOffset = XMFLOAT4(scaleX, scaleY, 0.0f, 0.0f);
	return constants;
}

bool LibretroFrameRenderer::Render()
{
	auto context = m_deviceResources->GetD3DDeviceContext();
	auto viewport = m_deviceResources->GetScreenViewport();
	context->RSSetViewports(1, &viewport);

	ID3D11RenderTargetView* const targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };
	context->OMSetRenderTargets(1, targets, nullptr);
	context->ClearRenderTargetView(m_deviceResources->GetBackBufferRenderTargetView(), DirectX::Colors::Black);

	if (!m_loadingComplete || !m_host)
	{
		return true;
	}

	LibretroFrameSnapshot frame = m_host->CopyFrame(m_frameTexture == nullptr);
	if (!frame.valid)
	{
		return true;
	}

	EnsureFrameTexture(frame);
	if (frame.dirty)
	{
		UploadFrameTexture(frame);
	}

	LibretroFrameConstants constants = BuildConstants(frame);
	context->UpdateSubresource1(m_constantBuffer.Get(), 0, nullptr, &constants, 0, 0, 0);

	UINT stride = sizeof(FrameVertex);
	UINT offset = 0;
	ID3D11Buffer* vertexBuffers[1] = { m_vertexBuffer.Get() };
	context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	context->IASetInputLayout(m_inputLayout.Get());

	context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	context->VSSetConstantBuffers1(0, 1, m_constantBuffer.GetAddressOf(), nullptr, nullptr);
	context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	context->PSSetShaderResources(0, 1, m_frameTextureView.GetAddressOf());
	context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
	context->Draw(4, 0);

	ID3D11ShaderResourceView* nullViews[1] = { nullptr };
	context->PSSetShaderResources(0, 1, nullViews);
	return true;
}

