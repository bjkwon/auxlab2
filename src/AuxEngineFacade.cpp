#include "AuxEngineFacade.h"

#include <cmath>
#include <cstdio>
#include <atomic>
#include <cctype>
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

std::optional<SignalData> buildSignalDataFromAuxObj(AuxObj obj, int defaultSampleRate) {
  if (!obj) {
    return std::nullopt;
  }

  const int channels = aux_num_channels(obj);
  if (channels <= 0) {
    return std::nullopt;
  }

  struct SegmentCopy {
    int startSample = 0;
    std::vector<double> samples;
  };

  SignalData data;
  data.isAudio = aux_is_audio(obj);
  data.sampleRate = 0;

  double minStartMs = std::numeric_limits<double>::infinity();
  std::vector<std::vector<SegmentCopy>> byChannel(static_cast<size_t>(channels));

  for (int ch = 0; ch < channels; ++ch) {
    const int segCount = aux_num_segments(obj, ch);
    for (int segIndex = 0; segIndex < segCount; ++segIndex) {
      AuxSignal seg{};
      if (!aux_get_segment(obj, ch, segIndex, seg)) {
        continue;
      }
      minStartMs = std::min(minStartMs, seg.tmark);
      if (seg.fs > 0) {
        data.sampleRate = seg.fs;
      }

      SegmentCopy copy;
      copy.samples.resize(seg.nSamples);
      if (seg.buf && seg.nSamples > 0) {
        std::copy(seg.buf, seg.buf + seg.nSamples, copy.samples.begin());
      }
      byChannel[static_cast<size_t>(ch)].push_back(std::move(copy));
    }
  }

  if (!std::isfinite(minStartMs)) {
    return std::nullopt;
  }
  if (data.sampleRate <= 0) {
    data.sampleRate = defaultSampleRate > 0 ? defaultSampleRate : 1;
  }

  int globalTotalSamples = 0;
  for (int ch = 0; ch < channels; ++ch) {
    const int segCount = aux_num_segments(obj, ch);
    for (int segIndex = 0; segIndex < segCount; ++segIndex) {
      AuxSignal seg{};
      if (!aux_get_segment(obj, ch, segIndex, seg)) {
        continue;
      }
      const int startSample = std::max(0, static_cast<int>(std::llround((seg.tmark - minStartMs) * data.sampleRate / 1000.0)));
      byChannel[static_cast<size_t>(ch)][static_cast<size_t>(segIndex)].startSample = startSample;
      globalTotalSamples = std::max(globalTotalSamples, startSample + static_cast<int>(seg.nSamples));
    }
  }

  if (globalTotalSamples <= 0) {
    return std::nullopt;
  }

  data.channels.reserve(static_cast<size_t>(channels));
  for (int ch = 0; ch < channels; ++ch) {
    ChannelData channel;
    channel.samples.assign(static_cast<size_t>(globalTotalSamples), 0.0);
    for (const auto& seg : byChannel[static_cast<size_t>(ch)]) {
      if (seg.samples.empty()) {
        continue;
      }
      const size_t start = static_cast<size_t>(std::max(0, seg.startSample));
      if (start >= channel.samples.size()) {
        continue;
      }
      const size_t count = std::min(seg.samples.size(), channel.samples.size() - start);
      std::copy_n(seg.samples.begin(), count, channel.samples.begin() + static_cast<qsizetype>(start));
    }
    data.channels.push_back(std::move(channel));
  }

  if (data.isAudio) {
    data.startTimeSec = minStartMs / 1000.0;
  }
  return data;
}

namespace {
constexpr uint16_t kTypeString = 0x0030;
constexpr uint16_t kTypeByte = 0x0050;
constexpr uint16_t kTypeCell = 0x1000;
constexpr uint16_t kTypeStrut = 0x2000;
constexpr uint16_t kTypeHandle = 0x4000;
constexpr double kRmsDbOffset = 3.0103;

std::string filterCapturedNoise(std::string s) {
  if (s.empty()) {
    return s;
  }
  std::stringstream input(s);
  std::string line;
  std::string out;
  while (std::getline(input, line)) {
    if (line.find("TSMSendMessageToUIServer: CFMessagePortSendRequest FAILED(-1)") != std::string::npos) {
      continue;
    }
    if (!out.empty()) {
      out.push_back('\n');
    }
    out += line;
  }
  if (!out.empty() && s.back() == '\n') {
    out.push_back('\n');
  }
  return out;
}

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

bool isScalarShape(uint16_t type) {
  return (type & 0x000F) == 1;
}

std::string structFaceOnlyPreview(const std::string& preview) {
  std::string p = trimAscii(preview);
  if (p.rfind("{face}", 0) != 0) {
    return p;
  }
  const auto sep = p.find(';');
  const std::string firstSegment = sep == std::string::npos ? p.substr(6) : p.substr(6, sep - 6);
  return scalarOnlyPreview(firstSegment);
}

std::string shortTypeTag(uint16_t type) {
  if ((type & kTypeCell) != 0) {
    return "CELL";
  }
  if ((type & kTypeHandle) != 0) {
    return "HNDL";
  }
  if ((type & kTypeStrut) != 0) {
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

bool isCompositePath(const std::string& path) {
  return path.find('.') != std::string::npos || path.find('{') != std::string::npos;
}

bool isIdent(const std::string& s) {
  if (s.empty()) {
    return false;
  }
  const unsigned char c0 = static_cast<unsigned char>(s.front());
  if (!(std::isalpha(c0) || c0 == '_')) {
    return false;
  }
  for (size_t i = 1; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (!(std::isalnum(c) || c == '_')) {
      return false;
    }
  }
  return true;
}

std::string makeTempPathName() {
  static std::atomic<unsigned long long> counter{0};
  const unsigned long long id = counter.fetch_add(1, std::memory_order_relaxed) + 1;
  return "__auxlab2_tmp_path__" + std::to_string(id);
}

class ScopedPathBinding {
public:
  ScopedPathBinding() = default;
  ~ScopedPathBinding() { cleanup(); }

  ScopedPathBinding(const ScopedPathBinding&) = delete;
  ScopedPathBinding& operator=(const ScopedPathBinding&) = delete;

  bool bind(auxContext*& ctx, const std::string& path, const auxConfig& cfg) {
    cleanup();
    if (!ctx || path.empty()) {
      return false;
    }
    ctx_ = ctx;
    if (!isCompositePath(path)) {
      pathName_ = path;
      obj_ = aux_get_var(ctx_, pathName_);
      return obj_ != nullptr;
    }

    tmpName_ = makeTempPathName();
    std::string preview;
    if (aux_eval(&ctx, tmpName_ + "=" + path, cfg, preview) != 0) {
      ctx_ = ctx;
      tmpName_.clear();
      return false;
    }
    ctx_ = ctx;
    pathName_ = tmpName_;
    obj_ = aux_get_var(ctx_, pathName_);
    if (!obj_) {
      cleanup();
      return false;
    }
    return true;
  }

  bool bindDirect(auxContext*& ctx, AuxObj obj) {
    cleanup();
    ctx_ = ctx;
    obj_ = obj;
    return obj_ != nullptr;
  }

  AuxObj obj() const { return obj_; }
  const std::string& pathName() const { return pathName_; }

private:
  void cleanup() {
    if (ctx_ && !tmpName_.empty()) {
      aux_del_var(ctx_, tmpName_);
    }
    obj_ = nullptr;
    pathName_.clear();
    tmpName_.clear();
    ctx_ = nullptr;
  }

  auxContext* ctx_ = nullptr;
  AuxObj obj_ = nullptr;
  std::string pathName_;
  std::string tmpName_;
};

AuxObj resolveObjByPath(auxContext*& ctx, const std::string& path, const auxConfig& cfg, ScopedPathBinding& binding) {
  const auto dot = path.find('.');
  const auto brace = path.find('{');

  // Direct one-level struct-member resolution: root.member
  if (dot != std::string::npos && brace == std::string::npos) {
    const std::string root = path.substr(0, dot);
    const std::string member = path.substr(dot + 1);
    if (isIdent(root) && isIdent(member) && path.find('.', dot + 1) == std::string::npos) {
      auto members = aux_get_struct(ctx, root);
      auto it = members.find(member);
      if (it != members.end() && binding.bindDirect(ctx, it->second)) {
        return it->second;
      }
    }
  }

  // Direct one-level cell-member resolution: root{N}
  if (brace != std::string::npos && dot == std::string::npos) {
    const std::string root = path.substr(0, brace);
    if (isIdent(root) && path.back() == '}') {
      const size_t close = path.find('}', brace + 1);
      if (close == path.size() - 1) {
        const std::string idxText = path.substr(brace + 1, close - brace - 1);
        bool digits = !idxText.empty();
        for (char ch : idxText) {
          if (!std::isdigit(static_cast<unsigned char>(ch))) {
            digits = false;
            break;
          }
        }
        if (digits) {
          const size_t oneBased = static_cast<size_t>(std::stoul(idxText));
          if (oneBased > 0) {
            auto cells = aux_get_cell(ctx, root);
            const size_t idx = oneBased - 1;
            if (idx < cells.size() && binding.bindDirect(ctx, cells[idx])) {
              return cells[idx];
            }
          }
        }
      }
    }
  }

  if (!binding.bind(ctx, path, cfg)) {
    return nullptr;
  }
  return binding.obj();
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

bool AuxEngineFacade::installGraphicsBackend(const auxGraphicsBackend& backend, std::string& err) {
  if (!rootCtx_) {
    err = "AUX engine is not initialized.";
    return false;
  }
  if (aux_install_graphics_backend(rootCtx_, backend) != 0) {
    err = "Failed to install graphics backend.";
    return false;
  }
  return true;
}

bool AuxEngineFacade::installPlaybackBackend(const auxPlaybackBackend& backend, std::string& err) {
  if (!rootCtx_) {
    err = "AUX engine is not initialized.";
    return false;
  }
  if (aux_install_playback_backend(rootCtx_, backend) != 0) {
    err = "Failed to install playback backend.";
    return false;
  }
  return true;
}

void AuxEngineFacade::clearGraphicsBackend() {
  if (!rootCtx_) {
    return;
  }
  aux_clear_graphics_backend(rootCtx_);
}

void AuxEngineFacade::clearPlaybackBackend() {
  if (!rootCtx_) {
    return;
  }
  aux_clear_playback_backend(rootCtx_);
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
    captured = filterCapturedNoise(cap.output());
  }

  out.output = captured;
  if (!preview.empty()) {
    if (!out.output.empty() && out.output.back() != '\n') {
      out.output.push_back('\n');
    }
    out.output += preview;
  }

  auxDebugInfo info{};
  const bool hasPauseInfo = activeCtx_ && aux_debug_get_pause_info(activeCtx_, info) == 0;
  if (out.status == static_cast<int>(auxEvalStatus::AUX_EVAL_PAUSED) || hasPauseInfo) {
    paused_ = true;
    if (hasPauseInfo) {
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

int AuxEngineFacade::pollAsync() {
  if (!activeCtx_ && !rootCtx_) {
    return 0;
  }
  int changed = 0;
  if (activeCtx_) {
    changed += aux_poll_async(activeCtx_);
  }
  if (rootCtx_ && rootCtx_ != activeCtx_) {
    changed += aux_poll_async(rootCtx_);
  }
  return changed;
}

std::vector<VarSnapshot> AuxEngineFacade::listVariables() const {
  std::vector<VarSnapshot> vars;
  auxContext* ctx = paused_ ? activeCtx_ : rootCtx_;
  if (!ctx) {
    ctx = activeCtx_;
  }
  if (!ctx) {
    return vars;
  }

  auto names = aux_enum_vars(ctx);
  vars.reserve(names.size());
  for (const auto& name : names) {
    auto obj = aux_get_var(ctx, name);
    if (!obj) {
      continue;
    }

    VarSnapshot snap;
    snap.name = name;
    snap.isAudio = aux_is_audio(obj);
    snap.channels = aux_num_channels(obj);
    snap.type = aux_type(obj);
    snap.typeTag = shortTypeTag(snap.type);
    aux_describe_var(ctx, obj, cfg_, snap.type, snap.size, snap.preview);
    if (snap.typeTag == "SCLR" || (snap.typeTag == "HNDL" && isScalarShape(snap.type))) {
      snap.preview = scalarOnlyPreview(snap.preview);
    } else if (snap.typeTag == "STRC") {
      snap.preview = structFaceOnlyPreview(snap.preview);
    }
    if (snap.isAudio) {
      snap.rms = formatRmsDb(obj);
    }
    vars.push_back(std::move(snap));
  }
  return vars;
}

std::vector<VarSnapshot> AuxEngineFacade::listStructMembers(const std::string& path) const {
  std::vector<VarSnapshot> out;
  auxContext* ctx = paused_ ? activeCtx_ : rootCtx_;
  if (!ctx) {
    ctx = activeCtx_;
  }
  if (!ctx || path.empty()) {
    return out;
  }

  ScopedPathBinding binding;
  if (!binding.bind(ctx, path, cfg_)) {
    return out;
  }

  std::map<std::string, AuxObj> members = aux_get_struct(ctx, binding.pathName());
  out.reserve(members.size());
  for (const auto& kv : members) {
    if (!kv.second) {
      continue;
    }
    VarSnapshot snap;
    snap.name = kv.first;
    snap.type = aux_type(kv.second);
    snap.typeTag = shortTypeTag(snap.type);
    snap.isAudio = aux_is_audio(kv.second);
    snap.channels = aux_num_channels(kv.second);
    aux_describe_var(ctx, kv.second, cfg_, snap.type, snap.size, snap.preview);
    if (snap.typeTag == "SCLR" || (snap.typeTag == "HNDL" && isScalarShape(snap.type))) {
      snap.preview = scalarOnlyPreview(snap.preview);
    } else if (snap.typeTag == "STRC") {
      snap.preview = structFaceOnlyPreview(snap.preview);
    }
    if (snap.isAudio) {
      snap.rms = formatRmsDb(kv.second);
    }
    out.push_back(std::move(snap));
  }
  return out;
}

std::vector<VarSnapshot> AuxEngineFacade::listCellMembers(const std::string& path) const {
  std::vector<VarSnapshot> out;
  auxContext* ctx = paused_ ? activeCtx_ : rootCtx_;
  if (!ctx) {
    ctx = activeCtx_;
  }
  if (!ctx || path.empty()) {
    return out;
  }

  ScopedPathBinding binding;
  if (!binding.bind(ctx, path, cfg_)) {
    return out;
  }

  std::vector<AuxObj> cells = aux_get_cell(ctx, binding.pathName());
  out.reserve(cells.size());
  for (size_t i = 0; i < cells.size(); ++i) {
    const AuxObj& obj = cells[i];
    if (!obj) {
      continue;
    }
    VarSnapshot snap;
    snap.name = std::to_string(i + 1);
    snap.type = aux_type(obj);
    snap.typeTag = shortTypeTag(snap.type);
    snap.isAudio = aux_is_audio(obj);
    snap.channels = aux_num_channels(obj);
    aux_describe_var(ctx, obj, cfg_, snap.type, snap.size, snap.preview);
    if (snap.typeTag == "SCLR" || (snap.typeTag == "HNDL" && isScalarShape(snap.type))) {
      snap.preview = scalarOnlyPreview(snap.preview);
    } else if (snap.typeTag == "STRC") {
      snap.preview = structFaceOnlyPreview(snap.preview);
    }
    if (snap.isAudio) {
      snap.rms = formatRmsDb(obj);
    }
    out.push_back(std::move(snap));
  }
  return out;
}

std::optional<SignalData> AuxEngineFacade::getSignalData(const std::string& varName) const {
  auxContext* ctx = activeCtx_;
  if (!ctx) {
    return std::nullopt;
  }

  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
  if (!obj) {
    return std::nullopt;
  }
  return buildSignalDataFromAuxObj(obj, aux_get_fs(ctx));
}

std::optional<QVector<double>> AuxEngineFacade::getNumericVector(const std::string& varName) const {
  auxContext* ctx = activeCtx_;
  if (!ctx) {
    return std::nullopt;
  }

  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
  if (!obj) {
    return std::nullopt;
  }

  if (aux_num_channels(obj) != 1) {
    return std::nullopt;
  }

  const size_t len = aux_flatten_channel_length(obj, 0);
  if (len == 0) {
    return std::nullopt;
  }

  QVector<double> values(static_cast<qsizetype>(len));
  if (aux_flatten_channel(obj, 0, values.data(), len) != len) {
    return std::nullopt;
  }
  return values;
}

std::optional<double> AuxEngineFacade::getScalarValue(const std::string& varName) const {
  auxContext* ctx = activeCtx_;
  if (!ctx) {
    return std::nullopt;
  }

  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
  if (!obj) {
    return std::nullopt;
  }

  if (aux_num_channels(obj) != 1) {
    return std::nullopt;
  }
  const size_t len = aux_flatten_channel_length(obj, 0);
  if (len != 1) {
    return std::nullopt;
  }

  double value = 0.0;
  if (aux_flatten_channel(obj, 0, &value, 1) != 1) {
    return std::nullopt;
  }
  return value;
}

std::vector<std::vector<double>> AuxEngineFacade::getSignalFftPowerDb(const std::string& varName, int viewStart, int viewLen) const {
  std::vector<std::vector<double>> out;
  if (!activeCtx_) {
    return out;
  }

  auxContext* ctx = activeCtx_;
  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
  if (!obj) {
    return out;
  }

  const int channels = aux_num_channels(obj);
  if (channels <= 0) {
    return out;
  }

  int sampleRate = aux_get_fs(ctx);
  if (sampleRate <= 0) sampleRate = 1;
  int offsetSamples = 0;
  const auto sig = getSignalData(varName);
  if (sig && sig->isAudio && sig->sampleRate > 0) {
    sampleRate = sig->sampleRate;
    offsetSamples = std::max(0, static_cast<int>(std::llround(sig->startTimeSec * sampleRate)));
  }
  const int start = std::max(0, viewStart);
  const int len = std::max(1, viewLen);

  out.resize(static_cast<size_t>(channels));
  for (int ch = 0; ch < channels; ++ch) {
    std::vector<double> db;
    if (aux_fft_power_db(obj, ch, start, len, offsetSamples, db)) {
      out[static_cast<size_t>(ch)] = std::move(db);
    }
  }
  return out;
}

std::optional<uint16_t> AuxEngineFacade::getValueType(const std::string& varName) const {
  auxContext* ctx = activeCtx_;
  if (!ctx) {
    return std::nullopt;
  }

  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
  if (!obj) {
    return std::nullopt;
  }
  return aux_type(obj);
}

bool AuxEngineFacade::isStringVar(const std::string& varName) const {
  auxContext* ctx = activeCtx_;
  if (!ctx) {
    return false;
  }
  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
  if (!obj) {
    return false;
  }
  return isTextType(aux_type(obj));
}

bool AuxEngineFacade::isStructVar(const std::string& varName) const {
  auxContext* ctx = activeCtx_;
  if (!ctx || varName.empty()) {
    return false;
  }
  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
  if (!obj) {
    return false;
  }
  const uint16_t type = aux_type(obj);
  return (type & (kTypeStrut | kTypeHandle)) != 0;
}

bool AuxEngineFacade::isCellVar(const std::string& varName) const {
  auxContext* ctx = activeCtx_;
  if (!ctx || varName.empty()) {
    return false;
  }
  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
  if (!obj) {
    return false;
  }
  return (aux_type(obj) & kTypeCell) != 0;
}

bool AuxEngineFacade::isBinaryVar(const std::string& varName) const {
  auxContext* ctx = activeCtx_;
  if (!ctx) {
    return false;
  }
  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
  if (!obj) {
    return false;
  }
  return (aux_type(obj) & 0xFFF0) == kTypeByte;
}

std::optional<std::string> AuxEngineFacade::getStringValue(const std::string& varName) const {
  auxContext* ctx = activeCtx_;
  if (!ctx) {
    return std::nullopt;
  }
  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
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
  aux_describe_var(ctx, obj, cfg, type, size, preview);
  return preview;
}

std::optional<BinaryData> AuxEngineFacade::getBinaryData(const std::string& varName) const {
  auxContext* ctx = activeCtx_;
  if (!ctx) {
    return std::nullopt;
  }
  ScopedPathBinding binding;
  auto obj = resolveObjByPath(ctx, varName, cfg_, binding);
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

bool AuxEngineFacade::setHandleValues(const std::string& varName, const std::vector<std::uint64_t>& ids) {
  if (!activeCtx_ || varName.empty()) {
    return false;
  }
  return aux_set_handle_values(activeCtx_, varName, ids) == 0;
}

bool AuxEngineFacade::updateRuntimeHandleMembers(std::uint64_t handleId, const std::map<std::string, double>& members) {
  if (!activeCtx_ || handleId == 0 || members.empty()) {
    return false;
  }
  bool updated = false;
  if (aux_update_runtime_handle_members(activeCtx_, handleId, members) == 0) {
    updated = true;
  }
  if (rootCtx_ && rootCtx_ != activeCtx_ && aux_update_runtime_handle_members(rootCtx_, handleId, members) == 0) {
    updated = true;
  }
  return updated;
}

std::string AuxEngineFacade::engineVersion() const {
  auxContext* ctx = activeCtx_ ? activeCtx_ : rootCtx_;
  if (!ctx) {
    return {};
  }
  return aux_version(ctx);
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
  if (!paused_ || !activeCtx_) {
    return std::nullopt;
  }
  auxDebugInfo info{};
  if (aux_debug_get_pause_info(activeCtx_, info) == 0) {
    return info;
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
