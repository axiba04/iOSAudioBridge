#pragma once

namespace audiobridge {
// Two consecutive observations of BOTH authorities are required before opening
// libusb again. Any incomplete observation invalidates the previous one.
class UsbRepairVerification final {
public:
    bool observe(bool normal_pnp, bool exact_usb_mux) noexcept {
        stable_ = normal_pnp && exact_usb_mux ? stable_ + 1 : 0;
        return stable_ >= 2;
    }
private:
    unsigned stable_{};
};
}
