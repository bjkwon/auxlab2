#pragma once

#include <auxe/auxe.h>
#include <QVector>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct VarSnapshot {
  std::string name;
  uint16_t type = 0;
  std::string typeTag;
  std::string size;
  std::string rms;
  std::string preview;
  bool isAudio = false;
  int channels = 0;
};

struct EvalResult {
  int status = 1;
  std::string output;
};

struct SignalSegment {
  int startSample = 0;
  int length = 0;
};

struct ChannelData {
  std::vector<double> samples;
  std::vector<double> imagSamples;
  std::vector<SignalSegment> segments;
};

struct SignalData {
  bool isAudio = false;
  bool isComplex = false;
  int sampleRate = 0;
  double startTimeSec = 0.0;
  std::vector<ChannelData> channels;
  // RMS in dB over the whole channel, computed once when the signal is loaded
  // so full-timeline RMS display doesn't need to re-scan the samples. One
  // entry per channel; +inf for an empty channel, -inf for pure silence.
  std::vector<double> fullRmsDb;
};

// Shared, immutable handle to a SignalData snapshot. Sharing this pointer
// through the display pipeline (rather than passing SignalData by value)
// avoids repeatedly deep-copying the sample buffers of large signals.
using SignalDataPtr = std::shared_ptr<const SignalData>;

struct BinaryData {
  std::vector<unsigned char> bytes;
};

struct RuntimeSettingsSnapshot {
  int sampleRate = 0;
  int displayPrecision = 0;
  int displayLimitX = 0;
  int displayLimitY = 0;
  int displayLimitBytes = 0;
  int displayLimitStr = 0;
  std::vector<std::string> udfPaths;
};

SignalDataPtr buildSignalDataFromAuxObj(AuxObj obj, int defaultSampleRate);

class AuxEngineFacade {
public:
  AuxEngineFacade();
  ~AuxEngineFacade();

  bool init();
  bool installGraphicsBackend(const auxGraphicsBackend& backend, std::string& err);
  bool installPlaybackBackend(const auxPlaybackBackend& backend, std::string& err);
  void clearGraphicsBackend();
  void clearPlaybackBackend();
  EvalResult eval(const std::string& command);
  int pollAsync();

  std::vector<VarSnapshot> listVariables() const;
  std::vector<VarSnapshot> listStructMembers(const std::string& path) const;
  std::vector<VarSnapshot> listCellMembers(const std::string& path) const;
  SignalDataPtr getSignalData(const std::string& varName) const;
  bool hasSignalData(const std::string& varName) const;
  std::optional<QVector<double>> getNumericVector(const std::string& varName) const;
  std::optional<double> getScalarValue(const std::string& varName) const;
  std::vector<std::vector<double>> getSignalFftPowerDb(const std::string& varName, int viewStart, int viewLen) const;
  std::optional<BinaryData> getBinaryData(const std::string& varName) const;
  std::optional<uint16_t> getValueType(const std::string& varName) const;
  bool isAudioVar(const std::string& varName) const;
  bool isBinaryVar(const std::string& varName) const;
  bool isStringVar(const std::string& varName) const;
  bool isStructVar(const std::string& varName) const;
  bool isCellVar(const std::string& varName) const;
  std::optional<std::string> getStringValue(const std::string& varName) const;
  bool loadUdfFile(const std::string& fullPath, std::string& err);
  bool reloadUdfByName(const std::string& udfName, std::string& err);
  bool setBreakpoint(const std::string& udfName, int line, bool enabled, std::string& err);
  std::set<int> getBreakpoints(const std::string& udfName) const;

  bool deleteVar(const std::string& varName);
  bool setHandleValues(const std::string& varName, const std::vector<std::uint64_t>& ids);
  bool updateRuntimeHandleMembers(std::uint64_t handleId, const std::map<std::string, double>& members);
  bool invokeRecordCallback(std::uint64_t sessionId, const std::string& callbackName, const auxRecordCallbackPayload& payload, std::string& output);
  bool attachRecordCallbackOutputsToHandle(std::uint64_t sessionId, std::uint64_t handleId);
  std::string engineVersion() const;

  bool isPaused() const;
  auxContext* activeContext() const;
  auxContext* rootContext() const;
  std::optional<auxDebugInfo> pauseInfo() const;
  RuntimeSettingsSnapshot runtimeSettings() const;
  bool applyRuntimeSettings(const RuntimeSettingsSnapshot& settings, std::string& err);

  bool hasDebugPauseInfo(auxDebugInfo& out) const;
  auxDebugAction debugResume(auxDebugAction action, std::string* output = nullptr);

private:
  std::string cachedRmsForObj(const AuxObj& obj) const;

  auxConfig cfg_{};
  auxContext* rootCtx_ = nullptr;
  mutable auxContext* activeCtx_ = nullptr;
  bool paused_ = false;
  auxDebugInfo pauseInfo_{};
  // Per-channel flattened-sample counts + formatted RMS, keyed by object
  // identity, so refreshVariables() doesn't rescan every audio object's
  // samples on every call. A per-channel length mismatch (reassignment,
  // in-place edit that changes length) forces recomputation for that
  // object; entries for since-deleted objects just go unused.
  mutable std::map<AuxObj, std::pair<std::vector<size_t>, std::string>> rmsCache_;
  // Same idea as rmsCache_, but for the built SignalData snapshot itself:
  // reconcileScopedWindows() calls getSignalData() for every open plot
  // window after every console command, so without this cache an unrelated
  // command would re-copy and re-scan a large signal's samples every time.
  mutable std::map<AuxObj, std::pair<std::vector<size_t>, SignalDataPtr>> signalDataCache_;
};
