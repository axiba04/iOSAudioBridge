// Adapted from the verified iPhoneMirror CaptureSession.cpp, GPL-3.0.
bool usbmux_contains_serial(std::string_view serial) noexcept {
    if (serial.empty()) return false;
    for (const auto port : {std::uint16_t{27015}}) {
        if (!transport::Socket::probe_loopback(port)) continue;
        try {
            transport::UsbMuxClient mux(port);
            for (const auto& device : mux.list_devices()) {
                if (transport::apple_usb_serial_equal(device.serial, serial))
                    return true;
            }
        } catch (...) {
        }
    }
    return false;
}

struct UsbConfigurationRestoreResult {
    bool normal_observed{};
    bool disable_requested{};
};

template <typename Observe, typename Disable>
UsbConfigurationRestoreResult restore_usb_configuration(
    std::string_view backend, Observe&& observe,
    Disable&& disable, bool primary_request_sent = false) noexcept {
    detail::UsbConfigurationRestorePolicy policy(primary_request_sent);
    // Windows tears down the temporary QuickTime composite node and starts
    // the Apple parent in separate PnP transactions. On the affected
    // libusb0/Apple filter stack this bounded state transition can take several
    // seconds; keep observing the descriptor/PnP evidence without issuing
    // another configuration request.
    constexpr auto RestoreObservationWindow = std::chrono::seconds(12);
    auto deadline = std::chrono::steady_clock::now() + RestoreObservationWindow;
    while (std::chrono::steady_clock::now() < deadline) {
        auto observation = detail::UsbConfigurationObservation::Missing;
        try { observation = observe(); } catch (...) {}

        const auto action = policy.observe(observation);
        if (action == detail::UsbConfigurationRestoreAction::Complete) {
            logging::write(std::format(
                "usb_configuration_restore backend={} result=normal disable_requested={}",
                backend, policy.disable_requested()));
            return {
                .normal_observed = true,
                .disable_requested = policy.disable_requested(),
            };
        }
        if (action == detail::UsbConfigurationRestoreAction::DisableQuickTime) {
            // 0x52 disconnects the device and can surface as I/O/NO_DEVICE even
            // when iOS accepted it. Mark it attempted before entering the
            // transport so no error path can send the request a second time.
            logging::write(std::format(
                "usb_configuration_restore backend={} action=disable_requested", backend));
            try { disable(); } catch (...) {}
            // The helper itself can wait up to 15 seconds for removal/arrival.
            // Give Apple management its full observation window AFTER it exits.
            deadline = std::chrono::steady_clock::now() + RestoreObservationWindow;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    logging::write(std::format(
        "usb_configuration_restore backend={} result=timeout disable_requested={}",
        backend, policy.disable_requested()));
    return {
        .normal_observed = false,
        .disable_requested = policy.disable_requested(),
    };
}

UsbConfigurationRestoreResult restore_libusb0_configuration(
    const transport::AppleUsbIdentity& identity,
    bool primary_request_sent = false,
    std::chrono::milliseconds automatic_restore_grace = std::chrono::milliseconds(3000)) noexcept {
    bool restore_request_sent = primary_request_sent;
    bool restore_switch_verified{};
    const auto filter_safety =
        device::inspect_apple_usb_filter_stack(identity.serial);
    // The exact-device helper now owns the old handle until removal/arrival is
    // complete, including on the known libusb0 + Apple filter stack. Keep an
    // indeterminate stack observation-only, but allow the guarded one-shot
    // restore for a positively identified safe or known-unsafe stack.
    const bool fallback_allowed =
        filter_safety.safety != device::AppleUsbFilterSafety::Indeterminate;
    if (filter_safety.safety == device::AppleUsbFilterSafety::Unsafe) {
        logging::write(logging::Level::Warning, "usb_safety",
            std::format(
                "usb_configuration_restore backend=libusb0 fallback=guarded_exact_pnp diagnostic={}",
                filter_safety.diagnostic));
    } else if (!fallback_allowed) {
        logging::write(logging::Level::Warning, "usb_safety",
            std::format(
                "usb_configuration_restore backend=libusb0 fallback=disabled diagnostic={}",
                filter_safety.diagnostic));
    }
    auto observe_configuration =
        [&, previous_state = std::optional<bool>{}, stable_normal = 0U,
            stable_quicktime_residual = 0U,
            automatic_restore_deadline = std::chrono::steady_clock::now() +
                automatic_restore_grace]() mutable {
        // USBMux is the exact, open-free authority for normal configuration.
        // Do not enumerate or reopen libusb0 while AppleUsbFilter is processing
        // the disconnecting control request.
        const bool mux_present = usbmux_contains_serial(identity.serial);
        const auto pnp =
            device::inspect_apple_normal_usb_stack(identity.serial);
        const bool pnp_normal =
            device::is_complete_apple_normal_usb_stack(pnp);
        const bool parent_present = pnp.parent_started;
        const bool normal = mux_present && pnp_normal && parent_present;
        stable_normal = normal ? stable_normal + 1U : 0U;
        // The QuickTime parent is started but has no normal management child
        // or exact USBMux row. A missing parent is only an in-flight PnP
        // transition and must never authorize a control open.
        const bool quicktime_residual = detail::is_libusb0_quicktime_pnp_state(
            mux_present, pnp_normal, parent_present, true);
        stable_quicktime_residual = quicktime_residual
            ? stable_quicktime_residual + 1U : 0U;
        auto observation = detail::stabilize_normal_configuration_observation(
            normal, stable_normal);
        // Closing the claimed stream handle can complete iOS's transition back
        // to the management configuration without another vendor request. Give
        // that observed transition a bounded grace period. Only if it fails to
        // stabilize do we authorize one separate-control fallback request.
        if (fallback_allowed && !restore_request_sent &&
            stable_quicktime_residual >= 2 &&
            std::chrono::steady_clock::now() >= automatic_restore_deadline)
            observation = detail::UsbConfigurationObservation::QuickTime;
        if (!previous_state || *previous_state != normal || stable_normal == 2 ||
            stable_quicktime_residual == 2) {
            logging::write(std::format(
                "usb_configuration_restore backend=libusb0 observation={} usbmux_exact_present={} pnp_normal_stack={} parent_present={} mi00_media_started={} mi01_management_started={} request_sent={} stable_normal={} stable_quicktime_residual={}",
                observation == detail::UsbConfigurationObservation::VerifiedNormal
                    ? "verified_normal" :
                observation == detail::UsbConfigurationObservation::QuickTime
                    ? "fallback_validation_ready" : normal ? "normal_pending" : "missing",
                mux_present, pnp_normal, parent_present,
                pnp.media_interface_started,
                pnp.management_interface_started, restore_request_sent,
                stable_normal, stable_quicktime_residual));
            previous_state = normal;
        }
        return observation;
    };
    auto result = restore_usb_configuration("libusb0", observe_configuration, [&] {
        restore_request_sent = true;
        restore_switch_verified =
            transport::LibUsb0Connection::disable_quicktime_configuration(identity);
    }, primary_request_sent);
    if (!result.normal_observed && restore_switch_verified && fallback_allowed &&
        device::is_apple_usb_parent_present(identity.serial)) {
        // Some iPads return to normal descriptors after 0x52, but Apple USBMux
        // loses the new management interface. Do not relax the success check:
        // cycle this device once in a separate process, then verify discovery
        // again. The helper requires a fresh exact serial and a normal active
        // descriptor (no QuickTime/placeholder interface) before it can cycle.
        // Never repeat 0x52 or retry a failed cycle.
        logging::write("usb_configuration_restore backend=libusb0 action=reconnect_normal");
        try {
            if (transport::LibUsb0Connection::reconnect_normal_configuration(identity)) {
                result = restore_usb_configuration("libusb0_reconnect",
                    observe_configuration, [] {}, true);
            }
        } catch (const std::exception& error) {
            logging::write(std::format("usb_configuration_restore reconnect_failed error={}",
                error.what()));
        } catch (...) {
            logging::write("usb_configuration_restore reconnect_failed error=unknown");
        }
    }
    result.disable_requested = restore_request_sent;
    return result;
}
