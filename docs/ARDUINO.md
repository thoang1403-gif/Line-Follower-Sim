# Firmware Arduino Uno + HC-05

## Sketch và phần cứng

Mở **`arduino/Control_Hardware/Control_Hardware.ino`**, cùng thư mục với `TelemetryLink.h`. Đây là nguồn điều khiển xe duy nhất, chuyển từ `Control_Hardware.cpp` ở gốc trên main (7018ea0). File cũ đã được bỏ. `arduino/TelemetryDemo/TelemetryDemo.ino` vẫn là sketch thử riêng, dữ liệu mô phỏng, không điều khiển motor; không chép hai `.ino` vào cùng thư mục. Firmware không thuộc target GUI Qt.

| Thiết bị | Chân Uno |
|---|---|
| Cảm biến trái / phải | D13 / D12, INPUT |
| L298N ENA / ENB | D3 / D11 |
| L298N IN1 / IN2 / IN3 / IN4 | D5 / D6 / D9 / D10 |
| HC-05 TXD → Uno RX | D0 |
| Uno TX → HC-05 RXD | D1, qua mạch hạ mức điện áp phù hợp |
| Mass tín hiệu | GND chung Uno, L298N và HC-05 |

Dùng HardwareSerial `Serial.begin(9600)`, 8N1, không SoftwareSerial. Cấu hình UART phía HC-05 ở **9600 baud** bằng chế độ cấu hình phù hợp module trước khi vận hành. **Chọn baud trong GUI không tự cấu hình lại UART HC-05.** GUI hiện mặc định 38400, cần chọn lại 9600 cho sketch xe này. Demo riêng vẫn dùng 38400 / 10 Hz.

## Điều khiển và dữ liệu thật

Giữ PWM nền khởi động **70**, Kp=25, Ki=0, Kd=0, chu kỳ PID **10 ms** và dt theo thời gian thực đã trôi qua. Chiều tiến giữ IN1/IN3=LOW, IN2/IN4=HIGH. Tích phân vẫn giới hạn ±5 khi Ki khác 0; Ki=0 thì xóa tích phân. Hiệu chỉnh u vẫn giới hạn ±min(base, 255−base), sau đó PWM trái=int(base+u), phải=int(base−u), chặn 0..255 trước khi ghi analogWrite.

| L / R | Hành vi giữ từ firmware gốc |
|---|---|
| LOW / LOW | e=0, chạy với PWM nền |
| HIGH / LOW | e=−1 |
| LOW / HIGH | e=+1 |
| HIGH / HIGH | Stop(), reset PID; có thể chạy lại khi cảm biến đổi nếu chưa bị khóa bởi heartbeat |

Hai cảm biến cùng mức không được tự suy thành sự kiện mất line. Firmware chưa có thuật toán tìm line hay xác nhận mất/tìm lại line: chỉ gửi mode 0 (dừng), 1 (chạy), `losses=lastLoss=0`; không phát mode 2. PWM là mức điều khiển 0..255, **không phải tốc độ thực/RPM**; không có encoder hoặc camera.

Mỗi vòng `loop()` chạy bước PID nếu đến hạn rồi luôn gọi `telemetryLink.poll()`, kể cả giữa hai bước và khi dừng. Adapter giữ nguyên CRC, seq, ACK, PING và giới hạn nhận/gửi tối đa 16 byte mỗi lần poll, chỉ ghi khi buffer UART còn chỗ. Không delay, đọc chờ, Serial.flush hay Serial.print debug trong luồng này.

Telemetry được tạo mỗi **200 ms (5 Hz)** khi frame trước đã gửi xong, gồm mức cảm biến thật, e=R−L, u sau giới hạn, PWM đã ghi ra hai chân EN, PWM nền, mode và gain. Giá trị Milli dùng round(value×1000), `synthetic=false` tương ứng `origin=1`. Khi dừng, u và PWM hai bánh bằng 0, vẫn cập nhật cảm biến mỗi 10 ms.

Adapter kiểm tra lệnh PWM: mục tiêu 20..180, bước thay đổi ≤10, cách nhau ≥3000 ms, chỉ khi mode=1; giữ ACK và xử lý lệnh lặp hiện có. `telemetry.base` giữ mục tiêu đã được chấp nhận, bước PID kế tiếp mới lấy vào `basePWM`; không ghi đè mục tiêu bằng snapshot cũ. ACK xác nhận chấp nhận lệnh; các PWM hai bánh trong snapshot luôn là giá trị đã ghi gần nhất. GUI đợi ACK và telemetry phản hồi PWM nền. Không hỗ trợ START/STOP hay thay gain qua GUI; thanh PWM thủ công và nút dừng GUI chỉ điều khiển mô phỏng.

## Boot ID và dừng khi mất heartbeat

Đã rà soát EEPROM trong repo: trước tích hợp chỉ TelemetryDemo dùng EEPROM 0..3; sketch điều khiển cũ không dùng EEPROM. Sketch mới dành **địa chỉ 0..3 (uint32_t)** cho bộ đếm boot, đọc/tăng/ghi bằng EEPROM.get/put **một lần trong setup**, không ghi trong loop. Giá trị chưa khởi tạo 0xffffffff chuyển về 1; ID luôn khác lần khởi động liền trước khi EEPROM hoạt động bình thường. Hai sketch dùng chung quy ước/vùng boot vì chạy luân phiên trên cùng board. Không dùng vùng này cho dữ liệu khác; nếu thay vùng phải kiểm tra lại cả hai sketch.

**Xe có thể chạy trước khi GUI kết nối.** Giữ hành vi khởi động cục bộ cũ: các chân được tắt trong setup, sau đó PID tự chạy theo cảm biến. Bộ giám sát heartbeat chỉ có hiệu lực sau PING hợp lệ hoặc lệnh PWM được adapter chấp nhận thiết lập phiên. Lệnh PWM bị từ chối không thiết lập phiên.

GUI gửi PING mỗi khoảng 500 ms khi telemetry còn mới. Quá **1500 ms kể từ heartbeat gần nhất**, adapter giữ `stopRequested=true`. Ngay sau poll, firmware gọi Stop(), ResetPID(), đặt mode=0, u=0, PWM L/R=0. Các bước PID sau kiểm tra cờ này nên không bật lại động cơ. Kết nối lại, PING mới hoặc thay mức cảm biến đều không xóa cờ; lệnh PWM khi dừng bị từ chối. **Trong phiên bản này, chỉ reset Arduino mới xóa trạng thái dừng do heartbeat**, sau reset lại có thể chạy trước GUI. Không thêm nút hoặc phần cứng điều khiển mới.

Frame đang truyền hoặc đã xếp vào UART trước khi Stop không được sửa giữa chừng; snapshot tiếp theo phản ánh trạng thái dừng. Khi mất đường truyền hoàn toàn, GUI chỉ biết dữ liệu cũ/mất kết nối và thấy trạng thái dừng sau khi nhận lại telemetry. Bộ giám sát không phát hiện được mất Bluetooth trước khi phiên đầu tiên được thiết lập.

## Nạp Uno và thử khi kê bánh

1. **Kê hai bánh khỏi mặt bàn**, cố định xe và chuẩn bị ngắt nguồn motor. Không dựa vào nút DỪNG XE trong GUI cho xe thật.
2. Tháo **cả RX/TX HC-05 khỏi D0/D1** khi nạp bằng USB. Uno dùng chung UART cho USB và HC-05; tránh để hai nguồn phát cùng điều khiển RX. Chọn board Arduino Uno và đúng cổng USB trong Arduino IDE, mở sketch `arduino/Control_Hardware/Control_Hardware.ino`, Verify rồi Upload.
3. Nếu đã có Arduino CLI và core AVR, từ gốc repo có thể dùng:

   ```bash
   arduino-cli compile --fqbn arduino:avr:uno arduino/Control_Hardware
   arduino-cli upload --fqbn arduino:avr:uno --port /dev/ttyACM0 arduino/Control_Hardware
   ```

   Thay `/dev/ttyACM0` bằng cổng thật (Windows: COM tương ứng). Đọc số flash/SRAM sau compile; host stub test không đo bộ nhớ Uno.
4. Sau upload, đóng Serial Monitor, nối lại HC-05 đúng bảng chân khi nguồn đã tắt. Xác nhận UART module là 9600, nguồn/GND và hạ mức RXD đúng. Cấp nguồn khi bánh vẫn đang kê; firmware có thể chạy ngay.
5. Ghép đôi HC-05 với máy tính, mở GUI → **Kết nối HC-05** → chọn cổng Bluetooth serial đã ghép đôi (ví dụ `/dev/rfcomm0` hoặc COM), chọn **9600**. GUI không tự ghép đôi. Xác nhận telemetry thật khoảng 5 Hz, gain 25/0/0, base 70, seq tăng, dữ liệu không lỗi CRC. Thử bốn tổ hợp cảm biến và đối chiếu bảng trên.
6. **Bắt đầu ghi lượt** để kiểm tra lệnh từ GUI. Thanh PWM thủ công không điều khiển xe thật. Khi bộ quy tắc/mô hình hợp lệ tạo đề xuất và **Áp dụng đề xuất** được mở, áp dụng một bước giảm; xem ACK và `applied_confirmed` trong events.csv, base/PWM phản hồi. Giữ chế độ tự động tắt khi kiểm tra ban đầu. Nếu chưa có đề xuất hợp lệ, chờ đủ cửa sổ; không coi thao tác kéo thanh PWM mô phỏng là đã thử lệnh thật.
7. Sau khi GUI đã nhận telemetry và gửi heartbeat, ngắt Bluetooth/đóng kết nối: xác nhận motor dừng sau khi hết hạn heartbeat (1500 ms tính từ heartbeat cuối tại Arduino). Kết nối lại và đổi mức cảm biến: bánh phải tiếp tục đứng, telemetry mode=0, PWM L/R=0. Reset Uno khi bánh còn kê: boot ID phải đổi, xe trở lại hành vi khởi động cũ.
8. Trước khi hạ bánh, đo jitter vòng PID, độ trễ UART/heartbeat và xác nhận dừng trên phần cứng. Các test phần mềm không thay thế bước này.

Hai bản `TelemetryLink.h` hiện giống hệt nhau để mỗi sketch có thể mở độc lập. Test `arduino_adapter_copies_match` phát hiện lệch bản; khi bảo trì adapter phải cập nhật cả hai. Xem `docs/TEST_RESULTS.md` cho kết quả kiểm thử và giới hạn đã kiểm chứng.
