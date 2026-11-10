# Kiểm thử bàn giao

## Tích hợp HC-05 vào Control_Hardware — 2026-11-11

Nguồn: `main` / `origin/main` tại `7018ea02dc0347720e1f3790e02fcb609f669e77`, đã fetch trước khi sửa; workspace ban đầu sạch, không có AGENTS.md. Không dùng ZIP. Không sửa GUI/AI; CMake chỉ bổ sung target/test trên host.

- `cmake -S . -B build-vscode` và `cmake --build build-vscode --parallel 4`: thành công. CMake báo thiếu XKB tùy chọn nhưng configure/build và GUI offscreen vẫn qua.
- `ctest --test-dir build-vscode --output-on-failure`: **5/5 qua** (`gui_smoke`, `core_tests`, `arduino_adapter_tests`, `arduino_control_tests`, `arduino_adapter_copies_match`).
- Test mới biên dịch trực tiếp sketch điều khiển với stub chân I/O/UART/EEPROM, rồi đưa bản tin qua `lf::Decoder` thật của GUI: chân/chiều motor, PWM nền 70 và gain 25/0/0, chu kỳ 10 ms, bốn tổ hợp cảm biến, u sau giới hạn, telemetry thật mỗi 200 ms, CRC/seq/ACK; lệnh chia gói, PWM áp dụng ở bước kế tiếp, không bị snapshot cũ ghi đè, lệnh lặp và từ chối sai bước/range/rate/state.
- Kiểm tra UART hết chỗ gửi vẫn chạy PID/watchdog; hết heartbeat gọi dừng và reset PID ngay cả giữa hai bước PID; kết nối lại/PING/cảm biến đổi không chạy lại; EEPROM chỉ ghi khi setup và không vượt địa chỉ 0..3. So sánh hai bản adapter bằng `cmake -E compare_files`.
- Máy ban đầu không có Arduino CLI/AVR. Đã tải và giải nén gói Ubuntu vào `/tmp/linefollower-avr-check/toolchain` (không cài hệ thống), dùng **avr-g++ 7.3.0**, avr-libc 2.0.0, Arduino AVR core **1.8.6**. Biên dịch C/C++/assembly của core, `.ino` dưới dạng C++, link ELF và tạo HEX thành công cho **cả Control_Hardware và TelemetryDemo**. Target ATmega328P, 16 MHz, variant standard, `ARDUINO_AVR_UNO`, `-Os`, `-flto`, `--gc-sections`, các cờ C++11 theo platform.txt. Không dùng stub khi build AVR. Đây là compile/link bằng toolchain AVR, không phải chạy Arduino CLI hoặc upload board.

Kết quả `avr-size -A` (flash = .text + .data; SRAM tĩnh = .data + .bss):

| Sketch | Flash / 32.256 byte khả dụng | SRAM tĩnh / 2.048 byte | SRAM còn cho stack/heap |
|---|---|---|---|
| Control_Hardware | 10.434 byte (32,3%) | 809 byte (39,5%) | 1.239 byte |
| TelemetryDemo | 7.916 byte (24,5%) | 796 byte (38,9%) | 1.252 byte |

Control_Hardware: .text=10.246, .data=188, .bss=621. Adapter dùng buffer cố định RX 96 + TX 240 + ACK 70 = 406 byte, nằm trong SRAM tĩnh nêu trên. Chưa đo đỉnh stack khi snprintf/sscanf, chưa đo jitter PID hay độ trễ truyền thực tế; số bộ nhớ tĩnh không thay thế đo runtime. Script build và ELF/HEX kiểm tra nằm tạm ở `/tmp/linefollower-avr-check`, không đưa binary vào repo; để tự build/nạp bằng Arduino IDE/CLI xem [ARDUINO.md](ARDUINO.md).

**Chưa nạp Arduino, chưa thử trên xe, chưa kết nối HC-05/L298N thật, chưa xác nhận mức điện áp/chiều quay trên phần cứng hoặc độ bền EEPROM khi mất nguồn.** Kiểm tra UART trên host không mô phỏng thời gian truyền 9600 baud/interrupts của AVR. Cần thử khi kê bánh, xác nhận heartbeat và đo thời gian trên board trước vận hành.

## Kết quả bàn giao trước tích hợp

Các kết quả ML/Python dưới đây là ghi nhận từ lần bàn giao trước, không chạy lại trong thay đổi firmware này.

Môi trường: Linux, GCC 13.3, CMake 3.28, Qt 6.4.2 Widgets/SerialPort; Python 3.12, scikit-learn 1.8.0. Build C++17 trong `build-vscode`.

- CMake configure và build: thành công cho GUI, core_tests, model_check và arduino_adapter_tests.
- CTest: 2/2 executable qua. Bao gồm parser gói chia nhỏ/nhiều gói, CRC lỗi, tràn frame, seq trùng/thiếu/reboot, cửa sổ thiếu dữ liệu, deadband, giới hạn policy, cây không hợp lệ; adapter UART TX, lệnh PWM, giới hạn bước/thời gian và watchdog.
- GUI offscreen: mở demo, bắt đầu lượt, nhận dự đoán, lưu xác nhận, gửi đề xuất; ACK + telemetry xác nhận PWM 100 → 95; ngắt dữ liệu làm fresh=false và khóa lệnh; kết thúc và tạo CSV/summary. Đã kiểm tra ảnh hiển thị.
- Pipeline ML: huấn luyện trên 12 lượt **fixture mô phỏng**, chia theo run ID; kiểm tra bộ huấn luyện mặc định loại mô phỏng. Không báo độ chính xác fixture như kết quả xe.
- model_check: 1080 hàng đặc trưng cho kết quả C++ khớp dự đoán Python được xuất trong parity.csv.
- Kiểm thử hàm chấm sự kiện: đúng một cảnh báo trước 1000 ms và một sự kiện bỏ sót theo fixture; kiểm tra code Python bằng py_compile.

Giới hạn lần bàn giao trước: chưa chạy trên Windows/MSYS2 của người dùng, chưa nạp Arduino, chưa kết nối HC-05/L298N thật, chưa đo jitter PID và chưa có dữ liệu xe thật. Compile AVR đã bổ sung ở mục tích hợp phía trên. Test adapter dùng HardwareSerial stub trên host, không đại diện kiểm thử motor. Số đo hiệu quả ML, cảnh báo sớm và A/B trên xe hiện chưa có.

Không đóng gói CMakeCache.txt, file build Linux hoặc thay `.vscode` của người dùng. Cần build lại trong `build-vscode` hiện có của dự án trên Windows.
