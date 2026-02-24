#pragma once

#include <auxe/auxe.h>
#include <optional>
#include <set>
#include <string>
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

struct ChannelData {
  std::vector<double> samples;
};

struct SignalData {
  bool isAudio = false;
  int sampleRate = 0;
  double startTimeSec = 0.0;
  std::vector<ChannelData> channels;
};

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

class AuxEngineFacade {
public:
  AuxEngineFacade();
  ~AuxEngineFacade();

  bool init();
  EvalResult eval(const std::string& command);

  std::vector<VarSnapshot> listVariables() const;
  std::optional<SignalData> getSignalData(const std::string& varName) const;
  std::optional<BinaryData> getBinaryData(const std::string& varName) const;
  bool isBinaryVar(const std::string& varName) const;
  bool isStringVar(const std::string& varName) const;
  std::optional<std::string> getStringValue(const std::string& varName) const;
  bool loadUdfFile(const std::string& fullPath, std::string& err);
  bool setBreakpoint(const std::string& udfName, int line, bool enabled, std::string& err);
  std::set<int> getBreakpoints(const std::string& udfName) const;

  bool deleteVar(const std::string& varName);

  bool isPaused() const;
  auxContext* activeContext() const;
  auxContext* rootContext() const;
  std::optional<auxDebugInfo> pauseInfo() const;
  RuntimeSettingsSnapshot runtimeSettings() const;
  bool applyRuntimeSettings(const RuntimeSettingsSnapshot& settings, std::string& err);

  bool hasDebugPauseInfo(auxDebugInfo& out) const;
  auxDebugAction debugResume(auxDebugAction action);

private:
  auxConfig cfg_{};
  auxContext* rootCtx_ = nullptr;
  mutable auxContext* activeCtx_ = nullptr;
  bool paused_ = false;
  auxDebugInfo pauseInfo_{};
};
