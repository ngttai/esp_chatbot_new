# Phase 5 — ESP-IDF build pass

Ngày xác nhận: 2026-09-22

## Phạm vi

Phase này chỉ đưa nguyên source UI đã đóng băng vào quá trình build ESP-IDF. Phase này
không thay đổi navigation nội bộ và chưa nối `speaker_ui_create()` vào `Display`; phần
nối lifecycle và hiển thị trên LCD thuộc Phase 6 và Phase 7.

## Lệnh build chuẩn

```sh
source /home/nttai/.espressif/v6.1/esp-idf/export.sh
idf.py bmgr -b esp_vocat_board_v1_0
idf.py build
```

## Thay đổi ở lớp build

- Khai báo các include directory của UI gốc trong `main` component.
- Thêm compatibility header để include `<lvgl/lvgl.h>` dùng được với component LVGL
  của ESP-IDF mà không sửa source UI.
- Bật font và widget LVGL mà UI gốc sử dụng.
- Khai báo trực tiếp LittleFS vì `main/CMakeLists.txt` tạo image `littlefs_data`.
- Khóa `brookesia_service_helper` ở `0.8.4` để dùng bộ dependency tương thích hiện tại.
- Tắt video processor tùy chọn vì VoCat v1.0 không có camera.

## Kết quả xác nhận

- Board: `esp_vocat_board_v1_0`.
- Target: `esp32s3`.
- Firmware build thành công: `build/example_agent_chatbot.bin`.
- Kích thước app: `0x557e20`; còn `0x5a81e0` byte (51%) trong app partition nhỏ nhất.
- 56 source file UI được compile vào `build/esp-idf/main/libmain.a`.
- `speaker_ui_create` và `speaker_ui_set_input` có trong archive của `main`.
- Không có SDL undefined symbol hoặc SDL library trong firmware link inputs.
- 74/74 checksum của source UI gốc đạt; thư mục UI gốc không có thay đổi.
- Regression simulator đạt 22/22 test, visual parity 16 màn hình và 4 vùng Clock.

## Ranh giới Phase 5

Các symbol `speaker_ui_*` chưa xuất hiện trong ELF cuối vì chưa có code firmware gọi
chúng. Đây là trạng thái mong đợi ở checkpoint build. Phase 6 sẽ tạo wrapper Display,
gọi `speaker_ui_create()`/`speaker_ui_set_input()` và khi đó linker sẽ giữ UI trong ELF.
Phase 7 mới xác nhận render/touch thật trên màn 360×360 của VoCat.

