# RFPS Portable 0.2.0 — Windows x64 foundation

Самостоятельный проект без Qt. UI построен на Win32 API, GPU-слой — Direct3D 11,
шейдеры компилируются через D3DCompiler. Целевая система — Windows 11 x64.

## Состояние реализации (без фиктивных функций)

- Win32 GUI: реализован.
- Перечисление DXGI адаптеров: реализовано.
- Создание D3D11 hardware device на выбранном адаптере: реализовано.
- Компиляция HLSL compute shader при запуске: реализована.
- Базовый compute shader upscaler (bilinear sampling + регулируемый unsharp mask):
  реализован как GPU-операция, но пока не подключён к живому источнику кадров.
- Windows Graphics Capture: ещё не реализован.
- Optical Flow Frame Generation x2/x3/x4: ещё не реализован.
- FPS/GPU/CPU/VRAM телеметрия, профили и горячие клавиши: ещё не реализованы.
- Вывод обработанного потока в отдельное окно / swap chain: ещё не реализован.

Приложение явно показывает текущие ограничения и не подменяет Optical Flow
смешиванием кадров. Это инженерный фундамент, а не готовая замена Magpie или
Lossless Scaling.

## Получить EXE без Visual Studio и Qt на своём ПК

1. Создайте бесплатный репозиторий на https://github.com/new.
2. Распакуйте архив и загрузите в репозиторий все файлы и папки проекта
   (включая скрытую папку `.github`).
3. Откройте репозиторий на GitHub → вкладка **Actions**.
4. Выберите **Build RFPS Portable (Windows x64)** → **Run workflow**.
5. Дождитесь зелёной галочки.
6. Откройте завершившийся запуск и скачайте артефакт
   **RFPS-Portable-Windows-x64** внизу страницы.

В архиве будет `RFPS.exe`. GitHub Actions собирает его на Windows Server 2022
с установленным MSVC и Windows SDK; локально устанавливать Visual Studio или Qt
не требуется. Для скачивания артефакта нужен аккаунт GitHub.

## Альтернативная локальная сборка

Если на ПК уже есть CMake и Visual Studio Build Tools:
```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```
Результат: `build\\Release\\RFPS.exe`.

## Архитектура следующего этапа

- Capture: Windows.Graphics.Capture (WinRT) + Direct3D11CaptureFramePool.
- GPU Core: D3D11 device/context, resource pool, fences/timestamps.
- Upscaler: spatial scaler + edge-aware reconstruction and sharpening.
- Frame Generation: optical flow, forward/backward consistency, occlusion /
  disocclusion masks, confidence-weighted warping, temporal rejection.
- Scheduler: frame pacing, latency budget, presentation.
- UI: Win32 now; may be migrated to a custom renderer without Qt.

## Примечание

GitHub Actions workflow automatically builds and publishes an EXE as a downloadable
workflow artifact. The current chat environment is not a Windows MSVC build host,
so the EXE is not prebuilt or claimed as tested here.
