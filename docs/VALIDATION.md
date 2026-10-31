# Thu dữ liệu, mô hình và kiểm chứng

## Dữ liệu thật

Ghi nhiều lượt độc lập, cùng mã điều kiện cho đường chạy/ánh sáng/bố trí cảm biến/pin tương đương. Thu cả đoạn thẳng, cong, chữ S, dao động do PID và chuyển tìm line. Không chỉ thu lỗi. Điểm bắt đầu: 20–30 lượt, nhiều mức PID/PWM; đây là kế hoạch thu mẫu chứ không bảo đảm đủ dữ liệu. Huấn luyện yêu cầu tối thiểu 6 lượt có nhãn; cần nhiều hơn để đánh giá đáng tin cậy.

Giữ lại samples.csv, features.csv, metadata, sự kiện và nhãn. Ghi người quan sát, mô tả đường, trạng thái pin và mọi can thiệp trong mã điều kiện hoặc sổ ghi chú. Lượt không đầy đủ/log lỗi không đưa vào dữ liệu học. Chỉ so sánh cùng bố trí và tần suất telemetry; `pid_delta_mean` phụ thuộc chu kỳ lấy mẫu.

Nhãn stable/oscillating/risk phải do người quan sát thống nhất tiêu chí trước khi xem dự đoán. Dao động: xe lắc hai bên không phải chỉ đổi hướng do chữ S. Risk: dấu hiệu vận hành nguy hiểm theo tiêu chí quan sát đã thống nhất; không được tự gán nhãn risk từ cùng ngưỡng quy tắc rồi tuyên bố AI tốt hơn quy tắc. Hai cảm biến số không phân biệt được mọi tình huống; nếu quan sát mơ hồ, để chưa có nhãn thay vì đoán.

Nút sửa lưu snapshot toàn cửa sổ; nút xác nhận lấy cả mẫu đúng. Công cụ label trong ml/pipeline.py gán nhãn cho các cửa sổ nằm trọn trong khoảng thời gian người quan sát đánh dấu. Không gán nhãn toàn bộ lượt nếu trong lượt có nhiều trạng thái. Nếu cần sửa nhãn, bản ghi cuối cùng của prediction_id được dùng; vẫn giữ lịch sử trong CSV. Không tự huấn luyện sau mỗi phản hồi.

## Huấn luyện và tích hợp

Cây quyết định depth=5, min_samples_leaf=5, class_weight=balanced là cấu hình ban đầu cố định. Python/scikit-learn huấn luyện, xuất JSON; C++ duyệt node. C++ chuyển feature sang float32 như sklearn để giảm sai khác ở ngưỡng. model_check so sánh toàn bộ fixture parity.csv với dự đoán Python.

Chia 67/33 theo run_id bằng GroupShuffleSplit seed=42; bắt buộc cả ba lớp xuất hiện ở train/test. Không chia ngẫu nhiên cửa sổ. Nếu một lần chạy dài bị tách thành nhiều thư mục, phải gom cùng group trước chia hoặc chỉ giữ một session cho một lượt; không dùng đổi run ID để đưa các đoạn liền kề sang test. Cấu hình feature phải đồng nhất. Nếu thử nhiều hyperparameter, tạo tập validation theo lượt trong phần train, giữ test nguyên cho đánh giá cuối; pipeline này chưa có tuning tự động.

Đọc evaluation.json: confusion matrix, precision/recall/F1 từng lớp, macro average, số cửa sổ/lượt. Không diễn giải nhiều cửa sổ chồng lấn thành nhiều quan sát độc lập. Nên bổ sung đánh giá theo từng lượt/ngày/đường chưa thấy. Test hiện tại không chứng minh tổng quát sang đường khác.

Mô hình phân loại **trạng thái hiện tại**. Chính sách giảm PWM tách riêng: stable giữ nguyên, oscillating giảm 5, risk giảm 10, chịu giới hạn bước/min/max và thời gian. Không có mô hình học PWM tối ưu, không tăng PWM tự động.

## Cảnh báo trước mất line

Định nghĩa t_loss trước thử: thời điểm xe thật không còn bám được line theo tiêu chí quan sát, đối chiếu thời điểm Arduino xác nhận RUN→SEARCH. `last_loss_tick` là mốc thuật toán, có thể trễ hơn mất line vật lý; không gọi nó là ground truth khi chưa đối chiếu. Hai cảm biến bằng nhau không tự là mất line.

Trên test runs giữ riêng:

```powershell
py ml/export_event_inputs.py RUNS trained-model event-inputs
py ml/evaluate_events.py event-inputs/warnings.csv event-inputs/losses.csv event-inputs/coverage.csv --horizon-ms 2000
```

Exporter dùng chính model trên test features, không dùng dự đoán baseline đã ghi. Trước chấm, người quan sát kiểm tra/sửa losses.csv theo mốc thực tế. Coverage phải loại đoạn thiếu/không xác nhận được dữ liệu. Nếu loss_count tăng >1 giữa hai bản tin chỉ có timestamp lần cuối, exporter dừng và báo lỗi; cần thêm nhật ký sự kiện Arduino hoặc gán nhãn độc lập cho các lần thiếu, không dùng kết quả chưa bổ sung để báo cáo.

Một cảnh báo đúng phải có 0 < t_loss − t_warning ≤ 2000 ms, với dữ liệu liên tục trong khoảng đánh giá. Dự đoán risk sau khi đã vào SEARCH không phải cảnh báo sớm. Các mẫu risk liên tiếp cách ≤750 ms được gộp thành một đợt; mỗi đợt chỉ ghép một sự kiện. Báo cáo detected, missed, false alarm episodes, thời gian báo trước và unscored do thiếu coverage. Khoảng horizon phải cố định trước khi xem test. Không có cảnh báo trước vẫn là kết quả hợp lệ, không sửa thời gian để làm đẹp kết quả.

## Thử PWM cố định và có hỗ trợ

Chạy xen kẽ A/B (hoặc ngẫu nhiên thứ tự), cùng đường, PID, cách đặt xe, pin/ánh sáng và điều kiện tải. A không áp dụng đề xuất, B áp dụng có kiểm soát. Ghi can thiệp, số lần mất line, thời gian hoàn thành đo độc lập, tỷ lệ lượt hoàn thành, và PWM history. Không suy tốc độ thực hay quãng đường từ PWM. So sánh trung vị và phân tán trên nhiều lượt, không chỉ chọn lượt đẹp.

Chỉ xác nhận chế độ tự động sau khi kiểm thử giới hạn PWM/ACK/mất kết nối và kết quả mô hình trên lượt thật giữ riêng đạt tiêu chí nhóm đặt trước. model.training_source='real' chỉ mô tả nguồn, không phải chứng nhận an toàn hay độ chính xác. Nếu không đạt, giữ chỉ tư vấn và thu dữ liệu thêm.

Nguồn kỹ thuật: https://doc.qt.io/qt-6/qserialport.html ; https://scikit-learn.org/stable/modules/generated/sklearn.model_selection.GroupShuffleSplit.html ; https://scikit-learn.org/stable/modules/generated/sklearn.tree.DecisionTreeClassifier.html
