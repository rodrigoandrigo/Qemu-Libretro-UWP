//
// DirectXPage.xaml.h
// DirectXPage class declaration.
//

#pragma once

#include "DirectXPage.g.h"

#include "Common\DeviceResources.h"
#include "Qemu_Libretro_UWPMain.h"

#include <string>
#include <cstdint>
#include <vector>
#include <map>
#include <set>

namespace Qemu_Libretro_UWP
{
	/// <summary>
	/// A page that hosts a DirectX SwapChainPanel.
	/// </summary>
	public ref class DirectXPage sealed
	{
	public:
		DirectXPage();
		virtual ~DirectXPage();

		void SaveInternalState(Windows::Foundation::Collections::IPropertySet^ state);
		void LoadInternalState(Windows::Foundation::Collections::IPropertySet^ state);

	private:
		// Low-level XAML rendering event handler.
		void OnRendering(Platform::Object^ sender, Platform::Object^ args);

		// Window event handlers.
		void OnVisibilityChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::VisibilityChangedEventArgs^ args);

		// DisplayInformation event handlers.
		void OnDpiChanged(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);
		void OnOrientationChanged(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);
		void OnDisplayContentsInvalidated(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);

		// Other event handlers.
		void SelectBootButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void SelectDriveButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void SelectCdromButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RemoveDriveButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RemoveCdromButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void DriveBootMediaBox_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void CdromBootMediaBox_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void ClearBootMediaButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ResetCommandDefaultsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void AlignCommandLineButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void StartButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void PauseButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ResumeButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void StopButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ShutdownButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ResetButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ShowTabsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ClearErrorLogButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void UpdateCommandLineButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ArgumentsHelpButton_PointerEntered(Platform::Object^ sender, Windows::UI::Xaml::Input::PointerRoutedEventArgs^ e);
		void ArgumentsHelpButton_PointerExited(Platform::Object^ sender, Windows::UI::Xaml::Input::PointerRoutedEventArgs^ e);
		void ArgumentsHelpHideTimer_Tick(Platform::Object^ sender, Platform::Object^ e);
		void SelectorLoadHideTimer_Tick(Platform::Object^ sender, Platform::Object^ e);
		void BootMediaSizeTimer_Tick(Platform::Object^ sender, Platform::Object^ e);
		void BootOption_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void BootOptionText_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::TextChangedEventArgs^ e);
		void FirmwarePathSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void ClearFirmwarePathButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void Architecture_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void QemuOption_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void DiagnosticProfile_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void MemorySlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e);
		void SmpSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e);
		void AdditionalArguments_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::TextChangedEventArgs^ e);
		void OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);
		void OnKeyUp(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);
		void OnCompositionScaleChanged(Windows::UI::Xaml::Controls::SwapChainPanel^ sender, Object^ args);
		void OnSwapChainPanelSizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
		void SetStatus(const std::wstring& text);
		void SetSelectorLoadProgress(double percent, const std::wstring& text, bool visible);
		void AppendError(const std::wstring& text);
		void ToggleInputCapture();
		void UpdateCaptureIndicators();
		void FocusEmulatorSurface();
		void SendPointerToCore(Windows::UI::Core::PointerEventArgs^ e);
		void RefreshCommandLinePreview();
		void RefreshBootMediaState();
		void UpdateBootMediaSize();
		void RefreshFirmwarePathSelectors();
		void AddFirmwarePathItem(Windows::UI::Xaml::Controls::ComboBox^ box, const std::wstring& display, const std::wstring& path);
		void SelectFirmwarePathValue(Windows::UI::Xaml::Controls::ComboBox^ box, Windows::UI::Xaml::Controls::TextBox^ textBox);
		void ClearFirmwarePathSelector(Windows::UI::Xaml::Controls::ComboBox^ box, Windows::UI::Xaml::Controls::TextBox^ textBox);
		void ClearBootMediaSelection(bool driveMedia);
		void SelectPreparedMedia(Windows::Storage::StorageFile^ file, bool cdromMedia);
		void DetectMediaOptions(Windows::Storage::StorageFile^ file, bool cdromMedia);
		void RefreshQemuOptionSelectors();
		void PopulateQemuOptionSelectors();
		void PopulateProfileOptions();
		void AddQemuOptionItems(Windows::UI::Xaml::Controls::ComboBox^ box, const wchar_t* defaultText, const std::vector<const wchar_t*>& candidates);
		bool CoreDllHasAscii(const wchar_t* value);
		std::wstring SelectedComboValue(Windows::UI::Xaml::Controls::ComboBox^ box);
		void SelectComboValue(Windows::UI::Xaml::Controls::ComboBox^ box, const wchar_t* value);
		void SelectArchitectureValue(const wchar_t* value);
		int SelectedProfileId() const;
		bool IsVideoTestProfile() const;
		void ApplySelectedProfile();
		std::wstring CurrentTarget() const;
		void ResetCommandDefaults();
		void UpdateCommandPreview();
		Platform::String^ BuildCommandLine();
		Platform::String^ BuildAutomaticCommandLine();
		Platform::String^ BuildAdditionalArguments();
		Platform::String^ FormatCommandLineVertical(Platform::String^ value);
		bool IsFileInFolder(Windows::Storage::StorageFile^ file, Windows::Storage::StorageFolder^ folder);
		bool EnsureMediaNbdServer(Windows::Storage::StorageFile^ mediaFile, bool readOnly, bool cdromMedia, std::wstring& url);
		void StopMediaNbdServer(bool cdromMedia);
		void StopAllMediaNbdServers();
		void OnMediaNbdConnectionReceived(Windows::Networking::Sockets::StreamSocketListener^ sender, Windows::Networking::Sockets::StreamSocketListenerConnectionReceivedEventArgs^ args);
		void ServeMediaNbdClient(Windows::Networking::Sockets::StreamSocket^ socket, std::wstring mediaPath, uint64_t mediaSize, bool readOnly);
		bool IsCommandLineFile(Windows::Storage::StorageFile^ file);
		Platform::String^ QuoteForCommandLine(Platform::String^ value);
		void StageBootFileAndStart();
		void WriteCommandLineAndStart(Platform::String^ commandLine);
		void StartWithCommandFile(Windows::Storage::StorageFile^ commandFile);
		void SetStartState(bool starting, bool running);
		void SetPausedState(bool paused);
		void SetStoppingState(bool stopping);
		void UpdateEmulatorButtons();
		unsigned MapVirtualKeyToRetro(Windows::System::VirtualKey key);

		// Track independent input on a background worker thread.
		Windows::Foundation::IAsyncAction^ m_inputLoopWorker;
		Windows::UI::Core::CoreIndependentInputSource^ m_coreInput;

		// Independent input handling functions.
		void OnPointerPressed(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);
		void OnPointerMoved(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);
		void OnPointerReleased(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);

		// Resources used to render DirectX content behind the XAML page.
		std::shared_ptr<DX::DeviceResources> m_deviceResources;
		std::unique_ptr<Qemu_Libretro_UWPMain> m_main; 
		Windows::Storage::StorageFile^ m_selectedBootFile;
		Windows::Storage::StorageFile^ m_stagedBootFile;
		Windows::Storage::StorageFile^ m_selectedDriveFile;
		Windows::Storage::StorageFile^ m_selectedCdromFile;
		Windows::Storage::StorageFile^ m_selectedCommandFile;
		Windows::Storage::StorageFile^ m_stagedDriveFile;
		Windows::Storage::StorageFile^ m_stagedCdromFile;
		Windows::Storage::StorageFile^ m_stagedCommandFile;
		Windows::Storage::StorageFile^ m_generatedCommandFile;
		Windows::Networking::Sockets::StreamSocketListener^ m_driveNbdListener;
		Windows::Networking::Sockets::StreamSocketListener^ m_cdromNbdListener;
		std::wstring m_driveNbdPath;
		std::wstring m_cdromNbdPath;
		uint64_t m_driveNbdSize;
		uint64_t m_cdromNbdSize;
		int m_driveNbdPort;
		int m_cdromNbdPort;
		bool m_driveNbdReadOnly;
		bool m_cdromNbdReadOnly;
		Windows::UI::Xaml::DispatcherTimer^ m_argumentsHelpHideTimer;
		Windows::UI::Xaml::DispatcherTimer^ m_selectorLoadHideTimer;
		Windows::UI::Xaml::DispatcherTimer^ m_bootMediaSizeTimer;
		bool m_updatingBootMediaLists;
		bool m_updatingBootMediaSize;
		bool m_updatingFirmwarePathLists;
		bool m_updatingQemuOptionLists;
		bool m_updatingProfileList;
		bool m_applyingProfile;
		bool m_coreDllOptionsLoaded;
		bool m_coreDllOptionsLoading;
		std::vector<unsigned char> m_coreDllData;
		std::set<std::string> m_coreDllAsciiTokens;
		std::map<std::wstring, bool> m_coreDllOptionPresence;
		std::map<std::wstring, std::map<std::wstring, bool>> m_qemuOptionPresenceByTarget;
		std::wstring m_currentOptionTarget;
		bool m_isStarting;
		bool m_isRunning;
		bool m_isPaused;
		bool m_isStopping;
		bool m_windowVisible;
		bool m_inputCaptured;
		bool m_ctrlDown;
		bool m_altDown;
		double m_inputSurfaceWidth;
		double m_inputSurfaceHeight;
		double m_emulatorPointerX;
		double m_emulatorPointerY;
		double m_lastPhysicalPointerX;
		double m_lastPhysicalPointerY;
		bool m_havePhysicalPointerPosition;
	};
}






