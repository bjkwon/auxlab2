#include "AuxEngineFacade.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace {
constexpr uint16_t kTypeString = 0x0030;
constexpr uint16_t kTypeCell = 0x1000;
constexpr uint16_t kTypeStrut = 0x2000;
constexpr uint16_t kTypeStruts = 0x4000;
constexpr double kRmsDbOffset = 3.0103;

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
  out.status = aux_eval(&activeCtx_, command, cfg_, preview);
  out.output = preview;

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

  data.channels.reserve(static_cast<size_t>(channels));
  for (int ch = 0; ch < channels; ++ch) {
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
