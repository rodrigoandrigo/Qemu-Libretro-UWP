#pragma once

#include "LibretroApi.h"

#include <string>
#include <windows.h>

namespace Qemu_Libretro_UWP
{
	class LibretroCore
	{
	public:
		LibretroCore();
		~LibretroCore();

		bool Load(std::wstring* error);
		bool IsLoaded() const { return m_module != nullptr; }
		bool ResolveExports(std::wstring* error);

		void SetCallbacks(
			retro_environment_t environment,
			retro_video_refresh_t video,
			retro_audio_sample_t audio,
			retro_audio_sample_batch_t audioBatch,
			retro_input_poll_t inputPoll,
			retro_input_state_t inputState);

		void Init();
		void Deinit();
		bool LoadGame(const retro_game_info* game);
		void UnloadGame();
		void Run();
		void Reset();
		void GetSystemInfo(retro_system_info* info);
		void GetSystemAvInfo(retro_system_av_info* info);

	private:
		template <typename T>
		bool Resolve(const char* name, T& target, std::wstring* error);

		HMODULE m_module;
		bool m_initialized;
		bool m_gameLoaded;

		void (*m_retro_set_environment)(retro_environment_t);
		void (*m_retro_set_video_refresh)(retro_video_refresh_t);
		void (*m_retro_set_audio_sample)(retro_audio_sample_t);
		void (*m_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
		void (*m_retro_set_input_poll)(retro_input_poll_t);
		void (*m_retro_set_input_state)(retro_input_state_t);
		void (*m_retro_init)();
		void (*m_retro_deinit)();
		unsigned (*m_retro_api_version)();
		void (*m_retro_get_system_info)(retro_system_info*);
		void (*m_retro_get_system_av_info)(retro_system_av_info*);
		bool (*m_retro_load_game)(const retro_game_info*);
		void (*m_retro_unload_game)();
		void (*m_retro_run)();
		void (*m_retro_reset)();
	};
}

