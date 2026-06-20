# Qemu-Libretro-UWP

Qemu-Libretro-UWP is a UWP host application for running `qemu_libretro.dll`.
It provides a DirectX-based libretro video host, a small boot configuration UI,
diagnostic logging, and command generation through `current.qemu_cmd_line`.

This project is experimental. The current packaged `qemu_libretro.dll` can load
and run QEMU in UWP, but its Win32 file block backend is not reliable inside the
UWP app container.

## Current Status

Working:

- Loading `qemu_libretro.dll` from the app package.
- Registering libretro callbacks for video, audio, input, environment, and logs.
- Rendering libretro video frames through DirectX.
- Running QEMU machine-only configurations.
- Running QEMU with non-file block backends such as `null-co`.
- Booting ISO images as read-only CD-ROM media through local NBD.
- Booting supported QEMU disk image formats through local NBD with write
  support when the staged copy can be opened read-write.
- Showing boot options, errors, and generated `qemu_cmd_line` in tabs.
- Collapsing the configuration tabs when the emulator starts.
- Writing diagnostic logs to the app `LocalState` folder.
- Serving selected boot media through a local NBD server so QEMU can access
  media without using its broken Win32 file backend.

Known limitation:

- QEMU commands that use a local real file-backed block device, such as
  `-cdrom`, `-drive file=C:/...`, or `-blockdev driver=file`, currently hang
  inside the first `retro_run` call. The app avoids this by exposing media as
  `nbd://127.0.0.1:<port>` and passing that URL to QEMU's NBD block driver.

## Repository Layout

- `DirectXPage.xaml` - Main UWP interface.
- `DirectXPage.xaml.cpp` - UI behavior, command generation, boot media staging,
  local NBD media server, and app log writing.
- `LibretroHost.*` - Native libretro host, callbacks, frame storage, logging,
  and run loop.
- `LibretroCore.*` - Dynamic loading wrapper for `qemu_libretro.dll`.
- `Qemu_Libretro_UWPMain.*` - DirectX render loop integration.
- `Content/LibretroFrameRenderer.*` - Uploads libretro frames to DirectX.
- `Package.appxmanifest` - UWP package manifest.

The Visual Studio project packages local runtime files from this project
directory:

- `qemu_libretro.dll`
- `qemu\**\*`

## Requirements

- Windows with UWP support.
- Visual Studio 18 Community or compatible MSBuild installation.
- Windows 10 SDK App Certification Kit for `signtool.exe`.
- A signing certificate, for example:

```text
C:\Certificates\UWP-Port_TemporaryKey.pfx
```

## Build

Example using Command Prompt:

```cmd
set "PROJECT_DIR=C:\Projects\Qemu-Libretro-UWP"
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"

"%MSBUILD%" "%PROJECT_DIR%\Qemu-Libretro-UWP.vcxproj" /p:Configuration=Debug /p:Platform=x64 /m /nr:false /v:minimal
```

The generated package is written under:

```text
Qemu-Libretro-UWP\AppPackages\Qemu-Libretro-UWP\Qemu-Libretro-UWP_1.0.0.0_x64_Debug_Test\
```

## Sign And Verify

Example using Command Prompt:

```cmd
set "PROJECT_DIR=C:\Projects\Qemu-Libretro-UWP"
set "SIGNTOOL=C:\Program Files (x86)\Windows Kits\10\App Certification Kit\signtool.exe"
set "PFX=C:\Certificates\UWP-Port_TemporaryKey.pfx"
set "MSIX=%PROJECT_DIR%\AppPackages\Qemu-Libretro-UWP\Qemu-Libretro-UWP_1.0.0.0_x64_Debug_Test\Qemu-Libretro-UWP_1.0.0.0_x64_Debug.msix"

"%SIGNTOOL%" sign /fd SHA256 /f "%PFX%" "%MSIX%"
"%SIGNTOOL%" verify /pa "%MSIX%"
```

## Usage

1. Install the signed MSIX package.
2. Open the app.
3. Use the Boot tab to select an ISO, disk image, supported QEMU disk image,
   or `.qemu_cmd_line` file.
4. Review the generated command in the `qemu_cmd_line` tab.
5. Press Start from the top command bar.

The available diagnostic profiles are:

- `Normal command` - Main boot path. The app serves selected media over a local
  NBD endpoint and passes that endpoint to QEMU as the boot disk/CD.
  ISO files are exposed read-only as CD-ROM when the CD-ROM option is enabled.
  Disk images are exposed as writable disks when possible.
- `Empty CD-ROM no file` - Verifies QEMU display and CD-ROM device behavior
  without opening a media file.
- `Null block` - Verifies QEMU block device behavior without a real file.

Generated format mapping:

- `.iso` as CD-ROM: `format=raw,media=cdrom,readonly=on`
- `.img` / `.raw`: `format=raw,media=disk`
- `.qcow`: `format=qcow,media=disk`
- `.qcow2`: `format=qcow2,media=disk`
- `.qed`: `format=qed,media=disk`
- `.vdi`: `format=vdi,media=disk`
- `.vmdk`: `format=vmdk,media=disk`
- `.vhd` / `.vpc`: `format=vpc,media=disk`
- `.vhdx`: `format=vhdx,media=disk`
- `.bochs`: `format=bochs,media=disk`
- `.cloop`: `format=cloop,media=disk`
- `.dmg`: `format=dmg,media=disk`
- `.hds` / `.parallels`: `format=parallels,media=disk`

The mapping follows the block format drivers present in `qemu-libretro-src`.
Multi-file formats such as some VMDK layouts still need their companion extent
files to be available to QEMU; single-file images are the reliable path.

## Supported QEMU Targets

The app can probe the packaged `qemu_libretro.dll` from the Errors tab. The
current binary reports these libretro architecture targets:

```text
aarch64, alpha, arm, i386, m68k, mips, mips64, mips64el, mipsel, ppc,
ppc64, riscv32, riscv64, s390x, sparc, sparc64, x86_64
```

These targets are available in the Boot tab architecture selector. Device and
machine availability still depends on the selected target, so advanced boot
commands may need target-specific `-machine`, `-cpu`, and block device options.

## Logs

Logs are written to the app `LocalState` folder:

- `qemu-uwp.log` - UI-side startup and command generation log.
- `qemu-uwp-native.log` - Native host, libretro callbacks, video frame, and
  run-loop diagnostics.
- `qemu-uwp-stderr.log` - Redirected stdout/stderr from the native core.

Typical useful lines:

```text
Start: local read-only NBD server started at nbd://127.0.0.1:10809
Start: qemu_cmd_line: ...
Host: qemu_libretro.dll loaded and exports resolved.
Host: calling first retro_run.
Host: video frame received ...
```

If a command using `file=` hangs, the log usually stops after:

```text
Host: calling first retro_run.
Host: watchdog: first retro_run has not returned after 3s.
```
