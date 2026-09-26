# Release v1.0.0

Ngày phát hành: 2026-09-26

## Phạm vi

- Port chính xác Speaker UI sang ESP VoCat v1.0 và PC simulator.
- Launcher, Quick Settings, Clock, Settings, WLAN và keyboard.
- Wi-Fi, brightness, volume, Factory Reset, About và Developer Mode thật.
- Native Emote và XiaoZhi agent.
- BQ27220 battery, touch GPIO7, head LED và BMI270 any-motion.
- PC simulator có deterministic stub, backend thật opt-in và self-test.

## Xác nhận release

- ESP-IDF v6.1 build cho `esp_vocat_board_v1_0`.
- Hardware smoke test trên ESP VoCat v1.0.
- Simulator regression: 22/22 CTest.
- Visual parity và source-integrity gate của UI port.

## Ghi chú

BMI270 dùng emote `confused` (`@@`) có sẵn trong engine Emote mới để thay cho
asset `dizzy` định dạng AAF của firmware `esp_speaker` cũ.
