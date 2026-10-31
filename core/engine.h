#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace lf {
constexpr int FeatureCount = 9;
inline const std::array<const char*, FeatureCount> FeatureNames = {
    "sensor_changes_s", "reversals_s", "pid_std", "pid_delta_mean",
    "pwm_diff_mean", "search_fraction", "search_entries_s", "pid_abs_mean", "base_mean"};
enum class State { Unknown = -1, Stable = 0, Oscillating = 1, Risk = 2 };
const char* stateKey(State s);
struct Sample {
    uint32_t boot=0, seq=0, tick=0, losses=0, lastLoss=0;
    int64_t t=0, received=0;
    int left=0, right=0, pwmL=0, pwmR=0, base=0, mode=0;
    bool synthetic=true;
    double error=0, output=0, kp=0, ki=0, kd=0;
    std::string wire;
};
struct Ack { uint32_t id=0; int code=0, base=0; }; // 0=OK, 1=range, 2=state, 3=rate, 4=syntax
uint16_t crc16(const std::string &s);
std::string frame(const std::string &body);
std::string encode(const Sample &s);
std::string command(uint32_t id, const std::string &op, int value);
class Decoder {
    std::string buffer;
    bool discard=false;
public:
    uint64_t errors=0;
    std::function<void(Sample)> sample;
    std::function<void(Ack)> ack;
    void feed(const std::string &bytes);
    void reset();
private:
    void line(const std::string &s);
};
// Arduino clock and sequence use unsigned rollover. A changed boot ID is a new session.
class Timeline {
    bool initialized=false;
    Sample previous;
public:
    uint64_t missing=0;
    void reset();
    // 1 accepted, 0 duplicate/out of order, 2 boot changed (accepted as a fresh origin).
    int accept(Sample &s);
};
struct FeatureConfig { int windowMs=3000; double deadband=5; int maxGapMs=600; };
struct Features {
    std::array<double, FeatureCount> x{};
    bool valid=false;
    int64_t start=0, end=0;
    std::string reason;
};
Features extract(const std::deque<Sample> &window, const FeatureConfig &cfg);
class Window {
    std::deque<Sample> data;
public:
    FeatureConfig cfg;
    void clear() { data.clear(); }
    void push(const Sample &s);
    Features features() const { return extract(data, cfg); }
    const std::deque<Sample>& samples() const { return data; }
};
struct Prediction { State state=State::Unknown; std::string reason; };
Prediction rules(const Features &f);
struct Node { int feature=-1, left=-1, right=-1, label=-1; double threshold=0; };
class Tree {
public:
    std::vector<Node> nodes;
    std::string id;
    bool realData=false;
    FeatureConfig config;
    bool validate(std::string &error) const;
    State predict(const Features &f) const;
};
struct PolicyConfig { int minimum=20, maximum=180, maxStep=10, intervalMs=3000, holdMs=5000, confirmations=3; };
class Policy {
    State last=State::Unknown;
    int streak=0;
    int64_t lastSent=-100000, holdUntil=0;
public:
    PolicyConfig cfg;
    void reset();
    void observe(State s);
    void hold(int64_t now);
    int propose(int base, State state) const;
    bool allowed(int64_t now, int base, int target, bool fresh, int mode, bool pending) const;
    void sent(int64_t now) { lastSent=now; streak=0; }
};
} // namespace lf
