// ===================================================
// 1. NETWORK & AWS DETAILS
// ===================================================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* aws_endpoint = "YOUR_AWS_IOT_ENDPOINT-ats.iot.ap-southeast-1.amazonaws.com";
const int aws_port = 8883;

// ===================================================
// 2. CERTIFICATES
// ===================================================
// Amazon Root CA 1
static const char root_ca[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
[PASTE YOUR AMAZON ROOT CA 1 HERE]
-----END CERTIFICATE-----
)EOF";

// Device Certificate
static const char device_cert[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
[PASTE YOUR DEVICE CERTIFICATE HERE]
-----END CERTIFICATE-----
)EOF";

// Device Private Key
static const char device_key[] PROGMEM = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
[PASTE YOUR DEVICE PRIVATE KEY HERE]
-----END RSA PRIVATE KEY-----
)EOF";