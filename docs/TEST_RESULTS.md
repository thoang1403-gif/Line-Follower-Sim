# Kiểm thử bàn giao

Môi trường: Linux, GCC 13.3, CMake 3.28, Qt 6.4.2 Widgets/SerialPort; Python 3.12, scikit-learn 1.8.0. Build C++17 trong `build-vscode`.

- CMake configure và build: thành công cho GUI, core_tests, model_check và arduino_adapter_tests.
- CTest: 2/2 executable qua. Bao gồm parser gói chia nhỏ/nhiều gói, CRC lỗi, tràn frame, seq trùng/thiếu/reboot, cửa sổ thiếu dữ liệu, deadband, giới hạn policy, cây không hợp lệ; adapter UART TX, lệnh PWM, giới hạn bước/thời gian và watchdog.
- GUI offscreen: mở demo, bắt đầu lượt, nhận dự đoán, lưu xác nhận, gửi đề xuất; ACK + telemetry xác nhận PWM 100 → 95; ngắt dữ liệu làm fresh=false và khóa lệnh; kết thúc và tạo CSV/summary. Đã kiểm tra ảnh hiển thị.
- Pipeline ML: huấn luyện trên 12 lượt **fixture mô phỏng**, chia theo run ID; kiểm tra bộ huấn luyện mặc định loại mô phỏng. Không báo độ chính xác fixture như kết quả xe.
- model_check: 1080 hàng đặc trưng cho kết quả C++ khớp dự đoán Python được xuất trong parity.csv.
- Kiểm thử hàm chấm sự kiện: đúng một cảnh báo trước 1000 ms và một sự kiện bỏ sót theo fixture; kiểm tra code Python bằng py_compile.

Giới hạn kiểm thử: chưa chạy trên Windows/MSYS2 của người dùng; chưa compile bằng toolchain AVR, chưa nạp Arduino, chưa kết nối HC-05/L298N thật, chưa đo jitter PID và chưa có dữ liệu xe thật. Test adapter dùng HardwareSerial stub trên host, không đại diện kiểm thử motor. Số đo hiệu quả ML, cảnh báo sớm và A/B trên xe hiện chưa có.

Không đóng gói CMakeCache.txt, file build Linux hoặc thay `.vscode` của người dùng. Cần build lại trong `build-vscode` hiện có của dự án trên Windows.
