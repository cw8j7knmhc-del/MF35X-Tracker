#pragma once
#include <Arduino.h>

// MF35X OTA public key. Public by design; the matching private key must stay secret.
static const uint8_t OTA_PUBLIC_KEY[] PROGMEM = R"MF35XKEY(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEArsXcSWK9FB1ygyinfPzx
1rtM4MVtiTgJ04KaRIdZVwj8EY9Q33CApcNCKo/h1+dmDcAcQgpyW99om92hCjrT
UsBtW18tgTDaQf0mKkw7GhONvu1YqeZUVOUTGX0O/7Slk5CCTi6a9jz5XrY+MPZg
KOm+HLPZB7OuamdbrCGboupZpAIGjHLiB9EVGCjiWssY/LLTC4EqW/RadoJT503h
jHVsRXuGSbsJurDIXXb/ML2IdhIAHTEbiwlhnji+EraOnmAVe70Hbh4axlCGVx0r
Aw2g9JpJd41113mrzpTc072YhWmTRDb0XstyC9t6i4g9OXiCSvivMJ4zIHk7M48d
RQIDAQAB
-----END PUBLIC KEY-----
)MF35XKEY";

static constexpr size_t OTA_PUBLIC_KEY_LEN = sizeof(OTA_PUBLIC_KEY);
