# Giao thức v1

ASCII, phân cách dấu phẩy, kết thúc `*HHHH\n`. HHHH là CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF, không reflection/xor cuối), tính trên toàn bộ phần trước `*`. Không có khoảng trắng. Các số thập phân trong bản tin truyền dưới dạng số nguyên nhân 1000.

```text
T,1,boot,seq,tick_ms,L,R,e_milli,u_milli,pwmL,pwmR,base,mode,kp_milli,ki_milli,kd_milli,loss_count,last_loss_tick,origin*CRC\n
C,1,id,PWM,target*CRC\n
C,1,0,PING,0*CRC\n
A,1,id,code,base,PWM*CRC\n
```

- boot: ID khác sau mỗi lần Arduino khởi động; sketch thử dùng EEPROM địa chỉ 0..3. Không dùng EEPROM này nếu firmware đang sử dụng vùng đó; cấp vùng khác hoặc cấp session ID riêng.
- seq, tick_ms: uint32, GUI hỗ trợ rollover. boot đổi kết thúc lượt; mẫu trùng/lùi bị bỏ.
- L/R: mức logic 0/1. e giữ đúng quy ước firmware; demo dùng R−L. GUI không tự suy mode từ hai cảm biến.
- u: giới hạn ±255; PWM L/R là độ lớn 0..255. Nếu robot quay lùi, mode phải phản ánh tìm line; cần bổ sung direction trong phiên bản giao thức sau nếu cần phân tích chiều quay.
- mode: 0 dừng, 1 chạy, 2 tìm line.
- loss_count: tăng đúng một lần khi thực sự chuyển sang tìm line; last_loss_tick: thời điểm sự kiện Arduino, không dùng thời điểm GUI nhận gói.
- origin: 0 mô phỏng, 1 cảm biến và điều khiển thật. Sketch thử luôn 0.
- ACK code: 0 OK, 1 sai giới hạn/bước, 2 không đang chạy, 3 điều chỉnh quá nhanh, 4 lệnh không hợp lệ. ACK chưa đủ: GUI đợi telemetry cùng PWM rồi mới ghi applied_confirmed.

10 Hz tại 38400 baud là mặc định đề xuất. HC-05 phải được cấu hình UART ở cùng baud; chọn baud trong GUI không thay cấu hình module. Tại 9600, bắt đầu 5 Hz (`TelemetryLink(Serial,200)`), đo độ dài bản tin thực. Với UART 8N1, ngân sách byte/s xấp xỉ baud/10; giữ telemetry + ACK dưới khoảng 60–70% để có dư địa. Không gửi mỗi vòng PID. GUI suy luận mỗi 500 ms, cửa sổ mặc định 3 s.

Parser tích lũy đến newline, xử lý gói chia nhỏ/nhiều gói một lần; bỏ CRC sai, sai schema/range; tối đa 255 byte/frame, tràn thì bỏ tới newline. QSerialPort buffer 4096 byte. Gap >600 ms làm bỏ cửa sổ; dữ liệu >1000 ms hoặc trễ tăng >1000 ms so với offset tối thiểu quan sát làm khóa lệnh. Đây là ước lượng trễ, không phải đồng bộ đồng hồ tuyệt đối; khi mở kết nối vào một backlog cũ chưa có mốc thời gian, không thể chứng minh độ mới tuyệt đối. Khi vận hành cần xóa backlog/khởi tạo phiên sạch, không replay file vào cổng thật.

GUI không gọi waitForReadyRead/waitForBytesWritten. Tree có tối đa 255 node, sâu tối đa 8; inference bị chặn kích thước nên chi phí nhỏ. Ghi file trên worker riêng. Đọc model tối đa 1 MiB khi chưa ghi lượt. I/O khi mở hộp thoại/nạp bảng có thể gây trễ ngắn; cần đo GUI latency khi thử thật.
