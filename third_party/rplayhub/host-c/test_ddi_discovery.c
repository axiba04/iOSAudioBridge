/* Replay the reported boot sequence through the real cdhost main + RSD parser:
 * the initial directory has the image mounter but no displayservice; mounting
 * the DDI publishes displayservice. Device I/O and the final API server are fakes.
 * Build in MSYS2 UCRT64:
 * gcc -DHAVE_USERNET -Icore host-c/test_ddi_discovery.c core/rp_xpc.c -lws2_32 -o build/test-ddi.exe
 */
#define main cdhost_program_main
#include "cdhost.c"
#undef main
#include <assert.h>

static int mounted, queries;
static const char handshake[] =
    "{\"serverAddress\":\"fd00::1\",\"clientParameters\":{\"address\":\"fd00::2\"},"
    "\"serverRSDPort\":60000,\"mtu\":16000}";

int imd_bringup(void **conn, char *udid, size_t ulen, char *name, size_t nlen,
                char *version, size_t vlen)
{
    *conn = (void *)1;
    snprintf(udid, ulen, "test-device");
    snprintf(name, nlen, "test-phone");
    snprintf(version, vlen, "27.0.1");
    return 0;
}
int imd_conn_send(void *conn, const void *buf, size_t n)
{ (void)conn; (void)buf; (void)n; return 0; }
int imd_conn_recv(void *conn, void *buf, size_t n)
{
    (void)conn;
    if (n == 10) {
        memcpy(buf, "CDTunnel", 8);
        ((uint8_t *)buf)[8] = (sizeof handshake - 1) >> 8;
        ((uint8_t *)buf)[9] = (sizeof handshake - 1) & 255;
    } else { assert(n == sizeof handshake - 1); memcpy(buf, handshake, n); }
    return 0;
}
long rp_tls_read(rp_tls_conn *c, void *buf, size_t n)
{ (void)c; (void)buf; (void)n; abort(); }
long rp_tls_write(rp_tls_conn *c, const void *buf, size_t n)
{ (void)c; (void)buf; (void)n; abort(); }
void usernet_enable(int on) { assert(on); }
int usernet_start(void *conn, const char *ours, const char *device)
{ (void)conn; (void)ours; (void)device; return 0; }
int usernet_connect(const char *addr, int port)
{ (void)addr; assert(port == 60000); return USERNET_FD_BASE; }
void usernet_close(int fd) { (void)fd; }
void tun_close(int fd) { (void)fd; }
long usernet_read(int fd, void *buf, size_t n)
{ (void)fd; (void)buf; (void)n; abort(); }
long usernet_write(int fd, const void *buf, size_t n)
{ (void)fd; (void)buf; (void)n; abort(); }

void rp_rxpc_init(rp_rxpc_session *s, rp_rxpc_io io, uint8_t *buf, size_t cap,
                  uint8_t *raw, size_t raw_cap)
{ (void)s; (void)io; (void)buf; (void)cap; (void)raw; (void)raw_cap; }
int rp_rxpc_handshake(rp_rxpc_session *s) { (void)s; return 0; }
int rp_rxpc_send(rp_rxpc_session *s, const uint8_t *body, size_t len, int reply)
{ (void)s; (void)body; (void)len; (void)reply; return 0; }
int rp_rxpc_recv(rp_rxpc_session *s, rp_xpc_obj *obj)
{
    (void)s;
    static uint8_t data[1024];
    rp_xpc_writer w;
    rp_xpc_writer_init(&w, data, sizeof data);
    rp_xpc_dict_begin(&w);
    rp_xpc_key(&w, "Services"); rp_xpc_dict_begin(&w);
    rp_xpc_key(&w, "com.apple.mobile.mobile_image_mounter.shim.remote");
    rp_xpc_dict_begin(&w); rp_xpc_set_string(&w, "Port", "52463"); rp_xpc_dict_end(&w);
    if (mounted) {
        rp_xpc_key(&w, "com.apple.coredevice.displayservice");
        rp_xpc_dict_begin(&w); rp_xpc_set_string(&w, "Port", "52500"); rp_xpc_dict_end(&w);
    }
    rp_xpc_dict_end(&w); rp_xpc_dict_end(&w);
    assert(!w.overflow);
    obj->data = data; obj->size = w.len;
    ++queries;
    return 0;
}
int cdhost_ddi_activate(const char *addr, long port, const char *dir)
{
    (void)addr; (void)dir;
    assert(port == 52463 && queries == 1);
    mounted = 1;
    return getenv("TEST_DDI_ALREADY") ? RP_DDI_ALREADY : RP_DDI_OK;
}
int api_serve(api_session *session)
{
    if (!mounted || queries < 2 || session->display_port != 52500) {
        fprintf(stderr, "FAIL: daemon received stale pre-DDI directory (queries=%d, display_port=%ld)\n",
                queries, session->display_port);
        exit(1);
    }
    puts("PASS: daemon receives displayservice discovered after DDI activation");
    exit(0);
}
int main(void)
{
    setenv("RPLAY_USERSPACE_NET", "1", 1);
    setenv("RPLAY_ATTACH_TUNNEL", "0", 1);
    char *args[] = { "test-ddi", NULL };
    return cdhost_program_main(1, args);
}
