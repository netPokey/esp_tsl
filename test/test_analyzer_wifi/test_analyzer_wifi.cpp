#include <unity.h>
#include <cstring>
#include "analyzer/analyzer_wifi.h"

void test_sanitize_accepts_normal_credentials()
{
    AnalyzerWifiCredentials c;
    TEST_ASSERT_TRUE(analyzerWifiSanitizeCredentials("wcqrmyybgs", "1234567890", c));
    TEST_ASSERT_EQUAL_STRING("wcqrmyybgs", c.ssid);
    TEST_ASSERT_EQUAL_STRING("1234567890", c.pass);
}

void test_sanitize_rejects_empty_ssid()
{
    AnalyzerWifiCredentials c;
    TEST_ASSERT_FALSE(analyzerWifiSanitizeCredentials("", "123", c));
    TEST_ASSERT_FALSE(analyzerWifiSanitizeCredentials(nullptr, "123", c));
}

void test_sanitize_rejects_too_long_ssid_or_pass()
{
    char ssid[kAnalyzerWifiMaxSsid + 2];
    memset(ssid, 'a', sizeof(ssid));
    ssid[sizeof(ssid) - 1] = '\0';
    char pass[kAnalyzerWifiMaxPass + 2];
    memset(pass, 'b', sizeof(pass));
    pass[sizeof(pass) - 1] = '\0';
    AnalyzerWifiCredentials c;
    TEST_ASSERT_FALSE(analyzerWifiSanitizeCredentials(ssid, "123", c));
    TEST_ASSERT_FALSE(analyzerWifiSanitizeCredentials("ssid", pass, c));
}

void test_sanitize_allows_empty_password()
{
    AnalyzerWifiCredentials c;
    TEST_ASSERT_TRUE(analyzerWifiSanitizeCredentials("open-ap", "", c));
    TEST_ASSERT_EQUAL_STRING("open-ap", c.ssid);
    TEST_ASSERT_EQUAL_STRING("", c.pass);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sanitize_accepts_normal_credentials);
    RUN_TEST(test_sanitize_rejects_empty_ssid);
    RUN_TEST(test_sanitize_rejects_too_long_ssid_or_pass);
    RUN_TEST(test_sanitize_allows_empty_password);
    return UNITY_END();
}
