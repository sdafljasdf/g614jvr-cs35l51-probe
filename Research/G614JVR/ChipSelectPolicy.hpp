// Offline model: board-specific role checks. No IOKit, GPIO or SPI interface.
#pragma once
#include <cstdint>
namespace g614jvr_probe {
enum class GpioRole { NativeCS, ExternalCS, AmpReset, SpeakerID, Interrupt, Unknown };
struct Evidence {
    uint32_t subsystem;
    bool exactController, exactHid, quirkApplicable, noDeviceProperties;
    bool spiResourcesMatch, gpio0IsExclusiveOutput, gpio1IsOutput, gpio2IsInput;
    uint16_t gpio0Pin, gpio1Pin, gpio2Pin;
};
struct Roles { bool accepted; GpioRole cs0, cs1, gpio0, gpio1, gpio2, gpio3; };
inline Roles classify(const Evidence& e) {
    const Roles reject{false,GpioRole::Unknown,GpioRole::Unknown,GpioRole::Unknown,
        GpioRole::Unknown,GpioRole::Unknown,GpioRole::Unknown};
    // Exact device + descriptor checks must all hold; do not infer from index alone.
    if (e.subsystem != 0x10431b93 || !e.exactController || !e.exactHid ||
        !e.quirkApplicable || !e.noDeviceProperties || !e.spiResourcesMatch ||
        !e.gpio0IsExclusiveOutput || !e.gpio1IsOutput || !e.gpio2IsInput ||
        e.gpio0Pin != 0x1d4 || e.gpio1Pin != 0x140 || e.gpio2Pin != 0xc0)
        return reject;
    return {true,GpioRole::NativeCS,GpioRole::ExternalCS,GpioRole::ExternalCS,
        GpioRole::AmpReset,GpioRole::SpeakerID,GpioRole::Unknown};
}
// Valid only for a single owned transaction; UNKNOWN and non-CS always reject.
inline bool permitsTransition(GpioRole role, bool transactionActive) {
    return transactionActive && (role == GpioRole::NativeCS || role == GpioRole::ExternalCS);
}
}
