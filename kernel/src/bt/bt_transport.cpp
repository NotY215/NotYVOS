#include <kernel/bt/bt.hpp>

namespace
{
notyvos::bt::HciSendFn g_send_fn = nullptr;
void* g_send_user = nullptr;
} // namespace

extern "C" void bt_store_transport(notyvos::bt::HciSendFn fn, void* user) noexcept
{
    g_send_fn = fn;
    g_send_user = user;
}

extern "C" bool bt_transport_send(const notyvos::u8* data, notyvos::usize len) noexcept
{
    if (!g_send_fn)
        return false;
    return g_send_fn(g_send_user, data, len);
}
