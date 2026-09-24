#pragma once

// Common USB identities extracted from iPhoneMirror; no libusb-1.0 dependency.

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace iPhoneMirror::transport {

struct UsbEndpointSet {
    std::uint8_t configuration{};
    std::uint8_t interface_number{};
    std::uint8_t alternate_setting{};
    std::uint8_t bulk_in{};
    std::uint8_t bulk_out{};
    std::uint16_t bulk_in_packet_size{};
    std::uint16_t bulk_out_packet_size{};
};

struct AppleUsbDevice {
    std::uint16_t vendor_id{};
    std::uint16_t product_id{};
    std::uint8_t bus{};
    std::uint8_t address{};
    std::string serial;
    // Stable physical-port identity for the lifetime of a cable connection.
    // USB addresses and PIDs may change when the hidden capture configuration
    // is enabled, so neither is suitable for matching a re-enumerated device.
    std::string topology_id;
    bool can_open{};
    bool mux_configuration{};
    bool quicktime_configuration{};
    // Active bConfigurationValue read from the currently opened device. A
    // QuickTime descriptor can remain cached after the device has returned to
    // its normal configuration, so descriptor presence alone is not state.
    std::uint8_t active_configuration{};
    bool active_configuration_known{};
    std::uint8_t configuration_count{};
    std::uint8_t highest_configuration_value{};
    UsbEndpointSet mux_endpoints;
    UsbEndpointSet quicktime_endpoints;
};

struct AppleUsbIdentity {
    std::string serial;
    std::string topology_id;
    std::uint16_t original_product_id{};
    // The QuickTime configuration is appended to the normal descriptor set.
    // Modern USB-C iPads commonly append configuration 6 rather than 5.
    std::uint8_t expected_quicktime_configuration{};
};

enum class AppleUsbMatchKind {
    None,
    Serial,
    Topology,
};

struct AppleUsbSelection {
    std::optional<std::size_t> index;
    AppleUsbMatchKind match_kind{AppleUsbMatchKind::None};
    bool ambiguous{};
    std::size_t serial_matches{};
    std::size_t topology_matches{};
};

[[nodiscard]] AppleUsbIdentity make_apple_usb_identity(
    const AppleUsbDevice& device) noexcept;
[[nodiscard]] AppleUsbSelection select_apple_usb_device(
    std::span<const AppleUsbDevice> devices, const AppleUsbIdentity& identity,
    bool require_quicktime) noexcept;
[[nodiscard]] bool apple_usb_candidate_in_scope(
    std::string_view candidate_topology,
    const AppleUsbIdentity& identity) noexcept;
[[nodiscard]] UsbEndpointSet select_best_quicktime_endpoints(
    std::span<const UsbEndpointSet> candidates) noexcept;
[[nodiscard]] UsbEndpointSet conventional_quicktime_endpoints(
    const AppleUsbIdentity& identity) noexcept;
[[nodiscard]] std::string describe_apple_usb_candidates(
    std::span<const AppleUsbDevice> devices, const AppleUsbIdentity& identity);

// A live capture handle already proves the device identity for its physical
// port. Cache that proof inside each backend's topology namespace. Capture
// startup prefers the backend already used by live sessions and never assumes
// that unrelated backend-specific topology strings are interchangeable.
[[nodiscard]] std::string cached_active_apple_usb_serial(
    std::string_view topology) noexcept;
[[nodiscard]] bool retain_active_apple_usb_identity(
    const AppleUsbIdentity& identity) noexcept;
void release_active_apple_usb_identity(
    std::string_view topology, std::string_view serial) noexcept;


} // namespace iPhoneMirror::transport
