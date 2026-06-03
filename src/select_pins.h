
#if defined(CAMERA_MODEL_TTGO_T_CAM_SIM)
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM 18
#define XCLK_GPIO_NUM  14
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5

#define Y9_GPIO_NUM    15
#define Y8_GPIO_NUM    16
#define Y7_GPIO_NUM    17
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

#define PWR_ON_PIN     1
#define PCIE_PWR_PIN   48
#define PCIE_RST_PIN   48
#define PCIE_TX_PIN    45
#define PCIE_RX_PIN    46
#define PCIE_LED_PIN   21

#define SD_MISO 40
#define SD_MOSI 38
#define SD_SCLK 39
#define SD_CS   47

#define HX711_DT  45
#define HX711_SCK 46

#define MAGNETIC_CONTACT_PIN 21  // GPIO pin connected to magnetic contact sensor
#define LED_PIN 44 //LED for light


#elif defined(CAMERA_MODEL_ESPCAM)

#define HX711_DT  12
#define HX711_SCK 13

#define MAGNETIC_CONTACT_PIN 14  // GPIO pin connected to magnetic contact sensor
#define LED_PIN 15 //LED for light


#else

#error "Please select the model of the board you want to use in main.cpp"
#endif
