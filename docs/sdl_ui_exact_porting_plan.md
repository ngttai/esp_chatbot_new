# Kế hoạch port chính xác SDL UI vào ESP Chatbot v1

## Mục tiêu

Port nguyên trạng UI từ `/home/nttai/ntt_ws/esp_project/sdl_ui_simulator` vào project `esp_chatbot_v1`, bao gồm:

- Launcher.
- Idle/Home.
- Quick Settings.
- Settings và toàn bộ trang con.
- WLAN keyboard và SoftAP QR.
- AI Profile.
- Clock/Weather.
- Gesture và navigation.
- Public self-test API.
- 11 self-test hiện có.

UI phải chạy bằng cùng một source trên ESP VoCat v1.0 và PC simulator.

## Nguyên tắc

- `/home/nttai/ntt_ws/esp_project/sdl_ui_simulator` là bản chuẩn đối chiếu.
- Không chỉnh layout, màu, font, animation, gesture hoặc navigation.
- Giữ nguyên `speaker_ui.c`, `speaker_ui.h` và public self-test API tối đa có thể.
- Mọi khác biệt bắt buộc do Brookesia hoặc ESP-IDF phải nằm trong wrapper hay adapter bên ngoài.
- Không kết nối service thật trong giai đoạn parity.
- Không xóa SquareLine UI hiện tại cho đến khi bản port vượt qua toàn bộ kiểm thử.
- Nếu bắt buộc sửa file UI lõi vì compiler hoặc khác biệt LVGL, phải ghi rõ file, dòng và lý do trước khi thay đổi.

## Phase 0 — Đóng băng baseline

1. Ghi nhận commit hiện tại của `sdl_ui_simulator`.
2. Lập manifest toàn bộ source và asset sẽ port.
3. Chạy và lưu kết quả 11 self-test hiện có.
4. Tạo golden screenshot cho:
   - Idle.
   - Launcher và launcher pressed.
   - Quick Settings.
   - Settings root/bottom.
   - WLAN root/bottom.
   - WLAN password.
   - SoftAP.
   - Sound.
   - Display.
   - About.
   - Developer.
   - Restore.
   - AI Profile.
   - Clock.
5. Ghi hash của các file UI nguồn để phát hiện thay đổi ngoài ý muốn.

Tiêu chí hoàn thành: simulator nguồn build được, 11/11 test pass và có đủ golden screenshot.

## Phase 1 — Vendor nguyên source vào project

Vị trí dự kiến:

```text
main/modules/display/speaker_ui/
├── src/
│   ├── speaker_ui.c
│   ├── speaker_ui.h
│   ├── esp_brookesia.h
│   ├── esp_mmap_assets.h
│   └── flip_clock/
├── vendor/
│   └── esp_speaker_ui/
└── PORTING_MANIFEST.md
```

Các bước:

1. Copy nguyên `speaker_ui.c/h`.
2. Copy nguyên `flip_clock/`.
3. Copy các generated UI/component.
4. Copy toàn bộ font và icon đang được CMake nguồn sử dụng.
5. Copy stub header cần thiết.
6. Giữ cấu trúc thư mục gần giống nguồn để giảm thay đổi include.
7. Lưu mapping `source path → destination path`.
8. Kiểm tra nội dung file port bằng hash và diff.

Tiêu chí hoàn thành: các file lõi giống bản nguồn; mọi khác biệt phải được ghi rõ.

## Phase 2 — Build UI trong host simulator mới

Thay UI SquareLine Settings hiện được load trong `host_sim/main.cpp` bằng UI shell đầy đủ.

Entrypoint dự kiến:

```cpp
speaker_ui_create();
speaker_ui_set_input(
    esp_brookesia::gui::lvgl::DisplaySource::get_instance().input()
);
```

Cần cập nhật:

- `host_sim/CMakeLists.txt` để compile đúng source và asset.
- `host_sim/lv_conf.h` để bật QR, keyboard, fonts và tính năng LVGL cần thiết.
- Giữ `LV_USE_SDL=0`; SDL vẫn do Brookesia HAL Linux sở hữu.
- Không sửa behavior UI để phù hợp với host.

Tiêu chí hoàn thành:

- Build thành công qua Brookesia HAL Linux.
- Interactive behavior giống simulator nguồn.
- Launcher, Quick Settings, Settings, AI Profile và Clock đều mở được.

## Phase 3 — Port self-test runner

Giữ nguyên API trong `speaker_ui.h`.

Tạo runner host-only:

```text
host_sim/tests/
├── self_test_main.cpp
├── input_injector.cpp
└── input_injector.hpp
```

Runner phải giữ nguyên:

- Tên 11 tùy chọn `--self-test-*`.
- Trình tự thao tác.
- Tọa độ.
- Threshold.
- Assertions.
- Exit code và kết quả pass/fail.

Điểm duy nhất thay đổi là cơ chế phát input:

```text
SDL_PushEvent cũ
       ↓
Brookesia Display::inject_touch mới
```

Đường đi kiểm thử:

```text
Test → Display service → HAL Linux → LVGL input → speaker_ui
```

Tiêu chí hoàn thành: 11/11 self-test pass trong target mới.

## Phase 4 — Visual parity

So sánh từng màn với golden screenshot từ Phase 0:

- Vị trí và kích thước widget.
- Font và glyph.
- Màu sắc.
- Circular clipping.
- Icon scale.
- Scroll position.
- Home indicator.
- Quick Settings overlay.
- Keyboard.
- Flip Clock.
- Weather panel.
- Animation start/end state.

Chấp nhận khác biệt chỉ ở mức renderer hoặc anti-aliasing giữa hai SDL path. Không chấp nhận khác biệt về geometry hoặc behavior.

Tiêu chí hoàn thành: tất cả màn đạt visual parity hoặc có báo cáo pixel-diff giải thích rõ.

## Phase 5 — Build nguyên UI trong ESP-IDF

Trạng thái build checkpoint: **COMPLETE (2026-09-22)**. Source UI đã compile nguyên
vẹn vào `main` component; xem `docs/ui_porting_phase5.md`. Việc nối lifecycle vào
Display và xác nhận LCD 360×360 vẫn thuộc Phase 6 và Phase 7.

1. Đưa cùng source UI vào `main` component.
2. Bật các cấu hình LVGL cần thiết trong `sdkconfig.defaults`.
3. Không thêm mock SDL vào firmware.
4. Lấy `lv_indev_t` từ Brookesia GUI LVGL.
5. Gọi cùng API khởi tạo:

   ```c
   speaker_ui_create();
   speaker_ui_set_input(input);
   ```

6. Giữ singleton lifecycle giống simulator nguồn.
7. Không tối ưu asset/font ở giai đoạn này.

Tiêu chí hoàn thành:

- ESP-IDF build thành công cho `esp_vocat_board_v1_0`.
- Không có symbol collision.
- Không có dependency SDL trong firmware.
- Shell hiển thị đúng 360×360 trên VoCat.

## Phase 6 — Tích hợp shell vào Display

Trạng thái tích hợp build/link: **COMPLETE (2026-09-22)**. Wrapper firmware đã chọn
Speaker UI làm shell chính, lấy input từ Brookesia LVGL và giữ nguyên navigation nội
bộ; xem `docs/ui_porting_phase6.md`. Xác nhận LCD/touch thật thuộc Phase 7.

Tạo wrapper tối thiểu, ví dụ:

```text
screens/speaker_shell.cpp
screens/speaker_shell.hpp
```

Wrapper chỉ làm:

- Khởi tạo UI shell.
- Chọn LVGL display source.
- Cung cấp input.
- Quản lý lock LVGL.
- Không sửa navigation nội bộ của `speaker_ui`.

Trong giai đoạn parity:

- `speaker_ui_create()` vẫn load đúng màn `idle` như source.
- Launcher vẫn mở bằng long press như source.
- Gesture Home và Quick Settings giữ nguyên.
- Chưa ánh xạ idle sang native Emote.
- Chưa thay AI Profile bằng Emote service.

State machine cũ được giữ lại trong source cho đến khi shell chạy ổn định. Việc chọn shell làm UI chính phải là thay đổi nhỏ, tách biệt và có thể hoàn tác.

## Phase 7 — Kiểm thử trên VoCat

Kiểm tra thủ công:

1. Boot vào idle.
2. Long press mở launcher.
3. Swipe launcher qua các page.
4. Mở và đóng Quick Settings.
5. Mở Settings và từng trang con.
6. Scroll trên màn hình tròn.
7. Mở WLAN keyboard.
8. Mở SoftAP QR.
9. Mở AI Profile.
10. Mở Clock.
11. Kiểm tra flip animation.
12. Home gesture từ từng màn.
13. Chạy lâu để phát hiện timer hoặc memory leak.

Đo thêm:

- Flash tăng bao nhiêu.
- Internal RAM và PSRAM.
- LVGL heap.
- CPU khi ở idle.
- CPU khi Clock hiển thị.
- CPU khi Clock bị ẩn.
- Số timer đang hoạt động.

Việc đo không đồng nghĩa với tối ưu hoặc thay đổi behavior.

Tiêu chí hoàn thành: toàn bộ flow UI hoạt động trên VoCat và không phát hiện lỗi runtime nghiêm trọng.

## Phase 8 — Service integration sau parity

Chỉ bắt đầu khi bản port chính xác đã được xác nhận. Đây là phase riêng:

- Wi-Fi thật.
- Volume thật.
- Brightness thật.
- Memory stats thật.
- Factory reset thật.
- Battery/power thật: VoCat v1.0 dùng fuel gauge TI BQ27220 trên I2C. Tham khảo
  driver, profile CEDV 650 mAh và `BatteryMonitor` từ project `esp_speaker`, nhưng
  adapter của project này phải dùng chung bus Board Manager `i2c_master` hiện có
  (không khởi tạo lại I2C port 0). Đưa SOC và trạng thái sạc thật vào đúng widget
  Quick Settings gốc; không dùng `100%` mock làm kết quả và không đổi layout UI.
- Clock/NTP thật: SNTP service đồng bộ system time, timezone phải đúng thiết bị
  (`UTC+7` cho cấu hình Việt Nam), và Clock UI gốc tiếp tục đọc qua `localtime()`.
- Touch sensor thật: tham khảo `TouchSensor` từ project `esp_speaker`, dùng pad
  cảm ứng vật lý GPIO7 của VoCat v1.0 và giữ touch slider ở trạng thái tắt như
  implementation gốc. Nối công tắc Settings → Input → Touch với callback thật;
  single click dùng để wake hoặc interrupt agent, long press dùng để sleep. Không
  thay đổi gesture của LCD touch hoặc layout UI.
- Native Emote.
- Agent integration.

Mọi thay đổi ở phase này phải đi qua adapter và không thay đổi giao diện hoặc gesture.

## Checkpoint bắt buộc

```text
P0: Baseline pass — COMPLETE (2026-09-18)
P1: Source integrity pass — COMPLETE (2026-09-18)
P2: Host build pass — COMPLETE (2026-09-18)
P3: 11/11 self-test pass — COMPLETE (2026-09-18)
P4: Visual parity pass — COMPLETE (2026-09-18)
P5: ESP-IDF build pass — COMPLETE (2026-09-22)
P6: Shell integration pass — COMPLETE (2026-09-22)
P7: VoCat runtime pass
P8: Service integration
```

Không chuyển sang checkpoint tiếp theo nếu checkpoint hiện tại chưa đạt hoặc chưa có báo cáo khác biệt rõ ràng.
