/* Reveal the Settings switch via AMFI action 0; do not enable or reboot the phone.
 * Protocol reference: pymobiledevice3/services/amfi.py (reveal_developer_mode_option_in_ui).
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/property_list_service.h>
#include <plist/plist.h>

static int reveal(const char *udid)
{
    int result = 1, count = 0;
    char **devices = NULL;
    idevice_t device = NULL;
    lockdownd_client_t lockdown = NULL;
    lockdownd_service_descriptor_t service = NULL;
    property_list_service_client_t client = NULL;
    plist_t request = NULL, reply = NULL;

    if (!udid) {
        if (idevice_get_device_list(&devices, &count) != IDEVICE_E_SUCCESS || count == 0) {
            fputs("No USB iPhone found. Connect it to this PC (not the VM), unlock it,\n"
                  "and trust this computer. Apple Mobile Device Support must be installed.\n", stderr);
            goto done;
        }
        if (count > 1) {
            fputs("More than one USB device is connected. Select one with --udid:\n", stderr);
            for (int i = 0; i < count; ++i) fprintf(stderr, "  %s\n", devices[i]);
            goto done;
        }
        udid = devices[0];
    }
    if (idevice_new_with_options(&device, udid, IDEVICE_LOOKUP_USBMUX) != IDEVICE_E_SUCCESS) {
        fputs("Cannot open that USB device. Check its connection and UDID.\n", stderr);
        goto done;
    }
    printf("Device: %s\n", udid);
    int error = lockdownd_client_new_with_handshake(device, &lockdown, "rplay-audio-setup");
    if (error != LOCKDOWN_E_SUCCESS) {
        fprintf(stderr, "Pairing failed (%d). Unlock the phone and accept Trust This Computer, then retry.\n", error);
        goto done;
    }
    error = lockdownd_start_service(lockdown, "com.apple.amfi.lockdown", &service);
    if (error != LOCKDOWN_E_SUCCESS) {
        fprintf(stderr, "Cannot start the Developer Mode service (%d). Keep the phone unlocked.\n", error);
        goto done;
    }
    error = property_list_service_client_new(device, service, &client);
    if (error != PROPERTY_LIST_SERVICE_E_SUCCESS) {
        fprintf(stderr, "Cannot connect to the Developer Mode service (%d).\n", error);
        goto done;
    }
    request = plist_new_dict();
    plist_dict_set_item(request, "action", plist_new_uint(0));
    error = property_list_service_send_xml_plist(client, request);
    if (error == PROPERTY_LIST_SERVICE_E_SUCCESS)
        error = property_list_service_receive_plist_with_timeout(client, &reply, 10000);
    if (error != PROPERTY_LIST_SERVICE_E_SUCCESS || !reply || plist_get_node_type(reply) != PLIST_DICT) {
        fprintf(stderr, "The phone did not return a valid reply (%d).\n", error);
        goto done;
    }
    uint8_t success = 0;
    plist_t value = plist_dict_get_item(reply, "success");
    if (value && plist_get_node_type(value) == PLIST_BOOLEAN) plist_get_bool_val(value, &success);
    if (!success) {
        char *message = NULL;
        value = plist_dict_get_item(reply, "Error");
        if (value && plist_get_node_type(value) == PLIST_STRING) plist_get_string_val(value, &message);
        fprintf(stderr, "The phone refused the reveal request: %s\n", message ? message : "success was not confirmed");
        free(message);
        goto done;
    }
    puts("The phone accepted the request to show Developer Mode.\n"
         "Reopen Settings > Privacy & Security > Developer Mode on the iPhone.\n"
         "Turn it on, follow the restart prompt, then unlock and confirm Turn On.\n"
         "After that, select CoreDevice USB in iOSAudioBridge and start listening.");
    result = 0;
done:
    if (reply) plist_free(reply);
    if (request) plist_free(request);
    if (client) property_list_service_client_free(client);
    if (service) lockdownd_service_descriptor_free(service);
    if (lockdown) lockdownd_client_free(lockdown);
    if (device) idevice_free(device);
    if (devices) idevice_device_list_free(devices);
    return result;
}

int main(int argc, char **argv)
{
    const char *udid = NULL;
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        puts("Usage: reveal-developer-mode [--udid UDID]\n"
             "Show the iPhone's Developer Mode switch. No Python, DDI or tunnel needed.\n"
             "Only reveals the switch; enabling it and restarting are done on the phone.");
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--udid") && *argv[2]) udid = argv[2];
    else if (argc != 1) { fputs("Usage: reveal-developer-mode [--udid UDID]\n", stderr); return 2; }
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fputs("Winsock initialization failed.\n", stderr); return 1; }
#endif
    int result = reveal(udid);
#ifdef _WIN32
    WSACleanup();
#endif
    return result;
}
