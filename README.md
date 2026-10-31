# Xe dò line — AI hỗ trợ vận hành / C++ Qt

Bản mở rộng từ main.cpp và CMakeLists.txt người dùng cung cấp. Giữ target `LineFollowerSim`, C++17, Qt Widgets và tên thư mục `build-vscode`. Không thay file cấu hình VS Code hay cache build của bạn. Gói không chứa binary/cache Linux để chép vào Windows.

**Chưa có mô hình đã kiểm chứng trên xe.** Có đầy đủ pipeline thu dữ liệu → gán nhãn → huấn luyện cây quyết định → đánh giá theo lượt → lưu JSON → suy luận C++. Dữ liệu mô phỏng chỉ kiểm tra phần mềm. Phần đề xuất PWM là chính sách giảm giới hạn, không phải PWM tối ưu được học.

## Kiến trúc và file

| File/thư mục | Thay đổi / mục đích |
|---|---|
| main.cpp | Giữ các ô kết nối/cảm biến/PID và widget QPainter; thêm AI, nút thao tác, chế độ, cấu hình, bảng lượt. Chỉ vẽ đầu ra PID cùng hàng cảm biến. |
| CMakeLists.txt | Giữ target cũ; thêm Qt SerialPort, core và test. |
| core/engine.* | CRC/parser, timestamp rollover, cửa sổ, 9 đặc trưng, baseline rules, cây quyết định, policy PWM. Không phụ thuộc Qt. |
| app/source.* | Nguồn mô phỏng hoặc QSerialPort bất đồng bộ, cùng bộ giải mã. |
| app/controller.* | Điều phối, dữ liệu cũ, ACK + telemetry, phiên chạy, dự đoán, nhãn phản hồi. |
| app/journal.* | Ghi file trên luồng riêng, hàng đợi giới hạn và báo lỗi ghi. |
| app/model_io.* | Kiểm tra JSON/schema/feature order/cây trước khi nạp. |
| arduino/TelemetryDemo/ | Sketch thử không điều khiển motor + adapter ghép vào firmware thật. |
| ml/ | Huấn luyện ngoại tuyến, gán nhãn khoảng thời gian, đánh giá sự kiện. |
| tests/ | Test lõi, adapter, parity Python–C++ và pipeline mô phỏng. |
| docs/ | Giao thức, ghép Arduino, thu dữ liệu và kiểm chứng. |

## Build Windows / VS Code / MSYS2

1. Đóng chương trình đang chạy. Sao lưu mã cũ. Chép **nội dung** thư mục này vào thư mục dự án hiện có; giữ nguyên `.vscode/` và `build-vscode/` của bạn.
2. Dùng đúng môi trường compiler đã cấu hình: UCRT64 hoặc MINGW64, không trộn hai loại. Thư viện mới cần thêm là SerialPort. Trong terminal MSYS2 tương ứng:

```bash
# Nếu compiler hiện có ở C:/msys64/ucrt64/bin:
pacman -S --needed mingw-w64-ucrt-x86_64-qt6-serialport
# Nếu ở C:/msys64/mingw64/bin, dùng gói này thay cho dòng trên:
# pacman -S --needed mingw-w64-x86_64-qt6-serialport
```

3. Từ thư mục dự án, dùng terminal đã có CMake/compiler/Qt trong PATH:

```powershell
cmake -S . -B build-vscode
cmake --build build-vscode --parallel 4
.\build-vscode\LineFollowerSim.exe
ctest --test-dir build-vscode --output-on-failure
```

Giữ generator/compiler trong cache cũ; không thêm `-G` hoặc đổi toolchain khi đã có cache. Nếu dùng generator nhiều cấu hình, chạy binary trong `build-vscode/Debug/`. Nếu thiếu DLL Qt, chạy trong môi trường MSYS2 đúng compiler hoặc dùng công cụ deploy Qt của cùng bộ cài. Không chạy riêng main.cpp bằng nút Run C++.

Gói đính kèm ban đầu không có `build-vscode`, `.vscode/tasks.json`, `.vscode/launch.json` hay firmware xe. Vì vậy bản này không sửa/đoán nội dung các file đó. Nếu lệnh build lỗi, gửi log và các file JSON đang dùng để ghép chính xác.

## Giai đoạn 1 — GUI + mô phỏng + chỉ tư vấn

File: main.cpp, core/, app/, CMakeLists.txt.

- Chọn **Dữ liệu mô phỏng → Connect**. Chọn **Dao động giả lập**.
- Điền mã điều kiện rồi **Bắt đầu ghi lượt**; chờ cửa sổ 3 s và 3 dự đoán liên tiếp (mỗi 0,5 s).
- GUI hiển thị dữ liệu mô phỏng, bộ quy tắc, PWM 100 và đề xuất giảm. PWM chưa thay khi chỉ có đề xuất.
- Nhấn **Áp dụng đề xuất**: chờ ACK rồi telemetry xác nhận. Với dao động, giảm 5; nguy cơ giảm tối đa 10. Sau thay PWM phải thu lại cửa sổ.
- **Giữ nguyên** hoãn áp dụng 5 s. **Đánh dấu dự đoán sai** mở chọn nhãn, lưu nguyên cửa sổ chụp tại thời điểm bấm; **Xác nhận đúng** giúp lấy mẫu cả dự đoán đúng, tránh chỉ học từ lỗi.
- Bật **Ngắt dữ liệu mô phỏng**: sau >1 s, mất trạng thái hợp lệ và khóa Áp dụng. Bỏ chọn để thu lại cửa sổ.
- Kết thúc lượt, mở thư mục CSV hoặc nạp bảng lượt đã lưu. Nút ghi lượt không điều khiển chạy/dừng motor thật.

Mặc định: min PWM 20, max 180, mỗi lần ≤10, cách ≥3 s, 3 kết luận liên tiếp. Các giới hạn này là điểm khởi đầu thử nghiệm, phải chỉnh theo xe. PWM ngoài khoảng sẽ không được tự kéo về khoảng bằng một bước lớn. Chính sách không tự tăng PWM khi ổn định. Chế độ tự động mặc định tắt, chỉ mở với mô hình dữ liệu thật, telemetry thật và người dùng xác nhận kiểm chứng trước lượt. Mất dữ liệu/ngắt kết nối đưa về tư vấn.

## Giai đoạn 2 — Bluetooth và thu dữ liệu thật

File: app/source.*, core/engine.*, arduino/. Đọc docs/PROTOCOL.md và docs/ARDUINO.md.

QSerialPort đã được triển khai thật; chưa kiểm tra với HC-05 của bạn. Sketch TelemetryDemo chạy để thử bản tin, **vẫn đánh dấu mô phỏng**, không điều khiển L298N. Phải ghép adapter vào `.ino` hiện có để lấy PID, PWM, trạng thái và sự kiện thật; Arduino giữ vòng PID và tìm line tại chỗ.

## Giai đoạn 3 — mô hình học máy thật

File: ml/, app/model_io.*, core/engine.*. Xem docs/VALIDATION.md.

Python chỉ dùng huấn luyện ngoại tuyến; GUI và suy luận vẫn C++/Qt:

```powershell
py -m pip install -r ml/requirements.txt
# Gán nhãn khoảng thời gian đã quan sát (ms theo t_ms trong samples.csv):
py ml/pipeline.py label "C:/Users/USER/Documents/LineFollowerRuns/RUN_ID" 10000 20000 stable
# Huấn luyện: chỉ nhận dữ liệu thật theo mặc định
py ml/pipeline.py train "C:/Users/USER/Documents/LineFollowerRuns" trained-model
# Kiểm tra kết quả Python khớp bộ suy luận C++:
.\build-vscode\model_check.exe trained-model/model.json trained-model/parity.csv
```

Nhấn **Nạp mô hình JSON** chọn trained-model/model.json. Phải kết thúc lượt trước khi đổi mô hình. Mô hình nạp cấu hình cửa sổ/dải nhiễu đi kèm. Không có xác suất tự chế. `evaluation.json` chứa confusion matrix, precision/recall/F1 và danh sách lượt train/test. Mô hình chỉ đánh giá trạng thái hiện tại; không tự nhận là cảnh báo sớm hay đã học PWM tối ưu.

## File dữ liệu xuất tự động

Mỗi lượt một thư mục trong Documents/LineFollowerRuns (đổi được):

- metadata.json: run ID, nguồn, thời điểm UTC, điều kiện, cấu hình, PID/PWM đầu.
- samples.csv: dữ liệu gốc hợp lệ + nguyên frame + dự đoán tại thời điểm nhận.
- features.csv: mỗi cửa sổ hợp lệ, cấu hình và 9 đặc trưng để huấn luyện.
- events.csv: đề xuất, gửi lệnh, ACK, áp dụng xác nhận, giữ nguyên, lỗi kết nối.
- labels.csv và feedback/*.csv: nhãn xác nhận/sửa, dự đoán gốc, timestamp và cửa sổ gốc.
- summary.json: thời gian phiên, thời gian chạy quan sát được, thời gian dự đoán từng trạng thái, thời gian chưa phân loại, số sự kiện, lịch sử PID/PWM. Không coi khoảng mất dữ liệu là ổn định; không suy ra quãng đường hay tốc độ.

Thống kê không đòi hỏi AI. `confirmed_duration_s` để null: nhãn người dùng chỉ xác nhận từng cửa sổ, có thể chồng lấn, chưa đủ để cộng thành thời gian xác nhận. Lỗi ghi log được báo và khóa áp dụng; nếu ổ đầy, dữ liệu có thể không hoàn chỉnh, phải loại lượt khỏi đánh giá.

## Kiểm thử có thể chạy lại

```powershell
ctest --test-dir build-vscode --output-on-failure
py tests/smoke_ml.py test-output .\build-vscode\model_check.exe
.\build-vscode\LineFollowerSim.exe --smoke-test smoke-output
```

smoke_ml tạo dữ liệu giả để kiểm tra pipeline và xuất mô hình đánh dấu simulation. **Không dùng số đo từ test này làm độ chính xác của xe.** Xem docs/TEST_RESULTS.md cho kết quả thực hiện khi bàn giao.
