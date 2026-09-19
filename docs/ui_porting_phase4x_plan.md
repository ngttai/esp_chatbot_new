# Phase 4.x — Mở rộng capability cho PC simulator

## Mục tiêu

Mở rộng simulator sau visual parity mà không thay đổi UI đã port. Phase 4.x chỉ
thêm host adapter và service/backend integration; không sửa layout, asset, font,
gesture, navigation hoặc 74 file UI được khóa hash.

Camera, video và BLE không thuộc phạm vi kế hoạch này.

## Nguyên tắc

- Build mặc định phải deterministic và dùng backend `stub`.
- Backend có thể tác động máy tính thật phải được bật rõ ràng qua CMake.
- CI không được truy cập Wi-Fi, microphone hoặc audio device thật.
- Dữ liệu simulator phải nằm trong Brookesia KV/FS sandbox.
- Mỗi checkpoint phải chạy lại self-test, visual parity và source hash.
- Không chuyển logic service vào `speaker_ui.c`.

## P4.1 — Capability framework

- Cấu hình backend media: `stub`, `auto`, `ffmpeg_portaudio`.
- Cấu hình backend Wi-Fi: `stub`, `auto`, `networkmanager`.
- Cấu hình backend power: `stub`, `auto`, `upower`.
- Display luôn dùng SDL2.
- Video và BLE luôn bị khóa ở `stub`.
- Có lệnh `--print-capabilities` không cần khởi động SDL.
- CMake từ chối backend name không hợp lệ.

Tiêu chí hoàn thành:

- Default build báo đúng `sdl2/linux-filesystem/stub/stub/stub`.
- Build và test cũ không thay đổi hành vi.
- 74/74 UI hash pass.

Kết quả:

- Default build: `display=sdl2`, `storage=linux-filesystem`, media/Wi-Fi/power=`stub`.
- Build với cả ba option `auto` thành công.
- Backend name không hợp lệ bị CMake từ chối.
- Capability CLI test pass.
- Regression: 13/13 CTest pass.
- Visual parity pass.
- Source integrity: 74/74 hash pass.

Trạng thái: **COMPLETE (2026-09-19)**.

## P4.2 — Volume simulation

- Quick Settings điều khiển mute/low/medium/high.
- Settings > Sound điều khiển phần trăm volume.
- Đồng bộ hai control bằng host adapter.
- Stub deterministic cho CI.
- PortAudio chỉ điều khiển stream do simulator phát, không đổi global OS volume.
- Lưu volume và mute trong Storage service.

Checkpoint: `P4.2 Volume simulation pass`.

## P4.3 — Storage và Factory Reset sandbox

- Lưu brightness, volume/mute, WLAN state và AI Profile.
- Factory Reset chỉ xóa KV/FS sandbox của simulator.
- Không xóa file project hoặc cấu hình máy tính.
- Có test persistence qua hai process riêng biệt.

Checkpoint: `P4.3 Persistence/reset pass`.

## P4.4 — Battery và power

- Backend stub có kịch bản pin/sạc deterministic.
- Backend UPower đọc pin laptop thật khi được bật.
- Desktop không có battery phải báo unavailable hoặc dùng mock rõ ràng.
- Không giả lập điều khiển charger.

Checkpoint: `P4.4 Power status pass`.

## P4.5 — Wi-Fi deterministic mock

- WLAN on/off, scan, connect, disconnect, timeout và retry.
- AP có khóa/không khóa và password đúng/sai.
- SoftAP start/stop giả lập.
- Chạy offline và không thay đổi network của host.

Checkpoint: `P4.5 Wi-Fi mock service pass`.

## P4.6 — Wi-Fi thật opt-in

- Chỉ hoạt động với `HOST_SIM_WIFI_BACKEND=networkmanager`.
- Scan, status, connect/disconnect qua NetworkManager.
- Không tự kết nối khi startup.
- Không ghi password vào source, artifact hoặc log.
- Không chạy trong CI.

Checkpoint: `P4.6 Real Wi-Fi opt-in pass`.

## P4.7 — Audio playback và microphone

- Playback/pause/resume/stop qua FFmpeg và PortAudio.
- Capture microphone qua PortAudio.
- Có stub deterministic khi không có audio device.
- Không tuyên bố tương đương ESP AFE, wake word hoặc echo cancellation trên VoCat.

Checkpoint: `P4.7 Host audio pass`.

## P4.8 — Clock, SNTP và Weather

- Mock cố định tiếp tục dùng cho screenshot regression.
- Có provider dùng system time hoặc SNTP.
- Weather HTTP thật là opt-in, key lấy từ environment.
- Có timeout, offline state và cache.
- Không thay đổi UI Clock.

Checkpoint: `P4.8 Clock/weather provider pass`.

## P4.9 — Regression gate

- Toàn bộ self-test cũ và test capability mới pass.
- 17 màn đạt visual parity.
- 74/74 UI hash pass.
- Default build không truy cập thiết bị thật.
- Không có credential trong repository hoặc log.

Checkpoint: `P4.9 Simulator capability regression pass`.

## Thứ tự thực hiện

```text
P4.1 Capability framework
  → P4.2 Volume
  → P4.3 Storage/Factory Reset
  → P4.4 Battery
  → P4.5 Wi-Fi mock
  → P4.6 Real Wi-Fi opt-in
  → P4.7 Audio/Microphone
  → P4.8 Clock/SNTP/Weather
  → P4.9 Regression gate
```
