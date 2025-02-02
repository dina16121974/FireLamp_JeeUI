/*
Copyright © 2020 Dmytro Korniienko (kDn)
JeeUI2 lib used under MIT License Copyright (c) 2019 Marsel Akhkamov

    This file is part of FireLamp_JeeUI.

    FireLamp_JeeUI is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FireLamp_JeeUI is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FireLamp_JeeUI.  If not, see <https://www.gnu.org/licenses/>.

  (Этот файл — часть FireLamp_JeeUI.

   FireLamp_JeeUI - свободная программа: вы можете перераспространять ее и/или
   изменять ее на условиях Стандартной общественной лицензии GNU в том виде,
   в каком она была опубликована Фондом свободного программного обеспечения;
   либо версии 3 лицензии, либо (по вашему выбору) любой более поздней
   версии.

   FireLamp_JeeUI распространяется в надежде, что она будет полезной,
   но БЕЗО ВСЯКИХ ГАРАНТИЙ; даже без неявной гарантии ТОВАРНОГО ВИДА
   или ПРИГОДНОСТИ ДЛЯ ОПРЕДЕЛЕННЫХ ЦЕЛЕЙ. Подробнее см. в Стандартной
   общественной лицензии GNU.

   Вы должны были получить копию Стандартной общественной лицензии GNU
   вместе с этой программой. Если это не так, см.
   <https://www.gnu.org/licenses/>.)
*/
#include "main.h"
#include "filehelpers.hpp"
#include <SPIFFSEditor.h>
#include "lamp.h"

#ifdef DS18B20
#include "DS18B20.h"
#endif

#include "w2812-rmt.hpp"

LedFB *mx = nullptr;

// FastLED controller
CLEDController *cled = nullptr;
ESP32RMT_WS2812B<COLOR_ORDER> *wsstrip = nullptr;

#ifdef ESP_USE_BUTTON
Buttons *myButtons;
#endif

#ifdef MP3PLAYER
MP3PlayerDevice *mp3 = nullptr;
#endif

#ifdef TM1637_CLOCK
// TM1637 display
// https://github.com/AKJ7/TM1637/
TMCLOCK *tm1637 = nullptr;
#endif

// forward declarations

/**
 * @brief restore gpio configurtion and initialise attached devices
 * 
 */
void gpio_setup();

// restores LED fb config from file
void led_fb_setup();

// mDNS announce for WLED app
void wled_announce(WiFiEvent_t cbEvent, WiFiEventInfo_t i);   // wifi_event_id_t onEvent(WiFiEventFuncCb cbEvent, arduino_event_id_t event = ARDUINO_EVENT_MAX);
// 404 handler
bool http_notfound(AsyncWebServerRequest *request);


// Arduino setup
void setup() {
    Serial.begin(115200);

    LOG(printf_P, PSTR("\n\nsetup: free heap: %d, PSRAM:%d\n"), ESP.getFreeHeap(), ESP.getFreePsram());

#ifdef EMBUI_USE_UDP
    embui.udp(); // Ответ на UDP запрс. в качестве аргумента - переменная, содержащая macid (по умолчанию)
#endif

    // Add mDNS handler for WLED app
#ifndef ESP8266
//    embui.set_callback(CallBack::attach, CallBack::STAGotIP, wled_announce);
    WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t i){wled_announce(e, i);});
#endif

    // add WLED mobile app handler
    embui.server.on("/win", HTTP_ANY, [](AsyncWebServerRequest *request){ wled_handle(request); } );
    // 404 handler for WLED workaround
    embui.on_notfound( [](AsyncWebServerRequest *r){ return http_notfound(r);} );

    // EmbUI
    embui.begin(); // Инициализируем EmbUI фреймворк - загружаем конфиг, запускаем WiFi и все зависимые от него службы

#ifdef EMBUI_USE_MQTT
    //embui.mqtt(embui.param("m_pref")), embui.param("m_host")), embui.param("m_port")).toInt(), embui.param("m_user")), embui.param("m_pass")), mqttCallback, true); // false - никакой автоподписки!!!
    //embui.mqtt(mqttCallback, true);
    embui.mqtt(mqttCallback, mqttConnect, true);
#endif

    myLamp.effects.setEffSortType((SORT_TYPE)embui.paramVariant(TCONST_effSort).as<int>()); // сортировка должна быть определена до заполнения
    myLamp.effects.initDefault(); // если вызывать из конструктора, то не забыть о том, что нужно инициализировать Serial.begin(115200); иначе ничего не увидеть!
    myLamp.events.loadConfig();

#ifdef RTC
    rtc.init();
#endif

#ifdef DS18B20
    ds_setup();
#endif

    // restore matrix current limit from config
    myLamp.lamp_init();

#ifdef ESP_USE_BUTTON
    myLamp.setbPin(embui.paramVariant(TCONST_PINB));
    myButtons = new Buttons(myLamp.getbPin(), PULL_MODE, NORM_OPEN);
    if (!myButtons->loadConfig()) {
      default_buttons();
      myButtons->saveConfig();
    }
#endif

    myLamp.events.setEventCallback(event_worker);

    // configure and init attached devices
    gpio_setup();
    // restore matrix configuration from file and create a proper LED buffer
    led_fb_setup();

#ifdef ESP8266
  embui.server.addHandler(new SPIFFSEditor("esp8266"),"esp8266"), LittleFS));
#else
  embui.server.addHandler(new SPIFFSEditor(LittleFS, "esp32", "esp32"));
#endif

    sync_parameters();

  embui.setPubInterval(10);   // change periodic WebUI publish interval from EMBUI_PUB_PERIOD to 10 secs

#ifdef ENCODER
  enc_setup();
#endif

    LOG(println, "setup() done");
}   // End setup()


void loop() {
    embui.handle(); // цикл, необходимый фреймворку

    myLamp.handle(); // цикл, обработка лампы
#ifdef ENCODER
    encLoop(); // цикл обработки событий энкодера. Эта функция будет отправлять в УИ изменения, только тогда, когда подошло время ее loop
#endif

#ifdef RTC
    rtc.updateRtcTime();
#endif

#ifdef TM1637_CLOCK
    EVERY_N_SECONDS(1) {
        if (tm1637) tm1637->tm_loop();
    }
#endif
#ifdef DS18B20
    EVERY_N_MILLIS(1000*DS18B_READ_DELAY + 25) {
        ds_loop();
    }
#endif
#ifdef USE_STREAMING
    if (ledStream)
        ledStream->handle();
#endif
}

//------------------------------------------

#ifdef EMBUI_USE_MQTT
// реализация autodiscovery
String ha_autodiscovery()
{
    LOG(println,"MQTT: Autodiscovery"));
    DynamicJsonDocument hass_discover(1024);
    String name = embui.param(P_hostname);
    String unique_id = embui.mc;

    hass_discover["~")] = embui.id(TCONST_embui_);     // embui.param(P_m_pref) + "/embui/")
    hass_discover["name")] = name;                // name
    hass_discover["uniq_id")] = unique_id;        // String(ESP.getChipId(), HEX); // unique_id

    hass_discover["avty_t")] = "~pub/online");  // availability_topic
    hass_discover["pl_avail")] = "1");          // payload_available
    hass_discover["pl_not_avail")] = "0");      // payload_not_available

    hass_discover["cmd_t")] = "~set/on");       // command_topic
    hass_discover["stat_t")] = "~pub/on");      // state_topic
    hass_discover["pl_on")] = "1");             // payload_on
    hass_discover["pl_off")] = "0");            // payload_off

    hass_discover["json_attr_t")] = "~pub/state"); // json_attributes_topic

    hass_discover["rgb_cmd_t")] = "~set/rgb";        // rgb_command_topic
    hass_discover["rgb_stat_t")] = "~pub/rgb";       // rgb_state_topic

    hass_discover["bri_cmd_t")] = "~set/g_bright");     // brightness_command_topic
    hass_discover["bri_stat_t")] = "~pub/dynCtrl0");    // brightness_state_topic
    hass_discover["bri_scl")] = 255;

    JsonArray data = hass_discover.createNestedArray("effect_list"));
    data.add(TCONST_Normal);
    data.add(TCONST_Alarm);
    data.add(TCONST_Demo);
    data.add(TCONST_RGB);
    data.add(TCONST_White);
    data.add(TCONST_Other);

    //---------------------

    hass_discover["fx_cmd_t")] = "~set/mode");                                 // effect_command_topic
    hass_discover["fx_stat_t")] = "~pub/state");                               // effect_state_topic
    hass_discover["fx_tpl")] = "{{ value_json.Mode }}");                       // effect_template

    hass_discover["clr_temp_cmd_t")] = "~set/speed");     // speed as color temperature
    hass_discover["clr_temp_stat_t")] = "~pub/speed");    // speed as color temperature
    hass_discover["min_mireds")] = 1;
    hass_discover["max_mireds")] = 255;

    hass_discover["whit_val_cmd_t")] = "~set/scale");     // scale as white level (Яркость белого)
    hass_discover["whit_val_stat_t")] = "~pub/scale");    // scale as white level
    hass_discover["whit_val_scl")] = 255;

    // hass_discover["xy_cmd_t")] = "~set/speed");     // scale as white level (Яркость белого)
    // hass_discover["xy_stat_t")] = "~pub/speed");    // scale as white level
    //hass_discover["whit_val_scl")] = 255; // 'xy_val_tpl':          'xy_value_template',

    String hass_discover_str;
    serializeJson(hass_discover, hass_discover_str);
    hass_discover.clear();

    embui.publishto(String("homeassistant/light/")) + name + "/config"), hass_discover_str, true);
    return hass_discover_str;
}

extern void mqtt_dummy_connect();
void mqttConnect(){ 
    mqtt_dummy_connect();
    ha_autodiscovery();
}

ICACHE_FLASH_ATTR void mqttCallback(const String &topic, const String &payload){ // функция вызывается, когда приходят данные MQTT
  LOG(printf_P, PSTR("Message [%s - %s]\n"), topic.c_str() , payload.c_str());
  if(topic.startsWith(TCONST_embui_get_)){
    String sendtopic=topic;
    sendtopic.replace(TCONST_embui_get_, "");
    if(sendtopic==TCONST_eff_config){
        sendtopic=String(TCONST_embui_pub_)+sendtopic;
        String effcfg;
        if (fshlpr::getfseffconfig(myLamp.effects.getCurrent(), effcfg)) embui.publish(sendtopic, effcfg, true); // отправляем обратно в MQTT в топик embui/pub/
    } else if(sendtopic==TCONST_state){
        sendData();
    }
  }
}

// Periodic MQTT publishing
void sendData(){
    // Здесь отсылаем текущий статус лампы и признак, что она живая (keepalive)
    DynamicJsonDocument obj(512);
    //JsonObject obj = doc.to<JsonObject>();
    switch (myLamp.getMode())
    {
        case LAMPMODE::MODE_NORMAL :
            obj[TCONST_Mode] = TCONST_Normal;
            break;
        case LAMPMODE::MODE_ALARMCLOCK :
            obj[TCONST_Mode] = TCONST_Alarm;
            break;
        case LAMPMODE::MODE_DEMO :
            obj[TCONST_Mode] = TCONST_Demo;
            break;
        case LAMPMODE::MODE_RGBLAMP :
            obj[TCONST_Mode] = TCONST_RGB;
            break;
        case LAMPMODE::MODE_WHITELAMP :
            obj[TCONST_Mode] = TCONST_White;
            break;
        default:
            obj[TCONST_Mode] = TCONST_Other;
            break;
    }
    obj[TCONST_Time] = String(embui.timeProcessor.getFormattedShortTime());
    obj[TCONST_Memory] = String(myLamp.getLampState().freeHeap);
    obj[TCONST_Uptime] = String(embui.getUptime());
    obj[TCONST_RSSI] = String(myLamp.getLampState().rssi);
    obj[TCONST_Ip] = WiFi.localIP().toString();
    obj[TCONST_Mac] = WiFi.macAddress();
    obj[TCONST_Host] = String("http://"))+WiFi.localIP().toString();
    obj[TCONST_Version] = embui.getEmbUIver();
    obj[TCONST_MQTTTopic] = embui.id(TCONST_embui_);     // embui.param(P_m_pref) + "/embui/")
    String sendtopic=TCONST_embui_pub_;
    sendtopic+=TCONST_state;
    String out;
    serializeJson(obj, out);
    LOG(println, "send MQTT Data :"));
    LOG(println, out);
    embui.publish(sendtopic, out, true); // отправляем обратно в MQTT в топик embui/pub/
}
#endif

void gpio_setup(){
    DynamicJsonDocument doc(512);
    embuifs::deserializeFile(doc, TCONST_fcfg_gpio);
    int rxpin, txpin;

    // LED Strip setup
    if (doc[TCONST_mx_gpio]){
        // create new led strip object with our configured pin
        wsstrip = new ESP32RMT_WS2812B<COLOR_ORDER>(doc[TCONST_mx_gpio].as<int>());
    }

#ifdef MP3PLAYER
    // spawn an instance of mp3player
    rxpin = doc[TCONST_mp3rx] | -1;
    txpin = doc[TCONST_mp3tx] | -1;
    LOG(printf_P, PSTR("DFPlayer: rx:%d tx:%d\n"), rxpin, txpin);
    mp3 = new MP3PlayerDevice(rxpin, txpin, embui.paramVariant(TCONST_mp3volume) | DFPLAYER_DEFAULT_VOL );
#endif

#ifdef TM1637_CLOCK
    rxpin = doc[TCONST_tm_clk] | -1;
    txpin = doc[TCONST_tm_dio] | -1;
    if (rxpin != -1 && txpin != -1){
        tm1637 = new TMCLOCK(rxpin, txpin);
        tm1637->tm_setup();
    }
#endif 
}

void wled_announce(WiFiEvent_t cbEvent, WiFiEventInfo_t i){
    switch (cbEvent){
        case SYSTEM_EVENT_STA_GOT_IP:
            MDNS.addService("wled", "tcp", 80);
            MDNS.addServiceTxt("wled", "tcp", "mac", (const char*)embui.macid());
        default:;
    }
}

// rewriter for buggy WLED app
// https://github.com/Aircoookie/WLED-App/issues/37
bool http_notfound(AsyncWebServerRequest *request){
    if (request->url().indexOf("win&") != -1){
        String req(request->url());
        req.replace("win&", "win?");
        request->redirect(req);
        return true;
    }
    // not our case, no action was made
    return false;
}

void led_fb_setup(){
    if (mx) return;     // this function is not idempotent, so refuse to mess with existing buffer
    DynamicJsonDocument doc(256);
    embuifs::deserializeFile(doc, TCONST_fcfg_ledstrip);
    JsonObject o = doc.as<JsonObject>();

    Mtrx_cfg cfg(
        o[TCONST_width] | 16,   // in case deserialization has failed, I create a default 16x16 buffer 
        o[TCONST_height] | 16,
        o[TCONST_snake],
        o[TCONST_vertical],
        o[TCONST_vflip],
        o[TCONST_hflip]
    );
    LOG(printf, "LED cfg: w,h:(%d,%d) snake:%d, vert:%d, vflip:%d, hflip:%d\n", cfg.w(), cfg.h(), cfg.snake(), cfg.vertical(), cfg.vmirror(), cfg.hmirror());

    mx = new LedFB(cfg);

    if (wsstrip){
        // attach buffer to RMT engine
        cled = &FastLED.addLeds(wsstrip, mx->data(), mx->size());
        // hook framebuffer to contoller
        mx->bind(cled);
    }

    // replace the buffer for lamp object
    myLamp.setLEDbuffer(mx);
}
