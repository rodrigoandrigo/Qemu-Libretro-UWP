//
// DirectXPage.xaml.cpp
// DirectXPage class implementation.
//

#include "pch.h"
#include "DirectXPage.xaml.h"
#include "LibretroApi.h"

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <initializer_list>
#include <ppltasks.h>
#include <set>
#include <sstream>
#include <vector>

using namespace Qemu_Libretro_UWP;

using namespace Platform;
using namespace Windows::ApplicationModel;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Devices::Input;
using namespace Windows::Graphics::Display;
using namespace Windows::Networking;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;
using namespace Windows::Storage::Streams;
using namespace Windows::System;
using namespace Windows::System::Threading;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Documents;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace concurrency;

namespace
{
	enum class TargetBlockStyle
	{
		Ide,
		Scsi,
		VirtioMmio,
		VirtioPci,
		VirtioCcw
	};

	struct TargetProfile
	{
		int memoryMb;
		const wchar_t* machineArgs;
		TargetBlockStyle blockStyle;
	};

	bool IsAnyTarget(const std::wstring& target, std::initializer_list<const wchar_t*> names)
	{
		for (const wchar_t* name : names)
		{
			if (_wcsicmp(target.c_str(), name) == 0)
			{
				return true;
			}
		}
		return false;
	}

	TargetProfile GetTargetProfile(const std::wstring& target)
	{
		if (IsAnyTarget(target, { L"x86_64" }))
		{
			return { 1024, L"", TargetBlockStyle::Ide };
		}
		if (IsAnyTarget(target, { L"i386" }))
		{
			return { 512, L"", TargetBlockStyle::Ide };
		}
		if (IsAnyTarget(target, { L"arm" }))
		{
			return { 256, L" -M virt", TargetBlockStyle::VirtioMmio };
		}
		if (IsAnyTarget(target, { L"aarch64" }))
		{
			return { 1024, L" -M virt", TargetBlockStyle::VirtioMmio };
		}
		if (IsAnyTarget(target, { L"riscv32" }))
		{
			return { 512, L" -M virt", TargetBlockStyle::VirtioMmio };
		}
		if (IsAnyTarget(target, { L"riscv64" }))
		{
			return { 1024, L" -M virt", TargetBlockStyle::VirtioMmio };
		}
		if (IsAnyTarget(target, { L"mips", L"mipsel" }))
		{
			return { 256, L" -M malta", TargetBlockStyle::Ide };
		}
		if (IsAnyTarget(target, { L"mips64", L"mips64el" }))
		{
			return { 512, L" -M malta", TargetBlockStyle::Ide };
		}
		if (IsAnyTarget(target, { L"ppc" }))
		{
			return { 512, L" -M mac99", TargetBlockStyle::Ide };
		}
		if (IsAnyTarget(target, { L"ppc64" }))
		{
			return { 1024, L" -M pseries", TargetBlockStyle::VirtioPci };
		}
		if (IsAnyTarget(target, { L"s390x" }))
		{
			return { 1024, L" -M s390-ccw-virtio", TargetBlockStyle::VirtioCcw };
		}
		if (IsAnyTarget(target, { L"sparc" }))
		{
			return { 256, L"", TargetBlockStyle::Scsi };
		}
		if (IsAnyTarget(target, { L"sparc64" }))
		{
			return { 512, L"", TargetBlockStyle::Scsi };
		}
		if (IsAnyTarget(target, { L"m68k" }))
		{
			return { 128, L" -M q800", TargetBlockStyle::Scsi };
		}
		if (IsAnyTarget(target, { L"alpha" }))
		{
			return { 256, L"", TargetBlockStyle::Scsi };
		}
		return { 512, L"", TargetBlockStyle::Ide };
	}

	const wchar_t* BlockInterface(TargetBlockStyle style)
	{
		return style == TargetBlockStyle::Scsi ? L"scsi" : L"ide";
	}

	const wchar_t* VirtioDevice(TargetBlockStyle style)
	{
		switch (style)
		{
		case TargetBlockStyle::VirtioMmio:
			return L"virtio-blk-device";
		case TargetBlockStyle::VirtioPci:
			return L"virtio-blk-pci";
		case TargetBlockStyle::VirtioCcw:
			return L"virtio-blk-ccw";
		default:
			return L"";
		}
	}

	bool IsTargetChar(unsigned char value)
	{
		return (value >= 'a' && value <= 'z') ||
			(value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9') ||
			value == '_';
	}

	bool ContainsTerminatedAscii(
		const std::vector<unsigned char>& data,
		const char* token,
		size_t begin,
		size_t end)
	{
		size_t length = strlen(token);
		if (length == 0 || begin >= data.size())
		{
			return false;
		}

		end = (std::min)(end, data.size());
		if (end < begin || end - begin < length)
		{
			return false;
		}

		for (size_t i = begin; i + length <= end; i++)
		{
			if (memcmp(data.data() + i, token, length) == 0)
			{
				bool beforeOk = i == 0 || data[i - 1] == 0;
				bool afterOk = i + length >= data.size() || data[i + length] == 0;
				if (beforeOk && afterOk)
				{
					return true;
				}
			}
		}

		return false;
	}

	size_t FindAscii(const std::vector<unsigned char>& data, const char* token)
	{
		size_t length = strlen(token);
		if (length == 0 || data.size() < length)
		{
			return std::wstring::npos;
		}

		for (size_t i = 0; i + length <= data.size(); i++)
		{
			if (memcmp(data.data() + i, token, length) == 0)
			{
				return i;
			}
		}

		return std::wstring::npos;
	}

	std::wstring WidenAscii(const std::string& value)
	{
		return std::wstring(value.begin(), value.end());
	}

	bool HasExtension(const std::wstring& name, const wchar_t* extension)
	{
		size_t extensionLength = wcslen(extension);
		return name.length() >= extensionLength &&
			_wcsicmp(name.c_str() + name.length() - extensionLength, extension) == 0;
	}

	std::wstring DiskFormatFromFileName(const std::wstring& name)
	{
		if (HasExtension(name, L".qcow2"))
		{
			return L"qcow2";
		}
		if (HasExtension(name, L".qcow"))
		{
			return L"qcow";
		}
		if (HasExtension(name, L".qed"))
		{
			return L"qed";
		}
		if (HasExtension(name, L".vdi"))
		{
			return L"vdi";
		}
		if (HasExtension(name, L".vmdk"))
		{
			return L"vmdk";
		}
		if (HasExtension(name, L".vhd") || HasExtension(name, L".vpc"))
		{
			return L"vpc";
		}
		if (HasExtension(name, L".vhdx"))
		{
			return L"vhdx";
		}
		if (HasExtension(name, L".bochs"))
		{
			return L"bochs";
		}
		if (HasExtension(name, L".cloop"))
		{
			return L"cloop";
		}
		if (HasExtension(name, L".dmg"))
		{
			return L"dmg";
		}
		if (HasExtension(name, L".hds") || HasExtension(name, L".parallels"))
		{
			return L"parallels";
		}
		return L"raw";
	}

	bool IsDriveMediaName(const std::wstring& name)
	{
		return HasExtension(name, L".iso") ||
			HasExtension(name, L".img") ||
			HasExtension(name, L".raw") ||
			HasExtension(name, L".qcow") ||
			HasExtension(name, L".qcow2") ||
			HasExtension(name, L".qed") ||
			HasExtension(name, L".vdi") ||
			HasExtension(name, L".vmdk") ||
			HasExtension(name, L".vhd") ||
			HasExtension(name, L".vpc") ||
			HasExtension(name, L".vhdx") ||
			HasExtension(name, L".bochs") ||
			HasExtension(name, L".cloop") ||
			HasExtension(name, L".dmg") ||
			HasExtension(name, L".hds") ||
			HasExtension(name, L".parallels") ||
			HasExtension(name, L".qemu_cmd_line");
	}

	bool IsCdromMediaName(const std::wstring& name)
	{
		return HasExtension(name, L".iso") ||
			HasExtension(name, L".img") ||
			HasExtension(name, L".raw") ||
			HasExtension(name, L".qcow") ||
			HasExtension(name, L".qcow2") ||
			HasExtension(name, L".qed") ||
			HasExtension(name, L".vdi") ||
			HasExtension(name, L".vmdk") ||
			HasExtension(name, L".vhd") ||
			HasExtension(name, L".vpc") ||
			HasExtension(name, L".vhdx") ||
			HasExtension(name, L".bochs") ||
			HasExtension(name, L".cloop") ||
			HasExtension(name, L".dmg") ||
			HasExtension(name, L".hds") ||
			HasExtension(name, L".parallels");
	}

	bool IsQemuFirmwareName(const std::wstring& name)
	{
		return HasExtension(name, L".bin") ||
			HasExtension(name, L".fd") ||
			HasExtension(name, L".rom") ||
			HasExtension(name, L".img") ||
			HasExtension(name, L".lid") ||
			HasExtension(name, L".e500") ||
			HasExtension(name, L".ndrv") ||
			name.find(L'.') == std::wstring::npos;
	}

	bool IsKernelBootName(const std::wstring& name)
	{
		return HasExtension(name, L".bin") ||
			HasExtension(name, L".elf") ||
			HasExtension(name, L".img") ||
			HasExtension(name, L".kernel") ||
			HasExtension(name, L".ub") ||
			HasExtension(name, L".uimage") ||
			HasExtension(name, L".vmlinux") ||
			name.find(L"Image") != std::wstring::npos ||
			name.find(L"kernel") != std::wstring::npos ||
			name.find(L"vmlinuz") != std::wstring::npos;
	}

	bool IsInitrdBootName(const std::wstring& name)
	{
		return HasExtension(name, L".cpio") ||
			HasExtension(name, L".gz") ||
			HasExtension(name, L".img") ||
			HasExtension(name, L".initrd") ||
			name.find(L"initramfs") != std::wstring::npos ||
			name.find(L"initrd") != std::wstring::npos;
	}

	std::wstring FormatByteSize(uint64_t bytes)
	{
		const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB" };
		double value = static_cast<double>(bytes);
		size_t unit = 0;
		while (value >= 1024.0 && unit + 1 < ARRAYSIZE(units))
		{
			value /= 1024.0;
			unit++;
		}

		wchar_t buffer[64] = {};
		if (unit == 0)
		{
			swprintf_s(buffer, L"%llu %s", static_cast<unsigned long long>(bytes), units[unit]);
		}
		else
		{
			swprintf_s(buffer, L"%.2f %s", value, units[unit]);
		}
		return buffer;
	}

	uint64_t FileSizeFromPath(const std::wstring& path)
	{
		CREATEFILE2_EXTENDED_PARAMETERS params = {};
		params.dwSize = sizeof(params);
		HANDLE file = CreateFile2(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, &params);
		if (file == INVALID_HANDLE_VALUE)
		{
			return 0;
		}

		LARGE_INTEGER size = {};
		bool ok = GetFileSizeEx(file, &size) != 0 && size.QuadPart > 0;
		CloseHandle(file);
		return ok ? static_cast<uint64_t>(size.QuadPart) : 0;
	}

	std::string NarrowAscii(const std::wstring& value)
	{
		std::string result;
		for (wchar_t ch : value)
		{
			if (ch > 0 && ch < 128)
			{
				result.push_back(static_cast<char>(ch));
			}
		}
		return result;
	}

	void AppendCandidates(std::vector<const wchar_t*>& target, const std::vector<const wchar_t*>& source)
	{
		for (const wchar_t* value : source)
		{
			target.push_back(value);
		}
	}

	std::vector<const wchar_t*> MachineCandidates(const std::wstring& target)
	{
		if (IsAnyTarget(target, { L"x86_64", L"i386" }))
		{
			return { L"pc", L"q35", L"microvm", L"isapc" };
		}
		if (IsAnyTarget(target, { L"arm", L"aarch64", L"riscv32", L"riscv64" }))
		{
			return { L"virt", L"virt-2.12", L"sifive_u" };
		}
		if (IsAnyTarget(target, { L"mips", L"mipsel", L"mips64", L"mips64el" }))
		{
			return { L"malta", L"mipssim" };
		}
		if (IsAnyTarget(target, { L"ppc" }))
		{
			return { L"mac99", L"g3beige", L"prep" };
		}
		if (IsAnyTarget(target, { L"ppc64" }))
		{
			return { L"pseries", L"powernv" };
		}
		if (IsAnyTarget(target, { L"s390x" }))
		{
			return { L"s390-ccw-virtio" };
		}
		if (IsAnyTarget(target, { L"m68k" }))
		{
			return { L"q800", L"virt" };
		}
		if (IsAnyTarget(target, { L"sparc", L"sparc64" }))
		{
			return { L"SS-5", L"sun4u", L"sun4v" };
		}
		return {};
	}

	std::vector<const wchar_t*> CpuCandidates(const std::wstring& target)
	{
		if (IsAnyTarget(target, { L"x86_64" }))
		{
			return { L"qemu64", L"max", L"core2duo", L"Nehalem", L"Opteron_G1" };
		}
		if (IsAnyTarget(target, { L"i386" }))
		{
			return { L"qemu32", L"486", L"pentium", L"pentium2", L"pentium3" };
		}
		if (IsAnyTarget(target, { L"arm", L"aarch64" }))
		{
			return { L"max", L"cortex-a7", L"cortex-a15", L"cortex-a53", L"cortex-a57", L"arm1176" };
		}
		if (IsAnyTarget(target, { L"mips", L"mipsel", L"mips64", L"mips64el" }))
		{
			return { L"24Kf", L"74Kf", L"P5600", L"MIPS64R2-generic" };
		}
		if (IsAnyTarget(target, { L"ppc", L"ppc64" }))
		{
			return { L"750", L"G3", L"G4", L"POWER7", L"POWER8", L"POWER9" };
		}
		if (IsAnyTarget(target, { L"riscv32", L"riscv64" }))
		{
			return { L"rv32", L"rv64", L"sifive-e51", L"sifive-u54" };
		}
		if (IsAnyTarget(target, { L"s390x" }))
		{
			return { L"qemu", L"max", L"z900", L"z196" };
		}
		return { L"max" };
	}

	std::vector<const wchar_t*> DeviceCandidates(const std::wstring& target)
	{
		std::vector<const wchar_t*> values = { L"usb-tablet", L"usb-kbd", L"scsi-hd", L"scsi-cd" };
		if (IsAnyTarget(target, { L"arm", L"aarch64", L"riscv32", L"riscv64" }))
		{
			values.push_back(L"virtio-blk-device");
			values.push_back(L"virtio-net-device");
			values.push_back(L"virtio-gpu-device");
		}
		else if (IsAnyTarget(target, { L"s390x" }))
		{
			values.push_back(L"virtio-blk-ccw");
			values.push_back(L"virtio-net-ccw");
		}
		else
		{
			values.push_back(L"virtio-blk-pci");
			values.push_back(L"virtio-net-pci");
			values.push_back(L"e1000");
			values.push_back(L"rtl8139");
			values.push_back(L"VGA");
		}
		return values;
	}

	std::vector<const wchar_t*> VgaCandidates(const std::wstring& target)
	{
		if (IsAnyTarget(target, { L"s390x" }))
		{
			return {};
		}
		return { L"std", L"cirrus", L"vmware", L"qxl", L"virtio", L"none" };
	}

	std::vector<const wchar_t*> MonitorCandidates()
	{
		return { L"none", L"vc", L"stdio" };
	}

	std::vector<const wchar_t*> NetdevCandidates()
	{
		return { L"user", L"socket", L"stream", L"dgram" };
	}

	std::vector<const wchar_t*> AllQemuOptionCandidates()
	{
		return
		{
			L"pc", L"q35", L"microvm", L"isapc", L"virt", L"virt-2.12", L"sifive_u",
			L"malta", L"mipssim", L"mac99", L"g3beige", L"prep", L"pseries", L"powernv",
			L"s390-ccw-virtio", L"q800", L"SS-5", L"sun4u", L"sun4v",
			L"qemu64", L"max", L"core2duo", L"Nehalem", L"Opteron_G1", L"qemu32",
			L"486", L"pentium", L"pentium2", L"pentium3", L"cortex-a7", L"cortex-a15",
			L"cortex-a53", L"cortex-a57", L"arm1176", L"24Kf", L"74Kf", L"P5600",
			L"MIPS64R2-generic", L"750", L"G3", L"G4", L"POWER7", L"POWER8", L"POWER9",
			L"rv32", L"rv64", L"sifive-e51", L"sifive-u54", L"qemu", L"z900", L"z196",
			L"usb-tablet", L"usb-kbd", L"scsi-hd", L"scsi-cd", L"virtio-blk-device",
			L"virtio-net-device", L"virtio-gpu-device", L"virtio-blk-ccw", L"virtio-net-ccw",
			L"virtio-blk-pci", L"virtio-net-pci", L"e1000", L"rtl8139", L"VGA",
			L"std", L"cirrus", L"vmware", L"qxl", L"virtio", L"none", L"vc", L"stdio",
			L"user", L"socket", L"stream", L"dgram"
		};
	}

	bool IsNetworkDeviceName(const std::wstring& value)
	{
		return value.find(L"net") != std::wstring::npos ||
			value == L"e1000" ||
			value == L"rtl8139";
	}

	enum ProfileId
	{
		ProfileNormal = 0,
		ProfileVideoTest = 1,
		ProfileLinuxCloudQcow2 = 2,
		ProfileUbuntu10 = 3,
		ProfileUbuntu14 = 4,
		ProfileWindows98 = 5,
		ProfileWindowsXP = 6,
		ProfileWindows7 = 7
	};

	const wchar_t* ProfileName(int profile)
	{
		switch (profile)
		{
		case ProfileVideoTest:
			return L"Video Teste";
		case ProfileLinuxCloudQcow2:
			return L"Linux cloud qcow2";
		case ProfileUbuntu10:
			return L"Ubuntu 10";
		case ProfileUbuntu14:
			return L"Ubuntu 14";
		case ProfileWindows98:
			return L"Windows 98";
		case ProfileWindowsXP:
			return L"Windows XP";
		case ProfileWindows7:
			return L"Windows 7";
		default:
			return L"Normal command";
		}
	}

	int ProfileIdFromName(const std::wstring& name)
	{
		for (int profile = ProfileNormal; profile <= ProfileWindows7; profile++)
		{
			if (_wcsicmp(name.c_str(), ProfileName(profile)) == 0)
			{
				return profile;
			}
		}
		return ProfileNormal;
	}

	bool IsProfileAvailableForTarget(int profile, const std::wstring& target)
	{
		if (profile == ProfileNormal || profile == ProfileVideoTest)
		{
			return true;
		}
		if (profile == ProfileLinuxCloudQcow2 || profile == ProfileUbuntu14 || profile == ProfileWindows7)
		{
			return IsAnyTarget(target, { L"x86_64" });
		}
		if (profile == ProfileUbuntu10 || profile == ProfileWindows98 || profile == ProfileWindowsXP)
		{
			return IsAnyTarget(target, { L"i386" });
		}
		return false;
	}

	bool IsCoreKeyDown(VirtualKey key)
	{
		CoreVirtualKeyStates state = Window::Current->CoreWindow->GetKeyState(key);
		return (static_cast<unsigned>(state) & static_cast<unsigned>(CoreVirtualKeyStates::Down)) != 0;
	}

	bool IsControlDown()
	{
		return IsCoreKeyDown(VirtualKey::Control) ||
			IsCoreKeyDown(VirtualKey::LeftControl) ||
			IsCoreKeyDown(VirtualKey::RightControl);
	}

	bool IsAltDown()
	{
		return IsCoreKeyDown(VirtualKey::Menu) ||
			IsCoreKeyDown(VirtualKey::LeftMenu) ||
			IsCoreKeyDown(VirtualKey::RightMenu);
	}

	std::wstring JoinSet(const std::set<std::string>& values)
	{
		if (values.empty())
		{
			return L"(none)";
		}

		std::wstring joined;
		for (const auto& value : values)
		{
			if (!joined.empty())
			{
				joined += L", ";
			}
			joined += WidenAscii(value);
		}
		return joined;
	}

	std::wstring ProbeQemuLibretroTargets(const std::wstring& dllPath)
	{
		std::wstringstream report;
		report << L"Target probe: reading " << dllPath << L"\r\n";

		CREATEFILE2_EXTENDED_PARAMETERS params = {};
		params.dwSize = sizeof(params);
		HANDLE dll = CreateFile2(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &params);
		if (dll == INVALID_HANDLE_VALUE)
		{
			report << L"Target probe: failed to open qemu_libretro.dll.";
			return report.str();
		}

		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(dll, &size) || size.QuadPart <= 0 || size.QuadPart > 512LL * 1024LL * 1024LL)
		{
			CloseHandle(dll);
			report << L"Target probe: invalid qemu_libretro.dll size.";
			return report.str();
		}

		std::vector<unsigned char> data(static_cast<size_t>(size.QuadPart));
		DWORD totalRead = 0;
		DWORD chunkRead = 0;
		while (totalRead < data.size())
		{
			DWORD remaining = static_cast<DWORD>((std::min)(data.size() - totalRead, static_cast<size_t>(1024 * 1024)));
			if (!ReadFile(dll, data.data() + totalRead, remaining, &chunkRead, nullptr) || chunkRead == 0)
			{
				break;
			}
			totalRead += chunkRead;
		}
		CloseHandle(dll);

		if (totalRead != data.size())
		{
			report << L"Target probe: incomplete DLL read.";
			return report.str();
		}

		static const char* knownTargets[] =
		{
			"aarch64",
			"alpha",
			"arm",
			"i386",
			"m68k",
			"mips",
			"mips64",
			"mips64el",
			"mipsel",
			"ppc",
			"ppc64",
			"riscv32",
			"riscv64",
			"s390x",
			"sparc",
			"sparc64",
			"x86_64"
		};

		std::set<std::string> tableTargets;
		size_t marker = FindAscii(data, "unsupported architecture");
		size_t tableBegin = marker == std::wstring::npos || marker < 4096 ? 0 : marker - 4096;
		size_t tableEnd = marker == std::wstring::npos ? data.size() : marker;
		for (const char* target : knownTargets)
		{
			if (ContainsTerminatedAscii(data, target, tableBegin, tableEnd))
			{
				tableTargets.insert(target);
			}
		}

		std::set<std::string> commandStrings;
		const char* prefix = "qemu-system-";
		size_t prefixLength = strlen(prefix);
		for (size_t i = 0; i + prefixLength < data.size(); i++)
		{
			if (memcmp(data.data() + i, prefix, prefixLength) != 0)
			{
				continue;
			}

			size_t start = i + prefixLength;
			size_t end = start;
			while (end < data.size() && IsTargetChar(data[end]))
			{
				end++;
			}
			if (end > start)
			{
				std::string target(reinterpret_cast<const char*>(data.data() + start), end - start);
				if (target != "ARCH")
				{
					commandStrings.insert(target);
				}
			}
		}

		report << L"Target probe: libretro arch table = " << JoinSet(tableTargets) << L"\r\n";
		report << L"Target probe: embedded qemu-system strings = " << JoinSet(commandStrings) << L"\r\n";
		report << L"Target probe: total targets from arch table = " << tableTargets.size() << L"\r\n";
		report << L"Target probe: use qemu-system-<target> -machine help in qemu_cmd_line to validate machines/devices per architecture.";
		return report.str();
	}
}

DirectXPage::DirectXPage():
	m_windowVisible(true),
	m_coreInput(nullptr),
	m_selectedBootFile(nullptr),
	m_stagedBootFile(nullptr),
	m_selectedDriveFile(nullptr),
	m_selectedCdromFile(nullptr),
	m_selectedCommandFile(nullptr),
	m_stagedDriveFile(nullptr),
	m_stagedCdromFile(nullptr),
	m_stagedCommandFile(nullptr),
	m_generatedCommandFile(nullptr),
	m_driveNbdListener(nullptr),
	m_cdromNbdListener(nullptr),
	m_driveNbdSize(0),
	m_cdromNbdSize(0),
	m_driveNbdPort(0),
	m_cdromNbdPort(0),
	m_driveNbdReadOnly(true),
	m_cdromNbdReadOnly(true),
	m_argumentsHelpHideTimer(nullptr),
	m_selectorLoadHideTimer(nullptr),
	m_bootMediaSizeTimer(nullptr),
	m_updatingBootMediaLists(false),
	m_updatingBootMediaSize(false),
	m_updatingFirmwarePathLists(false),
	m_updatingQemuOptionLists(false),
	m_updatingProfileList(false),
	m_applyingProfile(false),
	m_packageFirmwarePathCacheLoaded(false),
	m_packageFirmwarePathCacheLoading(false),
	m_coreDllOptionsLoaded(false),
	m_coreDllOptionsLoading(false),
	m_isStarting(false),
	m_isRunning(false),
	m_isPaused(false),
	m_isStopping(false),
	m_inputCaptured(true),
	m_ctrlDown(false),
	m_altDown(false),
	m_defaultPointerCursor(nullptr),
	m_windowPointerCaptured(false),
	m_mouseLeft(false),
	m_mouseRight(false),
	m_mouseMiddle(false),
	m_inputSurfaceWidth(1.0),
	m_inputSurfaceHeight(1.0)
{
	InitializeComponent();
	UpdateCaptureIndicators();
	m_inputSurfaceWidth = (std::max)(1.0, swapChainPanel->ActualWidth);
	m_inputSurfaceHeight = (std::max)(1.0, swapChainPanel->ActualHeight);

	m_argumentsHelpHideTimer = ref new DispatcherTimer();
	TimeSpan hideDelay;
	hideDelay.Duration = 10000000;
	m_argumentsHelpHideTimer->Interval = hideDelay;
	m_argumentsHelpHideTimer->Tick += ref new EventHandler<Object^>(this, &DirectXPage::ArgumentsHelpHideTimer_Tick);

	m_selectorLoadHideTimer = ref new DispatcherTimer();
	TimeSpan selectorHideDelay;
	selectorHideDelay.Duration = 40000000;
	m_selectorLoadHideTimer->Interval = selectorHideDelay;
	m_selectorLoadHideTimer->Tick += ref new EventHandler<Object^>(this, &DirectXPage::SelectorLoadHideTimer_Tick);

	m_bootMediaSizeTimer = ref new DispatcherTimer();
	TimeSpan sizeRefreshDelay;
	sizeRefreshDelay.Duration = 10000000;
	m_bootMediaSizeTimer->Interval = sizeRefreshDelay;
	m_bootMediaSizeTimer->Tick += ref new EventHandler<Object^>(this, &DirectXPage::BootMediaSizeTimer_Tick);
	m_bootMediaSizeTimer->Start();

	if (architectureBox != nullptr && memorySlider != nullptr)
	{
		ComboBoxItem^ selectedArch = dynamic_cast<ComboBoxItem^>(architectureBox->SelectedItem);
		if (selectedArch != nullptr)
		{
			std::wstring target(selectedArch->Content->ToString()->Data());
			memorySlider->Value = GetTargetProfile(target).memoryMb;
		}
	}

	// Register event handlers for the page lifecycle.
	CoreWindow^ window = Window::Current->CoreWindow;
	m_defaultPointerCursor = window->PointerCursor;
	if (m_defaultPointerCursor == nullptr)
	{
		m_defaultPointerCursor = ref new CoreCursor(CoreCursorType::Arrow, 0);
	}

	window->VisibilityChanged +=
		ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>(this, &DirectXPage::OnVisibilityChanged);
	window->KeyDown +=
		ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &DirectXPage::OnKeyDown);
	window->KeyUp +=
		ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &DirectXPage::OnKeyUp);

	MouseDevice::GetForCurrentView()->MouseMoved +=
		ref new TypedEventHandler<MouseDevice^, MouseEventArgs^>(this, &DirectXPage::OnMouseMoved);

	DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();

	currentDisplayInformation->DpiChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnDpiChanged);

	currentDisplayInformation->OrientationChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnOrientationChanged);

	DisplayInformation::DisplayContentsInvalidated +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnDisplayContentsInvalidated);

	swapChainPanel->CompositionScaleChanged += 
		ref new TypedEventHandler<SwapChainPanel^, Object^>(this, &DirectXPage::OnCompositionScaleChanged);

	swapChainPanel->SizeChanged +=
		ref new SizeChangedEventHandler(this, &DirectXPage::OnSwapChainPanelSizeChanged);

	// Device access is available at this point.
	// Device-dependent resources can be created here.
	m_deviceResources = std::make_shared<DX::DeviceResources>();
	m_deviceResources->SetSwapChainPanel(swapChainPanel);

	// Register the SwapChainPanel for independent pointer input events.
	auto workItemHandler = ref new WorkItemHandler([this] (IAsyncAction ^)
	{
		// CoreIndependentInputSource raises pointer events for the specified device types on the thread where it is created.
		m_coreInput = swapChainPanel->CreateCoreIndependentInputSource(
			Windows::UI::Core::CoreInputDeviceTypes::Mouse |
			Windows::UI::Core::CoreInputDeviceTypes::Touch |
			Windows::UI::Core::CoreInputDeviceTypes::Pen
			);

		// Register pointer events raised on the background thread.
		m_coreInput->PointerPressed += ref new TypedEventHandler<Object^, PointerEventArgs^>(this, &DirectXPage::OnPointerPressed);
		m_coreInput->PointerMoved += ref new TypedEventHandler<Object^, PointerEventArgs^>(this, &DirectXPage::OnPointerMoved);
		m_coreInput->PointerReleased += ref new TypedEventHandler<Object^, PointerEventArgs^>(this, &DirectXPage::OnPointerReleased);

		// Start processing input messages as they are delivered.
		m_coreInput->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
	});

	// Run the task on a dedicated high-priority background thread.
	m_inputLoopWorker = ThreadPool::RunAsync(workItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);

	m_main = std::unique_ptr<Qemu_Libretro_UWPMain>(new Qemu_Libretro_UWPMain(m_deviceResources));
	SetStatus(m_main->StatusText());
	m_main->StartRenderLoop();
	RefreshBootMediaState();
	PopulateProfileOptions();
	RefreshQemuOptionSelectors();
	UpdateCommandPreview();
}

DirectXPage::~DirectXPage()
{
	try
	{
		CoreWindow^ window = Window::Current->CoreWindow;
		if (window != nullptr)
		{
			if (m_windowPointerCaptured)
			{
				window->ReleasePointerCapture();
				m_windowPointerCaptured = false;
			}
			window->PointerCursor = m_defaultPointerCursor != nullptr
				? m_defaultPointerCursor
				: ref new CoreCursor(CoreCursorType::Arrow, 0);
		}
	}
	catch (...)
	{
	}
	StopAllMediaNbdServers();
	if (m_bootMediaSizeTimer != nullptr)
	{
		m_bootMediaSizeTimer->Stop();
	}
	// Stop rendering and event processing during destruction.
	m_main->StopRenderLoop();
	m_coreInput->Dispatcher->StopProcessEvents();
}

// Saves the current app state for suspend and termination events.
void DirectXPage::SaveInternalState(IPropertySet^ state)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->Trim();

	// Stop rendering when the app is suspended.
	m_main->StopRenderLoop();

	// Add app state save code here.
}

// Loads the current app state for resume events.
void DirectXPage::LoadInternalState(IPropertySet^ state)
{
	// Add app state load code here.

	// Start rendering when the app is resumed.
	m_main->StartRenderLoop();
}

// Manipuladores de eventos da janela.

void DirectXPage::OnVisibilityChanged(CoreWindow^ sender, VisibilityChangedEventArgs^ args)
{
	m_windowVisible = args->Visible;
	if (m_windowVisible)
	{
		m_main->StartRenderLoop();
	}
	else
	{
		m_main->StopRenderLoop();
	}
	UpdateCaptureIndicators();
}

// Manipuladores de eventos DisplayInformation.

void DirectXPage::OnDpiChanged(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	// Note: the LogicalDpi value retrieved here may not match the app's effective DPI
	// if it is being scaled for high-resolution devices. After DPI is set in DeviceResources,
	// always retrieve it with GetDpi.
	// Consulte DeviceResources.cpp para obter mais detalhes.
	m_deviceResources->SetDpi(sender->LogicalDpi);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnOrientationChanged(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetCurrentOrientation(sender->CurrentOrientation);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnDisplayContentsInvalidated(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->ValidateDevice();
}

void DirectXPage::SelectBootButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	SelectDriveButton_Click(sender, e);
}

void DirectXPage::SelectDriveButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	FileOpenPicker^ picker = ref new FileOpenPicker();
	picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
	picker->FileTypeFilter->Append(".iso");
	picker->FileTypeFilter->Append(".img");
	picker->FileTypeFilter->Append(".raw");
	picker->FileTypeFilter->Append(".qcow");
	picker->FileTypeFilter->Append(".qcow2");
	picker->FileTypeFilter->Append(".qed");
	picker->FileTypeFilter->Append(".vdi");
	picker->FileTypeFilter->Append(".vmdk");
	picker->FileTypeFilter->Append(".vhd");
	picker->FileTypeFilter->Append(".vpc");
	picker->FileTypeFilter->Append(".vhdx");
	picker->FileTypeFilter->Append(".bochs");
	picker->FileTypeFilter->Append(".cloop");
	picker->FileTypeFilter->Append(".dmg");
	picker->FileTypeFilter->Append(".hds");
	picker->FileTypeFilter->Append(".parallels");
	picker->FileTypeFilter->Append(".qemu_cmd_line");

	SetStatus(L"Opening file picker...");
	Concurrency::create_task(picker->PickSingleFileAsync()).then([this](StorageFile^ file)
	{
		if (file == nullptr)
		{
			SetStatus(L"Selection canceled.");
			return;
		}

		m_selectedBootFile = file;
		m_stagedBootFile = nullptr;
		m_stagedDriveFile = nullptr;
		m_stagedCommandFile = nullptr;
		StopMediaNbdServer(false);
		if (driveBootMediaBox != nullptr)
		{
			m_updatingBootMediaLists = true;
			driveBootMediaBox->SelectedIndex = 0;
			m_updatingBootMediaLists = false;
		}
		if (IsCommandLineFile(file))
		{
			m_selectedCommandFile = file;
			m_selectedDriveFile = nullptr;
			m_selectedCdromFile = nullptr;
			m_stagedCdromFile = nullptr;
			StopMediaNbdServer(true);
			SelectComboValue(driveFormatBox, nullptr);
			SelectComboValue(driveInterfaceBox, nullptr);
			SelectComboValue(driveCacheBox, nullptr);
			SelectComboValue(driveAioBox, nullptr);
			SelectComboValue(driveDiscardBox, nullptr);
			SelectComboValue(driveSnapshotBox, nullptr);
			SelectComboValue(driveReadonlyBox, nullptr);
			SelectComboValue(cdromFormatBox, nullptr);
			SelectComboValue(cdromInterfaceBox, nullptr);
			SelectComboValue(cdromCacheBox, nullptr);
			SelectComboValue(cdromAioBox, nullptr);
			if (cdromBootMediaBox != nullptr)
			{
				m_updatingBootMediaLists = true;
				cdromBootMediaBox->SelectedIndex = 0;
				m_updatingBootMediaLists = false;
			}
			selectedDriveText->Text = file->Path;
			selectedCdromText->Text = "No CD-ROM selected";
		}
		else
		{
			m_selectedCommandFile = nullptr;
			m_selectedDriveFile = file;
			selectedDriveText->Text = file->Path;
			DetectMediaOptions(file, false);
		}
		SetStatus(L"File selected. Review qemu_cmd_line and click Start.");

		if (IsCommandLineFile(file))
		{
			Concurrency::create_task(FileIO::ReadTextAsync(file)).then([this](String^ text)
			{
				commandLineBox->Text = text;
				UpdateCommandPreview();
			});
		}
		else
		{
			RefreshCommandLinePreview();
		}
	});
}

void DirectXPage::SelectCdromButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	FileOpenPicker^ picker = ref new FileOpenPicker();
	picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
	picker->FileTypeFilter->Append(".iso");
	picker->FileTypeFilter->Append(".img");
	picker->FileTypeFilter->Append(".raw");
	picker->FileTypeFilter->Append(".qcow");
	picker->FileTypeFilter->Append(".qcow2");
	picker->FileTypeFilter->Append(".qed");
	picker->FileTypeFilter->Append(".vdi");
	picker->FileTypeFilter->Append(".vmdk");
	picker->FileTypeFilter->Append(".vhd");
	picker->FileTypeFilter->Append(".vpc");
	picker->FileTypeFilter->Append(".vhdx");
	picker->FileTypeFilter->Append(".bochs");
	picker->FileTypeFilter->Append(".cloop");
	picker->FileTypeFilter->Append(".dmg");
	picker->FileTypeFilter->Append(".hds");
	picker->FileTypeFilter->Append(".parallels");

	SetStatus(L"Opening CD-ROM picker...");
	Concurrency::create_task(picker->PickSingleFileAsync()).then([this](StorageFile^ file)
	{
		if (file == nullptr)
		{
			SetStatus(L"Selection canceled.");
			return;
		}

		m_selectedCommandFile = nullptr;
		m_selectedCdromFile = file;
		m_stagedCommandFile = nullptr;
		m_stagedCdromFile = nullptr;
		StopMediaNbdServer(true);
		if (m_selectedDriveFile == nullptr)
		{
			selectedDriveText->Text = "No drive selected";
		}
		if (cdromBootMediaBox != nullptr)
		{
			m_updatingBootMediaLists = true;
			cdromBootMediaBox->SelectedIndex = 0;
			m_updatingBootMediaLists = false;
		}
		selectedCdromText->Text = file->Path;
		DetectMediaOptions(file, true);
		SetStatus(L"CD-ROM selected. Review qemu_cmd_line and click Start.");
		RefreshCommandLinePreview();
	});
}

void DirectXPage::RemoveDriveButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	ClearBootMediaSelection(false);
	SetStatus(L"Drive media removed from the boot configuration.");
}

void DirectXPage::RemoveCdromButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	ClearBootMediaSelection(true);
	SetStatus(L"CD-ROM media removed from the boot configuration.");
}

void DirectXPage::DriveBootMediaBox_SelectionChanged(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_updatingBootMediaLists || driveBootMediaBox == nullptr)
	{
		return;
	}

	ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(driveBootMediaBox->SelectedItem);
	StorageFile^ file = item != nullptr ? dynamic_cast<StorageFile^>(item->Tag) : nullptr;
	if (file == nullptr)
	{
		return;
	}

	SelectPreparedMedia(file, false);
}

void DirectXPage::CdromBootMediaBox_SelectionChanged(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_updatingBootMediaLists || cdromBootMediaBox == nullptr)
	{
		return;
	}

	ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(cdromBootMediaBox->SelectedItem);
	StorageFile^ file = item != nullptr ? dynamic_cast<StorageFile^>(item->Tag) : nullptr;
	if (file == nullptr)
	{
		return;
	}

	SelectPreparedMedia(file, true);
}

void DirectXPage::ClearBootMediaButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_isStarting || m_isRunning)
	{
		AppendError(L"boot_media clear ignored while the emulator is starting or running.");
		tabPanel->SelectedIndex = 1;
		return;
	}

	SetStatus(L"Clearing boot_media...");
	StopAllMediaNbdServers();
	m_selectedBootFile = nullptr;
	m_stagedBootFile = nullptr;
	m_selectedDriveFile = nullptr;
	m_selectedCdromFile = nullptr;
	m_selectedCommandFile = nullptr;
	m_stagedDriveFile = nullptr;
	m_stagedCdromFile = nullptr;
	m_stagedCommandFile = nullptr;
	selectedDriveText->Text = "No drive selected";
	selectedCdromText->Text = "No CD-ROM selected";
	SelectComboValue(driveFormatBox, nullptr);
	SelectComboValue(driveInterfaceBox, nullptr);
	SelectComboValue(driveCacheBox, nullptr);
	SelectComboValue(driveAioBox, nullptr);
	SelectComboValue(driveDiscardBox, nullptr);
	SelectComboValue(driveSnapshotBox, nullptr);
	SelectComboValue(driveReadonlyBox, nullptr);
	SelectComboValue(cdromFormatBox, nullptr);
	SelectComboValue(cdromInterfaceBox, nullptr);
	SelectComboValue(cdromCacheBox, nullptr);
	SelectComboValue(cdromAioBox, nullptr);
	commandLineBox->Text = "";
	UpdateCommandPreview();

	auto local = ApplicationData::Current->LocalFolder;
	Concurrency::create_task(local->CreateFolderAsync("boot_media", CreationCollisionOption::OpenIfExists)).then(
		[this, local](Concurrency::task<StorageFolder^> folderTask)
	{
		try
		{
			StorageFolder^ folder = folderTask.get();
			return Concurrency::create_task(folder->DeleteAsync(StorageDeleteOption::PermanentDelete)).then([local]()
			{
				return Concurrency::create_task(local->CreateFolderAsync("boot_media", CreationCollisionOption::OpenIfExists));
			});
		}
		catch (Exception^)
		{
			return Concurrency::create_task(local->CreateFolderAsync("boot_media", CreationCollisionOption::OpenIfExists));
		}
	}).then([this](Concurrency::task<StorageFolder^> clearTask)
	{
		try
		{
			clearTask.get();
			SetStatus(L"boot_media cleared.");
			RefreshBootMediaState();
		}
		catch (Exception^ ex)
		{
			std::wstring error = L"Failed to clear boot_media: ";
			error += ex->Message->Data();
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
			RefreshBootMediaState();
		}
	});
}

void DirectXPage::ResetCommandDefaultsButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	ResetCommandDefaults();
	SetStatus(L"Command options reset to startup defaults.");
}

void DirectXPage::AlignCommandLineButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (commandLineBox == nullptr || commandLineBox->Text == nullptr || commandLineBox->Text->Length() == 0)
	{
		commandLineBox->Text = BuildCommandLine();
	}
	commandLineBox->Text = FormatCommandLineVertical(commandLineBox->Text);
	UpdateCommandPreview();
	SetStatus(L"qemu commands aligned vertically.");
}

void DirectXPage::StartButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	if (m_isStarting)
	{
		AppendError(L"Start ignored: the emulator is still starting.");
		tabPanel->SelectedIndex = 1;
		return;
	}
	if (m_isRunning)
	{
		AppendError(L"Start ignored: the emulator is already running.");
		tabPanel->SelectedIndex = 1;
		return;
	}
	if (m_selectedCommandFile == nullptr && m_selectedDriveFile == nullptr && m_selectedCdromFile == nullptr && !IsVideoTestProfile())
	{
		AppendError(L"No drive, CD-ROM, or qemu_cmd_line file selected.");
		tabPanel->SelectedIndex = 1;
		return;
	}

	SetStartState(true, false);
	StageBootFileAndStart();
}

void DirectXPage::PauseButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (!m_isRunning || m_isPaused || m_isStopping)
	{
		return;
	}

	if (m_main->PauseEmulator())
	{
		SetPausedState(true);
		SetStatus(L"Pause requested. The emulator will pause after the current frame.");
	}
}

void DirectXPage::ResumeButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (!m_isRunning || !m_isPaused || m_isStopping)
	{
		return;
	}

	if (m_main->ResumeEmulator())
	{
		SetPausedState(false);
		SetStatus(L"Emulator resumed.");
	}
}

void DirectXPage::StopButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (!m_isRunning || m_isStopping)
	{
		return;
	}

	SetStoppingState(true);
	SetStatus(L"Stopping emulator...");
	auto dispatcher = Dispatcher;
	Concurrency::create_task([this]()
	{
		return m_main->StopEmulator();
	}).then([this, dispatcher](bool stopped)
	{
		dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, stopped]()
		{
			SetStoppingState(false);
			if (stopped)
			{
				StopAllMediaNbdServers();
				SetPausedState(false);
				SetStartState(false, false);
				SetStatus(L"Emulator stopped. Start is available.");
			}
			else
			{
				AppendError(L"Stop timed out: retro_run is still busy. Shutdown may also be unable to unload the core safely.");
				SetStartState(false, true);
				SetPausedState(false);
				SetStatus(L"Stop timed out. The emulator core is still busy.");
				tabPanel->SelectedIndex = 1;
			}
		}));
	});
}

void DirectXPage::ShutdownButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if ((!m_isRunning && !m_isPaused) || m_isStopping)
	{
		return;
	}

	SetStoppingState(true);
	SetStatus(L"Shutting down emulator core...");
	auto dispatcher = Dispatcher;
	Concurrency::create_task([this]()
	{
		return m_main->ShutdownEmulator();
	}).then([this, dispatcher](bool shutdown)
	{
		dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, shutdown]()
		{
			SetStoppingState(false);
			if (shutdown)
			{
				StopAllMediaNbdServers();
				SetPausedState(false);
				SetStartState(false, false);
				SetStatus(L"Emulator core stopped. Start is available.");
			}
			else
			{
				AppendError(L"Shutdown timed out: retro_run is still busy. The core could not be unloaded safely.");
				SetStartState(false, true);
				SetPausedState(false);
				SetStatus(L"Shutdown timed out. Restart the app if the core does not recover.");
				tabPanel->SelectedIndex = 1;
			}
		}));
	});
}

void DirectXPage::WriteCommandLineAndStart(String^ commandLine)
{
	if (commandLine == nullptr || commandLine->Length() == 0)
	{
		AppendError(L"qemu_cmd_line is empty. Select media or review the boot options.");
		tabPanel->SelectedIndex = 1;
		SetStartState(false, false);
		return;
	}

	SetStatus(L"Generating current.qemu_cmd_line...");
	AppendError(L"Start: generating current.qemu_cmd_line.");
	std::wstring commandLog = L"Start: qemu_cmd_line: ";
	commandLog += commandLine->Data();
	AppendError(commandLog);
	auto createFileTask = ApplicationData::Current->LocalFolder->CreateFileAsync(
		"current.qemu_cmd_line",
		CreationCollisionOption::ReplaceExisting);

	Concurrency::create_task(createFileTask).then([this, commandLine](Concurrency::task<StorageFile^> createTask)
	{
		try
		{
			m_generatedCommandFile = createTask.get();
			return Concurrency::create_task(FileIO::WriteTextAsync(m_generatedCommandFile, commandLine));
		}
		catch (Exception^ ex)
		{
			std::wstring error = L"Failed to create current.qemu_cmd_line: ";
			error += ex->Message->Data();
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
			SetStartState(false, false);
			return Concurrency::task_from_result();
		}
	}).then([this](Concurrency::task<void> writeTask)
	{
		try
		{
			writeTask.get();
			if (!m_isStarting)
			{
				return;
			}
			AppendError(L"Start: current.qemu_cmd_line written. Loading libretro core.");
			StartWithCommandFile(m_generatedCommandFile);
		}
		catch (Exception^ ex)
		{
			std::wstring error = L"Failed to write current.qemu_cmd_line: ";
			error += ex->Message->Data();
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
			SetStartState(false, false);
		}
	});
}

void DirectXPage::StageBootFileAndStart()
{
	SetStatus(L"Preparing media in the app local storage...");
	AppendError(L"Start: preparing media in LocalFolder\\boot_media.");
	m_stagedCommandFile = nullptr;
	m_stagedDriveFile = nullptr;
	m_stagedCdromFile = nullptr;

	Concurrency::create_task(ApplicationData::Current->LocalFolder->CreateFolderAsync(
		"boot_media",
		CreationCollisionOption::OpenIfExists)).then([this](Concurrency::task<StorageFolder^> folderTask)
	{
		try
		{
			StorageFolder^ folder = folderTask.get();
			if (m_selectedCommandFile != nullptr)
			{
				Concurrency::task<StorageFile^> commandTask = IsFileInFolder(m_selectedCommandFile, folder)
					? Concurrency::task_from_result<StorageFile^>(m_selectedCommandFile)
					: Concurrency::create_task(m_selectedCommandFile->CopyAsync(
						folder,
						m_selectedCommandFile->Name,
						NameCollisionOption::ReplaceExisting));
				return commandTask.then([this](StorageFile^ file)
				{
					m_stagedCommandFile = file;
				});
			}

			Concurrency::task<StorageFile^> driveTask = Concurrency::task_from_result<StorageFile^>(nullptr);
			if (m_selectedDriveFile != nullptr)
			{
				driveTask = IsFileInFolder(m_selectedDriveFile, folder)
					? Concurrency::task_from_result<StorageFile^>(m_selectedDriveFile)
					: Concurrency::create_task(m_selectedDriveFile->CopyAsync(folder, m_selectedDriveFile->Name, NameCollisionOption::ReplaceExisting));
			}

			return driveTask.then([this, folder](StorageFile^ driveFile)
			{
				m_stagedDriveFile = driveFile;
				Concurrency::task<StorageFile^> cdromTask = Concurrency::task_from_result<StorageFile^>(nullptr);
				if (m_selectedCdromFile != nullptr)
				{
					cdromTask = IsFileInFolder(m_selectedCdromFile, folder)
						? Concurrency::task_from_result<StorageFile^>(m_selectedCdromFile)
						: Concurrency::create_task(m_selectedCdromFile->CopyAsync(folder, m_selectedCdromFile->Name, NameCollisionOption::ReplaceExisting));
				}

				return cdromTask.then([this](StorageFile^ cdromFile)
				{
					m_stagedCdromFile = cdromFile;
				});
			});
		}
		catch (Exception^ ex)
		{
			std::wstring error = L"Failed to prepare boot_media folder: ";
			error += ex->Message->Data();
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
			SetStartState(false, false);
			return Concurrency::task_from_result();
		}
	}).then([this](Concurrency::task<void> stageTask)
	{
		try
		{
			stageTask.get();
			RefreshBootMediaState();
			if (m_stagedCommandFile != nullptr)
			{
				std::wstring staged = L"Start: qemu_cmd_line prepared at ";
				staged += m_stagedCommandFile->Path->Data();
				AppendError(staged);
				StartWithCommandFile(m_stagedCommandFile);
				return;
			}

			if (m_stagedDriveFile == nullptr && m_stagedCdromFile == nullptr && !IsVideoTestProfile())
			{
				AppendError(L"Start: no media was prepared.");
				SetStartState(false, false);
				return;
			}

			if (m_stagedDriveFile != nullptr)
			{
				std::wstring staged = L"Start: drive prepared at ";
				staged += m_stagedDriveFile->Path->Data();
				AppendError(staged);
			}
			if (m_stagedCdromFile != nullptr)
			{
				std::wstring staged = L"Start: CD-ROM prepared at ";
				staged += m_stagedCdromFile->Path->Data();
				AppendError(staged);
			}

			commandLineBox->Text = BuildCommandLine();
			UpdateCommandPreview();
			std::wstring profile = L"Start: profile: ";
			ComboBoxItem^ selectedProfile = diagnosticProfileBox != nullptr ? dynamic_cast<ComboBoxItem^>(diagnosticProfileBox->SelectedItem) : nullptr;
			profile += selectedProfile != nullptr ? selectedProfile->Content->ToString()->Data() : L"Normal command";
			AppendError(profile);
			WriteCommandLineAndStart(commandLineBox->Text);
		}
		catch (Exception^ ex)
		{
			std::wstring error = L"Failed to copy boot file to LocalFolder: ";
			error += ex->Message->Data();
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
			SetStartState(false, false);
		}
	});
}
void DirectXPage::ResetButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	m_main->ResetCore();
	SetStatus(L"Reset sent to core.");
}

void DirectXPage::ShowTabsButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	if (topPanel->Visibility == Windows::UI::Xaml::Visibility::Visible)
	{
		topPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
		showTabsButton->Label = "Show tabs";
	}
	else
	{
		topPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;
		showTabsButton->Label = "Hide tabs";
	}
}

void DirectXPage::ClearErrorLogButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;

	m_errorLogText.clear();
	if (errorLogBox != nullptr)
	{
		errorLogBox->Text = "No errors recorded.";
	}

	std::wstring logPath(ApplicationData::Current->LocalFolder->Path->Data());
	logPath += L"\\qemu-uwp.log";
	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	HANDLE logFile = CreateFile2(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, &params);
	if (logFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(logFile);
		SetStatus(L"Errors and logs cleared.");
	}
	else
	{
		SetStatus(L"Errors cleared. Could not clear qemu-uwp.log.");
	}
}

void DirectXPage::UpdateCommandLineButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	commandLineBox->Text = BuildCommandLine();
	UpdateCommandPreview();
	SetStatus(L"qemu commands updated.");
}

void DirectXPage::ArgumentsHelpButton_PointerEntered(Object^ sender, PointerRoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_argumentsHelpHideTimer != nullptr)
	{
		m_argumentsHelpHideTimer->Stop();
	}
	if (argumentsHelpPanel != nullptr)
	{
		argumentsHelpPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;
	}
}

void DirectXPage::ArgumentsHelpButton_PointerExited(Object^ sender, PointerRoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_argumentsHelpHideTimer != nullptr)
	{
		m_argumentsHelpHideTimer->Stop();
		m_argumentsHelpHideTimer->Start();
	}
}

void DirectXPage::ArgumentsHelpHideTimer_Tick(Object^ sender, Object^ e)
{
	(void)sender;
	(void)e;
	if (m_argumentsHelpHideTimer != nullptr)
	{
		m_argumentsHelpHideTimer->Stop();
	}
	if (argumentsHelpPanel != nullptr)
	{
		argumentsHelpPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	}
}

void DirectXPage::SelectorLoadHideTimer_Tick(Object^ sender, Object^ e)
{
	(void)sender;
	(void)e;
	if (m_selectorLoadHideTimer != nullptr)
	{
		m_selectorLoadHideTimer->Stop();
	}
	if (selectorLoadProgressPanel != nullptr)
	{
		selectorLoadProgressPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	}
}

void DirectXPage::BootMediaSizeTimer_Tick(Object^ sender, Object^ e)
{
	(void)sender;
	(void)e;
	UpdateBootMediaSize();
}

void DirectXPage::BootOption_Changed(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	RefreshCommandLinePreview();
}

void DirectXPage::BootOptionText_Changed(Object^ sender, TextChangedEventArgs^ e)
{
	RefreshCommandLinePreview();
}

void DirectXPage::FirmwarePathSelector_SelectionChanged(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)e;
	if (m_updatingFirmwarePathLists)
	{
		return;
	}

	ComboBox^ box = dynamic_cast<ComboBox^>(sender);
	if (box == nullptr || box->SelectedIndex <= 0)
	{
		return;
	}

	ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(box->SelectedItem);
	String^ path = item != nullptr ? dynamic_cast<String^>(item->Tag) : nullptr;
	if (path == nullptr)
	{
		return;
	}

	if (box == biosPathSelectorBox)
	{
		biosPathBox->Text = path;
	}
	else if (box == kernelPathSelectorBox)
	{
		kernelPathBox->Text = path;
	}
	else if (box == initrdPathSelectorBox)
	{
		initrdPathBox->Text = path;
	}
	else if (box == dtbPathSelectorBox)
	{
		dtbPathBox->Text = path;
	}

	RefreshCommandLinePreview();
}

void DirectXPage::ClearFirmwarePathButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)e;
	if (sender == clearBiosPathButton)
	{
		ClearFirmwarePathSelector(biosPathSelectorBox, biosPathBox);
	}
	else if (sender == clearKernelPathButton)
	{
		ClearFirmwarePathSelector(kernelPathSelectorBox, kernelPathBox);
	}
	else if (sender == clearInitrdPathButton)
	{
		ClearFirmwarePathSelector(initrdPathSelectorBox, initrdPathBox);
	}
	else if (sender == clearDtbPathButton)
	{
		ClearFirmwarePathSelector(dtbPathSelectorBox, dtbPathBox);
	}
	else if (sender == clearKernelAppendButton && kernelAppendBox != nullptr)
	{
		kernelAppendBox->Text = "";
	}
	RefreshCommandLinePreview();
}

void DirectXPage::Architecture_Changed(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;

	if (architectureBox == nullptr || memorySlider == nullptr)
	{
		return;
	}

	ComboBoxItem^ selectedArch = dynamic_cast<ComboBoxItem^>(architectureBox->SelectedItem);
	if (selectedArch != nullptr && !m_applyingProfile)
	{
		std::wstring target(selectedArch->Content->ToString()->Data());
		TargetProfile profile = GetTargetProfile(target);
		memorySlider->Value = profile.memoryMb;
	}

	PopulateProfileOptions();
	ApplySelectedProfile();
	RefreshQemuOptionSelectors();
	RefreshCommandLinePreview();
}

void DirectXPage::QemuOption_Changed(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_updatingQemuOptionLists)
	{
		return;
	}
	RefreshCommandLinePreview();
}

void DirectXPage::DiagnosticProfile_Changed(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_updatingProfileList)
	{
		return;
	}
	ApplySelectedProfile();
	RefreshCommandLinePreview();
}

void DirectXPage::MemorySlider_ValueChanged(Object^ sender, RangeBaseValueChangedEventArgs^ e)
{
	int memoryMb = static_cast<int>(e->NewValue + 0.5);
	if (memoryValueText != nullptr)
	{
		if (memoryMb <= 0)
		{
			memoryValueText->Text = "Default";
		}
		else
		{
			std::wstring text = std::to_wstring(memoryMb);
			text += L" MB";
			memoryValueText->Text = ref new String(text.c_str());
		}
	}
	RefreshCommandLinePreview();
}

void DirectXPage::AdditionalArguments_Changed(Object^ sender, TextChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	RefreshCommandLinePreview();
}

void DirectXPage::OnKeyDown(CoreWindow^ sender, KeyEventArgs^ args)
{
	VirtualKey keyCode = args->VirtualKey;
	m_ctrlDown = IsControlDown();
	m_altDown = IsAltDown();
	if (keyCode == VirtualKey::Control || keyCode == VirtualKey::LeftControl || keyCode == VirtualKey::RightControl)
	{
		m_ctrlDown = true;
	}
	if (keyCode == VirtualKey::Menu || keyCode == VirtualKey::LeftMenu || keyCode == VirtualKey::RightMenu)
	{
		m_altDown = true;
	}
	if (keyCode == VirtualKey::M && (m_ctrlDown || IsControlDown()) && (m_altDown || IsAltDown()))
	{
		ToggleInputCapture();
		args->Handled = true;
		return;
	}
	if (!m_isRunning || !m_inputCaptured)
	{
		return;
	}

	unsigned key = MapVirtualKeyToRetro(args->VirtualKey);
	if (key != 0)
	{
		m_main->SetKey(key, true);
		args->Handled = true;
	}
}

void DirectXPage::OnKeyUp(CoreWindow^ sender, KeyEventArgs^ args)
{
	VirtualKey keyCode = args->VirtualKey;
	if (keyCode == VirtualKey::Control || keyCode == VirtualKey::LeftControl || keyCode == VirtualKey::RightControl)
	{
		m_ctrlDown = IsControlDown();
	}
	if (keyCode == VirtualKey::Menu || keyCode == VirtualKey::LeftMenu || keyCode == VirtualKey::RightMenu)
	{
		m_altDown = IsAltDown();
	}
	if (!m_isRunning || !m_inputCaptured)
	{
		return;
	}

	unsigned key = MapVirtualKeyToRetro(args->VirtualKey);
	if (key != 0)
	{
		m_main->SetKey(key, false);
		args->Handled = true;
	}
}

void DirectXPage::OnPointerPressed(Object^ sender, PointerEventArgs^ e)
{
	if (!m_isRunning)
	{
		return;
	}

	if (!m_inputCaptured)
	{
		Dispatcher->RunAsync(CoreDispatcherPriority::High, ref new DispatchedHandler([this]()
		{
			m_inputCaptured = true;
			m_mouseLeft = false;
			m_mouseRight = false;
			m_mouseMiddle = false;
			if (m_main != nullptr)
			{
				m_main->ClearInput();
			}
			UpdateCaptureIndicators();
			SetStatus(L"Input captured by the emulator. Ctrl+Alt+M releases mouse and keyboard.");
		}));
		return;
	}

	UpdateMouseButtonState(e);
	SendPointerToCore(e);
}

void DirectXPage::OnPointerMoved(Object^ sender, PointerEventArgs^ e)
{
	if (!m_isRunning || !m_inputCaptured)
	{
		return;
	}

	UpdateMouseButtonState(e);
	SendPointerToCore(e);
}

void DirectXPage::OnPointerReleased(Object^ sender, PointerEventArgs^ e)
{
	if (!m_isRunning || !m_inputCaptured)
	{
		return;
	}

	UpdateMouseButtonState(e);
	SendPointerToCore(e);
}

void DirectXPage::OnMouseMoved(MouseDevice^ sender, MouseEventArgs^ e)
{
	(void)sender;
	if (!m_isRunning || !m_inputCaptured || m_main == nullptr || e == nullptr)
	{
		return;
	}

	int deltaX = e->MouseDelta.X;
	int deltaY = e->MouseDelta.Y;
	if (deltaX == 0 && deltaY == 0)
	{
		return;
	}

	m_main->AddMouseDelta(
		deltaX,
		deltaY,
		m_mouseLeft.load(),
		m_mouseRight.load(),
		m_mouseMiddle.load());
}

void DirectXPage::OnCompositionScaleChanged(SwapChainPanel^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetCompositionScale(sender->CompositionScaleX, sender->CompositionScaleY);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnSwapChainPanelSizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
	m_inputSurfaceWidth = (std::max)(1.0, static_cast<double>(e->NewSize.Width));
	m_inputSurfaceHeight = (std::max)(1.0, static_cast<double>(e->NewSize.Height));
	ApplyMouseCaptureState();

	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetLogicalSize(e->NewSize);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::SetStartState(bool starting, bool running)
{
	m_isStarting = starting;
	m_isRunning = running;
	if (!running)
	{
		m_isPaused = false;
	}
	if (startButton != nullptr)
	{
		if (starting)
		{
			startButton->Label = "Starting";
		}
		else if (m_isStopping)
		{
			startButton->Label = "Stopping";
		}
		else if (m_isPaused)
		{
			startButton->Label = "Paused";
		}
		else if (running)
		{
			startButton->Label = "Running";
		}
		else
		{
			startButton->Label = "Start";
		}
	}
	UpdateEmulatorButtons();
	UpdateCaptureIndicators();
}

void DirectXPage::SetPausedState(bool paused)
{
	m_isPaused = paused && m_isRunning;
	if (startButton != nullptr && m_isRunning && !m_isStopping)
	{
		startButton->Label = m_isPaused ? "Paused" : "Running";
	}
	UpdateEmulatorButtons();
	UpdateCaptureIndicators();
}

void DirectXPage::SetStoppingState(bool stopping)
{
	m_isStopping = stopping;
	if (startButton != nullptr && stopping)
	{
		startButton->Label = "Stopping";
	}
	UpdateEmulatorButtons();
	UpdateCaptureIndicators();
}

void DirectXPage::UpdateEmulatorButtons()
{
	bool canStart = !m_isStarting && !m_isRunning && !m_isStopping;

	if (startButton != nullptr)
	{
		startButton->IsEnabled = canStart;
	}
	if (pauseButton != nullptr)
	{
		pauseButton->IsEnabled = false;
	}
	if (resumeButton != nullptr)
	{
		resumeButton->IsEnabled = false;
	}
	if (stopButton != nullptr)
	{
		stopButton->IsEnabled = false;
	}
	if (shutdownButton != nullptr)
	{
		shutdownButton->IsEnabled = false;
	}
}

void DirectXPage::SetStatus(const std::wstring& text)
{
	statusText->Text = ref new String(text.c_str());
}

void DirectXPage::SetSelectorLoadProgress(double percent, const std::wstring& text, bool visible)
{
	if (selectorLoadProgressPanel == nullptr || selectorLoadProgressBar == nullptr || selectorLoadProgressText == nullptr)
	{
		return;
	}

	if (percent < 0.0)
	{
		percent = 0.0;
	}
	else if (percent > 100.0)
	{
		percent = 100.0;
	}

	selectorLoadProgressPanel->Visibility = visible
		? Windows::UI::Xaml::Visibility::Visible
		: Windows::UI::Xaml::Visibility::Collapsed;
	selectorLoadProgressBar->Value = percent;

	int roundedPercent = static_cast<int>(percent + 0.5);
	std::wstring display = std::to_wstring(roundedPercent);
	display += L"%";
	selectorLoadProgressText->Text = ref new String(display.c_str());

	if (m_selectorLoadHideTimer != nullptr)
	{
		m_selectorLoadHideTimer->Stop();
		if (visible && roundedPercent >= 100)
		{
			m_selectorLoadHideTimer->Start();
		}
	}
}

void DirectXPage::ToggleInputCapture()
{
	m_inputCaptured = !m_inputCaptured;
	if (m_main != nullptr)
	{
		m_main->ClearInput();
	}
	m_mouseLeft = false;
	m_mouseRight = false;
	m_mouseMiddle = false;
	UpdateCaptureIndicators();
	SetStatus(m_inputCaptured
		? L"Input captured by the emulator. Ctrl+Alt+M releases mouse and keyboard."
		: L"Input released to the interface. Ctrl+Alt+M captures mouse and keyboard.");
}

void DirectXPage::UpdateCaptureIndicators()
{
	Windows::UI::Xaml::Visibility state = (m_isRunning && m_inputCaptured)
		? Windows::UI::Xaml::Visibility::Visible
		: Windows::UI::Xaml::Visibility::Collapsed;
	bool controlsCanTakeKeyboard = !(m_isRunning && m_inputCaptured);
	if (showTabsButton != nullptr)
	{
		showTabsButton->IsTabStop = controlsCanTakeKeyboard;
	}
	if (startButton != nullptr)
	{
		startButton->IsTabStop = controlsCanTakeKeyboard;
	}
	if (keyboardCaptureIcon != nullptr)
	{
		keyboardCaptureIcon->Visibility = state;
	}
	if (mouseCaptureIcon != nullptr)
	{
		mouseCaptureIcon->Visibility = state;
	}
	if (m_isRunning && m_inputCaptured)
	{
		FocusEmulatorSurface();
	}
	ApplyMouseCaptureState();
}

void DirectXPage::ApplyMouseCaptureState()
{
	bool activeCapture = m_isRunning && m_inputCaptured && m_windowVisible;
	try
	{
		CoreWindow^ window = Window::Current->CoreWindow;
		if (window != nullptr)
		{
			if (activeCapture)
			{
				window->PointerCursor = nullptr;
				if (!m_windowPointerCaptured)
				{
					window->SetPointerCapture();
					m_windowPointerCaptured = true;
				}
			}
			else
			{
				if (m_windowPointerCaptured)
				{
					window->ReleasePointerCapture();
					m_windowPointerCaptured = false;
				}
				window->PointerCursor = m_defaultPointerCursor != nullptr
					? m_defaultPointerCursor
					: ref new CoreCursor(CoreCursorType::Arrow, 0);
			}
		}
	}
	catch (...)
	{
	}

	if (activeCapture)
	{
		FocusEmulatorSurface();
		return;
	}

	if (m_main != nullptr)
	{
		m_main->ClearPointer();
	}
	m_mouseLeft = false;
	m_mouseRight = false;
	m_mouseMiddle = false;
}

void DirectXPage::FocusEmulatorSurface()
{
	try
	{
		this->Focus(Windows::UI::Xaml::FocusState::Programmatic);
	}
	catch (...)
	{
	}
}

void DirectXPage::UpdateMouseButtonState(PointerEventArgs^ e)
{
	if (e == nullptr || e->CurrentPoint == nullptr)
	{
		return;
	}

	auto props = e->CurrentPoint->Properties;
	m_mouseLeft.store(props->IsLeftButtonPressed);
	m_mouseRight.store(props->IsRightButtonPressed);
	m_mouseMiddle.store(props->IsMiddleButtonPressed);
}

void DirectXPage::SendPointerToCore(PointerEventArgs^ e)
{
	if (m_main == nullptr || e == nullptr || e->CurrentPoint == nullptr)
	{
		return;
	}

	auto point = e->CurrentPoint;
	auto props = point->Properties;
	double width = m_inputSurfaceWidth;
	double height = m_inputSurfaceHeight;
	if (width <= 1.0 || height <= 1.0)
	{
		return;
	}

	double physicalX = static_cast<double>(point->Position.X);
	double physicalY = static_cast<double>(point->Position.Y);
	double clampedX = (std::max)(0.0, (std::min)(width, physicalX));
	double clampedY = (std::max)(0.0, (std::min)(height, physicalY));
	double pointerX = ((clampedX / width) * 65534.0) - 32767.0;
	double pointerY = ((clampedY / height) * 65534.0) - 32767.0;
	m_main->SetPointer(
		static_cast<float>(pointerX),
		static_cast<float>(pointerY),
		props->IsLeftButtonPressed,
		props->IsRightButtonPressed,
		props->IsMiddleButtonPressed);
}

void DirectXPage::AppendError(const std::wstring& text)
{
	std::wstring line = text;
	line += L"\r\n";
	OutputDebugStringW(line.c_str());

	std::wstring logPath(ApplicationData::Current->LocalFolder->Path->Data());
	logPath += L"\\qemu-uwp.log";
	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	HANDLE logFile = CreateFile2(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, OPEN_ALWAYS, &params);
	if (logFile != INVALID_HANDLE_VALUE)
	{
		int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
		if (size > 0)
		{
			std::string utf8(size, '\0');
			WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), &utf8[0], size, nullptr, nullptr);
			DWORD written = 0;
			WriteFile(logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
		}
		CloseHandle(logFile);
	}

	if (!m_errorLogText.empty())
	{
		m_errorLogText += L"\r\n";
	}
	m_errorLogText += text;
	errorLogBox->Text = ref new String(m_errorLogText.c_str());
}

void DirectXPage::RefreshBootMediaState()
{
	if (m_updatingBootMediaLists)
	{
		return;
	}

	m_updatingBootMediaLists = true;
	auto local = ApplicationData::Current->LocalFolder;
	Concurrency::create_task(local->CreateFolderAsync("boot_media", CreationCollisionOption::OpenIfExists)).then(
		[](StorageFolder^ folder)
	{
		return Concurrency::create_task(folder->GetFilesAsync());
	}).then([this](Concurrency::task<Windows::Foundation::Collections::IVectorView<StorageFile^>^> filesTask)
	{
		try
		{
			auto files = filesTask.get();
			std::wstring selectedDrivePath = m_selectedDriveFile != nullptr ? m_selectedDriveFile->Path->Data() : L"";
			std::wstring selectedCdromPath = m_selectedCdromFile != nullptr ? m_selectedCdromFile->Path->Data() : L"";
			uint64_t totalSize = 0;
			int driveIndex = 0;
			int cdromIndex = 0;
			int driveCount = 0;
			int cdromCount = 0;

			driveBootMediaBox->Items->Clear();
			cdromBootMediaBox->Items->Clear();

			ComboBoxItem^ driveDefault = ref new ComboBoxItem();
			driveDefault->Content = "Select prepared drive media";
			driveBootMediaBox->Items->Append(driveDefault);

			ComboBoxItem^ cdromDefault = ref new ComboBoxItem();
			cdromDefault->Content = "Select prepared CD-ROM media";
			cdromBootMediaBox->Items->Append(cdromDefault);

			for each (StorageFile^ file in files)
			{
				std::wstring name(file->Name->Data());
				std::wstring path(file->Path->Data());
				uint64_t fileSize = FileSizeFromPath(path);
				totalSize += fileSize;

				std::wstring display = name + L" (" + FormatByteSize(fileSize) + L")";
				if (IsDriveMediaName(name))
				{
					ComboBoxItem^ item = ref new ComboBoxItem();
					item->Content = ref new String(display.c_str());
					item->Tag = file;
					driveBootMediaBox->Items->Append(item);
					driveCount++;
					if (!selectedDrivePath.empty() && _wcsicmp(selectedDrivePath.c_str(), path.c_str()) == 0)
					{
						driveIndex = driveCount;
					}
				}
				if (IsCdromMediaName(name))
				{
					ComboBoxItem^ item = ref new ComboBoxItem();
					item->Content = ref new String(display.c_str());
					item->Tag = file;
					cdromBootMediaBox->Items->Append(item);
					cdromCount++;
					if (!selectedCdromPath.empty() && _wcsicmp(selectedCdromPath.c_str(), path.c_str()) == 0)
					{
						cdromIndex = cdromCount;
					}
				}
			}

			driveBootMediaBox->SelectedIndex = driveIndex;
			cdromBootMediaBox->SelectedIndex = cdromIndex;
			m_updatingBootMediaLists = false;

			std::wstring sizeText = L"boot_media: " + FormatByteSize(totalSize);
			bootMediaSizeText->Text = ref new String(sizeText.c_str());
			RefreshFirmwarePathSelectors();
		}
		catch (Exception^ ex)
		{
			m_updatingBootMediaLists = false;
			std::wstring error = L"Failed to refresh boot_media: ";
			error += ex->Message->Data();
			AppendError(error);
			RefreshFirmwarePathSelectors();
		}
	});
}

void DirectXPage::UpdateBootMediaSize()
{
	if (m_updatingBootMediaSize || bootMediaSizeText == nullptr)
	{
		return;
	}

	m_updatingBootMediaSize = true;
	auto local = ApplicationData::Current->LocalFolder;
	Concurrency::create_task(local->CreateFolderAsync("boot_media", CreationCollisionOption::OpenIfExists)).then(
		[](StorageFolder^ folder)
	{
		return Concurrency::create_task(folder->GetFilesAsync());
	}).then([this](Concurrency::task<Windows::Foundation::Collections::IVectorView<StorageFile^>^> filesTask)
	{
		try
		{
			auto files = filesTask.get();
			uint64_t totalSize = 0;
			for each (StorageFile^ file in files)
			{
				totalSize += FileSizeFromPath(file->Path->Data());
			}

			std::wstring sizeText = L"boot_media: " + FormatByteSize(totalSize);
			bootMediaSizeText->Text = ref new String(sizeText.c_str());
		}
		catch (...)
		{
		}
		m_updatingBootMediaSize = false;
	});
}

void DirectXPage::RefreshFirmwarePathSelectors()
{
	if (biosPathSelectorBox == nullptr || kernelPathSelectorBox == nullptr || initrdPathSelectorBox == nullptr || dtbPathSelectorBox == nullptr)
	{
		return;
	}
	if (m_updatingFirmwarePathLists)
	{
		return;
	}

	m_updatingFirmwarePathLists = true;
	auto resetBox = [](ComboBox^ box, const wchar_t* defaultText)
	{
		box->Items->Clear();
		ComboBoxItem^ item = ref new ComboBoxItem();
		item->Content = ref new String(defaultText);
		box->Items->Append(item);
		box->SelectedIndex = 0;
	};

	resetBox(biosPathSelectorBox, L"Select BIOS / firmware");
	resetBox(kernelPathSelectorBox, L"Select kernel");
	resetBox(initrdPathSelectorBox, L"Select initrd");
	resetBox(dtbPathSelectorBox, L"Select DTB");

	auto addEntry = [this](const std::wstring& name, const std::wstring& display, const std::wstring& path, bool packageFiles)
	{
		if (path.empty())
		{
			return;
		}

		if (packageFiles || !HasExtension(name, L".qemu_cmd_line"))
		{
			if (IsQemuFirmwareName(name))
			{
				AddFirmwarePathItem(biosPathSelectorBox, display, path);
			}
			if (IsKernelBootName(name))
			{
				AddFirmwarePathItem(kernelPathSelectorBox, display, path);
			}
			if (IsInitrdBootName(name))
			{
				AddFirmwarePathItem(initrdPathSelectorBox, display, path);
			}
			if (HasExtension(name, L".dtb"))
			{
				AddFirmwarePathItem(dtbPathSelectorBox, display, path);
			}
		}
	};

	if (!m_packageFirmwarePathCacheLoaded)
	{
		if (m_packageFirmwarePathCacheLoading)
		{
			m_updatingFirmwarePathLists = false;
			return;
		}

		m_packageFirmwarePathCacheLoading = true;
		Concurrency::create_task(Package::Current->InstalledLocation->GetFolderAsync("qemu")).then([](Concurrency::task<StorageFolder^> folderTask)
		{
			try
			{
				return Concurrency::create_task(folderTask.get()->GetFilesAsync());
			}
			catch (...)
			{
				return Concurrency::task_from_result<Windows::Foundation::Collections::IVectorView<StorageFile^>^>(nullptr);
			}
		}).then([](Concurrency::task<Windows::Foundation::Collections::IVectorView<StorageFile^>^> qemuFilesTask)
		{
			std::vector<FirmwarePathCacheEntry> entries;
			try
			{
				auto files = qemuFilesTask.get();
				if (files != nullptr)
				{
					for each (StorageFile^ file in files)
					{
						if (file == nullptr || file->Name == nullptr || file->Path == nullptr)
						{
							continue;
						}

						std::wstring name(file->Name->Data());
						if (!IsQemuFirmwareName(name) && !IsKernelBootName(name) && !IsInitrdBootName(name) && !HasExtension(name, L".dtb"))
						{
							continue;
						}

						FirmwarePathCacheEntry entry;
						entry.name = name;
						entry.display = L"qemu\\";
						entry.display += name;
						entry.path = file->Path->Data();
						entries.push_back(entry);
					}
				}
			}
			catch (...)
			{
			}
			return entries;
		}).then([this](Concurrency::task<std::vector<FirmwarePathCacheEntry>> entriesTask)
		{
			try
			{
				m_packageFirmwarePathCache = entriesTask.get();
				m_packageFirmwarePathCacheLoaded = true;
			}
			catch (...)
			{
			}
			m_packageFirmwarePathCacheLoading = false;
			m_updatingFirmwarePathLists = false;
			RefreshFirmwarePathSelectors();
		});
		return;
	}

	for (const FirmwarePathCacheEntry& entry : m_packageFirmwarePathCache)
	{
		addEntry(entry.name, entry.display, entry.path, true);
	}

	auto addFiles = [addEntry](Windows::Foundation::Collections::IVectorView<StorageFile^>^ files, const wchar_t* prefix, bool packageFiles)
	{
		if (files == nullptr)
		{
			return;
		}

		for each (StorageFile^ file in files)
		{
			if (file == nullptr || file->Name == nullptr || file->Path == nullptr)
			{
				continue;
			}

			std::wstring name(file->Name->Data());
			std::wstring path(file->Path->Data());
			std::wstring display(prefix);
			display += name;
			addEntry(name, display, path, packageFiles);
		}
	};

	auto local = ApplicationData::Current->LocalFolder;
	Concurrency::create_task(local->CreateFolderAsync("boot_media", CreationCollisionOption::OpenIfExists)).then([](StorageFolder^ folder)
	{
		return Concurrency::create_task(folder->GetFilesAsync());
	}).then([this, addFiles](Concurrency::task<Windows::Foundation::Collections::IVectorView<StorageFile^>^> bootFilesTask)
	{
		try
		{
			addFiles(bootFilesTask.get(), L"boot_media\\", false);
		}
		catch (...)
		{
		}

		SelectFirmwarePathValue(biosPathSelectorBox, biosPathBox);
		SelectFirmwarePathValue(kernelPathSelectorBox, kernelPathBox);
		SelectFirmwarePathValue(initrdPathSelectorBox, initrdPathBox);
		SelectFirmwarePathValue(dtbPathSelectorBox, dtbPathBox);
		m_updatingFirmwarePathLists = false;
	});
}

void DirectXPage::AddFirmwarePathItem(ComboBox^ box, const std::wstring& display, const std::wstring& path)
{
	if (box == nullptr || path.empty())
	{
		return;
	}

	ComboBoxItem^ item = ref new ComboBoxItem();
	item->Content = ref new String(display.c_str());
	item->Tag = ref new String(path.c_str());
	box->Items->Append(item);
}

void DirectXPage::SelectFirmwarePathValue(ComboBox^ box, TextBox^ textBox)
{
	if (box == nullptr || textBox == nullptr || textBox->Text == nullptr || textBox->Text->Length() == 0)
	{
		if (box != nullptr)
		{
			box->SelectedIndex = 0;
		}
		return;
	}

	std::wstring value(textBox->Text->Data());
	box->SelectedIndex = 0;
	for (unsigned int i = 1; i < box->Items->Size; i++)
	{
		ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(box->Items->GetAt(i));
		String^ tag = item != nullptr ? dynamic_cast<String^>(item->Tag) : nullptr;
		if (tag != nullptr && _wcsicmp(tag->Data(), value.c_str()) == 0)
		{
			box->SelectedIndex = static_cast<int>(i);
			return;
		}
	}
}

void DirectXPage::ClearFirmwarePathSelector(ComboBox^ box, TextBox^ textBox)
{
	bool wasUpdating = m_updatingFirmwarePathLists;
	m_updatingFirmwarePathLists = true;
	if (box != nullptr)
	{
		box->SelectedIndex = 0;
	}
	m_updatingFirmwarePathLists = wasUpdating;
	if (textBox != nullptr)
	{
		textBox->Text = "";
	}
}

void DirectXPage::ClearBootMediaSelection(bool cdromMedia)
{
	if (cdromMedia)
	{
		m_selectedCdromFile = nullptr;
		m_stagedCdromFile = nullptr;
		StopMediaNbdServer(true);
		selectedCdromText->Text = "No CD-ROM selected";
		SelectComboValue(cdromFormatBox, nullptr);
		SelectComboValue(cdromInterfaceBox, nullptr);
		SelectComboValue(cdromCacheBox, nullptr);
		SelectComboValue(cdromAioBox, nullptr);
		if (cdromBootMediaBox != nullptr)
		{
			m_updatingBootMediaLists = true;
			cdromBootMediaBox->SelectedIndex = 0;
			m_updatingBootMediaLists = false;
		}
	}
	else
	{
		m_selectedBootFile = nullptr;
		m_selectedDriveFile = nullptr;
		m_selectedCommandFile = nullptr;
		m_stagedBootFile = nullptr;
		m_stagedDriveFile = nullptr;
		m_stagedCommandFile = nullptr;
		StopMediaNbdServer(false);
		selectedDriveText->Text = "No drive selected";
		SelectComboValue(driveFormatBox, nullptr);
		SelectComboValue(driveInterfaceBox, nullptr);
		SelectComboValue(driveCacheBox, nullptr);
		SelectComboValue(driveAioBox, nullptr);
		SelectComboValue(driveDiscardBox, nullptr);
		SelectComboValue(driveSnapshotBox, nullptr);
		SelectComboValue(driveReadonlyBox, nullptr);
		if (driveBootMediaBox != nullptr)
		{
			m_updatingBootMediaLists = true;
			driveBootMediaBox->SelectedIndex = 0;
			m_updatingBootMediaLists = false;
		}
	}

	if (m_selectedCommandFile == nullptr && m_selectedDriveFile == nullptr && m_selectedCdromFile == nullptr)
	{
		commandLineBox->Text = "";
		UpdateCommandPreview();
	}
	else
	{
		RefreshCommandLinePreview();
	}
}

void DirectXPage::SelectPreparedMedia(StorageFile^ file, bool cdromMedia)
{
	if (file == nullptr)
	{
		return;
	}

	if (cdromMedia)
	{
		m_selectedCommandFile = nullptr;
		m_selectedCdromFile = file;
		m_stagedCommandFile = nullptr;
		m_stagedCdromFile = file;
		StopMediaNbdServer(true);
		if (m_selectedDriveFile == nullptr)
		{
			selectedDriveText->Text = "No drive selected";
		}
		selectedCdromText->Text = file->Path;
		DetectMediaOptions(file, true);
		SetStatus(L"Prepared CD-ROM selected. Review qemu_cmd_line and click Start.");
		RefreshCommandLinePreview();
		return;
	}

	m_selectedBootFile = file;
	m_stagedBootFile = file;
	m_stagedDriveFile = nullptr;
	m_stagedCommandFile = nullptr;
	StopMediaNbdServer(false);
	if (IsCommandLineFile(file))
	{
		m_selectedCommandFile = file;
		m_stagedCommandFile = file;
		m_selectedDriveFile = nullptr;
		m_selectedCdromFile = nullptr;
		m_stagedCdromFile = nullptr;
		StopMediaNbdServer(true);
		SelectComboValue(driveFormatBox, nullptr);
		SelectComboValue(driveInterfaceBox, nullptr);
		SelectComboValue(driveCacheBox, nullptr);
		SelectComboValue(driveAioBox, nullptr);
		SelectComboValue(driveDiscardBox, nullptr);
		SelectComboValue(driveSnapshotBox, nullptr);
		SelectComboValue(driveReadonlyBox, nullptr);
		SelectComboValue(cdromFormatBox, nullptr);
		SelectComboValue(cdromInterfaceBox, nullptr);
		SelectComboValue(cdromCacheBox, nullptr);
		SelectComboValue(cdromAioBox, nullptr);
		if (cdromBootMediaBox != nullptr)
		{
			m_updatingBootMediaLists = true;
			cdromBootMediaBox->SelectedIndex = 0;
			m_updatingBootMediaLists = false;
		}
		selectedDriveText->Text = file->Path;
		selectedCdromText->Text = "No CD-ROM selected";
		Concurrency::create_task(FileIO::ReadTextAsync(file)).then([this](String^ text)
		{
			commandLineBox->Text = text;
			UpdateCommandPreview();
		});
	}
	else
	{
		m_selectedCommandFile = nullptr;
		m_selectedDriveFile = file;
		m_stagedDriveFile = file;
		selectedDriveText->Text = file->Path;
		DetectMediaOptions(file, false);
		RefreshCommandLinePreview();
	}
	SetStatus(L"Prepared drive media selected. Review qemu_cmd_line and click Start.");
}

void DirectXPage::DetectMediaOptions(StorageFile^ file, bool cdromMedia)
{
	if (file == nullptr || file->Name == nullptr || IsCommandLineFile(file))
	{
		return;
	}

	std::wstring fileName(file->Name->Data());
	std::wstring detectedFormat = DiskFormatFromFileName(fileName);
	if (cdromMedia)
	{
		SelectComboValue(cdromFormatBox, detectedFormat.c_str());
		SelectComboValue(cdromInterfaceBox, nullptr);
		SelectComboValue(cdromCacheBox, nullptr);
		SelectComboValue(cdromAioBox, nullptr);
	}
	else
	{
		SelectComboValue(driveFormatBox, detectedFormat.c_str());
		SelectComboValue(driveInterfaceBox, nullptr);
		SelectComboValue(driveCacheBox, nullptr);
		SelectComboValue(driveAioBox, nullptr);
		SelectComboValue(driveDiscardBox, nullptr);
		SelectComboValue(driveSnapshotBox, nullptr);
		SelectComboValue(driveReadonlyBox, nullptr);
	}
}

bool DirectXPage::IsFileInFolder(StorageFile^ file, StorageFolder^ folder)
{
	if (file == nullptr || folder == nullptr || file->Path == nullptr || folder->Path == nullptr)
	{
		return false;
	}

	std::wstring filePath(file->Path->Data());
	std::wstring folderPath(folder->Path->Data());
	if (filePath.length() <= folderPath.length())
	{
		return false;
	}

	return _wcsnicmp(filePath.c_str(), folderPath.c_str(), folderPath.length()) == 0 &&
		(filePath[folderPath.length()] == L'\\' || filePath[folderPath.length()] == L'/');
}

void DirectXPage::RefreshQemuOptionSelectors()
{
	if (m_coreDllOptionsLoaded)
	{
		PopulateQemuOptionSelectors();
		SetSelectorLoadProgress(100.0, L"Selectors", true);
		return;
	}
	if (m_coreDllOptionsLoading)
	{
		return;
	}

	m_coreDllOptionsLoading = true;
	SetSelectorLoadProgress(0.0, L"Selectors", true);
	PopulateQemuOptionSelectors();
	std::wstring dllPath(Package::Current->InstalledLocation->Path->Data());
	dllPath += L"\\qemu_libretro.dll";
	auto tokens = std::make_shared<std::set<std::string>>();
	auto requiredTokens = std::make_shared<std::set<std::string>>();
	for (const wchar_t* candidate : AllQemuOptionCandidates())
	{
		std::string narrow = NarrowAscii(candidate);
		if (!narrow.empty())
		{
			requiredTokens->insert(narrow);
		}
	}
	auto dispatcher = Dispatcher;
	Concurrency::create_task([this, dllPath, tokens, requiredTokens, dispatcher]()
	{
		auto reportProgress = [this, dispatcher](double percent, const wchar_t* text)
		{
			auto message = std::make_shared<std::wstring>(text);
			dispatcher->RunAsync(CoreDispatcherPriority::Low, ref new DispatchedHandler([this, percent, message]()
			{
				SetSelectorLoadProgress(percent, *message, true);
			}));
		};

		reportProgress(5.0, L"Opening selectors");
		CREATEFILE2_EXTENDED_PARAMETERS params = {};
		params.dwSize = sizeof(params);
		HANDLE dll = CreateFile2(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &params);
		if (dll == INVALID_HANDLE_VALUE)
		{
			return;
		}

		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(dll, &size) || size.QuadPart <= 0 || size.QuadPart > 512LL * 1024LL * 1024LL)
		{
			CloseHandle(dll);
			return;
		}

		reportProgress(10.0, L"Scanning selectors");
		std::vector<unsigned char> buffer(1024 * 1024);
		std::string currentToken;
		uint64_t totalRead = 0;
		DWORD chunkRead = 0;
		double lastReported = 10.0;
		auto flushToken = [&]()
		{
			if (currentToken.size() >= 2 && requiredTokens->find(currentToken) != requiredTokens->end())
			{
				tokens->insert(currentToken);
			}
			currentToken.clear();
		};

		while (totalRead < static_cast<uint64_t>(size.QuadPart))
		{
			DWORD remaining = static_cast<DWORD>((std::min)(static_cast<uint64_t>(buffer.size()), static_cast<uint64_t>(size.QuadPart) - totalRead));
			if (!ReadFile(dll, buffer.data(), remaining, &chunkRead, nullptr) || chunkRead == 0)
			{
				tokens->clear();
				break;
			}
			totalRead += chunkRead;

			for (DWORD i = 0; i < chunkRead; i++)
			{
				unsigned char value = buffer[i];
				bool tokenChar =
					(value >= 'a' && value <= 'z') ||
					(value >= 'A' && value <= 'Z') ||
					(value >= '0' && value <= '9') ||
					value == '_' ||
					value == '-' ||
					value == '.' ||
					value == ',';
				if (tokenChar)
				{
					if (currentToken.size() < 96)
					{
						currentToken.push_back(static_cast<char>(value));
					}
				}
				else
				{
					flushToken();
				}
			}
			if (tokens->size() >= requiredTokens->size())
			{
				break;
			}

			double readProgress = 10.0 + (static_cast<double>(totalRead) * 85.0 / static_cast<double>(size.QuadPart));
			if (readProgress - lastReported >= 5.0 || totalRead >= static_cast<uint64_t>(size.QuadPart))
			{
				lastReported = readProgress;
				reportProgress(readProgress, L"Scanning selectors");
			}
		}
		CloseHandle(dll);
		flushToken();
		if (tokens->empty())
		{
			return;
		}

		reportProgress(tokens->empty() ? 0.0 : 95.0, tokens->empty() ? L"Selectors unavailable" : L"Preparing selectors");
	}).then([this, dispatcher, tokens]()
	{
		dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, tokens]()
		{
			if (!tokens->empty())
			{
				m_coreDllAsciiTokens.swap(*tokens);
				m_coreDllOptionsLoaded = true;
			}
			m_coreDllOptionsLoading = false;
			PopulateQemuOptionSelectors();
			if (SelectedProfileId() > ProfileNormal)
			{
				ApplySelectedProfile();
			}
			UpdateCommandPreview();
			SetSelectorLoadProgress(m_coreDllOptionsLoaded ? 100.0 : 0.0, m_coreDllOptionsLoaded ? L"Selectors" : L"Selectors unavailable", true);
		}));
	});
}

void DirectXPage::PopulateQemuOptionSelectors()
{
	std::wstring target = CurrentTarget();
	m_currentOptionTarget = target;

	if (m_coreDllOptionsLoaded && m_qemuOptionPresenceByTarget.find(target) == m_qemuOptionPresenceByTarget.end())
	{
		std::vector<const wchar_t*> candidates;
		AppendCandidates(candidates, MachineCandidates(target));
		AppendCandidates(candidates, CpuCandidates(target));
		AppendCandidates(candidates, DeviceCandidates(target));
		AppendCandidates(candidates, VgaCandidates(target));
		AppendCandidates(candidates, MonitorCandidates());
		AppendCandidates(candidates, NetdevCandidates());

		std::map<std::wstring, bool> targetPresence;
		for (const wchar_t* candidate : candidates)
		{
			if (candidate == nullptr)
			{
				continue;
			}

			std::wstring wide(candidate);
			if (targetPresence.find(wide) != targetPresence.end())
			{
				continue;
			}

			std::string narrow = NarrowAscii(wide);
			targetPresence[wide] = !narrow.empty() &&
				m_coreDllAsciiTokens.find(narrow) != m_coreDllAsciiTokens.end();
		}
		m_qemuOptionPresenceByTarget[target] = targetPresence;
	}

	m_updatingQemuOptionLists = true;
	AddQemuOptionItems(machineBox, m_coreDllOptionsLoaded ? L"Default machine" : L"Loading machines...", MachineCandidates(target));
	AddQemuOptionItems(cpuBox, m_coreDllOptionsLoaded ? L"Default CPU" : L"Loading CPUs...", CpuCandidates(target));
	AddQemuOptionItems(deviceBox, m_coreDllOptionsLoaded ? L"No extra device" : L"Loading devices...", DeviceCandidates(target));
	AddQemuOptionItems(vgaBox, m_coreDllOptionsLoaded ? L"Default VGA" : L"Loading VGA...", VgaCandidates(target));
	AddQemuOptionItems(monitorBox, m_coreDllOptionsLoaded ? L"No monitor argument" : L"Loading monitors...", MonitorCandidates());
	AddQemuOptionItems(netdevBox, m_coreDllOptionsLoaded ? L"No netdev" : L"Loading netdevs...", NetdevCandidates());
	m_updatingQemuOptionLists = false;
}

void DirectXPage::AddQemuOptionItems(ComboBox^ box, const wchar_t* defaultText, const std::vector<const wchar_t*>& candidates)
{
	if (box == nullptr)
	{
		return;
	}

	box->Items->Clear();
	ComboBoxItem^ defaultItem = ref new ComboBoxItem();
	defaultItem->Content = ref new String(defaultText);
	defaultItem->Tag = nullptr;
	box->Items->Append(defaultItem);

	if (m_coreDllOptionsLoaded)
	{
		for (const wchar_t* candidate : candidates)
		{
			if (candidate != nullptr && CoreDllHasAscii(candidate))
			{
				ComboBoxItem^ item = ref new ComboBoxItem();
				item->Content = ref new String(candidate);
				item->Tag = ref new String(candidate);
				box->Items->Append(item);
			}
		}
	}

	box->SelectedIndex = 0;
	box->Visibility = (!m_coreDllOptionsLoaded || box->Items->Size > 1)
		? Windows::UI::Xaml::Visibility::Visible
		: Windows::UI::Xaml::Visibility::Collapsed;
}

bool DirectXPage::CoreDllHasAscii(const wchar_t* value)
{
	if (value == nullptr || !m_coreDllOptionsLoaded)
	{
		return false;
	}

	auto targetCache = m_qemuOptionPresenceByTarget.find(m_currentOptionTarget);
	if (targetCache == m_qemuOptionPresenceByTarget.end())
	{
		return false;
	}

	auto found = targetCache->second.find(value);
	return found != targetCache->second.end() && found->second;
}

std::wstring DirectXPage::SelectedComboValue(ComboBox^ box)
{
	if (box == nullptr || box->SelectedIndex <= 0)
	{
		return std::wstring();
	}

	ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(box->SelectedItem);
	if (item == nullptr || item->Tag == nullptr)
	{
		return std::wstring();
	}

	String^ value = dynamic_cast<String^>(item->Tag);
	return value != nullptr ? std::wstring(value->Data()) : std::wstring();
}

void DirectXPage::SelectComboValue(ComboBox^ box, const wchar_t* value)
{
	if (box == nullptr)
	{
		return;
	}

	box->SelectedIndex = 0;
	if (value == nullptr || value[0] == L'\0')
	{
		return;
	}

	for (unsigned int i = 1; i < box->Items->Size; i++)
	{
		ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(box->Items->GetAt(i));
		String^ tag = item != nullptr ? dynamic_cast<String^>(item->Tag) : nullptr;
		if (tag != nullptr && _wcsicmp(tag->Data(), value) == 0)
		{
			box->SelectedIndex = static_cast<int>(i);
			return;
		}
	}
}

void DirectXPage::SelectArchitectureValue(const wchar_t* value)
{
	if (architectureBox == nullptr || value == nullptr)
	{
		return;
	}

	for (unsigned int i = 0; i < architectureBox->Items->Size; i++)
	{
		ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(architectureBox->Items->GetAt(i));
		String^ content = item != nullptr ? item->Content->ToString() : nullptr;
		if (content != nullptr && _wcsicmp(content->Data(), value) == 0)
		{
			architectureBox->SelectedIndex = static_cast<int>(i);
			return;
		}
	}
}

std::wstring DirectXPage::CurrentTarget() const
{
	ComboBoxItem^ selectedArch = architectureBox != nullptr ? dynamic_cast<ComboBoxItem^>(architectureBox->SelectedItem) : nullptr;
	return selectedArch != nullptr ? std::wstring(selectedArch->Content->ToString()->Data()) : std::wstring(L"x86_64");
}

int DirectXPage::SelectedProfileId() const
{
	ComboBoxItem^ selectedProfile = diagnosticProfileBox != nullptr ? dynamic_cast<ComboBoxItem^>(diagnosticProfileBox->SelectedItem) : nullptr;
	String^ content = selectedProfile != nullptr ? selectedProfile->Content->ToString() : nullptr;
	return content != nullptr ? ProfileIdFromName(content->Data()) : ProfileNormal;
}

bool DirectXPage::IsVideoTestProfile() const
{
	return SelectedProfileId() == ProfileVideoTest;
}

void DirectXPage::PopulateProfileOptions()
{
	if (diagnosticProfileBox == nullptr)
	{
		return;
	}

	int previousProfile = SelectedProfileId();
	std::wstring target = CurrentTarget();
	m_updatingProfileList = true;
	diagnosticProfileBox->Items->Clear();
	int selectedIndex = 0;
	for (int profile = ProfileNormal; profile <= ProfileWindows7; profile++)
	{
		if (!IsProfileAvailableForTarget(profile, target))
		{
			continue;
		}

		ComboBoxItem^ item = ref new ComboBoxItem();
		item->Content = ref new String(ProfileName(profile));
		diagnosticProfileBox->Items->Append(item);
		if (profile == previousProfile)
		{
			selectedIndex = static_cast<int>(diagnosticProfileBox->Items->Size) - 1;
		}
	}
	diagnosticProfileBox->SelectedIndex = selectedIndex;
	m_updatingProfileList = false;
}

void DirectXPage::ApplySelectedProfile()
{
	if (m_applyingProfile)
	{
		return;
	}

	m_applyingProfile = true;
	int profile = SelectedProfileId();
	std::wstring currentTarget = CurrentTarget();
	const wchar_t* arch = nullptr;
	int memoryMb = -1;
	const wchar_t* machine = nullptr;
	const wchar_t* cpu = nullptr;
	const wchar_t* vga = nullptr;
	const wchar_t* device = nullptr;
	const wchar_t* monitor = nullptr;
	const wchar_t* netdev = nullptr;
	const wchar_t* audio = nullptr;
	const wchar_t* usb = nullptr;
	const wchar_t* rtcBase = nullptr;
	int bootDevice = -1;
	int smpCount = -1;

	switch (profile)
	{
	case 0: // Normal command
		memoryMb = GetTargetProfile(currentTarget).memoryMb;
		bootDevice = 0;
		smpCount = 1;
		break;
	case 1: // Video Teste
		memoryMb = (std::min)(GetTargetProfile(currentTarget).memoryMb, 512);
		if (memoryMb <= 0)
		{
			memoryMb = 256;
		}
		monitor = L"none";
		bootDevice = 1;
		smpCount = 1;
		break;
	case 2: // Linux cloud qcow2
		arch = L"x86_64";
		memoryMb = 2048;
		smpCount = 2;
		machine = L"q35";
		cpu = L"qemu64";
		vga = L"virtio";
		device = L"virtio-net-pci";
		netdev = L"user";
		bootDevice = 1;
		break;
	case 3: // Ubuntu 10
		arch = L"i386";
		memoryMb = 1024;
		smpCount = 1;
		machine = L"pc";
		cpu = L"pentium3";
		vga = L"std";
		device = L"rtl8139";
		netdev = L"user";
		bootDevice = 0;
		break;
	case 4: // Ubuntu 14
		arch = L"x86_64";
		memoryMb = 2048;
		smpCount = 2;
		machine = L"pc";
		cpu = L"qemu64";
		vga = L"std";
		device = L"e1000";
		netdev = L"user";
		bootDevice = 0;
		break;
	case 5: // Windows 98
		arch = L"i386";
		memoryMb = 256;
		smpCount = 1;
		machine = L"pc";
		cpu = L"pentium2";
		vga = L"cirrus";
		device = L"rtl8139";
		netdev = L"user";
		audio = L"sb16";
		usb = L"off";
		rtcBase = L"localtime";
		bootDevice = 0;
		break;
	case 6: // Windows XP
		arch = L"i386";
		memoryMb = 512;
		smpCount = 1;
		machine = L"pc";
		cpu = L"pentium3";
		vga = L"std";
		device = L"rtl8139";
		netdev = L"user";
		audio = L"AC97";
		rtcBase = L"localtime";
		bootDevice = 0;
		break;
	case 7: // Windows 7
		arch = L"x86_64";
		memoryMb = 2048;
		smpCount = 2;
		machine = L"q35";
		cpu = L"qemu64";
		vga = L"std";
		device = L"e1000";
		netdev = L"user";
		audio = L"intel-hda";
		usb = L"on";
		rtcBase = L"localtime";
		bootDevice = 0;
		break;
	default:
		break;
	}

	if (arch != nullptr)
	{
		SelectArchitectureValue(arch);
	}
	if (memoryMb >= 0 && memorySlider != nullptr)
	{
		memorySlider->Value = memoryMb;
	}
	if (smpCount >= 0 && smpBox != nullptr)
	{
		std::wstring smpValue = std::to_wstring(smpCount);
		SelectComboValue(smpBox, smpValue.c_str());
	}
	PopulateQemuOptionSelectors();
	SelectComboValue(machineBox, machine);
	SelectComboValue(cpuBox, cpu);
	SelectComboValue(vgaBox, vga);
	SelectComboValue(deviceBox, device);
	SelectComboValue(monitorBox, monitor);
	SelectComboValue(netdevBox, netdev);
	SelectComboValue(bootOrderBox, nullptr);
	SelectComboValue(bootMenuBox, nullptr);
	SelectComboValue(bootStrictBox, nullptr);
	SelectComboValue(bootOnceBox, nullptr);
	SelectComboValue(driveFormatBox, nullptr);
	SelectComboValue(driveInterfaceBox, nullptr);
	SelectComboValue(driveCacheBox, nullptr);
	SelectComboValue(driveAioBox, nullptr);
	SelectComboValue(driveDiscardBox, nullptr);
	SelectComboValue(driveSnapshotBox, nullptr);
	SelectComboValue(driveReadonlyBox, nullptr);
	SelectComboValue(cdromFormatBox, nullptr);
	SelectComboValue(cdromInterfaceBox, nullptr);
	SelectComboValue(cdromCacheBox, nullptr);
	SelectComboValue(cdromAioBox, nullptr);
	SelectComboValue(audioDeviceBox, audio);
	SelectComboValue(usbModeBox, usb);
	SelectComboValue(inputDeviceBox, nullptr);
	SelectComboValue(rtcBaseBox, rtcBase);
	SelectComboValue(rtcClockBox, nullptr);
	SelectComboValue(debugBehaviorBox, nullptr);
	SelectComboValue(serialBox, nullptr);
	SelectComboValue(parallelBox, nullptr);
	SelectComboValue(debugFlagsBox, nullptr);
	ClearFirmwarePathSelector(biosPathSelectorBox, biosPathBox);
	ClearFirmwarePathSelector(kernelPathSelectorBox, kernelPathBox);
	ClearFirmwarePathSelector(initrdPathSelectorBox, initrdPathBox);
	ClearFirmwarePathSelector(dtbPathSelectorBox, dtbPathBox);
	if (kernelAppendBox != nullptr)
	{
		kernelAppendBox->Text = "";
	}
	if (bootDevice >= 0 && bootDeviceBox != nullptr)
	{
		bootDeviceBox->SelectedIndex = bootDevice;
	}

	m_applyingProfile = false;
	UpdateCommandPreview();
}

void DirectXPage::ResetCommandDefaults()
{
	m_selectedBootFile = nullptr;
	m_stagedBootFile = nullptr;
	m_selectedDriveFile = nullptr;
	m_selectedCdromFile = nullptr;
	m_selectedCommandFile = nullptr;
	m_stagedDriveFile = nullptr;
	m_stagedCdromFile = nullptr;
	m_stagedCommandFile = nullptr;
	StopAllMediaNbdServers();

	selectedDriveText->Text = "No drive selected";
	selectedCdromText->Text = "No CD-ROM selected";
	if (architectureBox != nullptr)
	{
		architectureBox->SelectedIndex = 0;
	}
	if (bootDeviceBox != nullptr)
	{
		bootDeviceBox->SelectedIndex = 0;
	}
	auto resetCombo = [](ComboBox^ box)
	{
		if (box != nullptr)
		{
			box->SelectedIndex = 0;
		}
	};
	resetCombo(bootOrderBox);
	resetCombo(bootMenuBox);
	resetCombo(bootStrictBox);
	resetCombo(bootOnceBox);
	resetCombo(driveFormatBox);
	resetCombo(driveInterfaceBox);
	resetCombo(driveCacheBox);
	resetCombo(driveAioBox);
	resetCombo(driveDiscardBox);
	resetCombo(driveSnapshotBox);
	resetCombo(driveReadonlyBox);
	resetCombo(cdromFormatBox);
	resetCombo(cdromInterfaceBox);
	resetCombo(cdromCacheBox);
	resetCombo(cdromAioBox);
	resetCombo(audioDeviceBox);
	resetCombo(usbModeBox);
	resetCombo(inputDeviceBox);
	resetCombo(rtcBaseBox);
	resetCombo(rtcClockBox);
	resetCombo(debugBehaviorBox);
	resetCombo(serialBox);
	resetCombo(parallelBox);
	resetCombo(debugFlagsBox);
	if (diagnosticProfileBox != nullptr)
	{
		diagnosticProfileBox->SelectedIndex = 0;
	}
	ClearFirmwarePathSelector(biosPathSelectorBox, biosPathBox);
	ClearFirmwarePathSelector(kernelPathSelectorBox, kernelPathBox);
	ClearFirmwarePathSelector(initrdPathSelectorBox, initrdPathBox);
	ClearFirmwarePathSelector(dtbPathSelectorBox, dtbPathBox);
	if (kernelAppendBox != nullptr)
	{
		kernelAppendBox->Text = "";
	}
	if (extraArgumentsBox != nullptr)
	{
		extraArgumentsBox->Text = "";
	}
	if (commandLineBox != nullptr)
	{
		commandLineBox->Text = "";
	}
	if (memorySlider != nullptr)
	{
		memorySlider->Value = GetTargetProfile(L"x86_64").memoryMb;
	}
	resetCombo(smpBox);
	RefreshBootMediaState();
	RefreshQemuOptionSelectors();
	UpdateCommandPreview();
}

void DirectXPage::RefreshCommandLinePreview()
{
	if (commandLineBox == nullptr || m_selectedCommandFile != nullptr)
	{
		UpdateCommandPreview();
		return;
	}

	if (m_selectedDriveFile == nullptr && m_selectedCdromFile == nullptr &&
		m_stagedDriveFile == nullptr && m_stagedCdromFile == nullptr)
	{
		UpdateCommandPreview();
		return;
	}

	commandLineBox->Text = BuildCommandLine();
	UpdateCommandPreview();
}

bool DirectXPage::EnsureMediaNbdServer(StorageFile^ mediaFile, bool readOnly, bool cdromMedia, std::wstring& url)
{
	if (mediaFile == nullptr)
	{
		return false;
	}

	std::wstring mediaPath(mediaFile->Path->Data());
	StreamSocketListener^ currentListener = cdromMedia ? m_cdromNbdListener : m_driveNbdListener;
	std::wstring& currentPath = cdromMedia ? m_cdromNbdPath : m_driveNbdPath;
	uint64_t& currentSize = cdromMedia ? m_cdromNbdSize : m_driveNbdSize;
	int& currentPort = cdromMedia ? m_cdromNbdPort : m_driveNbdPort;
	bool& currentReadOnly = cdromMedia ? m_cdromNbdReadOnly : m_driveNbdReadOnly;

	if (currentListener != nullptr && currentPath == mediaPath && currentPort > 0 && currentReadOnly == readOnly)
	{
		url = L"nbd://127.0.0.1:" + std::to_wstring(currentPort);
		return true;
	}

	StopMediaNbdServer(cdromMedia);

	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	DWORD desiredAccess = readOnly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
	HANDLE media = CreateFile2(mediaPath.c_str(), desiredAccess, FILE_SHARE_READ, OPEN_EXISTING, &params);
	if (media == INVALID_HANDLE_VALUE)
	{
		if (!readOnly)
		{
			AppendError(L"Start: media could not be opened for writing; using read-only NBD.");
			readOnly = true;
			desiredAccess = GENERIC_READ;
			media = CreateFile2(mediaPath.c_str(), desiredAccess, FILE_SHARE_READ, OPEN_EXISTING, &params);
		}
		if (media == INVALID_HANDLE_VALUE)
		{
			AppendError(L"Start: could not open media for the local NBD server.");
			return false;
		}
	}

	LARGE_INTEGER size = {};
	bool sizeOk = GetFileSizeEx(media, &size) != 0;
	CloseHandle(media);
	if (!sizeOk || size.QuadPart <= 0)
	{
		AppendError(L"Start: invalid media size for the local NBD server.");
		return false;
	}

	for (int port = 10809; port <= 10839; port++)
	{
		auto listener = ref new StreamSocketListener();
		listener->ConnectionReceived += ref new TypedEventHandler<StreamSocketListener^, StreamSocketListenerConnectionReceivedEventArgs^>(
			this, &DirectXPage::OnMediaNbdConnectionReceived);

		try
		{
			create_task(listener->BindServiceNameAsync(ref new String(std::to_wstring(port).c_str()))).wait();
			if (cdromMedia)
			{
				m_cdromNbdListener = listener;
			}
			else
			{
				m_driveNbdListener = listener;
			}
			currentPath = mediaPath;
			currentSize = static_cast<uint64_t>(size.QuadPart);
			currentPort = port;
			currentReadOnly = readOnly;
			url = L"nbd://127.0.0.1:" + std::to_wstring(port);
			AppendError((readOnly ? L"Start: local read-only NBD server started at " : L"Start: local read-write NBD server started at ") + url +
				(cdromMedia ? L" for CD-ROM." : L" for Drive."));
			return true;
		}
		catch (Exception^ ex)
		{
			std::wstring bindError = L"Start: porta NBD local indisponivel ";
			bindError += std::to_wstring(port);
			if (ex != nullptr && ex->Message != nullptr)
			{
				bindError += L": ";
				bindError += ex->Message->Data();
			}
			AppendError(bindError);
			delete listener;
		}
	}

	AppendError(L"Start: could not start local media NBD server.");
	return false;
}

void DirectXPage::StopMediaNbdServer(bool cdromMedia)
{
	StreamSocketListener^& listener = cdromMedia ? m_cdromNbdListener : m_driveNbdListener;
	std::wstring& path = cdromMedia ? m_cdromNbdPath : m_driveNbdPath;
	uint64_t& size = cdromMedia ? m_cdromNbdSize : m_driveNbdSize;
	int& port = cdromMedia ? m_cdromNbdPort : m_driveNbdPort;
	bool& readOnly = cdromMedia ? m_cdromNbdReadOnly : m_driveNbdReadOnly;

	if (listener != nullptr)
	{
		delete listener;
		listener = nullptr;
	}
	path.clear();
	size = 0;
	port = 0;
	readOnly = true;
}

void DirectXPage::StopAllMediaNbdServers()
{
	StopMediaNbdServer(false);
	StopMediaNbdServer(true);
}

void DirectXPage::OnMediaNbdConnectionReceived(StreamSocketListener^ sender, StreamSocketListenerConnectionReceivedEventArgs^ args)
{
	auto socket = args->Socket;
	bool cdromMedia = sender == m_cdromNbdListener;
	std::wstring mediaPath = cdromMedia ? m_cdromNbdPath : m_driveNbdPath;
	uint64_t mediaSize = cdromMedia ? m_cdromNbdSize : m_driveNbdSize;
	bool readOnly = cdromMedia ? m_cdromNbdReadOnly : m_driveNbdReadOnly;
	create_task([this, socket, mediaPath, mediaSize, readOnly]()
	{
		ServeMediaNbdClient(socket, mediaPath, mediaSize, readOnly);
	});
}

void DirectXPage::ServeMediaNbdClient(StreamSocket^ socket, std::wstring mediaPath, uint64_t mediaSize, bool readOnly)
{
	try
	{
		auto reader = ref new DataReader(socket->InputStream);
		reader->InputStreamOptions = InputStreamOptions::None;
		auto writer = ref new DataWriter(socket->OutputStream);

		auto writeBe16 = [writer](uint16_t value)
		{
			unsigned char bytes[2] =
			{
				static_cast<unsigned char>((value >> 8) & 0xff),
				static_cast<unsigned char>(value & 0xff)
			};
			writer->WriteBytes(ref new Platform::Array<unsigned char>(bytes, 2));
		};
		auto writeBe32 = [writer](uint32_t value)
		{
			unsigned char bytes[4] =
			{
				static_cast<unsigned char>((value >> 24) & 0xff),
				static_cast<unsigned char>((value >> 16) & 0xff),
				static_cast<unsigned char>((value >> 8) & 0xff),
				static_cast<unsigned char>(value & 0xff)
			};
			writer->WriteBytes(ref new Platform::Array<unsigned char>(bytes, 4));
		};
		auto writeBe64 = [writer](uint64_t value)
		{
			unsigned char bytes[8] =
			{
				static_cast<unsigned char>((value >> 56) & 0xff),
				static_cast<unsigned char>((value >> 48) & 0xff),
				static_cast<unsigned char>((value >> 40) & 0xff),
				static_cast<unsigned char>((value >> 32) & 0xff),
				static_cast<unsigned char>((value >> 24) & 0xff),
				static_cast<unsigned char>((value >> 16) & 0xff),
				static_cast<unsigned char>((value >> 8) & 0xff),
				static_cast<unsigned char>(value & 0xff)
			};
			writer->WriteBytes(ref new Platform::Array<unsigned char>(bytes, 8));
		};
		auto readBe16 = [reader]() -> uint16_t
		{
			unsigned char b0 = reader->ReadByte();
			unsigned char b1 = reader->ReadByte();
			return (static_cast<uint16_t>(b0) << 8) | b1;
		};
		auto readBe32 = [reader]() -> uint32_t
		{
			uint32_t value = 0;
			for (int i = 0; i < 4; i++)
			{
				value = (value << 8) | reader->ReadByte();
			}
			return value;
		};
		auto readBe64 = [reader]() -> uint64_t
		{
			uint64_t value = 0;
			for (int i = 0; i < 8; i++)
			{
				value = (value << 8) | reader->ReadByte();
			}
			return value;
		};

		writeBe64(0x4e42444d41474943ULL);
		writeBe64(0x0000420281861253ULL);
		writeBe64(mediaSize);
		writeBe32(readOnly ? 0x00000003 : 0x00000001);
		auto zeroes = ref new Platform::Array<unsigned char>(124);
		writer->WriteBytes(zeroes);
		create_task(writer->StoreAsync()).wait();

		CREATEFILE2_EXTENDED_PARAMETERS params = {};
		params.dwSize = sizeof(params);
		DWORD access = readOnly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
		HANDLE media = CreateFile2(mediaPath.c_str(), access, FILE_SHARE_READ, OPEN_EXISTING, &params);
		if (media == INVALID_HANDLE_VALUE)
		{
			delete writer;
			delete reader;
			delete socket;
			return;
		}

		while (true)
		{
			unsigned int loaded = create_task(reader->LoadAsync(28)).get();
			if (loaded < 28)
			{
				break;
			}

			uint32_t magic = readBe32();
			uint16_t flags = readBe16();
			(void)flags;
			uint16_t type = readBe16();
			uint64_t handle = readBe64();
			uint64_t offset = readBe64();
			uint32_t length = readBe32();
			if (magic != 0x25609513)
			{
				break;
			}
			if (type == 2)
			{
				break;
			}

			uint32_t error = 0;
			std::vector<unsigned char> payload;
			if (type == 0)
			{
				if (offset + length > mediaSize)
				{
					error = 22;
				}
				else
				{
					payload.resize(length);
					LARGE_INTEGER position = {};
					position.QuadPart = static_cast<LONGLONG>(offset);
					SetFilePointerEx(media, position, nullptr, FILE_BEGIN);
					DWORD read = 0;
					if (!ReadFile(media, payload.data(), length, &read, nullptr) || read != length)
					{
						error = 5;
					}
				}
			}
			else if (type == 1)
			{
				payload.resize(length);
				if (length > 0)
				{
					unsigned int writePayloadLoaded = create_task(reader->LoadAsync(length)).get();
					if (writePayloadLoaded < length)
					{
						break;
					}
					auto writePayload = ref new Platform::Array<unsigned char>(length);
					reader->ReadBytes(writePayload);
					memcpy(payload.data(), writePayload->Data, length);
				}

				if (readOnly)
				{
					error = 1;
				}
				else if (offset + length > mediaSize)
				{
					error = 22;
				}
				else
				{
					LARGE_INTEGER position = {};
					position.QuadPart = static_cast<LONGLONG>(offset);
					SetFilePointerEx(media, position, nullptr, FILE_BEGIN);
					DWORD written = 0;
					if (!WriteFile(media, payload.data(), length, &written, nullptr) || written != length)
					{
						error = 5;
					}
				}
			}
			else if (type != 3)
			{
				error = 95;
			}

			writeBe32(0x67446698);
			writeBe32(error);
			writeBe64(handle);
			if (error == 0 && type == 0 && !payload.empty())
			{
				writer->WriteBytes(ref new Platform::Array<unsigned char>(payload.data(), static_cast<unsigned int>(payload.size())));
			}
			create_task(writer->StoreAsync()).wait();
		}

		CloseHandle(media);
		delete writer;
		delete reader;
		delete socket;
	}
	catch (...)
	{
		try
		{
			delete socket;
		}
		catch (...)
		{
		}
	}
}

void DirectXPage::UpdateCommandPreview()
{
	if (commandPreviewBlock == nullptr)
	{
		return;
	}

	commandPreviewBlock->Blocks->Clear();
	Paragraph^ paragraph = ref new Paragraph();

	String^ automatic = BuildAutomaticCommandLine();
	if (automatic != nullptr && automatic->Length() > 0)
	{
		Run^ autoRun = ref new Run();
		autoRun->Text = automatic;
		autoRun->Foreground = ref new SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 125, 211, 252));
		paragraph->Inlines->Append(autoRun);
	}

	String^ additional = BuildAdditionalArguments();
	if (additional != nullptr && additional->Length() > 0)
	{
		Run^ separator = ref new Run();
		separator->Text = "\r\n";
		paragraph->Inlines->Append(separator);

		Run^ additionalRun = ref new Run();
		std::wstring text = L"Additional QEMU arguments: ";
		text += additional->Data();
		additionalRun->Text = ref new String(text.c_str());
		additionalRun->Foreground = ref new SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 250, 204, 21));
		paragraph->Inlines->Append(additionalRun);
	}

	if (paragraph->Inlines->Size == 0)
	{
		Run^ emptyRun = ref new Run();
		emptyRun->Text = "No generated command yet.";
		emptyRun->Foreground = ref new SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 156, 163, 175));
		paragraph->Inlines->Append(emptyRun);
	}

	commandPreviewBlock->Blocks->Append(paragraph);
}

String^ DirectXPage::BuildAdditionalArguments()
{
	if (extraArgumentsBox != nullptr && extraArgumentsBox->Text != nullptr && extraArgumentsBox->Text->Length() > 0)
	{
		return extraArgumentsBox->Text;
	}
	return "";
}

String^ DirectXPage::BuildCommandLine()
{
	String^ automatic = BuildAutomaticCommandLine();
	String^ additional = BuildAdditionalArguments();
	std::wstring command = automatic != nullptr ? automatic->Data() : L"";
	if (additional != nullptr && additional->Length() > 0)
	{
		if (!command.empty())
		{
			command += L" ";
		}
		command += additional->Data();
	}
	return ref new String(command.c_str());
}

String^ DirectXPage::BuildAutomaticCommandLine()
{
	String^ arch = "x86_64";
	ComboBoxItem^ selectedArch = dynamic_cast<ComboBoxItem^>(architectureBox->SelectedItem);
	if (selectedArch != nullptr)
	{
		arch = selectedArch->Content->ToString();
	}
	std::wstring target(arch->Data());
	TargetProfile targetProfile = GetTargetProfile(target);

	int memoryMb = static_cast<int>(memorySlider->Value + 0.5);

	StorageFile^ driveFile = m_stagedDriveFile != nullptr ? m_stagedDriveFile : m_selectedDriveFile;
	StorageFile^ cdromFile = m_stagedCdromFile != nullptr ? m_stagedCdromFile : m_selectedCdromFile;
	bool canStartDriveServer = m_stagedDriveFile != nullptr;
	bool canStartCdromServer = m_stagedCdromFile != nullptr;
	std::wstring command = L"qemu-system-";
	command += arch->Data();
	command += L" -libretro";
	std::wstring selectedMachine = SelectedComboValue(machineBox);
	if (!selectedMachine.empty())
	{
		command += L" -M ";
		command += selectedMachine;
	}
	else
	{
		command += targetProfile.machineArgs;
	}

	if (memoryMb > 0)
	{
		command += L" -m ";
		command += std::to_wstring(memoryMb);
		command += L"M";
	}

	std::wstring selectedSmp = SelectedComboValue(smpBox);
	if (!selectedSmp.empty())
	{
		command += L" -smp ";
		command += selectedSmp;
	}

	std::wstring selectedCpu = SelectedComboValue(cpuBox);
	if (!selectedCpu.empty())
	{
		command += L" -cpu ";
		command += selectedCpu;
	}

	std::wstring selectedVga = SelectedComboValue(vgaBox);
	if (!selectedVga.empty())
	{
		command += L" -vga ";
		command += selectedVga;
	}

	std::wstring selectedMonitor = SelectedComboValue(monitorBox);
	if (!selectedMonitor.empty())
	{
		command += L" -monitor ";
		command += selectedMonitor;
	}

	std::wstring selectedNetdev = SelectedComboValue(netdevBox);
	if (!selectedNetdev.empty())
	{
		command += L" -netdev ";
		command += selectedNetdev;
		command += L",id=net0";
	}

	std::wstring selectedDevice = SelectedComboValue(deviceBox);
	if (!selectedDevice.empty())
	{
		command += L" -device ";
		command += selectedDevice;
		if (!selectedNetdev.empty() && IsNetworkDeviceName(selectedDevice))
		{
			command += L",netdev=net0";
		}
	}

	auto appendBootOption = [this, &command](const std::wstring& fallbackOrder)
	{
		std::wstring order = SelectedComboValue(bootOrderBox);
		if (order.empty())
		{
			order = fallbackOrder;
		}

		std::wstring menu = SelectedComboValue(bootMenuBox);
		std::wstring strict = SelectedComboValue(bootStrictBox);
		std::wstring once = SelectedComboValue(bootOnceBox);
		bool useExtendedSyntax = !menu.empty() || !strict.empty() || !once.empty() || order.length() > 1;
		command += L" -boot ";
		if (useExtendedSyntax)
		{
			command += L"order=";
			command += order;
			if (!menu.empty())
			{
				command += L",menu=";
				command += menu;
			}
			if (!strict.empty())
			{
				command += L",strict=";
				command += strict;
			}
			if (!once.empty())
			{
				command += L",once=";
				command += once;
			}
		}
		else
		{
			command += order;
		}
	};

	int diagnosticProfile = diagnosticProfileBox != nullptr ? diagnosticProfileBox->SelectedIndex : 0;
	if (diagnosticProfile == 1)
	{
		appendBootOption(L"c");
	}
	else
	{
		bool hasBootDevice = false;
		if (driveFile != nullptr)
		{
			std::wstring fileName(driveFile->Name->Data());
			std::wstring diskFormat = DiskFormatFromFileName(fileName);
			std::wstring selectedDriveFormat = SelectedComboValue(driveFormatBox);
			if (!selectedDriveFormat.empty())
			{
				diskFormat = selectedDriveFormat;
			}
			std::wstring mediaUrl;
			std::wstring driveInterface = SelectedComboValue(driveInterfaceBox);
			TargetBlockStyle driveBlockStyle = targetProfile.blockStyle;
			bool useUsbStorage = false;
			if (_wcsicmp(driveInterface.c_str(), L"ide") == 0)
			{
				driveBlockStyle = TargetBlockStyle::Ide;
			}
			else if (_wcsicmp(driveInterface.c_str(), L"scsi") == 0)
			{
				driveBlockStyle = TargetBlockStyle::Scsi;
			}
			else if (_wcsicmp(driveInterface.c_str(), L"virtio") == 0)
			{
				driveBlockStyle = targetProfile.blockStyle == TargetBlockStyle::VirtioMmio ||
					targetProfile.blockStyle == TargetBlockStyle::VirtioPci ||
					targetProfile.blockStyle == TargetBlockStyle::VirtioCcw
					? targetProfile.blockStyle
					: TargetBlockStyle::VirtioPci;
			}
			else if (_wcsicmp(driveInterface.c_str(), L"usb") == 0)
			{
				useUsbStorage = true;
			}
			bool useVirtio = driveBlockStyle == TargetBlockStyle::VirtioMmio ||
				driveBlockStyle == TargetBlockStyle::VirtioPci ||
				driveBlockStyle == TargetBlockStyle::VirtioCcw;
			if (canStartDriveServer && EnsureMediaNbdServer(driveFile, false, false, mediaUrl))
			{
				command += L" -drive file=";
				command += QuoteForCommandLine(ref new String(mediaUrl.c_str()))->Data();
			}
			else
			{
				command += L" -drive file=\"nbd://127.0.0.1:10809\"";
			}
			command += L",format=";
			command += diskFormat;
			std::wstring driveCache = SelectedComboValue(driveCacheBox);
			if (!driveCache.empty())
			{
				command += L",cache=";
				command += driveCache;
			}
			std::wstring driveAio = SelectedComboValue(driveAioBox);
			if (!driveAio.empty())
			{
				command += L",aio=";
				command += driveAio;
			}
			std::wstring driveDiscard = SelectedComboValue(driveDiscardBox);
			if (!driveDiscard.empty())
			{
				command += L",discard=";
				command += driveDiscard;
			}
			std::wstring driveSnapshot = SelectedComboValue(driveSnapshotBox);
			if (!driveSnapshot.empty())
			{
				command += L",snapshot=";
				command += driveSnapshot;
			}
			std::wstring driveReadonly = SelectedComboValue(driveReadonlyBox);
			if (!driveReadonly.empty())
			{
				command += L",readonly=";
				command += driveReadonly;
			}
			if (useUsbStorage)
			{
				command += L",if=none,id=drive0,media=disk -device usb-storage,drive=drive0";
			}
			else if (useVirtio)
			{
				command += L",if=none,id=drive0,media=disk -device ";
				command += VirtioDevice(driveBlockStyle);
				command += L",drive=drive0";
			}
			else
			{
				command += L",if=";
				command += BlockInterface(driveBlockStyle);
				command += L",media=disk";
			}
			hasBootDevice = true;
		}

		if (cdromFile != nullptr)
		{
			std::wstring mediaUrl;
			std::wstring cdromInterface = SelectedComboValue(cdromInterfaceBox);
			TargetBlockStyle cdromBlockStyle = targetProfile.blockStyle;
			bool useUsbCdrom = false;
			if (_wcsicmp(cdromInterface.c_str(), L"ide") == 0)
			{
				cdromBlockStyle = TargetBlockStyle::Ide;
			}
			else if (_wcsicmp(cdromInterface.c_str(), L"scsi") == 0)
			{
				cdromBlockStyle = TargetBlockStyle::Scsi;
			}
			else if (_wcsicmp(cdromInterface.c_str(), L"virtio") == 0)
			{
				cdromBlockStyle = targetProfile.blockStyle == TargetBlockStyle::VirtioMmio ||
					targetProfile.blockStyle == TargetBlockStyle::VirtioPci ||
					targetProfile.blockStyle == TargetBlockStyle::VirtioCcw
					? targetProfile.blockStyle
					: TargetBlockStyle::VirtioPci;
			}
			else if (_wcsicmp(cdromInterface.c_str(), L"usb") == 0)
			{
				useUsbCdrom = true;
			}
			bool useVirtio = cdromBlockStyle == TargetBlockStyle::VirtioMmio ||
				cdromBlockStyle == TargetBlockStyle::VirtioPci ||
				cdromBlockStyle == TargetBlockStyle::VirtioCcw;
			if (canStartCdromServer && EnsureMediaNbdServer(cdromFile, true, true, mediaUrl))
			{
				command += L" -drive file=";
				command += QuoteForCommandLine(ref new String(mediaUrl.c_str()))->Data();
			}
			else
			{
				command += L" -drive file=\"nbd://127.0.0.1:10810\"";
			}
			std::wstring cdromFormat = SelectedComboValue(cdromFormatBox);
			command += L",format=";
			command += cdromFormat.empty() ? L"raw" : cdromFormat;
			std::wstring cdromCache = SelectedComboValue(cdromCacheBox);
			if (!cdromCache.empty())
			{
				command += L",cache=";
				command += cdromCache;
			}
			std::wstring cdromAio = SelectedComboValue(cdromAioBox);
			if (!cdromAio.empty())
			{
				command += L",aio=";
				command += cdromAio;
			}
			if (useUsbCdrom)
			{
				command += L",if=none,id=cdrom0,media=cdrom,readonly=on -device usb-storage,drive=cdrom0";
			}
			else if (useVirtio)
			{
				command += L",if=none,id=cdrom0,media=cdrom,readonly=on -device ";
				command += VirtioDevice(cdromBlockStyle);
				command += L",drive=cdrom0";
			}
			else
			{
				command += L",if=";
				command += BlockInterface(cdromBlockStyle);
				command += L",media=cdrom,readonly=on";
			}
			hasBootDevice = true;
		}

		if (hasBootDevice)
		{
			int bootDevice = bootDeviceBox != nullptr ? bootDeviceBox->SelectedIndex : 0;
			if (bootDevice == 1)
			{
				appendBootOption(L"c");
			}
			else if (bootDevice == 2)
			{
				appendBootOption(L"d");
			}
			else
			{
				appendBootOption(cdromFile != nullptr ? L"d" : L"c");
			}
		}
		else
		{
			appendBootOption(L"c");
		}
	}

	std::wstring audioDevice = SelectedComboValue(audioDeviceBox);
	if (!audioDevice.empty())
	{
		if (_wcsicmp(audioDevice.c_str(), L"intel-hda") == 0)
		{
			command += L" -device intel-hda -device hda-duplex";
		}
		else
		{
			command += L" -device ";
			command += audioDevice;
		}
	}

	std::wstring usbMode = SelectedComboValue(usbModeBox);
	if (_wcsicmp(usbMode.c_str(), L"on") == 0)
	{
		command += L" -usb";
	}
	else if (_wcsicmp(usbMode.c_str(), L"off") == 0)
	{
		command += L" -machine usb=off";
	}
	else if (_wcsicmp(usbMode.c_str(), L"ehci") == 0)
	{
		command += L" -device usb-ehci";
	}
	else if (_wcsicmp(usbMode.c_str(), L"xhci") == 0)
	{
		command += L" -device qemu-xhci";
	}

	std::wstring inputDevices = SelectedComboValue(inputDeviceBox);
	if (!inputDevices.empty())
	{
		if (usbMode.empty())
		{
			command += L" -usb";
		}
		size_t start = 0;
		while (start < inputDevices.length())
		{
			size_t comma = inputDevices.find(L',', start);
			std::wstring device = inputDevices.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
			if (!device.empty())
			{
				command += L" -device ";
				command += device;
			}
			if (comma == std::wstring::npos)
			{
				break;
			}
			start = comma + 1;
		}
	}

	std::wstring rtcBase = SelectedComboValue(rtcBaseBox);
	std::wstring rtcClock = SelectedComboValue(rtcClockBox);
	if (!rtcBase.empty() || !rtcClock.empty())
	{
		command += L" -rtc";
		bool firstRtcOption = true;
		if (!rtcBase.empty())
		{
			command += L" base=";
			command += rtcBase;
			firstRtcOption = false;
		}
		if (!rtcClock.empty())
		{
			command += firstRtcOption ? L" clock=" : L",clock=";
			command += rtcClock;
		}
	}

	std::wstring serial = SelectedComboValue(serialBox);
	if (!serial.empty())
	{
		command += L" -serial ";
		command += serial;
	}

	std::wstring parallel = SelectedComboValue(parallelBox);
	if (!parallel.empty())
	{
		command += L" -parallel ";
		command += parallel;
	}

	std::wstring debugFlags = SelectedComboValue(debugFlagsBox);
	if (!debugFlags.empty())
	{
		command += L" -d ";
		command += debugFlags;
	}

	std::wstring debugBehavior = SelectedComboValue(debugBehaviorBox);
	if (!debugBehavior.empty())
	{
		command += L" ";
		command += debugBehavior;
	}

	auto appendTextOption = [this, &command](const wchar_t* option, TextBox^ box, bool quote)
	{
		if (box == nullptr || box->Text == nullptr || box->Text->Length() == 0)
		{
			return;
		}
		std::wstring value(box->Text->Data());
		while (!value.empty() && iswspace(value.front()))
		{
			value.erase(value.begin());
		}
		while (!value.empty() && iswspace(value.back()))
		{
			value.pop_back();
		}
		if (value.empty())
		{
			return;
		}
		command += L" ";
		command += option;
		command += L" ";
		if (quote)
		{
			command += QuoteForCommandLine(ref new String(value.c_str()))->Data();
		}
		else
		{
			command += value;
		}
	};

	appendTextOption(L"-bios", biosPathBox, true);
	appendTextOption(L"-kernel", kernelPathBox, true);
	appendTextOption(L"-initrd", initrdPathBox, true);
	appendTextOption(L"-dtb", dtbPathBox, true);
	appendTextOption(L"-append", kernelAppendBox, true);

	return ref new String(command.c_str());
}

String^ DirectXPage::FormatCommandLineVertical(String^ value)
{
	if (value == nullptr || value->Length() == 0)
	{
		return "";
	}

	std::wstring input(value->Data());
	std::vector<std::wstring> tokens;
	std::wstring current;
	bool inQuotes = false;
	for (size_t i = 0; i < input.length(); i++)
	{
		wchar_t ch = input[i];
		if (ch == L'"')
		{
			inQuotes = !inQuotes;
			current += ch;
		}
		else if (!inQuotes && iswspace(ch))
		{
			if (!current.empty())
			{
				tokens.push_back(current);
				current.clear();
			}
		}
		else
		{
			current += ch;
		}
	}
	if (!current.empty())
	{
		tokens.push_back(current);
	}

	std::wstring output;
	for (size_t i = 0; i < tokens.size(); i++)
	{
		if (i == 0)
		{
			output += tokens[i];
		}
		else if (!tokens[i].empty() && tokens[i][0] == L'-')
		{
			output += L"\r\n  ";
			output += tokens[i];
		}
		else
		{
			output += L" ";
			output += tokens[i];
		}
	}
	return ref new String(output.c_str());
}

bool DirectXPage::IsCommandLineFile(StorageFile^ file)
{
	if (file == nullptr)
	{
		return false;
	}

	std::wstring name(file->Name->Data());
	return name.length() >= 14 && _wcsicmp(name.c_str() + name.length() - 14, L".qemu_cmd_line") == 0;
}

String^ DirectXPage::QuoteForCommandLine(String^ value)
{
	std::wstring quoted = L"\"";
	if (value != nullptr)
	{
		for (const wchar_t* ch = value->Data(); *ch != L'\0'; ch++)
		{
			if (*ch == L'"')
			{
				quoted += L"\\\"";
			}
			else if (*ch == L'\\')
			{
				quoted += L"/";
			}
			else
			{
				quoted += *ch;
			}
		}
	}
	quoted += L"\"";
	return ref new String(quoted.c_str());
}

void DirectXPage::StartWithCommandFile(StorageFile^ commandFile)
{
	if (commandFile == nullptr)
	{
		AppendError(L"Could not create current.qemu_cmd_line.");
		SetStartState(false, false);
		return;
	}

	SetStatus(L"Loading qemu_libretro.dll and initializing the core...");
	auto dispatcher = Dispatcher;
	m_main->SetProgressCallback(std::function<void(const std::wstring&)>());

	Concurrency::create_task([this, commandFile, dispatcher]()
	{
		bool loaded = false;
		std::wstring message;
		try
		{
			loaded = m_main->LoadGame(commandFile, &message);
		}
		catch (Exception^ ex)
		{
			message = L"UWP exception while loading the core: ";
			if (ex != nullptr && ex->Message != nullptr)
			{
				message += ex->Message->Data();
			}
		}
		catch (const std::exception&)
		{
			message = L"C++ exception while loading the core.";
		}
		catch (...)
		{
			message = L"Unknown exception while loading the core.";
		}

		if (loaded)
		{
			dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this]()
			{
				AppendError(L"Libretro core loaded. Starting execution.");
				SetStatus(m_main->StatusText());
				topPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
				showTabsButton->Label = "Show tabs";
				bottomAppBar->IsOpen = false;
				SetStartState(false, true);
			}));

			m_main->RunLoadedGame();
			return;
		}

		auto error = std::make_shared<std::wstring>(message.empty()
			? L"Failed to load the libretro core. The core returned an error without details."
			: message);
		dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, error]()
		{
			AppendError(*error);
			SetStatus(*error);
			tabPanel->SelectedIndex = 1;
			SetStartState(false, false);
		}));
	});
}
unsigned DirectXPage::MapVirtualKeyToRetro(VirtualKey key)
{
	unsigned keyValue = static_cast<unsigned>(key);
	if (key >= VirtualKey::Number0 && key <= VirtualKey::Number9)
	{
		return static_cast<unsigned>('0') + (keyValue - static_cast<unsigned>(VirtualKey::Number0));
	}
	if (key >= VirtualKey::A && key <= VirtualKey::Z)
	{
		return static_cast<unsigned>('a') + (keyValue - static_cast<unsigned>(VirtualKey::A));
	}
	if (key >= VirtualKey::NumberPad0 && key <= VirtualKey::NumberPad9)
	{
		return RETROK_KP0 + (keyValue - static_cast<unsigned>(VirtualKey::NumberPad0));
	}
	if (key >= VirtualKey::F1 && key <= VirtualKey::F15)
	{
		return RETROK_F1 + (keyValue - static_cast<unsigned>(VirtualKey::F1));
	}

	switch (key)
	{
	case VirtualKey::Back:
		return RETROK_BACKSPACE;
	case VirtualKey::Tab:
		return RETROK_TAB;
	case VirtualKey::Enter:
		return RETROK_RETURN;
	case VirtualKey::Escape:
		return RETROK_ESCAPE;
	case VirtualKey::Space:
		return RETROK_SPACE;
	case VirtualKey::Pause:
		return RETROK_PAUSE;
	case VirtualKey::CapitalLock:
		return RETROK_CAPSLOCK;
	case VirtualKey::Scroll:
		return RETROK_SCROLLOCK;
	case VirtualKey::NumberKeyLock:
		return RETROK_NUMLOCK;
	case VirtualKey::Snapshot:
		return RETROK_PRINT;
	case VirtualKey::Delete:
		return RETROK_DELETE;
	case VirtualKey::Left:
		return RETROK_LEFT;
	case VirtualKey::Right:
		return RETROK_RIGHT;
	case VirtualKey::Up:
		return RETROK_UP;
	case VirtualKey::Down:
		return RETROK_DOWN;
	case VirtualKey::Home:
		return RETROK_HOME;
	case VirtualKey::End:
		return RETROK_END;
	case VirtualKey::PageUp:
		return RETROK_PAGEUP;
	case VirtualKey::PageDown:
		return RETROK_PAGEDOWN;
	case VirtualKey::Insert:
		return RETROK_INSERT;
	case VirtualKey::Shift:
	case VirtualKey::LeftShift:
		return RETROK_LSHIFT;
	case VirtualKey::RightShift:
		return RETROK_RSHIFT;
	case VirtualKey::Control:
	case VirtualKey::LeftControl:
		return RETROK_LCTRL;
	case VirtualKey::RightControl:
		return RETROK_RCTRL;
	case VirtualKey::Menu:
	case VirtualKey::LeftMenu:
		return RETROK_LALT;
	case VirtualKey::RightMenu:
		return RETROK_RALT;
	case VirtualKey::Multiply:
		return RETROK_KP_MULTIPLY;
	case VirtualKey::Add:
		return RETROK_KP_PLUS;
	case VirtualKey::Separator:
		return RETROK_COMMA;
	case VirtualKey::Subtract:
		return RETROK_KP_MINUS;
	case VirtualKey::Decimal:
		return RETROK_KP_PERIOD;
	case VirtualKey::Divide:
		return RETROK_KP_DIVIDE;
	default:
		break;
	}

	switch (keyValue)
	{
	case 0xBA:
		return RETROK_SEMICOLON;
	case 0xBB:
		return RETROK_EQUALS;
	case 0xBC:
		return RETROK_COMMA;
	case 0xBD:
		return RETROK_MINUS;
	case 0xBE:
		return RETROK_PERIOD;
	case 0xBF:
		return RETROK_SLASH;
	case 0xC0:
		return RETROK_BACKQUOTE;
	case 0xDB:
		return RETROK_LEFTBRACKET;
	case 0xDC:
		return RETROK_BACKSLASH;
	case 0xDD:
		return RETROK_RIGHTBRACKET;
	case 0xDE:
		return RETROK_QUOTE;
	case 0xE2:
		return RETROK_LESS;
	default:
		return 0;
	}
}










