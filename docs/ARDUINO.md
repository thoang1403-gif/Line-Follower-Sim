# Ghép vào chương trình Arduino thật

**Cần file .ino điều khiển xe hiện tại để ghép chính xác.** Chưa có file này nên không thay PID, chân motor, quy tắc 00/11 hay tìm line bằng giả định. TelemetryDemo.ino chỉ thử UART, không bật motor. Adapter đã có code thực, firmware xe phải cung cấp các hook dưới đây.

1. Chép TelemetryLink.h cùng thư mục `.ino`. Khai báo `Telemetry telemetry; TelemetryLink link(Serial,100);` hoặc cổng HardwareSerial khác nếu board có. Không dùng SoftwareSerial để khẳng định truyền không chặn.
2. setup: Serial.begin(baud phù hợp HC-05); cấp boot ID tăng sau mỗi reset; đặt synthetic=false **chỉ trong firmware đọc xe thật**. Bảo đảm vùng EEPROM không xung đột. Dùng USB và HC-05 chung UART Uno cần tháo đường module khi upload; không cho hai bộ phát cùng điều khiển RX.
3. Vòng lặp: thực hiện bước PID đến hạn trước; cập nhật snapshot với cảm biến thực, e, u, độ lớn PWM hai bánh, PWM nền, mode và Kp/Ki/Kd đang áp dụng. Các trường Milli = round(value*1000). Không tính PID lần nữa trên laptop.
4. Khi thuật toán Arduino xác nhận mất line và chuyển RUN → SEARCH, tăng losses và lưu lastLoss=millis() đúng một lần. Cả hai mức bằng nhau chưa đủ để suy ra mất line. Quy tắc xác nhận cần xét bố trí cảm biến, nền/line, trạng thái trước và debounce thực nghiệm.
5. `link.poll(telemetry,millis());` sau bước điều khiển. Adapter nhận tối đa 16 byte và gửi tối đa 16 byte/mỗi lần gọi, chỉ ghi số byte có chỗ trong buffer UART. Không delay, không Serial.flush. Format telemetry chỉ mỗi 100/200 ms khi frame trước đã gửi xong. Cần đo thời gian format và jitter PID trên board; đây chưa phải chứng minh thời gian thực.
6. Sau poll, dùng `telemetry.base` làm PWM nền ở bước PID kế tiếp; không ghi đè nó bằng biến base cũ mỗi vòng. Giới hạn 20..180, thay ≤10, cách ≥3 s được kiểm tra lại ở Arduino. Tùy xe phải sửa đồng thời policy firmware và GUI nếu đổi giới hạn.
7. Khi `link.stopRequested`: đặt trạng thái STOP, tắt PWM cả hai motor qua hàm dừng hiện có; chỉ cho chạy lại bằng thao tác chủ động tại xe. Cờ được giữ đến khi firmware chủ động reset sau kiểm tra. Adapter chỉ phát yêu cầu dừng, không biết chân L298N để tự ghi pin.

Sau khi nhận PING/PWM hợp lệ, thiếu heartbeat quá 1500 ms phát yêu cầu dừng. Trước khi có phiên laptop, Arduino tiếp tục theo hành vi khởi động cục bộ của firmware. GUI gửi PING mỗi 500 ms chỉ khi đang nhận telemetry mới; khi mất chiều telemetry cũng ngừng heartbeat để Arduino fallback. Mất Bluetooth luôn ngăn GUI áp dụng đề xuất mới. Không tự nối lại rồi chạy lại.

PID và tìm line vẫn do Arduino tự thực hiện. GUI không gửi START/STOP/PID gains thật trong giao thức v1; các nút PID hiện chỉ áp dụng demo và bị khóa khi dùng serial thật. Chỉnh PID firmware, ghi lại các gain thực bằng telemetry rồi so sánh các lượt. Tính năng gửi gain từ GUI có thể thêm sau khi đọc `.ino` và quy tắc cập nhật an toàn.

Kiểm tra trên bàn trước: xác nhận CRC/seq → thử đổi PWM khi không tải → kiểm tra ACK + telemetry → rút Bluetooth và xác nhận hàm dừng motor được gọi → đo jitter vòng điều khiển → thử đường chạy ở PWM thấp. Host stub test không thay thế compile AVR hoặc kiểm tra phần cứng.
