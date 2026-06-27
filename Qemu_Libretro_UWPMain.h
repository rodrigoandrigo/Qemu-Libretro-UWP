#pragma once

#include "Common\StepTimer.h"
#include "Common\DeviceResources.h"
#include "Content\LibretroFrameRenderer.h"
#include "LibretroHost.h"

#include <functional>
#include <string>

// Renders Direct2D and 3D content on screen.
namespace Qemu_Libretro_UWP
{
	class Qemu_Libretro_UWPMain : public DX::IDeviceNotify
	{
	public:
		Qemu_Libretro_UWPMain(const std::shared_ptr<DX::DeviceResources>& deviceResources);
		~Qemu_Libretro_UWPMain();
		void CreateWindowSizeDependentResources();
		void StartTracking() { m_pointerPressed = true; }
		void TrackingUpdate(float positionX, float positionY);
		void StopTracking();
		bool IsTracking() { return m_pointerPressed; }
		void StartRenderLoop();
		void StopRenderLoop();
		bool LoadGame(Windows::Storage::StorageFile^ file, std::wstring* error);
		void RunLoadedGame();
		bool PauseEmulator();
		bool ResumeEmulator();
		bool StopEmulator();
		bool ShutdownEmulator();
		void ResetCore();
		void SetProgressCallback(std::function<void(const std::wstring&)> callback);
		void SetKey(unsigned retroKey, bool pressed);
		void SetPointer(float positionX, float positionY, bool left, bool right, bool middle);
		void ClearPointer();
		void ClearInput();
		std::wstring StatusText() const;
		Concurrency::critical_section& GetCriticalSection() { return m_criticalSection; }

		// IDeviceNotify
		virtual void OnDeviceLost();
		virtual void OnDeviceRestored();

	private:
		void ProcessInput();
		void Update();
		bool Render();

		// Cached pointer to device resources.
		std::shared_ptr<DX::DeviceResources> m_deviceResources;

		LibretroHost m_host;
		std::unique_ptr<LibretroFrameRenderer> m_frameRenderer;

		Windows::Foundation::IAsyncAction^ m_renderLoopWorker;
		Concurrency::critical_section m_criticalSection;

		// Render loop timer.
		DX::StepTimer m_timer;

		// Track the current input pointer position.
		bool m_pointerPressed;
	};
}
