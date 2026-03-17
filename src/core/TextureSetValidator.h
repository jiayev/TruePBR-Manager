#pragma once

#include "PBRTextureSet.h"

#include <string>
#include <vector>

namespace tpbr
{

/// Severity level for validation messages.
enum class ValidationSeverity
{
    Warning,
    Error,
};

/// A single validation issue.
struct ValidationIssue
{
    ValidationSeverity severity;
    std::string message;
};

/// Validates a PBRTextureSet and returns a list of issues.
class TextureSetValidator
{
  public:
    /// Run all validation checks on a texture set.
    static std::vector<ValidationIssue> validate(const PBRTextureSet& ts);

  private:
    static void checkRequiredSlots(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues);
    static void checkResolutionConsistency(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues);
    static void checkPowerOfTwo(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues);
    static void checkFeatureTextures(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues);
    static void checkMatchTexture(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues);
    static void checkMatchAliases(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues);
    static void checkSlotConflicts(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues);
};

} // namespace tpbr
