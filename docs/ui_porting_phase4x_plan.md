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

Kết quả:

- Quick Settings điều khiển đúng Mute/30%/60%/90% như firmware.
- Settings > Sound điều khiển 0–100%; 0% đồng thời bật mute.
- Hai control đồng bộ với trạng thái Audio Playback service.
- Backend mặc định vẫn là `stub`; không thay đổi global OS volume.
- Volume/mute được Audio Playback service lưu qua Storage service.
- Regression: 14/14 CTest pass, gồm self-test volume mới.
- Visual parity pass.
- Source integrity: 74/74 hash pass.

Trạng thái: **COMPLETE (2026-09-19)**.

## P4.3 — Storage và Factory Reset sandbox

- Lưu brightness, volume/mute, WLAN state và AI Profile.
- Factory Reset chỉ xóa KV/FS sandbox của simulator.
- Không xóa file project hoặc cấu hình máy tính.
- Có test persistence qua hai process riêng biệt.

Checkpoint: `P4.3 Persistence/reset pass`.

Kết quả:

- Brightness được lưu bởi Display service; volume/mute được lưu bởi Audio
  Playback service.
- WLAN on/off và AI Profile được lưu trong namespace host-only `HostSimulator`.
- Factory Reset chạy từ nút UI gốc, reset ba nhóm KV và chỉ dọn bốn virtual
  mount trong sandbox `.brookesia/fs`.
- Host adapter bật lại backlight sau reset để cửa sổ SDL không bị đen theo mặc
  định backlight-off của Display service trên embedded.
- Project, cấu hình hệ điều hành và dữ liệu ngoài sandbox không bị đụng tới.
- Chuỗi bốn test qua bốn process riêng biệt xác nhận write/read/reset/defaults.
- Regression: 18/18 CTest pass.
- Visual parity pass.
- Source integrity: 74/74 hash pass.

Trạng thái: **COMPLETE (2026-09-19)**.

## P4.4 — Battery và power

- Backend stub có kịch bản pin/sạc deterministic.
- Backend UPower đọc pin laptop thật khi được bật.
- Desktop không có battery phải báo unavailable hoặc dùng mock rõ ràng.
- Không giả lập điều khiển charger.

Checkpoint: `P4.4 Power status pass`.

Kết quả:

- Host adapter đọc `PowerBattery` từ Brookesia HAL Linux và cập nhật phần trăm,
  trạng thái biểu tượng sạc trên Quick Settings gốc.
- Stub mặc định cố định ở 67%, nguồn ngoài và đang sạc để CI deterministic.
- Backend `upower` vẫn là opt-in; khi thiếu dependency hoặc không có pin,
  Brookesia báo fallback rõ ràng sang mock.
- Trạng thái không có phần trăm hiển thị `--%`.
- Adapter chỉ đọc trạng thái, không gọi API điều khiển charger.
- Có self-test `--self-test-power-simulation` cho HAL state và UI binding.
- Regression: 19/19 CTest pass.
- Visual parity pass.
- Source integrity: 74/74 hash pass.

Trạng thái: **COMPLETE (2026-09-19)**.

## P4.5 — Wi-Fi deterministic mock

- WLAN on/off, scan, connect, disconnect, timeout và retry.
- AP có khóa/không khóa và password đúng/sai.
- SoftAP start/stop giả lập.
- Chạy offline và không thay đổi network của host.

Checkpoint: `P4.5 Wi-Fi mock service pass`.

Kết quả:

- Host adapter nối WLAN switch, Quick Settings Wi-Fi, password flow và màn
  SoftAP gốc với Brookesia `WifiLinux`.
- Stub scan deterministic trả về `ESP-Lab`, `NTT_Office` và `Guest`, gồm AP có
  khóa và không khóa.
- Có kịch bản password sai/đúng, connect/disconnect, timeout lần đầu và retry
  thành công.
- Mở/rời màn SoftAP QR gốc sẽ start/stop SoftAP mock.
- Backend `stub` chạy offline, không đọc hoặc thay đổi network của host.
- `auto`/`networkmanager` thiếu backend thật sẽ dùng đúng policy stub đã resolve,
  không chỉ dựa vào tên option CMake.
- Có self-test `--self-test-wifi-mock` kiểm tra cả API và UI binding.
- Regression: **20/20 CTest pass**.
- Visual parity: **pass** cho 16 màn tĩnh và 4 vùng Clock không chứa dữ liệu
  thời gian động.
- Source integrity: **74/74 hash pass**, không sửa UI gốc đã import.

Trạng thái: **COMPLETE (2026-09-21)**.

## P4.6 — Wi-Fi thật opt-in

- Chỉ hoạt động với `HOST_SIM_WIFI_BACKEND=networkmanager`.
- Scan, status, connect/disconnect qua NetworkManager.
- Không tự kết nối khi startup.
- Không ghi password vào source, artifact hoặc log.
- Không chạy trong CI.

Checkpoint: `P4.6 Real Wi-Fi opt-in pass`.

Tiến độ:

- NetworkManager chỉ được phép resolve khi chọn chính xác
  `HOST_SIM_WIFI_BACKEND=networkmanager`; Wi-Fi `auto` vẫn dùng stub an toàn.
- Capability output phân biệt backend được yêu cầu và backend thực tế đã
  resolve.
- Scan thật được ánh xạ runtime vào ba row WLAN có sẵn; không sửa source UI.
- Status được đọc qua Brookesia Connectivity; locked AP dùng password screen
  gốc, open AP kết nối trực tiếp.
- Startup không tự connect và cleanup không tự disconnect Wi-Fi máy tính.
- Wi-Fi mock test không được đăng ký khi backend thật đã resolve.
- Stub regression: **20/20 CTest pass**; riêng Wi-Fi mock pass **10 lần liên
  tiếp**, gồm cả open AP đi qua row UI gốc.
- Visual parity: **pass**; source integrity: **74/74 hash pass**.
- Build `auto` đã xác nhận `wifi resolved: stub`.
- Build opt-in với `network-manager`, `nmcli` và `libnm-dev` đã xác nhận
  `wifi resolved: networkmanager`; CTest của build này chỉ đăng ký **19 test**,
  không đăng ký Wi-Fi mock.
- Self-test thật chỉ đọc đã scan **20 AP**, đọc trạng thái connected và xác nhận
  ba kết quả đầu được bind vào row WLAN gốc.
- Snapshot NetworkManager trước/sau self-test giống hệt nhau: không tự connect,
  disconnect hoặc bật SoftAP. Connect/disconnect thật chỉ chạy khi người dùng
  thao tác rõ ràng trong simulator.

Trạng thái: **COMPLETE (2026-09-21)**.

## P4.7 — Audio playback và microphone

- Playback/pause/resume/stop qua FFmpeg và PortAudio.
- Capture microphone qua PortAudio.
- Có stub deterministic khi không có audio device.
- Không tuyên bố tương đương ESP AFE, wake word hoặc echo cancellation trên VoCat.

Checkpoint: `P4.7 Host audio pass`.

Tiến độ:

- Capability output phân biệt media backend được yêu cầu và backend thực tế đã
  resolve (`stub` hoặc `ffmpeg_portaudio`).
- Playback test đi qua đúng Brookesia `AudioPlayback` service với chuỗi
  play/pause/resume/stop.
- Microphone test đọc đúng `AudioLinux:Recorder`; stub mặc định trả mẫu `0x5A`
  deterministic và không chạm speaker/microphone của máy tính.
- Self-test mặc định `--self-test-audio-stub` đã được thêm vào CTest.
- Self-test thật `--self-test-audio-real` tạo tone WAV tạm, phát qua
  FFmpeg/PortAudio và capture khoảng 200 ms từ microphone mặc định; không lưu
  sample thu âm và không chạy trong CI.
- Khi dependency thật chưa có, `auto` resolve về `stub` và test thật từ chối
  chạy rõ ràng, không giả báo pass.
- Regression stub: **21/21 CTest pass**.
- Build opt-in đã resolve đúng `media resolved: ffmpeg_portaudio`; CTest build
  thật không đăng ký audio stub.
- Self-test thật đã phát tone qua FFmpeg/PortAudio và capture thành công
  **12.800 byte**, **16 kHz**, **2 channel** từ microphone mặc định.
- Không thêm AFE, wake word hoặc echo cancellation giả lập.

Trạng thái: **COMPLETE (2026-09-21)**.

## P4.8 — Clock, SNTP và Weather

- Mock cố định tiếp tục dùng cho screenshot regression.
- Có provider dùng system time hoặc SNTP.
- Weather HTTP thật là opt-in, key lấy từ environment.
- Có timeout, offline state và cache.
- Không thay đổi UI Clock.

Checkpoint: `P4.8 Clock/weather provider pass`.

Kết quả:

- Clock UI gốc tiếp tục gọi `localtime()`; backend mặc định dùng system time và
  không truy cập mạng.
- Backend SNTP opt-in dùng `NetworkLinux:SntpClient`, có timeout và giữ system
  time nếu server không phản hồi; test thật đã sync thành công với
  `pool.ntp.org`.
- Host adapter cung cấp đúng ABI `weather_client_get()` cho target simulator;
  file `weather_source.c` import vẫn giữ nguyên hash và không bị sửa.
- Weather mặc định dùng fixed mock, offline và giữ nguyên screenshot regression.
- OpenWeather opt-in dùng Brookesia HTTPS client, key chỉ đọc từ environment,
  vị trí lấy từ latitude/longitude environment, timeout 5 giây và refresh 10
  phút.
- Có explicit offline state; sau lần fetch thành công, last-success cache trong
  memory tiếp tục cấp dữ liệu khi request sau thất bại.
- Parser deterministic map nhiệt độ, condition, icon, sunrise/sunset, gió và 4
  ngày forecast; self-test kiểm tra cả binding mock vào Clock UI gốc.
- API key không được ghi vào source, artifact, log hoặc commit.
- OpenWeather thật đã fetch và bind thành công dữ liệu `Thong Tay Hoi`, `26°C`,
  `Moderate rain`.
- Regression mặc định: **22/22 CTest pass**.

Trạng thái: **COMPLETE (2026-09-21)**.

## P4.9 — Regression gate

- Toàn bộ self-test cũ và test capability mới pass.
- 17 màn đạt visual parity.
- 74/74 UI hash pass.
- Default build không truy cập thiết bị thật.
- Không có credential trong repository hoặc log.

Checkpoint: `P4.9 Simulator capability regression pass`.

Kết quả:

- Thêm một regression gate duy nhất để build cấu hình mặc định, chạy toàn bộ
  CTest, visual parity, source integrity và credential scan.
- Gate xác nhận backend mặc định là media/Wi-Fi/power stub, system time và
  weather mock; không tự động truy cập thiết bị hoặc dịch vụ mạng thật.
- Toàn bộ self-test mặc định pass.
- 17 màn đạt visual parity: 16 màn tĩnh và 4 vùng ổn định của Clock pass.
- Source integrity đạt **74/74 hash pass** và không có file sửa/thêm trong thư
  mục UI import.
- Không có OpenWeather-style credential, `.env` hoặc log được Git track.

Trạng thái: **COMPLETE (2026-09-21)**.

## Post-P4.9 — WLAN keyboard refinement

- Giữ nguyên toàn bộ source UI import; thay đổi được áp dụng bằng host adapter.
- Number keyboard bỏ nút ẩn; hàng `1 2 3 Del` có padding hai bên cân nhau.
- Special keyboard bỏ nút ẩn và dùng đúng thứ tự hàng chức năng
  `123`, `,`, `Space`, `.`, `Del`.
- Lowercase/uppercase giữ nguyên nút ẩn hiện tại.
- WLAN keyboard self-test kiểm tra trực tiếp cả bốn layout.
- Regression gate: **22/22 CTest pass**, visual parity pass và **74/74 UI hash
  pass**.

Trạng thái: **COMPLETE (2026-09-22)**.

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
