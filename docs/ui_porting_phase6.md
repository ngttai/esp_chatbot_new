# Phase 6 — Speaker shell integration

Ngày xác nhận build/link: 2026-09-22

## Phạm vi

Phase này chọn Speaker UI đã port làm UI chính của firmware. Không sửa navigation,
layout, gesture, asset hoặc bất kỳ file nào trong thư mục UI được khóa hash.

## Cách tích hợp

- `ScreenSpeakerShell` là wrapper firmware tối thiểu.
- Wrapper kiểm tra Brookesia LVGL display source đã chạy và đúng 360×360.
- Wrapper lấy primary `lv_indev_t` trực tiếp từ `gui::lvgl::DisplaySource`.
- Wrapper khóa LVGL bằng `esp_lv_adapter_lock()` rồi gọi đúng:

  ```c
  speaker_ui_create();
  speaker_ui_set_input(input);
  ```

- `Display::start()` chọn LVGL source rồi khởi động Speaker shell.
- State machine Settings/Emote cũ vẫn còn trong source nhưng không được chọn làm UI
  khởi động trong giai đoạn parity.
- UI gốc vẫn tự load màn `idle`; long press, Home và Quick Settings gesture vẫn do
  `speaker_ui` quản lý.

## Xử lý xung đột QR

LVGL và Emote graphics cùng đóng gói Nayuki `qrcodegen` bằng các symbol public giống
nhau. Khi màn SoftAP QR được link vào ELF, hai bản phát sinh multiple definition.
Build prefix bản private của LVGL thành `lvgl_qrcodegen_*`; không sửa UI và không bỏ QR.

## Kết quả xác nhận tĩnh

- Build thành công cho `esp_vocat_board_v1_0` / ESP32-S3.
- `speaker_ui_create`, `speaker_ui_set_input` và `ScreenSpeakerShell::start()` đều có
  trong `build/example_agent_chatbot.elf`.
- Không còn symbol collision.
- Không có SDL symbol hoặc SDL library trong firmware.
- Firmware size: `0x6190d0`, còn 45% app partition.
- UI shell làm tăng `0xc12b0` byte so với build Phase 5.
- 74/74 checksum UI gốc đạt và không có diff trong thư mục UI gốc.
- Simulator regression đạt 22/22 test; visual parity đạt 16 màn hình và 4 vùng Clock.

## Bước tiếp theo

Phase 7 cần VoCat v1.0 thật để flash và kiểm tra LCD/touch. Build/link không thể xác
nhận orientation, touch coordinate, long press và gesture trên controller thật.

