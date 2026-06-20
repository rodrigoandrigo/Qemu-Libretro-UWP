#pragma once

#include "LibretroApi.h"
#include "LibretroCore.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Qemu_Libretro_UWP
{
	struct LibretroFrameSnapshot
	{
		unsigned width;
		unsigned height;
		bool valid;
		bool dirty;
		std::vector<uint32_t> pixels;
	};

	class LibretroHost
	{
	public:
		LibretroHost();
		~LibretroHost();

		bool LoadGame(Windows::Storage::StorageFile^ file, std::wstring* error);
		void RunLoadedGame();
		void Stop();
		void Reset();
		void SetProgressCallback(std::function<void(const std::wstring&)> callback);
		void SetKey(unsigned retroKey, bool pressed);
		void SetPointer(float x, float y, bool left, bool right, bool middle);
		void ClearPointer();
		void ClearInput();
		LibretroFrameSnapshot CopyFrame(bool forcePixels = false);
		std::wstring StatusText() const;

	private:
		static LibretroHost* s_activeHost;

		static bool EnvironmentCallback(unsigned cmd, void* data);
		static void VideoRefreshCallback(const void* data, unsigned width, unsigned height, size_t pitch);
		static void AudioSampleCallback(int16_t left, int16_t right);
		static size_t AudioSampleBatchCallback(const int16_t* data, size_t frames);
		static void InputPollCallback();
		static int16_t InputStateCallback(unsigned port, unsigned device, unsigned index, unsigned id);
		static void LogCallback(int level, const char* format, ...);
		static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* info);
		static void SignalHandler(int signal);
		static void AtExitHandler();
		static void TerminateHandler();
		static void InvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved);

		bool OnEnvironment(unsigned cmd, void* data);
		void OnVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch);
		int16_t OnInputState(unsigned port, unsigned device, unsigned index, unsigned id);
		void RunLoop(Windows::Foundation::IAsyncAction^ action);
		void StartRunLoop();
		void SetStatus(const std::wstring& text);
		void Trace(const std::wstring& text);
		static void InstallProcessDiagnostics();
		static void TraceProcessDiagnostic(const std::wstring& text);

		static std::string Narrow(Platform::String^ value);
		static std::string Narrow(const std::wstring& value);
		static std::wstring Widen(const char* value);
		static uint32_t ConvertPixel16(uint16_t value, retro_pixel_format format);

		LibretroCore m_core;
		std::atomic<bool> m_running;
		std::atomic<bool> m_waitingForFirstRun;
		Windows::Foundation::IAsyncAction^ m_worker;

		mutable std::mutex m_frameMutex;
		unsigned m_frameWidth;
		unsigned m_frameHeight;
		unsigned m_videoFrameCount;
		unsigned m_emptyVideoFrameCount;
		bool m_frameValid;
		bool m_frameDirty;
		std::vector<uint32_t> m_framePixels;

		mutable std::mutex m_inputMutex;
		std::array<bool, 512> m_keys;
		int16_t m_mouseX;
		int16_t m_mouseY;
		float m_lastPointerX;
		float m_lastPointerY;
		bool m_havePointerPosition;
		bool m_mouseLeft;
		bool m_mouseRight;
		bool m_mouseMiddle;
		retro_keyboard_callback m_keyboardCallback;

		mutable std::mutex m_statusMutex;
		std::wstring m_status;
		mutable std::mutex m_progressMutex;
		std::function<void(const std::wstring&)> m_progressCallback;
		std::wstring m_logPath;
		std::wstring m_stderrPath;
		std::string m_systemDirectory;
		std::string m_saveDirectory;
		std::string m_gamePath;
		retro_pixel_format m_pixelFormat;
		double m_frameDelayMs;
		unsigned m_runLoopCount;
	};
}
