#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class ProfileStepType {
    Direct,
    Wait,
    Soak,
    RampTime,
    RampRate,
    Jump,
};

struct ProfileStep {
    ProfileStepType type = ProfileStepType::Direct;

    // direct/soak/ramp targets
    double setpointC = 0.0;

    // wait
    bool hasWaitTime = false;
    double waitTimeS = 0.0;
    bool hasPvTarget = false;
    double pvTargetC = 0.0;

    // soak
    double soakTimeS = 0.0;
    bool guaranteedSoak = false;
    double deviationC = 0.0;

    // ramp_time
    double rampTimeS = 0.0;

    // ramp_rate
    double rampRateCPerS = 0.0;

    // jump (1-based for external schema)
    int targetStepNumber = 1;
    int repeatCount = 0;
};

struct ProfileDefinition {
    int schemaVersion = 1;
    std::string name;
    std::string description;
    std::vector<ProfileStep> steps;
};

struct ProfileValidationError {
    int stepIndex = -1; // -1 means profile-level
    std::string field;
    std::string message;
};

struct ProfileSlotSummary {
    int slotIndex = 0;
    bool occupied = false;
    std::string name;
    std::size_t stepCount = 0;
};

struct ProfileRuntimeStatus {
    bool running = false;
    std::string name;
    std::string source = "none";
    int slotIndex = -1;
    int currentStepNumber = 0;
    std::string currentStepType = "none";
    double stepElapsedS = 0.0;
    double profileElapsedS = 0.0;
    std::string lastEndReason = "none";
};

enum class ProfileEndReason {
    None,
    Completed,
    CancelledByUser,
    ControllerStopped,
    TransitionGuard,
    StartFailed,
    InvalidProfile,
};

class ProfileEngine {
public:
    static ProfileEngine& getInstance();
    ProfileEngine(const ProfileEngine&) = delete;
    ProfileEngine& operator=(const ProfileEngine&) = delete;
    ProfileEngine(ProfileEngine&&) = delete;
    ProfileEngine& operator=(ProfileEngine&&) = delete;

    static constexpr int MAX_SLOTS = 5;
    static constexpr int MAX_STEPS = 40;
    static constexpr int SCHEMA_VERSION = 1;

    esp_err_t Initialize();

    std::vector<ProfileValidationError> ValidateProfile(const ProfileDefinition& profile) const;
    esp_err_t ParseProfileJson(const std::string& jsonText, ProfileDefinition& outProfile, std::vector<ProfileValidationError>& outErrors) const;
    std::string SerializeProfileJson(const ProfileDefinition& profile) const;

    esp_err_t SetUploadedProfile(const ProfileDefinition& profile, std::vector<ProfileValidationError>* outErrors = nullptr);
    std::optional<ProfileDefinition> GetUploadedProfile() const;
    void ClearUploadedProfile();

    std::array<ProfileSlotSummary, MAX_SLOTS> GetSlotSummaries() const;
    esp_err_t GetSlotProfile(int slotIndex, ProfileDefinition& outProfile) const;
    esp_err_t SaveProfileToSlot(int slotIndex, const ProfileDefinition& profile);
    esp_err_t DeleteSlotProfile(int slotIndex);

    esp_err_t StartFromUploaded();
    esp_err_t StartFromSlot(int slotIndex);
    esp_err_t CancelRunning(ProfileEndReason reason = ProfileEndReason::CancelledByUser);
    void Tick(double dtSeconds);

    ProfileRuntimeStatus GetRuntimeStatus() const;
    bool IsRunning() const;

private:
    ProfileEngine();
    static ProfileEngine* instance;

    mutable SemaphoreHandle_t stateMutex = nullptr;
    bool initialized = false;

    bool hasUploadedProfile = false;
    ProfileDefinition uploadedProfile;

    bool running = false;
    ProfileDefinition activeProfile;
    std::string activeSource = "none"; // uploaded|slot|none
    int activeSlotIndex = -1;
    int currentStepIndex = 0;
    double currentStepElapsedS = 0.0;
    double currentProfileElapsedS = 0.0;
    double currentStepStartSetpointC = 0.0;
    bool waitTimeLatched = false;
    bool waitPvLatched = false;
    double soakAccumulatedS = 0.0;
    std::unordered_map<int, int> jumpRemainingByStep;
    std::string lastEndReason = "none";

    bool IsValidSlotIndex(int slotIndex) const;
    esp_err_t LoadProfileFromSlotLocked(int slotIndex, ProfileDefinition& outProfile) const;
    esp_err_t SaveProfileToSlotLocked(int slotIndex, const ProfileDefinition& profile);
    esp_err_t DeleteSlotLocked(int slotIndex);

    bool EnterStepLocked(int stepIndex);
    bool ExecuteCurrentStepLocked(double dtSeconds, int& transitionsTaken);
    void ResetJumpCountersInRangeLocked(int startStepInclusive, int endStepExclusive);
    void EndRunLocked(ProfileEndReason reason, bool stopChamber);
    static const char* StepTypeToString(ProfileStepType type);
    static const char* EndReasonToString(ProfileEndReason reason);
};
