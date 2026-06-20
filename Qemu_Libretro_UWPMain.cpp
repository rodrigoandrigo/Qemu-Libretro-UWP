#include "pch.h"
#include "Qemu_Libretro_UWPMain.h"
#include "Common\DirectXHelper.h"

using namespace Qemu_Libretro_UWP;
using namespace Windows::Foundation;
using namespace Windows::System::Threading;
using namespace Concurrency;

// Loads and initializes app assets when the app is loaded.
Qemu_Libretro_UWPMain::Qemu_Libretro_UWPMain(const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	m_deviceResources(deviceResources),
	m_pointerPressed(false)
{
	// Registre-se para ser notificado se o Dispositivo for perdido ou recriado
	m_deviceResources->RegisterDeviceNotify(this);

	m_frameRenderer = std::unique_ptr<LibretroFrameRenderer>(new LibretroFrameRenderer(m_deviceResources, &m_host));

	m_timer.SetFixedTimeStep(true);
	m_timer.SetTargetElapsedSeconds(1.0 / 60);
}

Qemu_Libretro_UWPMain::~Qemu_Libretro_UWPMain()
{
	// Unregister device notification.
	m_deviceResources->RegisterDeviceNotify(nullptr);
}

// Updates app state when the window size changes, such as device orientation changes.
void Qemu_Libretro_UWPMain::CreateWindowSizeDependentResources() 
{
	m_frameRenderer->CreateWindowSizeDependentResources();
}

void Qemu_Libretro_UWPMain::StartRenderLoop()
{
	// If the animation renderer loop is already running, do not start another thread.
	if (m_renderLoopWorker != nullptr && m_renderLoopWorker->Status == AsyncStatus::Started)
	{
		return;
	}

	// Create a task that runs on a background thread.
	auto workItemHandler = ref new WorkItemHandler([this](IAsyncAction ^ action)
	{
		// Compute the updated frame and render once per vertical blank interval.
		while (action->Status == AsyncStatus::Started)
		{
			critical_section::scoped_lock lock(m_criticalSection);
			Update();
			if (Render())
			{
				m_deviceResources->Present();
			}
		}
	});

	// Execute a tarefa em um thread em segundo plano de alta prioridade dedicado.
	m_renderLoopWorker = ThreadPool::RunAsync(workItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);
}

void Qemu_Libretro_UWPMain::StopRenderLoop()
{
	m_renderLoopWorker->Cancel();
}

// Updates app state once per frame.
void Qemu_Libretro_UWPMain::Update() 
{
	ProcessInput();

	m_timer.Tick([&]()
	{
	});
}

// Process all user input before updating game state.
void Qemu_Libretro_UWPMain::ProcessInput()
{
}

// Renders the current frame according to the current app state.
// Retorna true se o quadro foi renderizado e estiver pronto para ser exibido.
bool Qemu_Libretro_UWPMain::Render() 
{
	// Do not render before the first update.
	if (m_timer.GetFrameCount() == 0)
	{
		return false;
	}

	auto context = m_deviceResources->GetD3DDeviceContext();

	// Redefina o visor como destino para toda a tela.
	auto viewport = m_deviceResources->GetScreenViewport();
	context->RSSetViewports(1, &viewport);

	// Reset render targets to the screen.
	ID3D11RenderTargetView *const targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };
	context->OMSetRenderTargets(1, targets, m_deviceResources->GetDepthStencilView());

	// Clear the back buffer and depth stencil view.
	context->ClearRenderTargetView(m_deviceResources->GetBackBufferRenderTargetView(), DirectX::Colors::Black);
	context->ClearDepthStencilView(m_deviceResources->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	m_frameRenderer->Render();

	return true;
}

bool Qemu_Libretro_UWPMain::LoadGame(Windows::Storage::StorageFile^ file, std::wstring* error)
{
	return m_host.LoadGame(file, error);
}

void Qemu_Libretro_UWPMain::RunLoadedGame()
{
	m_host.RunLoadedGame();
}

void Qemu_Libretro_UWPMain::ResetCore()
{
	m_host.Reset();
}

void Qemu_Libretro_UWPMain::SetProgressCallback(std::function<void(const std::wstring&)> callback)
{
	m_host.SetProgressCallback(callback);
}

void Qemu_Libretro_UWPMain::SetKey(unsigned retroKey, bool pressed)
{
	m_host.SetKey(retroKey, pressed);
}

void Qemu_Libretro_UWPMain::SetPointer(float positionX, float positionY, bool left, bool right, bool middle)
{
	m_host.SetPointer(positionX, positionY, left, right, middle);
}

void Qemu_Libretro_UWPMain::ClearPointer()
{
	m_pointerPressed = false;
	m_host.ClearPointer();
}

void Qemu_Libretro_UWPMain::ClearInput()
{
	m_pointerPressed = false;
	m_host.ClearInput();
}

std::wstring Qemu_Libretro_UWPMain::StatusText() const
{
	return m_host.StatusText();
}

void Qemu_Libretro_UWPMain::TrackingUpdate(float positionX, float positionY)
{
	m_host.SetPointer(positionX, positionY, m_pointerPressed, false, false);
}

void Qemu_Libretro_UWPMain::StopTracking()
{
	m_pointerPressed = false;
	m_host.ClearPointer();
}

// Notifies renderers that device resources must be released.
void Qemu_Libretro_UWPMain::OnDeviceLost()
{
	m_frameRenderer->ReleaseDeviceDependentResources();
}

// Notifies renderers that device resources can now be recreated.
void Qemu_Libretro_UWPMain::OnDeviceRestored()
{
	m_frameRenderer->CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();
}
