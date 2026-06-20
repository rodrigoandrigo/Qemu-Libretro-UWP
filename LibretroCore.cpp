#include "pch.h"
#include "LibretroCore.h"

#include <sstream>

using namespace Qemu_Libretro_UWP;

LibretroCore::LibretroCore() :
	m_module(nullptr),
	m_initialized(false),
	m_gameLoaded(false),
	m_retro_set_environment(nullptr),
	m_retro_set_video_refresh(nullptr),
	m_retro_set_audio_sample(nullptr),
	m_retro_set_audio_sample_batch(nullptr),
	m_retro_set_input_poll(nullptr),
	m_retro_set_input_state(nullptr),
	m_retro_init(nullptr),
	m_retro_deinit(nullptr),
	m_retro_api_version(nullptr),
	m_retro_get_system_info(nullptr),
	m_retro_get_system_av_info(nullptr),
	m_retro_load_game(nullptr),
	m_retro_unload_game(nullptr),
	m_retro_run(nullptr),
	m_retro_reset(nullptr)
{
}

LibretroCore::~LibretroCore()
{
	if (m_gameLoaded)
	{
		UnloadGame();
	}
	if (m_initialized)
	{
		Deinit();
	}
	if (m_module)
	{
		FreeLibrary(m_module);
		m_module = nullptr;
	}
}

bool LibretroCore::Load(std::wstring* error)
{
	if (m_module)
	{
		return true;
	}

	m_module = LoadPackagedLibrary(L"qemu_libretro.dll", 0);
	if (!m_module)
	{
		DWORD code = GetLastError();
		if (error)
		{
			std::wstringstream stream;
			stream << L"LoadPackagedLibrary(qemu_libretro.dll) failed. Win32 error " << code
				<< L". The DLL must be packaged and compatible with UWP.";
			*error = stream.str();
		}
		return false;
	}

	return ResolveExports(error);
}

template <typename T>
bool LibretroCore::Resolve(const char* name, T& target, std::wstring* error)
{
	FARPROC proc = GetProcAddress(m_module, name);
	if (!proc)
	{
		if (error)
		{
			std::wstringstream stream;
			stream << L"Missing libretro export: " << name;
			*error = stream.str();
		}
		return false;
	}

	target = reinterpret_cast<T>(proc);
	return true;
}

bool LibretroCore::ResolveExports(std::wstring* error)
{
	return Resolve("retro_set_environment", m_retro_set_environment, error) &&
		Resolve("retro_set_video_refresh", m_retro_set_video_refresh, error) &&
		Resolve("retro_set_audio_sample", m_retro_set_audio_sample, error) &&
		Resolve("retro_set_audio_sample_batch", m_retro_set_audio_sample_batch, error) &&
		Resolve("retro_set_input_poll", m_retro_set_input_poll, error) &&
		Resolve("retro_set_input_state", m_retro_set_input_state, error) &&
		Resolve("retro_init", m_retro_init, error) &&
		Resolve("retro_deinit", m_retro_deinit, error) &&
		Resolve("retro_api_version", m_retro_api_version, error) &&
		Resolve("retro_get_system_info", m_retro_get_system_info, error) &&
		Resolve("retro_get_system_av_info", m_retro_get_system_av_info, error) &&
		Resolve("retro_load_game", m_retro_load_game, error) &&
		Resolve("retro_unload_game", m_retro_unload_game, error) &&
		Resolve("retro_run", m_retro_run, error) &&
		Resolve("retro_reset", m_retro_reset, error);
}

void LibretroCore::SetCallbacks(
	retro_environment_t environment,
	retro_video_refresh_t video,
	retro_audio_sample_t audio,
	retro_audio_sample_batch_t audioBatch,
	retro_input_poll_t inputPoll,
	retro_input_state_t inputState)
{
	m_retro_set_environment(environment);
	m_retro_set_video_refresh(video);
	m_retro_set_audio_sample(audio);
	m_retro_set_audio_sample_batch(audioBatch);
	m_retro_set_input_poll(inputPoll);
	m_retro_set_input_state(inputState);
}

void LibretroCore::Init()
{
	if (!m_initialized)
	{
		m_retro_init();
		m_initialized = true;
	}
}

void LibretroCore::Deinit()
{
	if (m_initialized)
	{
		m_retro_deinit();
		m_initialized = false;
	}
}

bool LibretroCore::LoadGame(const retro_game_info* game)
{
	if (m_gameLoaded)
	{
		UnloadGame();
	}

	m_gameLoaded = m_retro_load_game(game);
	return m_gameLoaded;
}

void LibretroCore::UnloadGame()
{
	if (m_gameLoaded)
	{
		m_retro_unload_game();
		m_gameLoaded = false;
	}
}

void LibretroCore::Run()
{
	m_retro_run();
}

void LibretroCore::Reset()
{
	if (m_gameLoaded)
	{
		m_retro_reset();
	}
}

void LibretroCore::GetSystemInfo(retro_system_info* info)
{
	m_retro_get_system_info(info);
}

void LibretroCore::GetSystemAvInfo(retro_system_av_info* info)
{
	m_retro_get_system_av_info(info);
}
