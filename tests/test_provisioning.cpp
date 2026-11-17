#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "tw/captive_net.h"
#include "tw/graphics.h"
#include "tw/provisioning.h"
#include "tw/web.h"

using namespace tw;

TEST(Credentials, FlashRecordRoundTripsAndRejectsCorruption) {
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(crc32(check, sizeof(check)), 0xCBF43926u);  // standard CRC-32 check value

    const auto creds = makeCredentials("Home Network 5G", "correct horse battery");
    ASSERT_TRUE(creds.has_value());
    uint8_t page[kCredentialRecordSize];
    encodeCredentials(*creds, page);
    EXPECT_EQ(decodeCredentials(page, sizeof(page)), creds);

    uint8_t corrupt[kCredentialRecordSize];
    std::memcpy(corrupt, page, sizeof(page));
    corrupt[12] ^= 0x01;  // bit flip inside the SSID
    EXPECT_FALSE(decodeCredentials(corrupt, sizeof(corrupt)).has_value());

    uint8_t erased[kCredentialRecordSize];
    std::memset(erased, 0xFF, sizeof(erased));  // never-written flash
    EXPECT_FALSE(decodeCredentials(erased, sizeof(erased)).has_value());

    const auto open = makeCredentials("CoffeeShop", "");
    encodeCredentials(*open, page);
    EXPECT_EQ(decodeCredentials(page, sizeof(page)), open);
}

TEST(Credentials, ValidatesSsidAndPassphraseLengths) {
    EXPECT_EQ(validateCredentials("", "password123"), CredentialError::SsidEmpty);
    EXPECT_EQ(validateCredentials(std::string(33, 'x'), "password123"), CredentialError::SsidTooLong);
    EXPECT_EQ(validateCredentials("Net", "short"), CredentialError::PasswordLength);
    EXPECT_EQ(validateCredentials("Net", std::string(64, 'p')), CredentialError::PasswordLength);
    EXPECT_EQ(validateCredentials(std::string(32, 'x'), std::string(63, 'p')), CredentialError::None);
    EXPECT_EQ(validateCredentials("Net", ""), CredentialError::None);  // open network
    EXPECT_FALSE(makeCredentials("Net", "short").has_value());
}

TEST(Web, ParsesRequestAndWaitsForFullBody) {
    const std::string full =
        "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\ncontent-length: 27\r\n\r\nssid=My+Net&password=p%40ss";
    web::Request req;
    ASSERT_EQ(web::parseRequest(full, req), web::ParseStatus::Complete);
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.path, "/save");
    EXPECT_EQ(req.body, "ssid=My+Net&password=p%40ss");

    EXPECT_EQ(web::parseRequest(full.substr(0, full.size() - 3), req), web::ParseStatus::Incomplete);
    EXPECT_EQ(web::parseRequest("GET / HTTP/1.1\r\nHost: x\r\n", req), web::ParseStatus::Incomplete);
    EXPECT_EQ(web::parseRequest("GARBAGE\r\n\r\n", req), web::ParseStatus::Invalid);
    EXPECT_EQ(web::parseRequest("POST / HTTP/1.1\r\nContent-Length: 99999\r\n\r\n", req), web::ParseStatus::Invalid);

    ASSERT_EQ(web::parseRequest("GET /generate_204 HTTP/1.1\r\n\r\n", req), web::ParseStatus::Complete);
    EXPECT_EQ(req.path, "/generate_204");
    EXPECT_TRUE(req.body.empty());
}

TEST(Web, DecodesFormFields) {
    const std::string_view body = "ssid=My+Net&password=p%40ss%26%3D";
    char out[64];
    ASSERT_EQ(web::formField(body, "ssid", out, sizeof(out)), 6u);
    EXPECT_STREQ(out, "My Net");
    ASSERT_TRUE(web::formField(body, "password", out, sizeof(out)).has_value());
    EXPECT_STREQ(out, "p@ss&=");

    EXPECT_FALSE(web::formField(body, "missing", out, sizeof(out)).has_value());
    EXPECT_FALSE(web::formField(body, "pass", out, sizeof(out)).has_value()) << "no prefix matches";
    EXPECT_FALSE(web::formField("x=%4", "x", out, sizeof(out)).has_value());
    EXPECT_FALSE(web::formField("x=%zz", "x", out, sizeof(out)).has_value());
    char tiny[4];
    EXPECT_FALSE(web::formField("x=abcd", "x", tiny, sizeof(tiny)).has_value());
    ASSERT_EQ(web::formField("x=", "x", out, sizeof(out)), 0u);
}

TEST(Web, SetupPageEscapesNetworkNames) {
    const char* nets[] = {"Home<script>alert(1)</script>", "Cafe \"5G\" & more"};
    std::vector<char> buf(4096);
    const std::size_t n = web::renderSetupPage(nets, 2, "Bad <password>", buf.data(), buf.size());
    ASSERT_GT(n, 0u);
    const std::string page(buf.data(), n);

    EXPECT_EQ(page.find("<script>"), std::string::npos);
    EXPECT_NE(page.find("Home&lt;script&gt;alert(1)&lt;/script&gt;"), std::string::npos);
    EXPECT_NE(page.find("Cafe &quot;5G&quot; &amp; more"), std::string::npos);
    EXPECT_NE(page.find("Bad &lt;password&gt;"), std::string::npos);
    EXPECT_NE(page.find("action=/save"), std::string::npos);

    char small[64];
    EXPECT_EQ(web::renderSetupPage(nets, 2, "", small, sizeof(small)), 0u) << "never truncates silently";
}

TEST(CaptiveDns, AnswersAQueriesWithPortalAddress) {
    std::vector<uint8_t> q = {0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
    for (const char* label : {"connectivitycheck", "gstatic", "com"}) {
        q.push_back(static_cast<uint8_t>(std::strlen(label)));
        q.insert(q.end(), label, label + std::strlen(label));
    }
    q.insert(q.end(), {0, 0, 1, 0, 1});  // root, QTYPE A, QCLASS IN

    uint8_t out[512];
    const std::size_t n = dns::buildCaptiveReply(q.data(), q.size(), kPortalIp, out, sizeof(out));
    ASSERT_EQ(n, q.size() + 16);
    EXPECT_EQ(out[0], 0x12);
    EXPECT_EQ(out[1], 0x34);
    EXPECT_EQ(out[2], 0x85);  // QR | AA | RD
    EXPECT_EQ(out[7], 1);     // one answer
    EXPECT_EQ(std::vector<uint8_t>(out + n - 4, out + n), (std::vector<uint8_t>{192, 168, 4, 1}));

    q[q.size() - 3] = 28;  // AAAA: answered with no records
    EXPECT_EQ(dns::buildCaptiveReply(q.data(), q.size(), kPortalIp, out, sizeof(out)), q.size());
    EXPECT_EQ(out[7], 0);

    q[2] |= 0x80;  // a response, not a query
    EXPECT_EQ(dns::buildCaptiveReply(q.data(), q.size(), kPortalIp, out, sizeof(out)), 0u);
    q[2] &= 0x7F;
    EXPECT_EQ(dns::buildCaptiveReply(q.data(), 20, kPortalIp, out, sizeof(out)), 0u) << "truncated name";
}

TEST(CaptiveDhcp, OffersStableLeasesWithPortalAsDns) {
    uint8_t discover[300] = {};
    discover[0] = 1;
    discover[1] = 1;
    discover[2] = 6;
    discover[4] = 0xDE, discover[5] = 0xAD, discover[6] = 0xBE, discover[7] = 0xEF;
    discover[10] = 0x80;  // broadcast flag
    const uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::memcpy(discover + 28, mac, 6);
    discover[236] = 0x63, discover[237] = 0x82, discover[238] = 0x53, discover[239] = 0x63;
    discover[240] = 53, discover[241] = 1, discover[242] = dhcp::Discover;
    discover[243] = 255;

    const auto msg = dhcp::parseClientMessage(discover, sizeof(discover));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, dhcp::Discover);
    EXPECT_EQ(msg->xid, 0xDEADBEEFu);

    dhcp::LeasePool pool(0xC0A80410);  // 192.168.4.16
    const auto ip = pool.assign(msg->mac);
    EXPECT_EQ(ip, 0xC0A80410u);
    EXPECT_EQ(pool.assign(msg->mac), ip) << "same client keeps its address";
    const uint8_t other[6] = {0x02, 0, 0, 0, 0, 1};
    EXPECT_EQ(pool.assign(other), 0xC0A80411u);

    uint8_t reply[600];
    ASSERT_EQ(dhcp::buildReply(*msg, dhcp::Offer, *ip, kPortalIp, 3600, reply, sizeof(reply)), dhcp::kMinPacket);
    EXPECT_EQ(reply[0], 2);
    EXPECT_EQ(std::vector<uint8_t>(reply + 16, reply + 20), (std::vector<uint8_t>{192, 168, 4, 16}));
    EXPECT_EQ(std::memcmp(reply + 28, mac, 6), 0);
    EXPECT_EQ(reply[240], 53);
    EXPECT_EQ(reply[242], dhcp::Offer);
    // Option 6 (DNS) points clients at our captive DNS server.
    bool foundDns = false;
    for (std::size_t i = 240; i < 300 && reply[i] != 255; i += 2 + reply[i + 1]) {
        if (reply[i] == 6) foundDns = std::vector<uint8_t>(reply + i + 2, reply + i + 6) == std::vector<uint8_t>{192, 168, 4, 1};
    }
    EXPECT_TRUE(foundDns);

    discover[237] = 0;  // bad magic cookie
    EXPECT_FALSE(dhcp::parseClientMessage(discover, sizeof(discover)).has_value());
}

TEST(SetupMode, LongPressFiresOnceAndScreenShowsInstructions) {
    LongPressDetector button(3000);
    EXPECT_FALSE(button.update(false, 0));
    EXPECT_FALSE(button.update(true, 100));
    EXPECT_FALSE(button.update(true, 3099));
    EXPECT_TRUE(button.update(true, 3100));
    EXPECT_FALSE(button.update(true, 6000)) << "fires once per press";
    EXPECT_FALSE(button.update(false, 6100));
    EXPECT_FALSE(button.update(true, 7000));
    EXPECT_FALSE(button.update(false, 8000));  // released early (bounce/tap)
    EXPECT_FALSE(button.update(true, 9000));
    EXPECT_FALSE(button.update(true, 11999));
    EXPECT_TRUE(button.update(true, 12000));

    ScreenModel m;
    m.setupMode = true;
    m.timeValid = true;
    Framebuffer fb, ref;
    renderScreen(fb, m);
    drawText(ref, (64 - textWidth(kSetupApName)) / 2, 16, kSetupApName, colors::kDate);
    for (int y = 16; y < 23; ++y)
        for (int x = 0; x < 64; ++x) ASSERT_EQ(fb.get(x, y), ref.get(x, y));
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) ASSERT_NE(fb.get(x, y), colors::kTime) << "clock hidden in setup mode";
}
