//
// Verification pass selection, set via CompilerOptions::verificationMode.
//

#ifndef DJINN_VERIFICATION_MODE_H
#define DJINN_VERIFICATION_MODE_H

enum class VerificationMode
{
    // Pass not scheduled (default: zero behavior change).
    Off,
    // Run the pass and build the Verification IR; no diagnostics emitted yet.
    Report,
    // Report + trace logging of every obligation collected and a summary.
    Trace
};

[[nodiscard]] inline const char* verification_mode_name(const VerificationMode mode)
{
    switch (mode)
    {
        case VerificationMode::Report: return "report";
        case VerificationMode::Trace: return "trace";
        case VerificationMode::Off: return "off";
    }
    return "off";
}

#endif //DJINN_VERIFICATION_MODE_H
