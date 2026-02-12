#pragma once

#include <auxe/auxe.h>
#include <optional>
#include <string>
#include <vector>

struct VarSnapshot {
  std::string name;
  uint16_t type = 0;
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
  std::vector<ChannelData> channels;
};

class AuxEngineFacade {
public:
  AuxEngineFacade();
  ~AuxEngineFacade();

  bool init();
  EvalResult eval(const std::string& command);

  std::vector<VarSnapshot> listVariables() const;
  std::optional<SignalData> getSignalData(const std::string& varName) const;

  bool deleteVar(const std::string& varName);

  bool isPaused() const;
  auxContext* activeContext() const;
  auxContext* rootContext() const;
  std::optional<auxDebugInfo> pauseInfo() const;

  bool hasDebugPauseInfo(auxDebugInfo& out) const;
  auxDebugAction debugResume(auxDebugAction action);

private:
  auxConfig cfg_{};
  auxContext* rootCtx_ = nullptr;
  mutable auxContext* activeCtx_ = nullptr;
  bool paused_ = false;
  auxDebugInfo pauseInfo_{};
};
