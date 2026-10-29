#include "../Common/WakeOnLan.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

using namespace moonlight_xbox_dx::WakeOnLan;

static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    const std::vector<InterfaceIPv4> xbox{ { "10.0.0.42", 24 } };
    Check(DirectedBroadcast("10.0.0.3", xbox) == "10.0.0.255", "10.x host uses the Xbox /24, never the old class-A mask");
    Check(DirectedBroadcast("10.0.0.3/24", xbox) == "10.0.0.255", "CIDR notation in a saved address is accepted");
    Check(DirectedBroadcast("10.0.0.3/8", xbox) == "10.0.0.255", "the real adapter prefix takes precedence over a saved suffix");
    Check(DirectedBroadcast("10.0.0.3:47989", xbox) == "10.0.0.255", "custom host ports do not change the subnet");
    Check(DirectedBroadcast("10.0.0.3", { { "10.0.12.2", 16 } }) == "10.0.255.255", "a real /16 is preserved");
    Check(DirectedBroadcast("192.168.3.14", { { "192.168.2.40", 23 } }) == "192.168.3.255", "non-octet-aligned prefixes work");
    Check(DirectedBroadcast("10.0.0.3", { { "10.0.0.40", 25 } }) == "10.0.0.127", "a /25 broadcast uses only the host bits");
    Check(DirectedBroadcast("10.0.0.130", { { "10.0.0.140", 25 } }) == "10.0.0.255", "upper /25 network is distinguished");
    Check(DirectedBroadcast("10.0.0.3", { { "192.168.0.2", 24 }, { "10.0.0.42", 24 } }) == "10.0.0.255",
        "matching adapter wins over another adapter");
    const std::vector<InterfaceIPv4> overlapping{ { "10.10.0.2", 8 }, { "10.0.0.42", 24 } };
    Check(DirectedBroadcast("10.0.0.3", overlapping) == "10.0.0.255", "most specific local subnet wins");
    auto reversed = overlapping;
    std::reverse(reversed.begin(), reversed.end());
    Check(DirectedBroadcast("10.0.0.3", reversed) == "10.0.0.255", "adapter enumeration order does not choose the subnet");
    Check(DirectedBroadcast("10.0.1.3", xbox).empty(), "off-subnet host never gets a guessed broadcast");
    Check(DirectedBroadcast("10.0.0.3", {}).empty(), "missing interface metadata never falls back to address classes");
    for (unsigned prefix : { 0u, 31u, 32u, 33u, 255u })
        Check(DirectedBroadcast("10.0.0.3", { { "10.0.0.42", prefix } }).empty(), "unknown and point-to-point prefixes have no broadcast");
    for (const auto* ip : { "", "10.0.0", "10.0.0.256", "10.0.0.-3", "10.0.0.3.4", "host.local", "fe80::1", "1a.0.0.3" })
        Check(DirectedBroadcast(ip, xbox).empty(), "invalid or non-IPv4 hosts cannot become broadcast destinations");
    Check(DirectedBroadcast("10.0.0.3", { { "not-an-address", 24 } }).empty(), "malformed interface addresses are ignored");
    Check(HostIPv4("010.000.000.003:47989") == "10.0.0.3", "IPv4 destinations are normalized before socket use");

    Check(std::find(std::begin(Ports), std::end(Ports), 7) != std::end(Ports), "all wake addresses are sent to port 7");
    Check(std::find(std::begin(Ports), std::end(Ports), 9) != std::end(Ports), "all wake addresses are sent to port 9");
    for (int port : Ports) Check(port > 0 && port <= 65535, "no datagram is sent to port zero or an invalid port");

    const std::wstring mac = L"04:42:1a:0a:5c:07";
    Check(IsUsableMac(mac), "the supplied hardware MAC is usable");
    Check(IsUsableMac(L"04-42-1A-0A-5C-07") && IsUsableMac(L"04421a0a5c07"), "standard MAC formats are accepted");
    Check(IsUsableMac(L"00:11:22:33:44:55"), "a unicast MAC may start with zero");
    Check(IsUsableMac(L"02:11:22:33:44:55"), "locally administered unicast MACs remain valid");
    for (const auto* hidden : { L"", L"00:00:00:00:00:00", L"00-00-00-00-00-00", L"000000000000" }) {
        Check(!IsUsableMac(hidden), "serverinfo placeholders cannot pass the shared MAC setter guard");
        bool rejected = false;
        try { MagicPacket(hidden); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "placeholder MACs cannot generate a wake packet");
    }
    for (const auto* invalid : { L"ff:ff:ff:ff:ff:ff", L"01:42:1a:0a:5c:07", L"04:42:1a:0a:5c", L"04:42:1a:0a:5c:gg", L"04:42-1a:0a:5c:07", L"04:42:1a:0a:5c:07x" })
        Check(!IsUsableMac(invalid), "malformed, broadcast, and multicast MACs cannot replace a saved unicast MAC");
    const auto packet = MagicPacket(mac);
    Check(packet.size() == 102, "magic packet has the required 102 bytes");
    for (size_t i = 0; i < 6; ++i) Check(static_cast<unsigned char>(packet[i]) == 0xff, "magic packet starts with six FF bytes");
    const unsigned char expected[] = { 0x04, 0x42, 0x1a, 0x0a, 0x5c, 0x07 };
    for (size_t repeat = 0; repeat < 16; ++repeat)
        for (size_t i = 0; i < 6; ++i)
            Check(static_cast<unsigned char>(packet[6 + repeat * 6 + i]) == expected[i], "exact hardware MAC repeats sixteen times");
    std::puts("Wake-on-LAN subnet, port, MAC guard, and packet tests passed");
}
