#include "ProfileEngine.hpp"

#include "Controller.hpp"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr double kMinSetpointC = 0.0;
constexpr double kMaxSetpointC = 300.0;
constexpr double kPvToleranceC = 1.0;
constexpr int kMaxTransitionsPerTick = 256;
constexpr const char* kNvsPartition = "nvs";
constexpr const char* kNvsNamespace = "profiles";

class ScopedLock {
public:
    explicit ScopedLock(SemaphoreHandle_t mutex)
        : mutex_(mutex), locked_(false) {
        if (mutex_ != nullptr) {
            locked_ = (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE);
        }
    }

    ~ScopedLock() {
        if (locked_ && mutex_ != nullptr) {
            xSemaphoreGive(mutex_);
        }
    }

    bool Locked() const { return locked_; }

private:
    SemaphoreHandle_t mutex_;
    bool locked_;
};

bool BuildSlotBlobKey(int slotIndex, char* outKey, std::size_t keyLen) {
    if (outKey == nullptr || keyLen < 10 || slotIndex < 0 || slotIndex >= ProfileEngine::MAX_SLOTS) {
        return false;
    }
    std::snprintf(outKey, keyLen, "slot%d_blob", slotIndex);
    return true;
}

bool BuildSlotNameKey(int slotIndex, char* outKey, std::size_t keyLen) {
    if (outKey == nullptr || keyLen < 10 || slotIndex < 0 || slotIndex >= ProfileEngine::MAX_SLOTS) {
        return false;
    }
    std::snprintf(outKey, keyLen, "slot%d_name", slotIndex);
    return true;
}

esp_err_t OpenProfilesNvs(nvs_handle_t& outHandle) {
    esp_err_t err = nvs_flash_init_partition(kNvsPartition);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase_partition(kNvsPartition));
        err = nvs_flash_init_partition(kNvsPartition);
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    return nvs_open_from_partition(kNvsPartition, kNvsNamespace, NVS_READWRITE, &outHandle);
}

std::optional<ProfileStepType> StepTypeFromString(const std::string& type) {
    if (type == "direct") return ProfileStepType::Direct;
    if (type == "wait") return ProfileStepType::Wait;
    if (type == "soak") return ProfileStepType::Soak;
    if (type == "ramp_time") return ProfileStepType::RampTime;
    if (type == "ramp_rate") return ProfileStepType::RampRate;
    if (type == "jump") return ProfileStepType::Jump;
    return std::nullopt;
}

bool ReadOptionalNumber(cJSON* parent, const char* field, bool& hasValue, double& outValue) {
    hasValue = false;
    cJSON* item = cJSON_GetObjectItem(parent, field);
    if (item == nullptr) {
        return true;
    }
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    hasValue = true;
    outValue = item->valuedouble;
    return true;
}

void AddValidationError(std::vector<ProfileValidationError>& errors, int stepIndex, const char* field, const char* message) {
    ProfileValidationError err;
    err.stepIndex = stepIndex;
    err.field = (field != nullptr) ? field : "";
    err.message = (message != nullptr) ? message : "Validation error";
    errors.push_back(err);
}
}

ProfileEngine* ProfileEngine::instance = nullptr;

ProfileEngine& ProfileEngine::getInstance() {
    if (instance == nullptr) {
        instance = new ProfileEngine();
    }
    return *instance;
}

ProfileEngine::ProfileEngine() {
    stateMutex = xSemaphoreCreateMutex();
}

esp_err_t ProfileEngine::Initialize() {
    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (initialized) {
        return ESP_OK;
    }

    initialized = true;
    return ESP_OK;
}

std::vector<ProfileValidationError> ProfileEngine::ValidateProfile(const ProfileDefinition& profile) const {
    std::vector<ProfileValidationError> errors;

    if (profile.name.empty()) {
        AddValidationError(errors, -1, "name", "name is required");
    }

    if (profile.steps.empty()) {
        AddValidationError(errors, -1, "steps", "steps must not be empty");
        return errors;
    }

    if (profile.steps.size() > static_cast<std::size_t>(MAX_STEPS)) {
        AddValidationError(errors, -1, "steps", "too many steps");
    }

    const int stepCount = static_cast<int>(profile.steps.size());
    for (int stepIndex = 0; stepIndex < stepCount; ++stepIndex) {
        const ProfileStep& step = profile.steps[static_cast<std::size_t>(stepIndex)];
        switch (step.type) {
            case ProfileStepType::Direct:
                if (step.setpointC < kMinSetpointC || step.setpointC > kMaxSetpointC) {
                    AddValidationError(errors, stepIndex, "setpoint_c", "direct setpoint must be within [0,300]");
                }
                break;

            case ProfileStepType::Wait:
                if (!step.hasWaitTime && !step.hasPvTarget) {
                    AddValidationError(errors, stepIndex, "wait", "wait requires wait_time_s and/or pv_target_c");
                }
                if (step.hasWaitTime && step.waitTimeS <= 0.0) {
                    AddValidationError(errors, stepIndex, "wait_time_s", "wait_time_s must be > 0");
                }
                break;

            case ProfileStepType::Soak:
                if (step.setpointC < kMinSetpointC || step.setpointC > kMaxSetpointC) {
                    AddValidationError(errors, stepIndex, "setpoint_c", "soak setpoint must be within [0,300]");
                }
                if (step.soakTimeS <= 0.0) {
                    AddValidationError(errors, stepIndex, "soak_time_s", "soak_time_s must be > 0");
                }
                if (step.guaranteedSoak && step.deviationC <= 0.0) {
                    AddValidationError(errors, stepIndex, "deviation_c", "deviation_c must be > 0 when guaranteed is true");
                }
                break;

            case ProfileStepType::RampTime:
                if (step.setpointC < kMinSetpointC || step.setpointC > kMaxSetpointC) {
                    AddValidationError(errors, stepIndex, "setpoint_c", "ramp_time setpoint must be within [0,300]");
                }
                if (step.rampTimeS <= 0.0) {
                    AddValidationError(errors, stepIndex, "ramp_time_s", "ramp_time_s must be > 0");
                }
                break;

            case ProfileStepType::RampRate:
                if (step.setpointC < kMinSetpointC || step.setpointC > kMaxSetpointC) {
                    AddValidationError(errors, stepIndex, "setpoint_c", "ramp_rate setpoint must be within [0,300]");
                }
                if (step.rampRateCPerS <= 0.0) {
                    AddValidationError(errors, stepIndex, "ramp_rate_c_per_s", "ramp_rate_c_per_s must be > 0");
                }
                break;

            case ProfileStepType::Jump:
                if (step.targetStepNumber < 1 || step.targetStepNumber > stepCount) {
                    AddValidationError(errors, stepIndex, "target_step_number", "target_step_number out of range");
                } else if (step.targetStepNumber >= (stepIndex + 1)) {
                    AddValidationError(errors, stepIndex, "target_step_number", "jump target must be backward");
                }
                if (step.repeatCount < 0) {
                    AddValidationError(errors, stepIndex, "repeat_count", "repeat_count must be >= 0");
                }
                break;
        }
    }

    return errors;
}

esp_err_t ProfileEngine::ParseProfileJson(const std::string& jsonText, ProfileDefinition& outProfile, std::vector<ProfileValidationError>& outErrors) const {
    outErrors.clear();

    cJSON* root = cJSON_Parse(jsonText.c_str());
    if (root == nullptr) {
        AddValidationError(outErrors, -1, "json", "invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        AddValidationError(outErrors, -1, "json", "profile must be a JSON object");
        return ESP_ERR_INVALID_ARG;
    }

    ProfileDefinition parsed;

    cJSON* schemaVersion = cJSON_GetObjectItem(root, "schema_version");
    if (schemaVersion == nullptr) {
        parsed.schemaVersion = SCHEMA_VERSION;
    } else if (cJSON_IsNumber(schemaVersion)) {
        parsed.schemaVersion = schemaVersion->valueint;
    } else {
        cJSON_Delete(root);
        AddValidationError(outErrors, -1, "schema_version", "schema_version must be numeric");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON* name = cJSON_GetObjectItem(root, "name");
    if (!cJSON_IsString(name)) {
        cJSON_Delete(root);
        AddValidationError(outErrors, -1, "name", "name must be a string");
        return ESP_ERR_INVALID_ARG;
    }
    parsed.name = name->valuestring;

    cJSON* description = cJSON_GetObjectItem(root, "description");
    if (description != nullptr && !cJSON_IsString(description)) {
        cJSON_Delete(root);
        AddValidationError(outErrors, -1, "description", "description must be a string");
        return ESP_ERR_INVALID_ARG;
    }
    parsed.description = (description != nullptr && description->valuestring != nullptr) ? description->valuestring : "";

    cJSON* steps = cJSON_GetObjectItem(root, "steps");
    if (!cJSON_IsArray(steps)) {
        cJSON_Delete(root);
        AddValidationError(outErrors, -1, "steps", "steps must be an array");
        return ESP_ERR_INVALID_ARG;
    }

    const int stepCount = cJSON_GetArraySize(steps);
    parsed.steps.reserve(static_cast<std::size_t>(std::max(0, stepCount)));

    for (int i = 0; i < stepCount; ++i) {
        cJSON* rawStep = cJSON_GetArrayItem(steps, i);
        if (!cJSON_IsObject(rawStep)) {
            cJSON_Delete(root);
            AddValidationError(outErrors, i, "step", "step must be an object");
            return ESP_ERR_INVALID_ARG;
        }

        cJSON* type = cJSON_GetObjectItem(rawStep, "type");
        if (!cJSON_IsString(type)) {
            cJSON_Delete(root);
            AddValidationError(outErrors, i, "type", "type must be a string");
            return ESP_ERR_INVALID_ARG;
        }

        const std::optional<ProfileStepType> stepType = StepTypeFromString(type->valuestring != nullptr ? type->valuestring : "");
        if (!stepType.has_value()) {
            cJSON_Delete(root);
            AddValidationError(outErrors, i, "type", "unknown step type");
            return ESP_ERR_INVALID_ARG;
        }

        ProfileStep step;
        step.type = stepType.value();

        switch (step.type) {
            case ProfileStepType::Direct: {
                cJSON* setpoint = cJSON_GetObjectItem(rawStep, "setpoint_c");
                if (!cJSON_IsNumber(setpoint)) {
                    cJSON_Delete(root);
                    AddValidationError(outErrors, i, "setpoint_c", "direct step requires numeric setpoint_c");
                    return ESP_ERR_INVALID_ARG;
                }
                step.setpointC = setpoint->valuedouble;
                break;
            }

            case ProfileStepType::Wait: {
                double waitTime = 0.0;
                double pvTarget = 0.0;
                bool hasWait = false;
                bool hasPv = false;
                if (!ReadOptionalNumber(rawStep, "wait_time_s", hasWait, waitTime) ||
                    !ReadOptionalNumber(rawStep, "pv_target_c", hasPv, pvTarget)) {
                    cJSON_Delete(root);
                    AddValidationError(outErrors, i, "wait", "wait_time_s and pv_target_c must be numeric if present");
                    return ESP_ERR_INVALID_ARG;
                }
                step.hasWaitTime = hasWait;
                step.waitTimeS = waitTime;
                step.hasPvTarget = hasPv;
                step.pvTargetC = pvTarget;
                break;
            }

            case ProfileStepType::Soak: {
                cJSON* setpoint = cJSON_GetObjectItem(rawStep, "setpoint_c");
                cJSON* soakTime = cJSON_GetObjectItem(rawStep, "soak_time_s");
                if (!cJSON_IsNumber(setpoint) || !cJSON_IsNumber(soakTime)) {
                    cJSON_Delete(root);
                    AddValidationError(outErrors, i, "soak", "soak step requires setpoint_c and soak_time_s");
                    return ESP_ERR_INVALID_ARG;
                }

                step.setpointC = setpoint->valuedouble;
                step.soakTimeS = soakTime->valuedouble;

                cJSON* guaranteed = cJSON_GetObjectItem(rawStep, "guaranteed");
                if (guaranteed != nullptr) {
                    if (!cJSON_IsBool(guaranteed)) {
                        cJSON_Delete(root);
                        AddValidationError(outErrors, i, "guaranteed", "guaranteed must be a boolean");
                        return ESP_ERR_INVALID_ARG;
                    }
                    step.guaranteedSoak = cJSON_IsTrue(guaranteed);
                }

                cJSON* deviation = cJSON_GetObjectItem(rawStep, "deviation_c");
                if (deviation != nullptr) {
                    if (!cJSON_IsNumber(deviation)) {
                        cJSON_Delete(root);
                        AddValidationError(outErrors, i, "deviation_c", "deviation_c must be numeric");
                        return ESP_ERR_INVALID_ARG;
                    }
                    step.deviationC = deviation->valuedouble;
                }
                break;
            }

            case ProfileStepType::RampTime: {
                cJSON* setpoint = cJSON_GetObjectItem(rawStep, "setpoint_c");
                cJSON* rampTime = cJSON_GetObjectItem(rawStep, "ramp_time_s");
                if (!cJSON_IsNumber(setpoint) || !cJSON_IsNumber(rampTime)) {
                    cJSON_Delete(root);
                    AddValidationError(outErrors, i, "ramp_time", "ramp_time step requires setpoint_c and ramp_time_s");
                    return ESP_ERR_INVALID_ARG;
                }

                step.setpointC = setpoint->valuedouble;
                step.rampTimeS = rampTime->valuedouble;
                break;
            }

            case ProfileStepType::RampRate: {
                cJSON* setpoint = cJSON_GetObjectItem(rawStep, "setpoint_c");
                cJSON* rampRate = cJSON_GetObjectItem(rawStep, "ramp_rate_c_per_s");
                if (!cJSON_IsNumber(setpoint) || !cJSON_IsNumber(rampRate)) {
                    cJSON_Delete(root);
                    AddValidationError(outErrors, i, "ramp_rate", "ramp_rate step requires setpoint_c and ramp_rate_c_per_s");
                    return ESP_ERR_INVALID_ARG;
                }

                step.setpointC = setpoint->valuedouble;
                step.rampRateCPerS = rampRate->valuedouble;
                break;
            }

            case ProfileStepType::Jump: {
                cJSON* target = cJSON_GetObjectItem(rawStep, "target_step_number");
                cJSON* repeatCount = cJSON_GetObjectItem(rawStep, "repeat_count");
                if (!cJSON_IsNumber(target) || !cJSON_IsNumber(repeatCount)) {
                    cJSON_Delete(root);
                    AddValidationError(outErrors, i, "jump", "jump step requires target_step_number and repeat_count");
                    return ESP_ERR_INVALID_ARG;
                }

                step.targetStepNumber = target->valueint;
                step.repeatCount = repeatCount->valueint;
                break;
            }
        }

        parsed.steps.push_back(step);
    }

    cJSON_Delete(root);

    outErrors = ValidateProfile(parsed);
    if (!outErrors.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    outProfile = parsed;
    return ESP_OK;
}

std::string ProfileEngine::SerializeProfileJson(const ProfileDefinition& profile) const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema_version", profile.schemaVersion);
    cJSON_AddStringToObject(root, "name", profile.name.c_str());
    cJSON_AddStringToObject(root, "description", profile.description.c_str());

    cJSON* steps = cJSON_CreateArray();
    for (const ProfileStep& step : profile.steps) {
        cJSON* stepObj = cJSON_CreateObject();
        cJSON_AddStringToObject(stepObj, "type", StepTypeToString(step.type));

        switch (step.type) {
            case ProfileStepType::Direct:
                cJSON_AddNumberToObject(stepObj, "setpoint_c", step.setpointC);
                break;
            case ProfileStepType::Wait:
                if (step.hasWaitTime) {
                    cJSON_AddNumberToObject(stepObj, "wait_time_s", step.waitTimeS);
                }
                if (step.hasPvTarget) {
                    cJSON_AddNumberToObject(stepObj, "pv_target_c", step.pvTargetC);
                }
                break;
            case ProfileStepType::Soak:
                cJSON_AddNumberToObject(stepObj, "setpoint_c", step.setpointC);
                cJSON_AddNumberToObject(stepObj, "soak_time_s", step.soakTimeS);
                if (step.guaranteedSoak) {
                    cJSON_AddBoolToObject(stepObj, "guaranteed", step.guaranteedSoak);
                    cJSON_AddNumberToObject(stepObj, "deviation_c", step.deviationC);
                }
                break;
            case ProfileStepType::RampTime:
                cJSON_AddNumberToObject(stepObj, "setpoint_c", step.setpointC);
                cJSON_AddNumberToObject(stepObj, "ramp_time_s", step.rampTimeS);
                break;
            case ProfileStepType::RampRate:
                cJSON_AddNumberToObject(stepObj, "setpoint_c", step.setpointC);
                cJSON_AddNumberToObject(stepObj, "ramp_rate_c_per_s", step.rampRateCPerS);
                break;
            case ProfileStepType::Jump:
                cJSON_AddNumberToObject(stepObj, "target_step_number", step.targetStepNumber);
                cJSON_AddNumberToObject(stepObj, "repeat_count", step.repeatCount);
                break;
        }

        cJSON_AddItemToArray(steps, stepObj);
    }
    cJSON_AddItemToObject(root, "steps", steps);

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed == nullptr) {
        return "";
    }

    std::string out(printed);
    cJSON_free(printed);
    return out;
}

esp_err_t ProfileEngine::SetUploadedProfile(const ProfileDefinition& profile, std::vector<ProfileValidationError>* outErrors) {
    const std::vector<ProfileValidationError> errors = ValidateProfile(profile);
    if (!errors.empty()) {
        if (outErrors != nullptr) {
            *outErrors = errors;
        }
        return ESP_ERR_INVALID_ARG;
    }

    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    uploadedProfile = profile;
    hasUploadedProfile = true;
    if (outErrors != nullptr) {
        outErrors->clear();
    }

    return ESP_OK;
}

std::optional<ProfileDefinition> ProfileEngine::GetUploadedProfile() const {
    ScopedLock lock(stateMutex);
    if (!lock.Locked() || !hasUploadedProfile) {
        return std::nullopt;
    }
    return uploadedProfile;
}

void ProfileEngine::ClearUploadedProfile() {
    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return;
    }

    hasUploadedProfile = false;
    uploadedProfile = ProfileDefinition{};
}

bool ProfileEngine::IsValidSlotIndex(int slotIndex) const {
    return slotIndex >= 0 && slotIndex < MAX_SLOTS;
}

esp_err_t ProfileEngine::LoadProfileFromSlotLocked(int slotIndex, ProfileDefinition& outProfile) const {
    if (!IsValidSlotIndex(slotIndex)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = OpenProfilesNvs(handle);
    if (err != ESP_OK) {
        return err;
    }

    char blobKey[16] = {};
    if (!BuildSlotBlobKey(slotIndex, blobKey, sizeof(blobKey))) {
        nvs_close(handle);
        return ESP_ERR_INVALID_ARG;
    }

    std::size_t blobSize = 0;
    err = nvs_get_blob(handle, blobKey, nullptr, &blobSize);
    if (err != ESP_OK) {
        nvs_close(handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_ERR_NOT_FOUND;
        }
        return err;
    }

    std::string jsonBlob(blobSize, '\0');
    err = nvs_get_blob(handle, blobKey, jsonBlob.data(), &blobSize);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    std::vector<ProfileValidationError> parseErrors;
    err = ParseProfileJson(jsonBlob, outProfile, parseErrors);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ProfileEngine::SaveProfileToSlotLocked(int slotIndex, const ProfileDefinition& profile) {
    if (!IsValidSlotIndex(slotIndex)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = OpenProfilesNvs(handle);
    if (err != ESP_OK) {
        return err;
    }

    char blobKey[16] = {};
    char nameKey[16] = {};
    if (!BuildSlotBlobKey(slotIndex, blobKey, sizeof(blobKey)) ||
        !BuildSlotNameKey(slotIndex, nameKey, sizeof(nameKey))) {
        nvs_close(handle);
        return ESP_ERR_INVALID_ARG;
    }

    std::size_t existingSize = 0;
    err = nvs_get_blob(handle, blobKey, nullptr, &existingSize);
    if (err == ESP_OK) {
        nvs_close(handle);
        return ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    const std::string jsonBlob = SerializeProfileJson(profile);
    if (jsonBlob.empty()) {
        nvs_close(handle);
        return ESP_FAIL;
    }

    err = nvs_set_blob(handle, blobKey, jsonBlob.data(), jsonBlob.size());
    if (err == ESP_OK) {
        err = nvs_set_str(handle, nameKey, profile.name.c_str());
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t ProfileEngine::DeleteSlotLocked(int slotIndex) {
    if (!IsValidSlotIndex(slotIndex)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = OpenProfilesNvs(handle);
    if (err != ESP_OK) {
        return err;
    }

    char blobKey[16] = {};
    char nameKey[16] = {};
    if (!BuildSlotBlobKey(slotIndex, blobKey, sizeof(blobKey)) ||
        !BuildSlotNameKey(slotIndex, nameKey, sizeof(nameKey))) {
        nvs_close(handle);
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_erase_key(handle, blobKey);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    err = nvs_erase_key(handle, nameKey);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

std::array<ProfileSlotSummary, ProfileEngine::MAX_SLOTS> ProfileEngine::GetSlotSummaries() const {
    std::array<ProfileSlotSummary, MAX_SLOTS> out{};

    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return out;
    }

    for (int slot = 0; slot < MAX_SLOTS; ++slot) {
        out[static_cast<std::size_t>(slot)].slotIndex = slot;

        ProfileDefinition profile;
        const esp_err_t err = LoadProfileFromSlotLocked(slot, profile);
        if (err == ESP_OK) {
            out[static_cast<std::size_t>(slot)].occupied = true;
            out[static_cast<std::size_t>(slot)].name = profile.name;
            out[static_cast<std::size_t>(slot)].stepCount = profile.steps.size();
        }
    }

    return out;
}

esp_err_t ProfileEngine::GetSlotProfile(int slotIndex, ProfileDefinition& outProfile) const {
    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return LoadProfileFromSlotLocked(slotIndex, outProfile);
}

esp_err_t ProfileEngine::SaveProfileToSlot(int slotIndex, const ProfileDefinition& profile) {
    const std::vector<ProfileValidationError> errors = ValidateProfile(profile);
    if (!errors.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return SaveProfileToSlotLocked(slotIndex, profile);
}

esp_err_t ProfileEngine::DeleteSlotProfile(int slotIndex) {
    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return DeleteSlotLocked(slotIndex);
}

bool ProfileEngine::EnterStepLocked(int stepIndex) {
    if (stepIndex < 0 || stepIndex >= static_cast<int>(activeProfile.steps.size())) {
        return false;
    }

    currentStepIndex = stepIndex;
    currentStepElapsedS = 0.0;
    waitTimeLatched = false;
    waitPvLatched = false;
    soakAccumulatedS = 0.0;
    currentStepStartSetpointC = Controller::getInstance().GetSetPoint();
    return true;
}

void ProfileEngine::ResetJumpCountersInRangeLocked(int startStepInclusive, int endStepExclusive) {
    const int start = std::max(0, startStepInclusive);
    const int end = std::min(static_cast<int>(activeProfile.steps.size()), endStepExclusive);
    for (int idx = start; idx < end; ++idx) {
        const ProfileStep& step = activeProfile.steps[static_cast<std::size_t>(idx)];
        if (step.type == ProfileStepType::Jump) {
            jumpRemainingByStep[idx] = step.repeatCount;
        }
    }
}

bool ProfileEngine::ExecuteCurrentStepLocked(double dtSeconds, int& transitionsTaken) {
    if (currentStepIndex < 0 || currentStepIndex >= static_cast<int>(activeProfile.steps.size())) {
        return false;
    }

    ProfileStep& step = activeProfile.steps[static_cast<std::size_t>(currentStepIndex)];

    currentStepElapsedS += std::max(0.0, dtSeconds);
    currentProfileElapsedS += std::max(0.0, dtSeconds);

    bool advance = false;
    int nextStepIndex = currentStepIndex + 1;

    switch (step.type) {
        case ProfileStepType::Direct: {
            (void)Controller::getInstance().SetSetPointFromProfile(step.setpointC);
            advance = true;
            break;
        }

        case ProfileStepType::Wait: {
            if (step.hasWaitTime && !waitTimeLatched && currentStepElapsedS >= step.waitTimeS) {
                waitTimeLatched = true;
            }
            if (step.hasPvTarget && !waitPvLatched) {
                const double pv = Controller::getInstance().GetProcessValue();
                if (std::abs(pv - step.pvTargetC) <= kPvToleranceC) {
                    waitPvLatched = true;
                }
            }

            const bool timeSatisfied = (!step.hasWaitTime) || waitTimeLatched;
            const bool pvSatisfied = (!step.hasPvTarget) || waitPvLatched;
            advance = timeSatisfied && pvSatisfied;
            break;
        }

        case ProfileStepType::Soak: {
            (void)Controller::getInstance().SetSetPointFromProfile(step.setpointC);
            if (!step.guaranteedSoak) {
                soakAccumulatedS += std::max(0.0, dtSeconds);
            } else {
                const double pv = Controller::getInstance().GetProcessValue();
                if (std::abs(pv - step.setpointC) <= step.deviationC) {
                    soakAccumulatedS += std::max(0.0, dtSeconds);
                }
            }
            advance = soakAccumulatedS >= step.soakTimeS;
            break;
        }

        case ProfileStepType::RampTime: {
            const double duration = std::max(0.001, step.rampTimeS);
            const double progress = std::clamp(currentStepElapsedS / duration, 0.0, 1.0);
            const double setpoint = currentStepStartSetpointC + (step.setpointC - currentStepStartSetpointC) * progress;
            (void)Controller::getInstance().SetSetPointFromProfile(setpoint);
            advance = currentStepElapsedS >= duration;
            break;
        }

        case ProfileStepType::RampRate: {
            const double delta = step.setpointC - currentStepStartSetpointC;
            const double duration = std::max(std::abs(delta) / std::max(step.rampRateCPerS, 0.001), 0.001);
            const double progress = std::clamp(currentStepElapsedS / duration, 0.0, 1.0);
            const double setpoint = currentStepStartSetpointC + delta * progress;
            (void)Controller::getInstance().SetSetPointFromProfile(setpoint);
            advance = currentStepElapsedS >= duration;
            break;
        }

        case ProfileStepType::Jump: {
            int& remaining = jumpRemainingByStep[currentStepIndex];
            if (remaining > 0) {
                remaining -= 1;
                nextStepIndex = step.targetStepNumber - 1;
                ResetJumpCountersInRangeLocked(nextStepIndex, currentStepIndex);
                advance = true;
            } else {
                // Reset for potential outer-loop re-entry.
                remaining = step.repeatCount;
                advance = true;
            }
            break;
        }
    }

    if (!advance) {
        return true;
    }

    transitionsTaken += 1;
    if (transitionsTaken > kMaxTransitionsPerTick) {
        EndRunLocked(ProfileEndReason::TransitionGuard, true);
        return false;
    }

    if (nextStepIndex >= static_cast<int>(activeProfile.steps.size())) {
        EndRunLocked(ProfileEndReason::Completed, true);
        return false;
    }

    if (!EnterStepLocked(nextStepIndex)) {
        EndRunLocked(ProfileEndReason::InvalidProfile, true);
        return false;
    }

    // Continue evaluating immediate transitions this tick.
    return true;
}

void ProfileEngine::EndRunLocked(ProfileEndReason reason, bool stopChamber) {
    const bool wasRunning = running;
    running = false;
    lastEndReason = EndReasonToString(reason);

    activeProfile = ProfileDefinition{};
    activeSource = "none";
    activeSlotIndex = -1;
    currentStepIndex = 0;
    currentStepElapsedS = 0.0;
    currentProfileElapsedS = 0.0;
    currentStepStartSetpointC = 0.0;
    waitTimeLatched = false;
    waitPvLatched = false;
    soakAccumulatedS = 0.0;
    jumpRemainingByStep.clear();

    Controller::getInstance().SetProfileSetpointLock(false);

    if (stopChamber && wasRunning && Controller::getInstance().IsRunning()) {
        (void)Controller::getInstance().Stop();
    }
}

esp_err_t ProfileEngine::StartFromUploaded() {
    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!hasUploadedProfile) {
        return ESP_ERR_NOT_FOUND;
    }

    const std::vector<ProfileValidationError> errors = ValidateProfile(uploadedProfile);
    if (!errors.empty()) {
        lastEndReason = EndReasonToString(ProfileEndReason::InvalidProfile);
        return ESP_ERR_INVALID_ARG;
    }

    activeProfile = uploadedProfile;
    activeSource = "uploaded";
    activeSlotIndex = -1;

    jumpRemainingByStep.clear();
    for (int idx = 0; idx < static_cast<int>(activeProfile.steps.size()); ++idx) {
        const ProfileStep& step = activeProfile.steps[static_cast<std::size_t>(idx)];
        if (step.type == ProfileStepType::Jump) {
            jumpRemainingByStep[idx] = step.repeatCount;
        }
    }

    running = true;
    lastEndReason = EndReasonToString(ProfileEndReason::None);
    currentProfileElapsedS = 0.0;
    EnterStepLocked(0);
    Controller::getInstance().SetProfileSetpointLock(true);

    if (!Controller::getInstance().IsRunning()) {
        const esp_err_t startErr = Controller::getInstance().Start();
        if (startErr != ESP_OK) {
            EndRunLocked(ProfileEndReason::StartFailed, false);
            return startErr;
        }
    }

    int transitionsTaken = 0;
    while (running) {
        const int beforeStep = currentStepIndex;
        const bool keepRunning = ExecuteCurrentStepLocked(0.0, transitionsTaken);
        if (!keepRunning) {
            break;
        }
        if (currentStepIndex == beforeStep) {
            break;
        }
    }

    return ESP_OK;
}

esp_err_t ProfileEngine::StartFromSlot(int slotIndex) {
    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (running) {
        return ESP_ERR_INVALID_STATE;
    }

    ProfileDefinition slotProfile;
    esp_err_t err = LoadProfileFromSlotLocked(slotIndex, slotProfile);
    if (err != ESP_OK) {
        return err;
    }

    const std::vector<ProfileValidationError> errors = ValidateProfile(slotProfile);
    if (!errors.empty()) {
        lastEndReason = EndReasonToString(ProfileEndReason::InvalidProfile);
        return ESP_ERR_INVALID_ARG;
    }

    activeProfile = slotProfile;
    activeSource = "slot";
    activeSlotIndex = slotIndex;

    jumpRemainingByStep.clear();
    for (int idx = 0; idx < static_cast<int>(activeProfile.steps.size()); ++idx) {
        const ProfileStep& step = activeProfile.steps[static_cast<std::size_t>(idx)];
        if (step.type == ProfileStepType::Jump) {
            jumpRemainingByStep[idx] = step.repeatCount;
        }
    }

    running = true;
    lastEndReason = EndReasonToString(ProfileEndReason::None);
    currentProfileElapsedS = 0.0;
    EnterStepLocked(0);
    Controller::getInstance().SetProfileSetpointLock(true);

    if (!Controller::getInstance().IsRunning()) {
        err = Controller::getInstance().Start();
        if (err != ESP_OK) {
            EndRunLocked(ProfileEndReason::StartFailed, false);
            return err;
        }
    }

    int transitionsTaken = 0;
    while (running) {
        const int beforeStep = currentStepIndex;
        const bool keepRunning = ExecuteCurrentStepLocked(0.0, transitionsTaken);
        if (!keepRunning) {
            break;
        }
        if (currentStepIndex == beforeStep) {
            break;
        }
    }

    return ESP_OK;
}

esp_err_t ProfileEngine::CancelRunning(ProfileEndReason reason) {
    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!running) {
        return ESP_ERR_INVALID_STATE;
    }

    EndRunLocked(reason, true);
    return ESP_OK;
}

void ProfileEngine::Tick(double dtSeconds) {
    ScopedLock lock(stateMutex);
    if (!lock.Locked() || !running) {
        return;
    }

    if (!Controller::getInstance().IsRunning()) {
        EndRunLocked(ProfileEndReason::ControllerStopped, false);
        return;
    }

    int transitionsTaken = 0;
    while (running) {
        const int beforeStep = currentStepIndex;
        const bool keepRunning = ExecuteCurrentStepLocked(dtSeconds, transitionsTaken);
        dtSeconds = 0.0;
        if (!keepRunning) {
            break;
        }
        if (currentStepIndex == beforeStep) {
            break;
        }
    }
}

ProfileRuntimeStatus ProfileEngine::GetRuntimeStatus() const {
    ProfileRuntimeStatus status;

    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return status;
    }

    status.running = running;
    status.lastEndReason = lastEndReason;

    if (!running) {
        return status;
    }

    status.name = activeProfile.name;
    status.source = activeSource;
    status.slotIndex = activeSlotIndex;
    status.currentStepNumber = currentStepIndex + 1;
    if (currentStepIndex >= 0 && currentStepIndex < static_cast<int>(activeProfile.steps.size())) {
        status.currentStepType = StepTypeToString(activeProfile.steps[static_cast<std::size_t>(currentStepIndex)].type);
    }
    status.stepElapsedS = currentStepElapsedS;
    status.profileElapsedS = currentProfileElapsedS;

    return status;
}

bool ProfileEngine::IsRunning() const {
    ScopedLock lock(stateMutex);
    if (!lock.Locked()) {
        return false;
    }
    return running;
}

const char* ProfileEngine::StepTypeToString(ProfileStepType type) {
    switch (type) {
        case ProfileStepType::Direct: return "direct";
        case ProfileStepType::Wait: return "wait";
        case ProfileStepType::Soak: return "soak";
        case ProfileStepType::RampTime: return "ramp_time";
        case ProfileStepType::RampRate: return "ramp_rate";
        case ProfileStepType::Jump: return "jump";
    }
    return "direct";
}

const char* ProfileEngine::EndReasonToString(ProfileEndReason reason) {
    switch (reason) {
        case ProfileEndReason::None: return "none";
        case ProfileEndReason::Completed: return "completed";
        case ProfileEndReason::CancelledByUser: return "cancelled_by_user";
        case ProfileEndReason::ControllerStopped: return "controller_stopped";
        case ProfileEndReason::TransitionGuard: return "transition_guard_abort";
        case ProfileEndReason::StartFailed: return "start_failed";
        case ProfileEndReason::InvalidProfile: return "invalid_profile";
    }
    return "none";
}
