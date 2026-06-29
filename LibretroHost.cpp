#include "pch.h"
#include "LibretroHost.h"

#include <algorithm>
#include <cstdarg>
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstdlib>
#include <crtdbg.h>
#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <io.h>
#include <new>
#include <thread>
#include <windows.storage.h>

using namespace Qemu_Libretro_UWP;
using namespace Windows::ApplicationModel;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::System::Threading;

LibretroHost* LibretroHost::s_activeHost = nullptr;
static bool g_processDiagnosticsInstalled = false;
static std::wstring g_processDiagnosticLogPath;

LibretroHost::LibretroHost() :
	m_running(false),
	m_paused(false),
	m_waitingForFirstRun(false),
	m_worker(nullptr),
	m_frameWidth(0),
	m_frameHeight(0),
	m_videoFrameCount(0),
	m_emptyVideoFrameCount(0),
	m_frameValid(false),
	m_frameDirty(false),
	m_mouseX(0),
	m_mouseY(0),
	m_pointerX(0),
	m_pointerY(0),
	m_lastPointerX(0.0f),
	m_lastPointerY(0.0f),
	m_havePointerPosition(false),
	m_mouseLeft(false),
	m_mouseRight(false),
	m_mouseMiddle(false),
	m_pixelFormat(RETRO_PIXEL_FORMAT_XRGB8888),
	m_frameDelayMs(16.0),
	m_runLoopCount(0)
{
	m_keys.fill(false);
	m_keyboardCallback = {};
	m_systemDirectory = Narrow(Package::Current->InstalledLocation->Path);
	m_saveDirectory = Narrow(ApplicationData::Current->LocalFolder->Path);
	m_logPath = std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\qemu-uwp-native.log";
	m_stderrPath = std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\qemu-uwp-stderr.log";
	g_processDiagnosticLogPath = m_logPath;
	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	HANDLE logFile = CreateFile2(m_logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, &params);
	if (logFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(logFile);
	}
	FILE* stream = nullptr;
	_wfreopen_s(&stream, m_stderrPath.c_str(), L"w", stderr);
	_wfreopen_s(&stream, m_stderrPath.c_str(), L"a", stdout);
	setvbuf(stderr, nullptr, _IONBF, 0);
	setvbuf(stdout, nullptr, _IONBF, 0);
	InstallProcessDiagnostics();
	Trace(L"Host: stderr/stdout redirected to " + m_stderrPath);
	SetStatus(L"Select supported QEMU media or a .qemu_cmd_line file.");
}

LibretroHost::~LibretroHost()
{
	Stop();
	if (s_activeHost == this)
	{
		s_activeHost = nullptr;
	}
}

bool LibretroHost::LoadGame(StorageFile^ file, std::wstring* error)
{
	Trace(L"Host: LoadGame called.");
	if (file == nullptr)
	{
		if (error)
		{
			*error = L"No file selected.";
		}
		return false;
	}

	Trace(L"Host: stopping previous instance.");
	if (!Stop())
	{
		if (error)
		{
			*error = L"Previous emulator instance did not stop cleanly. Use Shutdown or restart the app before starting another emulator.";
		}
		return false;
	}
	s_activeHost = this;

	Trace(L"Host: loading qemu_libretro.dll.");
	if (!m_core.Load(error))
	{
		Trace(L"Host: failed to load qemu_libretro.dll.");
		return false;
	}
	Trace(L"Host: qemu_libretro.dll loaded and exports resolved.");

	Trace(L"Host: registering libretro callbacks.");
	m_core.SetCallbacks(
		&LibretroHost::EnvironmentCallback,
		&LibretroHost::VideoRefreshCallback,
		&LibretroHost::AudioSampleCallback,
		&LibretroHost::AudioSampleBatchCallback,
		&LibretroHost::InputPollCallback,
		&LibretroHost::InputStateCallback);

	Trace(L"Host: calling retro_init.");
	m_core.Init();
	Trace(L"Host: retro_init returned.");

	retro_system_info info = {};
	Trace(L"Host: calling retro_get_system_info.");
	m_core.GetSystemInfo(&info);
	Trace(L"Host: retro_get_system_info returned.");

	m_gamePath = Narrow(file->Path);
	Trace(L"Host: boot file: " + std::wstring(file->Path->Data()));
	retro_game_info game = {};
	game.path = m_gamePath.c_str();
	game.data = nullptr;
	game.size = 0;
	game.meta = nullptr;

	Trace(L"Host: calling retro_load_game.");
	if (!m_core.LoadGame(&game))
	{
		Trace(L"Host: retro_load_game returned failure.");
		if (error)
		{
			*error = L"retro_load_game failed for: " + std::wstring(file->Path->Data());
		}
		return false;
	}
	Trace(L"Host: retro_load_game returned success.");

	retro_system_av_info av = {};
	Trace(L"Host: calling retro_get_system_av_info.");
	m_core.GetSystemAvInfo(&av);
	Trace(L"Host: retro_get_system_av_info returned.");
	if (av.timing.fps > 1.0)
	{
		m_frameDelayMs = 1000.0 / av.timing.fps;
	}

	std::wstring status = L"Running ";
	status += file->Name->Data();
	if (info.library_name)
	{
		status += L" via ";
		status += std::wstring(info.library_name, info.library_name + strlen(info.library_name));
	}
	SetStatus(status);
	m_running = true;
	m_paused = false;
	Trace(L"Host: LoadGame completed.");
	return true;
}

bool LibretroHost::Pause()
{
	if (!m_running)
	{
		return false;
	}
	m_paused = true;
	SetStatus(L"Emulator paused.");
	Trace(L"Host: pause requested.");
	return true;
}

bool LibretroHost::Resume()
{
	if (!m_running)
	{
		return false;
	}
	m_paused = false;
	Trace(L"Host: resume requested.");
	StartRunLoop();
	SetStatus(L"Emulator resumed.");
	return true;
}

bool LibretroHost::Stop()
{
	m_running = false;
	m_paused = false;
	if (!WaitForRunLoopToStop(3000))
	{
		SetStatus(L"Stop requested, but the emulator core is still busy.");
		Trace(L"Host: stop timed out while waiting for retro_run to return.");
		return false;
	}
	m_core.UnloadGame();
	m_core.Deinit();
	if (s_activeHost == this)
	{
		s_activeHost = nullptr;
	}
	SetStatus(L"Emulator stopped.");
	Trace(L"Host: stopped and deinitialized.");
	return true;
}

bool LibretroHost::Shutdown()
{
	m_running = false;
	m_paused = false;
	if (!WaitForRunLoopToStop(3000))
	{
		SetStatus(L"Shutdown requested, but the emulator core is still busy.");
		Trace(L"Host: shutdown timed out while waiting for retro_run to return.");
		return false;
	}
	m_core.UnloadGame();
	m_core.Deinit();
	if (s_activeHost == this)
	{
		s_activeHost = nullptr;
	}
	SetStatus(L"Emulator core stopped.");
	Trace(L"Host: core stopped and deinitialized.");
	return true;
}

void LibretroHost::Reset()
{
	m_core.Reset();
}

void LibretroHost::RunLoadedGame()
{
	if (!m_running)
	{
		return;
	}

	Trace(L"Host: iniciando loop retro_run em thread de trabalho.");
	StartRunLoop();
}

void LibretroHost::SetProgressCallback(std::function<void(const std::wstring&)> callback)
{
	std::lock_guard<std::mutex> lock(m_progressMutex);
	m_progressCallback = callback;
}

void LibretroHost::StartRunLoop()
{
	if (!m_running)
	{
		return;
	}

	if (m_worker != nullptr && m_worker->Status == Windows::Foundation::AsyncStatus::Started)
	{
		return;
	}
	auto handler = ref new WorkItemHandler([this](IAsyncAction^ action)
	{
		RunLoop(action);
	});
	m_worker = ThreadPool::RunAsync(handler, WorkItemPriority::High, WorkItemOptions::TimeSliced);
}

void LibretroHost::RunLoop(IAsyncAction^ action)
{
	bool firstFrame = true;
	while (m_running && (action == nullptr || action->Status == Windows::Foundation::AsyncStatus::Started))
	{
		if (m_paused)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
			continue;
		}
		m_runLoopCount++;
		if (firstFrame)
		{
			Trace(L"Host: preparing first retro_run call.");
			m_waitingForFirstRun = true;
			std::thread([this]()
			{
				std::this_thread::sleep_for(std::chrono::seconds(3));
				if (m_waitingForFirstRun)
				{
					Trace(L"Host: watchdog: first retro_run has not returned after 3s.");
				}
				std::this_thread::sleep_for(std::chrono::seconds(7));
				if (m_waitingForFirstRun)
				{
					Trace(L"Host: watchdog: first retro_run has not returned after 10s; probable deadlock in libretro core.");
				}
			}).detach();
			Trace(L"Host: calling first retro_run.");
		}
		m_core.Run();
		if (firstFrame)
		{
			m_waitingForFirstRun = false;
			Trace(L"Host: first retro_run returned.");
			firstFrame = false;
		}
		else if (m_runLoopCount == 60 || m_runLoopCount == 300 || (m_runLoopCount % 1800) == 0)
		{
			Trace(L"Host: retro_run active, iterations=" + std::to_wstring(m_runLoopCount));
		}
		if (m_frameDelayMs > 1.0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(
				static_cast<int>((std::min)(m_frameDelayMs, 16.0))));
		}
	}
}

bool LibretroHost::WaitForRunLoopToStop(unsigned timeoutMs)
{
	if (m_worker == nullptr)
	{
		return true;
	}

	unsigned elapsed = 0;
	while (m_worker->Status == Windows::Foundation::AsyncStatus::Started && elapsed < timeoutMs)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		elapsed += 10;
	}

	if (m_worker->Status == Windows::Foundation::AsyncStatus::Started)
	{
		return false;
	}

	m_worker = nullptr;
	return true;
}

void LibretroHost::SetKey(unsigned retroKey, bool pressed)
{
	if (retroKey >= m_keys.size())
	{
		return;
	}

	std::lock_guard<std::mutex> lock(m_inputMutex);
	if (m_keys[retroKey] == pressed)
	{
		return;
	}
	m_keys[retroKey] = pressed;
	if (m_keyboardCallback.callback != nullptr)
	{
		m_keyboardCallback.callback(pressed, retroKey, 0, 0);
	}
}

void LibretroHost::SetPointer(float x, float y, bool left, bool right, bool middle)
{
	std::lock_guard<std::mutex> lock(m_inputMutex);
	m_pointerX = static_cast<int16_t>((std::max)(-32767, (std::min)(32767, static_cast<int>(std::lround(x)))));
	m_pointerY = static_cast<int16_t>((std::max)(-32767, (std::min)(32767, static_cast<int>(std::lround(y)))));
	m_lastPointerX = x;
	m_lastPointerY = y;
	m_havePointerPosition = true;
	m_mouseLeft = left;
	m_mouseRight = right;
	m_mouseMiddle = middle;
}

void LibretroHost::AddMouseDelta(int deltaX, int deltaY, bool left, bool right, bool middle)
{
	std::lock_guard<std::mutex> lock(m_inputMutex);
	int accumulatedX = static_cast<int>(m_mouseX) + deltaX;
	int accumulatedY = static_cast<int>(m_mouseY) + deltaY;
	m_mouseX = static_cast<int16_t>((std::max)(-32768, (std::min)(32767, accumulatedX)));
	m_mouseY = static_cast<int16_t>((std::max)(-32768, (std::min)(32767, accumulatedY)));
	m_mouseLeft = left;
	m_mouseRight = right;
	m_mouseMiddle = middle;
}

void LibretroHost::ClearPointer()
{
	std::lock_guard<std::mutex> lock(m_inputMutex);
	m_mouseX = 0;
	m_mouseY = 0;
	m_pointerX = 0;
	m_pointerY = 0;
	m_lastPointerX = 0.0f;
	m_lastPointerY = 0.0f;
	m_havePointerPosition = false;
	m_mouseLeft = false;
	m_mouseRight = false;
	m_mouseMiddle = false;
}

void LibretroHost::ClearInput()
{
	std::lock_guard<std::mutex> lock(m_inputMutex);
	for (unsigned i = 0; i < m_keys.size(); i++)
	{
		if (m_keys[i] && m_keyboardCallback.callback != nullptr)
		{
			m_keyboardCallback.callback(false, i, 0, 0);
		}
		m_keys[i] = false;
	}
	m_mouseX = 0;
	m_mouseY = 0;
	m_pointerX = 0;
	m_pointerY = 0;
	m_lastPointerX = 0.0f;
	m_lastPointerY = 0.0f;
	m_havePointerPosition = false;
	m_mouseLeft = false;
	m_mouseRight = false;
	m_mouseMiddle = false;
}

LibretroFrameSnapshot LibretroHost::CopyFrame(bool forcePixels)
{
	std::lock_guard<std::mutex> lock(m_frameMutex);
	LibretroFrameSnapshot frame = {};
	frame.width = m_frameWidth;
	frame.height = m_frameHeight;
	frame.valid = m_frameValid;
	frame.dirty = m_frameDirty || forcePixels;
	if (frame.valid && (m_frameDirty || forcePixels))
	{
		frame.pixels = m_framePixels;
		m_frameDirty = false;
	}
	return frame;
}

std::wstring LibretroHost::StatusText() const
{
	std::lock_guard<std::mutex> lock(m_statusMutex);
	return m_status;
}

void LibretroHost::SetStatus(const std::wstring& text)
{
	std::lock_guard<std::mutex> lock(m_statusMutex);
	m_status = text;
}

bool LibretroHost::EnvironmentCallback(unsigned cmd, void* data)
{
	return s_activeHost ? s_activeHost->OnEnvironment(cmd, data) : false;
}

void LibretroHost::VideoRefreshCallback(const void* data, unsigned width, unsigned height, size_t pitch)
{
	if (s_activeHost)
	{
		s_activeHost->OnVideoRefresh(data, width, height, pitch);
	}
}

void LibretroHost::AudioSampleCallback(int16_t, int16_t)
{
}

size_t LibretroHost::AudioSampleBatchCallback(const int16_t*, size_t frames)
{
	return frames;
}

void LibretroHost::InputPollCallback()
{
}

int16_t LibretroHost::InputStateCallback(unsigned port, unsigned device, unsigned index, unsigned id)
{
	return s_activeHost ? s_activeHost->OnInputState(port, device, index, id) : 0;
}

void LibretroHost::LogCallback(int, const char* format, ...)
{
	if (format == nullptr)
	{
		return;
	}

	char buffer[2048] = {};
	va_list args;
	va_start(args, format);
	vsnprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
	va_end(args);

	std::wstring message = L"qemu-libretro: ";
	message += Widen(buffer);
	if (s_activeHost)
	{
		s_activeHost->Trace(message);
	}
	else
	{
		OutputDebugStringW(message.c_str());
		OutputDebugStringW(L"\r\n");
	}
}

bool LibretroHost::OnEnvironment(unsigned cmd, void* data)
{
	switch (cmd)
	{
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		m_pixelFormat = *reinterpret_cast<retro_pixel_format*>(data);
		return m_pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888 ||
			m_pixelFormat == RETRO_PIXEL_FORMAT_RGB565 ||
			m_pixelFormat == RETRO_PIXEL_FORMAT_0RGB1555;

	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*reinterpret_cast<const char**>(data) = m_systemDirectory.c_str();
		return true;

	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*reinterpret_cast<const char**>(data) = m_saveDirectory.c_str();
		return true;

	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		return true;

	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*reinterpret_cast<bool*>(data) = true;
		return true;

	case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
		if (data != nullptr)
		{
			std::lock_guard<std::mutex> lock(m_inputMutex);
			m_keyboardCallback = *reinterpret_cast<retro_keyboard_callback*>(data);
		}
		return true;

	case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
		return true;

	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		if (data != nullptr)
		{
			retro_system_av_info* info = reinterpret_cast<retro_system_av_info*>(data);
			Trace(L"Host: SET_SYSTEM_AV_INFO " + std::to_wstring(info->geometry.base_width) +
				L"x" + std::to_wstring(info->geometry.base_height) +
				L", fps=" + std::to_wstring(info->timing.fps));
		}
		return true;

	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		reinterpret_cast<retro_log_callback*>(data)->log = &LibretroHost::LogCallback;
		return true;

	case RETRO_ENVIRONMENT_SET_MESSAGE:
		if (data != nullptr)
		{
			retro_message* message = reinterpret_cast<retro_message*>(data);
			if (message->msg != nullptr)
			{
				Trace(L"qemu-libretro message: " + Widen(message->msg));
			}
		}
		return true;

	case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
		if (data != nullptr)
		{
			retro_message_ext* message = reinterpret_cast<retro_message_ext*>(data);
			if (message->msg != nullptr)
			{
				Trace(L"qemu-libretro message: " + Widen(message->msg));
			}
		}
		return true;

	case RETRO_ENVIRONMENT_SHUTDOWN:
		Trace(L"Host: core requested shutdown.");
		m_running = false;
		return true;

	default:
		Trace(L"Host: unhandled environment cmd=" + std::to_wstring(cmd));
		return false;
	}
}

void LibretroHost::OnVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
	if (data == nullptr || width == 0 || height == 0)
	{
		m_emptyVideoFrameCount++;
		if (m_emptyVideoFrameCount == 1 || m_emptyVideoFrameCount == 60 || m_emptyVideoFrameCount == 300)
		{
			Trace(L"Host: empty video_refresh, width=" + std::to_wstring(width) +
				L", height=" + std::to_wstring(height) +
				L", pitch=" + std::to_wstring(pitch) +
				L", total=" + std::to_wstring(m_emptyVideoFrameCount));
		}
		return;
	}

	std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
	if (m_pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888)
	{
		for (unsigned y = 0; y < height; y++)
		{
			const uint32_t* source = reinterpret_cast<const uint32_t*>(
				reinterpret_cast<const uint8_t*>(data) + static_cast<size_t>(y) * pitch);
			uint32_t* target = pixels.data() + static_cast<size_t>(y) * width;
			for (unsigned x = 0; x < width; x++)
			{
				target[x] = 0xff000000u | (source[x] & 0x00ffffffu);
			}
		}
	}
	else
	{
		for (unsigned y = 0; y < height; y++)
		{
			const uint16_t* source = reinterpret_cast<const uint16_t*>(
				reinterpret_cast<const uint8_t*>(data) + static_cast<size_t>(y) * pitch);
			uint32_t* target = pixels.data() + static_cast<size_t>(y) * width;
			for (unsigned x = 0; x < width; x++)
			{
				target[x] = ConvertPixel16(source[x], m_pixelFormat);
			}
		}
	}

	std::lock_guard<std::mutex> lock(m_frameMutex);
	m_frameWidth = width;
	m_frameHeight = height;
	m_framePixels.swap(pixels);
	m_frameValid = true;
	m_frameDirty = true;
	m_videoFrameCount++;
	if (m_videoFrameCount == 1 || m_videoFrameCount == 60 || m_videoFrameCount == 300)
	{
		uint64_t sample = 0;
		size_t nonBlack = 0;
		size_t sampleCount = (std::min)(m_framePixels.size(), static_cast<size_t>(4096));
		for (size_t i = 0; i < sampleCount; i++)
		{
			uint32_t pixel = m_framePixels[i];
			sample += pixel;
			if ((pixel & 0x00ffffffu) != 0)
			{
				nonBlack++;
			}
		}
		Trace(L"Host: video frame received " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
			L", pitch=" + std::to_wstring(pitch) +
			L", total=" + std::to_wstring(m_videoFrameCount) +
			L", sample=" + std::to_wstring(sample) +
			L", nonBlack=" + std::to_wstring(nonBlack));
	}
}

int16_t LibretroHost::OnInputState(unsigned, unsigned device, unsigned, unsigned id)
{
	std::lock_guard<std::mutex> lock(m_inputMutex);
	if (device == RETRO_DEVICE_KEYBOARD)
	{
		return id < m_keys.size() && m_keys[id] ? 1 : 0;
	}
	if (device == RETRO_DEVICE_MOUSE)
	{
		switch (id)
		{
		case RETRO_DEVICE_ID_MOUSE_X:
		{
			int16_t dx = m_mouseX;
			m_mouseX = 0;
			return dx;
		}
		case RETRO_DEVICE_ID_MOUSE_Y:
		{
			int16_t dy = m_mouseY;
			m_mouseY = 0;
			return dy;
		}
		case RETRO_DEVICE_ID_MOUSE_LEFT:
			return m_mouseLeft ? 1 : 0;
		case RETRO_DEVICE_ID_MOUSE_RIGHT:
			return m_mouseRight ? 1 : 0;
		case RETRO_DEVICE_ID_MOUSE_MIDDLE:
			return m_mouseMiddle ? 1 : 0;
		default:
			return 0;
		}
	}
	if (device == RETRO_DEVICE_POINTER)
	{
		switch (id)
		{
		case RETRO_DEVICE_ID_POINTER_X:
			return m_pointerX;
		case RETRO_DEVICE_ID_POINTER_Y:
			return m_pointerY;
		case RETRO_DEVICE_ID_POINTER_PRESSED:
			return m_mouseLeft ? 1 : 0;
		case RETRO_DEVICE_ID_POINTER_COUNT:
			return 1;
		default:
			return 0;
		}
	}
	return 0;
}

std::string LibretroHost::Narrow(Platform::String^ value)
{
	return value ? Narrow(std::wstring(value->Data())) : std::string();
}

std::string LibretroHost::Narrow(const std::wstring& value)
{
	if (value.empty())
	{
		return std::string();
	}

	int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	std::string result(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0], size, nullptr, nullptr);
	return result;
}

std::wstring LibretroHost::Widen(const char* value)
{
	if (value == nullptr || value[0] == '\0')
	{
		return std::wstring();
	}

	int size = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
	if (size <= 1)
	{
		return std::wstring();
	}

	int length = static_cast<int>(strlen(value));
	std::wstring result(static_cast<size_t>(size - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value, length, &result[0], size - 1);
	return result;
}

void LibretroHost::Trace(const std::wstring& text)
{
	std::wstring line = text + L"\r\n";
	OutputDebugStringW(line.c_str());

	if (!m_logPath.empty())
	{
		CREATEFILE2_EXTENDED_PARAMETERS params = {};
		params.dwSize = sizeof(params);
		HANDLE logFile = CreateFile2(m_logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, OPEN_ALWAYS, &params);
		if (logFile != INVALID_HANDLE_VALUE)
		{
			LARGE_INTEGER zero = {};
			SetFilePointerEx(logFile, zero, nullptr, FILE_END);
			std::string utf8 = Narrow(line);
			DWORD written = 0;
			WriteFile(logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
			FlushFileBuffers(logFile);
			CloseHandle(logFile);
		}
	}

	std::function<void(const std::wstring&)> callback;
	{
		std::lock_guard<std::mutex> lock(m_progressMutex);
		callback = m_progressCallback;
	}

	if (callback)
	{
		try
		{
			callback(text);
		}
		catch (...)
		{
			OutputDebugStringW(L"LibretroHost::Trace callback failed.\r\n");
		}
	}
}

void LibretroHost::InstallProcessDiagnostics()
{
	if (g_processDiagnosticsInstalled)
	{
		return;
	}

	g_processDiagnosticsInstalled = true;
	SetUnhandledExceptionFilter(&LibretroHost::UnhandledExceptionHandler);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	_set_invalid_parameter_handler(&LibretroHost::InvalidParameterHandler);
	std::signal(SIGABRT, &LibretroHost::SignalHandler);
	std::signal(SIGSEGV, &LibretroHost::SignalHandler);
	std::signal(SIGILL, &LibretroHost::SignalHandler);
	std::signal(SIGFPE, &LibretroHost::SignalHandler);
	std::set_terminate(&LibretroHost::TerminateHandler);
	std::atexit(&LibretroHost::AtExitHandler);
	TraceProcessDiagnostic(L"Host: process diagnostics installed.");
}

void LibretroHost::TraceProcessDiagnostic(const std::wstring& text)
{
	std::wstring line = text + L"\r\n";
	OutputDebugStringW(line.c_str());

	std::wstring path = g_processDiagnosticLogPath;
	if (path.empty() && s_activeHost)
	{
		path = s_activeHost->m_logPath;
	}
	if (path.empty())
	{
		return;
	}

	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	HANDLE logFile = CreateFile2(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, OPEN_ALWAYS, &params);
	if (logFile != INVALID_HANDLE_VALUE)
	{
		std::string utf8 = Narrow(line);
		DWORD written = 0;
		WriteFile(logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
		FlushFileBuffers(logFile);
		CloseHandle(logFile);
	}
}

LONG WINAPI LibretroHost::UnhandledExceptionHandler(EXCEPTION_POINTERS* info)
{
	if (info && info->ExceptionRecord)
	{
		TraceProcessDiagnostic(L"Host: unhandled exception code=0x" +
			std::to_wstring(info->ExceptionRecord->ExceptionCode) +
			L", address=0x" + std::to_wstring(reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress)));
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

void LibretroHost::SignalHandler(int signal)
{
	TraceProcessDiagnostic(L"Host: signal received=" + std::to_wstring(signal));
	std::_Exit(128 + signal);
}

void LibretroHost::AtExitHandler()
{
	TraceProcessDiagnostic(L"Host: atexit chamado; possivel exit() dentro do core.");
}

void LibretroHost::TerminateHandler()
{
	TraceProcessDiagnostic(L"Host: std::terminate chamado.");
	std::abort();
}

void LibretroHost::InvalidParameterHandler(const wchar_t*, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t)
{
	std::wstring message = L"Host: invalid parameter handler.";
	if (function)
	{
		message += L" function=";
		message += function;
	}
	if (file)
	{
		message += L" file=";
		message += file;
	}
	message += L" line=" + std::to_wstring(line);
	TraceProcessDiagnostic(message);
}

uint32_t LibretroHost::ConvertPixel16(uint16_t value, retro_pixel_format format)
{
	unsigned r = 0;
	unsigned g = 0;
	unsigned b = 0;
	if (format == RETRO_PIXEL_FORMAT_RGB565)
	{
		r = ((value >> 11) & 0x1f) << 3;
		g = ((value >> 5) & 0x3f) << 2;
		b = (value & 0x1f) << 3;
	}
	else
	{
		r = ((value >> 10) & 0x1f) << 3;
		g = ((value >> 5) & 0x1f) << 3;
		b = (value & 0x1f) << 3;
	}
	return 0xff000000u | (r << 16) | (g << 8) | b;
}
