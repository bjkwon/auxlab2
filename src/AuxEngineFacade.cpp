#include "AuxEngineFacade.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <limits>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#define AUX_DUP _dup
#define AUX_DUP2 _dup2
#define AUX_FILENO _fileno
#define AUX_CLOSE _close
#else
#include <unistd.h>
#define AUX_DUP dup
#define AUX_DUP2 dup2
#define AUX_FILENO fileno
#define AUX_CLOSE close
#endif

namespace {
constexpr uint16_t kTypeString = 0x0030;
constexpr uint16_t kTypeByte = 0x0050;
constexpr uint16_t kTypeCell = 0x1000;
constexpr uint16_t kTypeStrut = 0x2000;
constexpr uint16_t kTypeStruts = 0x4000;
constexpr double kRmsDbOffset = 3.0103;

std::string trimAscii(std::string s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) {
    return {};
  }
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string scalarOnlyPreview(const std::string& preview) {
  std::string p = trimAscii(preview);
  if (p.rfind("type", 0) == 0) {
    const auto comma = p.find(',');
    if (comma != std::string::npos) {
      return trimAscii(p.substr(comma + 1));
    }
  }
  return p;
}

std::string shortTypeTag(uint16_t type) {
  if ((type & kTypeCell) != 0) {
    return "CELL";
  }
  if ((type & (kTypeStrut | kTypeStruts)) != 0) {
    return "STRC";
  }
  if ((type & 0xFFF0) == kTypeString) {
    return "TEXT";
  }
  if ((type & 0xFFF0) == kTypeByte) {
    return "BIN";
  }

  const uint16_t shape = type & 0x000F;
  if (shape == 1) {
    return "SCLR";
  }
  if (shape == 2 || shape == 3) {
    return "VECT";
  }
  return "";
}

bool isTextType(uint16_t type) {
  return (type & 0xFFF0) == kTypeString;
}

std::string formatRmsDb(const AuxObj& obj) {
  const int channels = aux_num_channels(obj);
  if (channels <= 0) {
    return "";
  }

  std::ostringstream out;
  bool first = true;
  for (int ch = 0; ch < channels; ++ch) {
    const size_t len = aux_flatten_channel_length(obj, ch);
    if (!first) {
      out << ", ";
    }
    first = false;

    if (len == 0) {
      out << "inf";
      continue;
    }

    std::vector<double> samples(len, 0.0);
    aux_flatten_channel(obj, ch, samples.data(), samples.size());

    long double sumSq = 0.0;
    for (double v : samples) {
      sumSq += static_cast<long double>(v) * static_cast<long double>(v);
    }
    const long double mean = sumSq / static_cast<long double>(len);
    if (mean <= 0.0) {
      out << "-inf";
      continue;
    }
    const double rmsDb = 20.0 * std::log10(std::sqrt(static_cast<double>(mean))) + kRmsDbOffset;
    out << std::fixed << std::setprecision(1) << rmsDb;
  }
  return out.str();
}

std::string readTmpFile(FILE* f) {
  if (!f) {
    return {};
  }
  std::fflush(f);
  std::fseek(f, 0, SEEK_SET);
  std::string out;
  char buf[4096];
  while (true) {
    const size_t n = std::fread(buf, 1, sizeof(buf), f);
    if (n == 0) {
      break;
    }
    out.append(buf, n);
  }
  return out;
}

class ScopedStdCapture {
public:
  ScopedStdCapture() {
    std::fflush(stdout);
    std::fflush(stderr);

    oldStdoutFd_ = AUX_DUP(AUX_FILENO(stdout));
    oldStderrFd_ = AUX_DUP(AUX_FILENO(stderr));
    if (oldStdoutFd_ < 0 || oldStderrFd_ < 0) {
      return;
    }

    stdoutTmp_ = std::tmpfile();
    stderrTmp_ = std::tmpfile();
    if (!stdoutTmp_ || !stderrTmp_) {
      return;
    }

    if (AUX_DUP2(AUX_FILENO(stdoutTmp_), AUX_FILENO(stdout)) < 0) {
      return;
    }
    if (AUX_DUP2(AUX_FILENO(stderrTmp_), AUX_FILENO(stderr)) < 0) {
      return;
    }

    oldCoutBuf_ = std::cout.rdbuf(coutBuffer_.rdbuf());
    oldCerrBuf_ = std::cerr.rdbuf(cerrBuffer_.rdbuf());
    active_ = true;
  }

  ~ScopedStdCapture() {
    restore();
  }

  std::string output() const {
    std::cout.flush();
    std::cerr.flush();
    std::string s = readTmpFile(stdoutTmp_);
    s += readTmpFile(stderrTmp_);
    s += coutBuffer_.str();
    s += cerrBuffer_.str();
    return s;
  }

private:
  void restore() {
    if (oldCoutBuf_) {
      std::cout.rdbuf(oldCoutBuf_);
      oldCoutBuf_ = nullptr;
    }
    if (oldCerrBuf_) {
      std::cerr.rdbuf(oldCerrBuf_);
      oldCerrBuf_ = nullptr;
    }

    std::fflush(stdout);
    std::fflush(stderr);

    if (oldStdoutFd_ >= 0) {
      AUX_DUP2(oldStdoutFd_, AUX_FILENO(stdout));
      AUX_CLOSE(oldStdoutFd_);
      oldStdoutFd_ = -1;
    }
    if (oldStderrFd_ >= 0) {
      AUX_DUP2(oldStderrFd_, AUX_FILENO(stderr));
      AUX_CLOSE(oldStderrFd_);
      oldStderrFd_ = -1;
    }

    if (stdoutTmp_) {
      std::fclose(stdoutTmp_);
      stdoutTmp_ = nullptr;
    }
    if (stderrTmp_) {
      std::fclose(stderrTmp_);
      stderrTmp_ = nullptr;
    }
    active_ = false;
  }

  bool active_ = false;
  int oldStdoutFd_ = -1;
  int oldStderrFd_ = -1;
  FILE* stdoutTmp_ = nullptr;
  FILE* stderrTmp_ = nullptr;
  std::ostringstream coutBuffer_;
  std::ostringstream cerrBuffer_;
  std::streambuf* oldCoutBuf_ = nullptr;
  std::streambuf* oldCerrBuf_ = nullptr;
};
}  // namespace

AuxEngineFacade::AuxEngineFacade() {
  cfg_.sample_rate = 22050;
  cfg_.display_precision = 6;
  cfg_.display_limit_x = 10;
  cfg_.display_limit_y = 10;
  cfg_.display_limit_bytes = 256;
  cfg_.display_limit_str = 32;
  cfg_.debug_hook = nullptr;
}

AuxEngineFacade::~AuxEngineFacade() {
  if (rootCtx_) {
    aux_close(rootCtx_);
  }
}

bool AuxEngineFacade::init() {
  rootCtx_ = aux_init(&cfg_);
  activeCtx_ = rootCtx_;
  return rootCtx_ != nullptr;
}

EvalResult AuxEngineFacade::eval(const std::string& command) {
  EvalResult out;
  if (!rootCtx_) {
    out.status = 1;
    out.output = "AUX engine is not initialized.";
    return out;
  }

  std::string preview;
  std::string captured;
  {
    ScopedStdCapture cap;
    out.status = aux_eval(&activeCtx_, command, cfg_, preview);
    captured = cap.output();
  }

  out.output = captured;
  if (!preview.empty()) {
    if (!out.output.empty() && out.output.back() != '\n') {
      out.output.push_back('\n');
    }
    out.output += preview;
  }

  if (out.status == static_cast<int>(auxEvalStatus::AUX_EVAL_PAUSED)) {
    paused_ = true;
    auxDebugInfo info{};
    if (aux_debug_get_pause_info(activeCtx_, info) == 0) {
      pauseInfo_ = info;
      activeCtx_ = info.ctx ? *info.ctx : activeCtx_;
    } else {
      activeCtx_ = activeCtx_ ? activeCtx_ : rootCtx_;
    }
  } else {
    paused_ = false;
    activeCtx_ = rootCtx_;
  }
  return out;
}

std::vector<VarSnapshot> AuxEngineFacade::listVariables() const {
  std::vector<VarSnapshot> vars;
  if (!activeCtx_) {
    return vars;
  }

  auto names = aux_enum_vars(activeCtx_);
  vars.reserve(names.size());
  for (const auto& name : names) {
    auto obj = aux_get_var(activeCtx_, name);
    if (!obj) {
      continue;
    }

    VarSnapshot snap;
    snap.name = name;
    snap.isAudio = aux_is_audio(obj);
    snap.channels = aux_num_channels(obj);
    snap.type = aux_type(obj);
    snap.typeTag = shortTypeTag(snap.type);
    aux_describe_var(activeCtx_, obj, cfg_, snap.type, snap.size, snap.preview);
    if (snap.typeTag == "SCLR") {
      snap.preview = scalarOnlyPreview(snap.preview);
    }
    if (snap.isAudio) {
      snap.rms = formatRmsDb(obj);
    }
    vars.push_back(std::move(snap));
  }
  return vars;
}

std::optional<SignalData> AuxEngineFacade::getSignalData(const std::string& varName) const {
  if (!activeCtx_) {
    return std::nullopt;
  }

  auto obj = aux_get_var(activeCtx_, varName);
  if (!obj) {
    return std::nullopt;
  }

  const int channels = aux_num_channels(obj);
  if (channels <= 0) {
    return std::nullopt;
  }

  SignalData data;
  data.isAudio = aux_is_audio(obj);
  data.sampleRate = aux_get_fs(activeCtx_);
  double minStartMs = std::numeric_limits<double>::infinity();

  data.channels.reserve(static_cast<size_t>(channels));
  for (int ch = 0; ch < channels; ++ch) {
    AuxSignal seg{};
    if (aux_get_segment(obj, ch, 0, seg)) {
      minStartMs = std::min(minStartMs, seg.tmark);
      if (data.sampleRate <= 0 && seg.fs > 0) {
        data.sampleRate = seg.fs;
      }
    }

    const auto len = aux_flatten_channel_length(obj, ch);
    if (len == 0) {
      continue;
    }

    ChannelData channel;
    channel.samples.resize(len);
    aux_flatten_channel(obj, ch, channel.samples.data(), channel.samples.size());
    data.channels.push_back(std::move(channel));
  }

  if (data.channels.empty()) {
    return std::nullopt;
  }
  if (data.isAudio && std::isfinite(minStartMs)) {
    data.startTimeSec = minStartMs / 1000.0;
  }
  return data;
}

bool AuxEngineFacade::isStringVar(const std::string& varName) const {
  if (!activeCtx_) {
    return false;
  }
  auto obj = aux_get_var(activeCtx_, varName);
  if (!obj) {
    return false;
  }
  return isTextType(aux_type(obj));
}

bool AuxEngineFacade::isBinaryVar(const std::string& varName) const {
  if (!activeCtx_) {
    return false;
  }
  auto obj = aux_get_var(activeCtx_, varName);
  if (!obj) {
    return false;
  }
  return (aux_type(obj) & 0xFFF0) == kTypeByte;
}

std::optional<std::string> AuxEngineFacade::getStringValue(const std::string& varName) const {
  if (!activeCtx_) {
    return std::nullopt;
  }
  auto obj = aux_get_var(activeCtx_, varName);
  if (!obj) {
    return std::nullopt;
  }

  uint16_t type = aux_type(obj);
  if (!isTextType(type)) {
    return std::nullopt;
  }

  auxConfig cfg = cfg_;
  cfg.display_limit_str = 1024 * 1024;
  cfg.display_limit_bytes = 1024 * 1024;

  std::string size;
  std::string preview;
  aux_describe_var(activeCtx_, obj, cfg, type, size, preview);
  return preview;
}

std::optional<BinaryData> AuxEngineFacade::getBinaryData(const std::string& varName) const {
  if (!activeCtx_) {
    return std::nullopt;
  }
  auto obj = aux_get_var(activeCtx_, varName);
  if (!obj) {
    return std::nullopt;
  }

  if ((aux_type(obj) & 0xFFF0) != kTypeByte) {
    return std::nullopt;
  }

  BinaryData out;
  const int channels = aux_num_channels(obj);
  for (int ch = 0; ch < channels; ++ch) {
    const int segments = aux_num_segments(obj, ch);
    for (int segIndex = 0; segIndex < segments; ++segIndex) {
      AuxSignal seg{};
      if (!aux_get_segment(obj, ch, segIndex, seg)) {
        continue;
      }
      if (seg.bufType != 'B' || !seg.buf || seg.nSamples == 0) {
        continue;
      }
      const auto* ptr = reinterpret_cast<const unsigned char*>(seg.buf);
      out.bytes.insert(out.bytes.end(), ptr, ptr + seg.nSamples);
    }
  }

  if (out.bytes.empty()) {
    return std::nullopt;
  }
  return out;
}

bool AuxEngineFacade::loadUdfFile(const std::string& fullPath, std::string& err) {
  if (!activeCtx_) {
    err = "AUX context is not initialized.";
    return false;
  }

  std::filesystem::path p(fullPath);
  if (!std::filesystem::exists(p)) {
    err = "UDF file not found.";
    return false;
  }

  const std::string udfName = p.stem().string();
  const std::string udfDir = p.parent_path().string();

  const int defRes = aux_define_udf(activeCtx_, udfName, udfDir, err);
  if (defRes != 0) {
    if (err.empty()) {
      err = "Failed to define UDF.";
    }
    return false;
  }

  const int regRes = aux_register_udf(activeCtx_, udfName);
  if (regRes != 0) {
    err = "Failed to register UDF.";
    return false;
  }
  return true;
}

bool AuxEngineFacade::setBreakpoint(const std::string& udfName, int line, bool enabled, std::string& err) {
  if (!activeCtx_) {
    err = "AUX context is not initialized.";
    return false;
  }
  if (udfName.empty() || line <= 0) {
    err = "Invalid UDF name or line number.";
    return false;
  }

  const int regRes = aux_register_udf(activeCtx_, udfName);
  if (regRes != 0) {
    err = "UDF is not registered.";
    return false;
  }

  int rc = 0;
  if (enabled) {
    rc = aux_debug_add_breakpoints(activeCtx_, udfName, std::vector<int>{line});
  } else {
    rc = aux_debug_del_breakpoints(activeCtx_, udfName, std::vector<int>{-line});
  }
  if (rc != 0) {
    err = enabled ? "Failed to add breakpoint." : "Failed to remove breakpoint.";
    return false;
  }
  return true;
}

std::set<int> AuxEngineFacade::getBreakpoints(const std::string& udfName) const {
  std::set<int> out;
  if (!activeCtx_ || udfName.empty()) {
    return out;
  }

  std::vector<int> lines;
  auto current = aux_debug_view_breakpoints(activeCtx_, udfName, lines);
  for (int v : current) {
    if (v > 0) {
      out.insert(v);
    }
  }
  return out;
}

bool AuxEngineFacade::deleteVar(const std::string& varName) {
  if (!activeCtx_) {
    return false;
  }
  return aux_del_var(activeCtx_, varName) == 0;
}

bool AuxEngineFacade::isPaused() const {
  return paused_;
}

auxContext* AuxEngineFacade::activeContext() const {
  return activeCtx_;
}

auxContext* AuxEngineFacade::rootContext() const {
  return rootCtx_;
}

std::optional<auxDebugInfo> AuxEngineFacade::pauseInfo() const {
  if (!paused_) {
    return std::nullopt;
  }
  return pauseInfo_;
}

RuntimeSettingsSnapshot AuxEngineFacade::runtimeSettings() const {
  RuntimeSettingsSnapshot out;
  out.sampleRate = cfg_.sample_rate;
  out.displayPrecision = cfg_.display_precision;
  out.displayLimitX = cfg_.display_limit_x;
  out.displayLimitY = cfg_.display_limit_y;
  out.displayLimitBytes = cfg_.display_limit_bytes;
  out.displayLimitStr = cfg_.display_limit_str;

  const auxContext* ctx = activeCtx_ ? activeCtx_ : rootCtx_;
  if (ctx) {
    const std::string allPaths = aux_get_udfpath(const_cast<auxContext*>(ctx));
    std::string current;
    std::istringstream lines(allPaths);
    while (std::getline(lines, current)) {
      if (!current.empty()) {
        out.udfPaths.push_back(current);
      }
    }
  }

  return out;
}

bool AuxEngineFacade::applyRuntimeSettings(const RuntimeSettingsSnapshot& settings, std::string& err) {
  if (settings.sampleRate <= 0) {
    err = "Sampling rate must be a positive integer.";
    return false;
  }
  if (settings.displayPrecision < 0 || settings.displayLimitX < 0 || settings.displayLimitY < 0 ||
      settings.displayLimitBytes < 0 || settings.displayLimitStr < 0) {
    err = "Display settings must be non-negative integers.";
    return false;
  }

  cfg_.sample_rate = settings.sampleRate;
  cfg_.display_precision = settings.displayPrecision;
  cfg_.display_limit_x = settings.displayLimitX;
  cfg_.display_limit_y = settings.displayLimitY;
  cfg_.display_limit_bytes = settings.displayLimitBytes;
  cfg_.display_limit_str = settings.displayLimitStr;
  cfg_.search_paths = settings.udfPaths;

  auxContext* ctx = activeCtx_ ? activeCtx_ : rootCtx_;
  if (!ctx && !rootCtx_) {
    return true;
  }

  if (ctx && aux_set_fs(ctx, settings.sampleRate) != 0) {
    err = "Failed to update engine sampling rate.";
    return false;
  }
  if (rootCtx_ && rootCtx_ != ctx && aux_set_fs(rootCtx_, settings.sampleRate) != 0) {
    err = "Failed to update engine sampling rate.";
    return false;
  }

  const RuntimeSettingsSnapshot cur = runtimeSettings();
  for (const std::string& p : cur.udfPaths) {
    aux_remove_udfpath(ctx, p);
  }
  for (const std::string& p : settings.udfPaths) {
    if (!p.empty()) {
      aux_add_udfpath(ctx, p);
    }
  }

  return true;
}

bool AuxEngineFacade::hasDebugPauseInfo(auxDebugInfo& out) const {
  if (!paused_ || !activeCtx_) {
    return false;
  }
  if (aux_debug_get_pause_info(activeCtx_, out) != 0) {
    return false;
  }
  return true;
}

auxDebugAction AuxEngineFacade::debugResume(auxDebugAction action) {
  if (!rootCtx_) {
    return auxDebugAction::AUX_DEBUG_NO_DEBUG;
  }

  const auto r = aux_debug_resume(&activeCtx_, action);

  auxDebugInfo info{};
  if (aux_debug_get_pause_info(activeCtx_, info) == 0) {
    paused_ = true;
    pauseInfo_ = info;
    activeCtx_ = info.ctx ? *info.ctx : activeCtx_;
  } else {
    paused_ = false;
    activeCtx_ = rootCtx_;
  }
  return r;
}
