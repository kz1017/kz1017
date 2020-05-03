#include <M5Stack.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// GAS設定
const String url = "https://script.google.com/macros/s/***/exec";

// 日本語フォント設定
const char* f24 = "genshin-regular-24pt";

// Wi-Fi設定
const char* ssid = "***";
const char* password = "***";

// NTP設定
#define TIMEZONE_JST  (3600 * 9)  // 日本標準時は、UTC（世界標準時）よりも９時間進んでいる。
#define DAYLIGHTOFFSET_JST  (0)   // 日本は、サマータイム（夏時間）はない。
#define NTP_SERVER1   "ntp.nict.jp"  // NTPサーバー
#define NTP_SERVER2   "ntp.jst.mfeed.ad.jp"   // NTPサーバー
static struct tm timeinfo;
int lasthour;
int lastmin;
String disp;

//ArduinoJson
const size_t capacity = JSON_OBJECT_SIZE(3) + 2*JSON_OBJECT_SIZE(10) + 170;
StaticJsonDocument<capacity> doc;
JsonObject weather;

// setup
void setup() {

  //M5初期化
  M5.begin();
  Serial.begin(9600); //M5.begin()の後にSerialの初期化をしている
  Serial.setDebugOutput(true);
  M5.Power.begin();
  dacWrite(25, 0); //ノイズ対策

  // フォント
  M5.Lcd.loadFont(f24, SD); // SDカードからフォント読み込み
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.setTextColor(BLACK, WHITE); 

  // Wifi接続
  WiFi.mode(WIFI_STA); 
  while(WiFi.status() != WL_CONNECTED){
    WiFi.disconnect();
    M5.Lcd.print("Wi-Fi APに接続しています");
    WiFi.begin(ssid, password);  //  Wi-Fi APに接続
    for(int i=0; i<20; i++){
      if(WiFi.status() == WL_CONNECTED) break;
      M5.Lcd.print(".");
      delay(500);
    }
  }
  M5.Lcd.println(" Wi-Fi APに接続しました。");
  M5.Lcd.print("IP address: "); M5.Lcd.println(WiFi.localIP());
  
  // NTP設定
  configTime( TIMEZONE_JST, DAYLIGHTOFFSET_JST, NTP_SERVER1, NTP_SERVER2 );

  lasthour = -1;
  lastmin = -1;
  disp = "明日";
}

// loop関数
void loop() {
  String filename;
  char buf[40];
  const char* koumoku[] = {"0-6", "6-12", "12-18", "18-24"};

  // LCDをいったんクリア
  M5.Lcd.fillScreen(WHITE);

  // 10分に1回GASから天気予報を取得
  get_time();
  if(lastmin == -1 || (get_min() % 10 == 0 && lastmin != get_min())){
    lastmin = get_min(); lasthour = get_hour();
    //M5.Lcd.unloadFont();
    weather = getWeather(url);
    //M5.Lcd.loadFont(f24, SD); // SDカードからフォント読み込み
  }

  // (今日 or 明日)の天気は…を表示
  M5.Lcd.setCursor(2, 2);
  if(disp == "明日"){
    disp = "今日";
    String message = "今日の" + weather["地域"].as<String>() + "の天気は…";
    M5.Lcd.println(message);
  }else{
    disp = "明日";
    String message = "明日の" + weather["地域"].as<String>() + "の天気は…";
    M5.Lcd.println(message);
  }

  // 天気を表示
  sprintf(buf, "/weather/%d_day.jpg", weather[disp]["天気"].as<int>());
  M5.Lcd.drawJpgFile(SD, buf, 2, 32);

  // 気温を表示
  M5.Lcd.setTextDatum(0);
  M5.Lcd.setTextColor(RED, WHITE); 
  M5.Lcd.drawString(weather[disp]["最高気温"].as<String>(), 200, 64);
  M5.Lcd.setTextColor(BLUE, WHITE); 
  M5.Lcd.drawString(weather[disp]["最低気温"].as<String>(), 200, 100);


  // 降水確率を表示
  M5.Lcd.drawJpgFile(SD, "/weather/table.jpg", 10, 138);
  M5.Lcd.setTextDatum(4);
  M5.Lcd.setTextColor(BLACK, WHITE); 
  for(int i = 0; i < 4; i++){
    M5.Lcd.drawString(weather[disp][koumoku[i]].as<String>(), 48 + 75 * i, 176);
  }

  // 更新時刻を表示
  M5.Lcd.setCursor(152, 214);
  String message = weather["更新時刻"].as<String>() + "更新";
  M5.Lcd.print(message);

  // 15秒で今日と明日を切り替え
  delay(1000*15);
}

//時刻を取得する
void get_time(){
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
}

// 時(hour)を取得
int get_hour(){
  return timeinfo.tm_hour;
}

// 分(min)を取得
int get_min(){
  return timeinfo.tm_min;
}

// 天気をGASから取得
JsonObject getWeather(String url) { 
  HTTPClient http;

  // Locationヘッダを取得する準備(リダイレクト先の確認用)
  const char* headers[] = {"Location"};
  http.collectHeaders(headers, 1);

  // getメソッドで情報取得(ここでLocationヘッダも取得)
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.GET();//GETメソッドで接続

  // 200OKの場合
  if (httpCode == HTTP_CODE_OK) {
    // ペイロード(JSON)を取得
    String payload = "";
    payload = http.getString();

    // JSONをオブジェクトに格納
    deserializeJson(doc, payload); //HTTPのレスポンス文字列をJSONオブジェクトに変換
    http.end();
    Serial.println(payload);

    // JsonObjectで返却
    return doc.as<JsonObject>();

  // 302 temporaly movedの場合
  }else if(httpCode == 302){
    http.end();

    // LocationヘッダにURLが格納されているのでそちらに再接続
    return getWeather(http.header(headers[0]));

  // timeoutの時は再試行
  }else if(httpCode == -11){
    http.end();
    return getWeather(url);

  // その他
  }else{
    http.end();
    Serial.printf("httpCode: %d", httpCode);
    return doc.as<JsonObject>();
  }
}
