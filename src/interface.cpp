/*
Copyright © 2023 Emil Muratov (vortigont)
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
#include "interface.h"
#include "effects.h"
#include "ui.h"
#include "extra_tasks.h"
#include "events.h"
#include "alarm.h"
#include "templates.hpp"

#ifdef TM1637_CLOCK
    #include "tm.h"				// Подключаем функции
#endif
#ifdef DS18B20
#include "DS18B20.h"			// термодатчик даллас
#endif
#ifdef ENCODER
    #include "enc.h"
#endif
#ifdef RTC
    #include "rtc.h"
#endif
#include LANG_FILE                  //"text_res.h"

#ifdef DS18B20
#include "DS18B20.h"
#endif

#include "basicui.h"
#include "actions.hpp"
#include <type_traits>
// задержка вывода ip адреса при включении лампы после перезагрузки
#define SHOWIP_DELAY    5
#define RESCHEDULE_DELAY    100         // async callback delay

#ifdef ESP8266
#define NUM_OUPUT_PINS  16
#define GPIO_NUM_NC -1
#endif

// numeric indexes for pages
enum class page : uint8_t {
    main = 0,
    eff_config,
    mike,
    setup_ledstrip,
    setup_dfplayer,
    setup_bttn,
    setup_encdr,
    setup_other,
    setup_esp,
    setup_gpio,
    _count,             // not a page but a len marker
    begin = 0,
    end = _count
};

// enumerator for gpio setup form
enum class gpio_device:uint8_t {
    ledstrip,
    dfplayer,
    mosfet,
    aux,
    tmdisplay
};

namespace INTERFACE {
// ------------- глобальные переменные построения интерфейса
// планировщик заполнения списка
Task *optionsTask = nullptr;        // задача для отложенной генерации списка
Task *delayedOptionTask = nullptr;  // текущая отложенная задача, для сброса при повторных входах
CtrlsTask *ctrlsTask = nullptr;       // планировщик контролов

static EffectListElem *confEff = nullptr; // эффект, который сейчас конфигурируется на странице "Управление списком эффектов"
static DEV_EVENT *cur_edit_event = NULL; // текущее редактируемое событие, сбрасывается после сохранения
// ------------- глобальные переменные построения интерфейса
} // namespace INTERFACE
using namespace INTERFACE;

/**
 * @brief enumerator with a files of effect lists for webui 
 * i.e. cached json files with effect names for drop down lists 
 */
enum class lstfile_t {
    selected,
    full,
    all
};

// forward declarations
void block_effects_main(Interface *interf, JsonObject *data);
void block_effect_params(Interface *interf, JsonObject *data);
void show_effects_config(Interface *interf, JsonObject *data);
void show_settings_mp3(Interface *interf, JsonObject *data);
void show_settings_enc(Interface *interf, JsonObject *data);
void page_gpiocfg(Interface *interf, JsonObject *data);
void page_settings_other(Interface *interf, JsonObject *data);
void section_sys_settings_frame(Interface *interf, JsonObject *data);
void show_settings_butt(Interface *interf, JsonObject *data);
void page_ledstrip_setup(Interface *interf, JsonObject *data);

/**
 * @brief rebuild cached json file with effects names list
 * i.e. used for sideloading in WebUI
 * @param full - rebuild full list or brief, excluding hidden effs
 * todo: implement an event queue
 */
void rebuild_effect_list_files(lstfile_t lst){
    if (delayedOptionTask)      // task is already running, skip
        return;
    // schedule a task to rebuild effects names list files
    delayedOptionTask = new Task(500, TASK_ONCE,
        [lst](){
            switch (lst){
                case lstfile_t::full :
                    build_eff_names_list_file(myLamp.effects, true);
                    if (embui.ws.count()){  // refresh UI page with a regenerated list
                        Interface interf(&embui, &embui.ws, 1024);
                        show_effects_config(&interf, nullptr);
                    }
                    break;
                case lstfile_t::all :
                    build_eff_names_list_file(myLamp.effects, true);
                    // intentionally fall-trough this to default
                default :
                    build_eff_names_list_file(myLamp.effects);
                    if (embui.ws.count()){  // refresh UI page with a regenerated list
                        Interface interf(&embui, &embui.ws, 1024);
                        section_main_frame(&interf, nullptr);
                    }
            }
        },
        &ts, true, nullptr, [](){ delayedOptionTask=nullptr; }, true
    );
}

// Функция преобразования для конфига
uint64_t stoull(const String &str){
    uint64_t tmp = 0;
    LOG(printf_P, PSTR("STOULL %s \n"), str.c_str());
    for (uint8_t i = 0; i < str.length(); i++){
        if (i)
            tmp *= 10;
        tmp += (int)str[i] - 48;
    }
    return tmp;
}
// Функция преобразования для конфига
String ulltos(uint64_t longlong){
    String bfr;
    while (longlong){
        int8_t i = longlong % 10;
        bfr += String(i);
        longlong /= 10;
    }
    String tmp;
    for (int i = bfr.length()-1; i >= 0; i--)
        tmp += (String)bfr[i];
    return tmp;
}

void resetAutoTimers(bool isEffects=false) // сброс таймера демо и настройка автосохранений
{
    myLamp.demoTimer(T_RESET);
    if(isEffects)
        myLamp.effects.autoSaveConfig();
}

/**
 * @brief when action is called to display a specific page
 * this selector picks and calls correspoding method
 * using common selector simplifes and reduces a number of registered actions required 
 * 
 */
void show_page_selector(Interface *interf, JsonObject *data){
    if (!interf || !data || (*data)[TCONST_sh_page].isNull()) return;  // quit if no section specified

    // get a page index
    page idx = static_cast<page>((*data)[TCONST_sh_page].as<int>());

    switch (idx){
        case page::eff_config :   // страница "Управление списком эффектов"
            show_effects_config(interf, nullptr);
            return;
    #ifdef MIC_EFFECTS
        case page::mike :         // страница настроек микрофона
            show_settings_mic(interf, nullptr);
            return;
    #endif
    #ifdef MP3PLAYER
        case page::setup_dfplayer :    // страница настроек dfplayer
            show_settings_mp3(interf, nullptr);
            return;
    #endif  // #ifdef MP3PLAYER
    #ifdef ESP_USE_BUTTON
        case page::setup_bttn :    // страница настроек кнопки
            show_settings_butt(interf, nullptr);
            return;
    #endif
    #ifdef ENCODER
        case page::setup_encdr :    // страница настроек кнопки
            show_settings_enc(interf, nullptr);
            return;
    #endif
        case page::setup_gpio :     // страница настроек GPIO
            return page_gpiocfg(interf, nullptr);
        case page::setup_esp :      // страница настроек ESP
            return section_sys_settings_frame(interf, nullptr);
        case page::setup_other :    // страница "настройки"-"другие"
            return page_settings_other(interf, nullptr);
        case page::setup_ledstrip : // led struip setup
            return page_ledstrip_setup(interf, nullptr);

        default:                    // by default simply show main page
            section_main_frame(interf, nullptr);
    }

}

/**
 * @brief - callback function that is triggered every EMBUI_PUB_PERIOD seconds via EmbUI scheduler
 * used to publish periodic updates to websocket clients, if any
 * 
 */
void pubCallback(Interface *interf){
    LOG(println, "pubCallback :");
    if (!interf) return;
    interf->json_frame_value();
    interf->value(TCONST_pTime, TimeProcessor::getInstance().getFormattedShortTime(), true);

#if !defined(ESP32) || !defined(BOARD_HAS_PSRAM)    
    #ifdef PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED
        uint32_t iram;
        uint32_t dram;
        {
            HeapSelectIram ephemeral;
            iram = ESP.getFreeHeap();
        }
        {
            HeapSelectDram ephemeral;
            dram = ESP.getFreeHeap();
        }
        interf->value(TCONST_pMem, String(dram)+" / "+String(iram), true);
    #else
        interf->value(TCONST_pMem, myLamp.getLampState().freeHeap, true);
    #endif
#else
    if(psramFound()){
        interf->value(TCONST_pMem, String(ESP.getFreeHeap())+" / "+String(ESP.getFreePsram()), true);
        LOG(printf_P, PSTR("Free PSRAM: %d\n"), ESP.getFreePsram());
    } else {
        interf->value(TCONST_pMem, myLamp.getLampState().freeHeap, true);
    }
#endif
    char fuptime[16];
    uint32_t tm = millis()/1000;
    sprintf_P(fuptime, PSTR("%u.%02u:%02u:%02u"),tm/86400,(tm/3600)%24,(tm/60)%60,tm%60);
    interf->value(TCONST_pUptime, fuptime, true);
    interf->value(TCONST_pFS, myLamp.getLampState().fsfreespace, true);
#ifdef DS18B20
    interf->value(TCONST_pTemp, String(getTemp())+"°C", true);
#endif
    int32_t rssi = myLamp.getLampState().rssi;
    interf->value(TCONST_pRSSI, String(constrain(map(rssi, -85, -40, 0, 100),0,100)) + "% (" + rssi + "dBm)", true);
    interf->json_frame_flush();
}

void block_menu(Interface *interf, JsonObject *data){
    if (!interf) return;
    // создаем меню
    interf->json_section_menu();

    interf->option(TCONST_effects, TINTF_000);        //  Эффекты
    interf->option(TCONST_lamptext, TINTF_001);       //  Вывод текста
    interf->option(TCONST_drawing, TINTF_0CE);        //  Рисование
#ifdef USE_STREAMING
    interf->option(TCONST_streaming, TINTF_0E2);      //  Трансляция
#endif
    interf->option(TCONST_show_event, TINTF_011);     //  События
    interf->option(TCONST_settings, TINTF_002);       //  настройки

    interf->json_section_end();
}

/**
 * блок с настройками параметров эффекта
 * выводится на странице "Управление списком эффектов"
 */
void block_effect_params(Interface *interf, JsonObject *data){
    if (!interf) return;

    String tmpSoundfile;    // tmpName,
    //myLamp.effects.loadeffname(tmpName,confEff->eff_nb);
    myLamp.effects.loadsoundfile(tmpSoundfile,confEff->eff_nb);

    interf->json_section_begin(TCONST_set_effect);

    interf->text(TCONST_effname, "", TINTF_effrename);       // поле под новое имя оставляем пустым
#ifdef MP3PLAYER
    interf->text(TCONST_soundfile, tmpSoundfile.c_str(), TINTF_0B2);
#endif
    interf->json_section_line();
        interf->checkbox(TCONST_eff_sel, confEff->canBeSelected(), TINTF_in_sel_lst);      // доступен для выбора в выпадающем списке на главной странице
        interf->checkbox(TCONST_eff_fav, confEff->isFavorite(), TINTF_in_demo);            // доступен в демо-режиме
    interf->json_section_end();

    interf->spacer();

    // sorting option
    interf->select(TCONST_effSort, TINTF_040);
        interf->option(SORT_TYPE::ST_BASE, TINTF_041);
        interf->option(SORT_TYPE::ST_END, TINTF_042);
        interf->option(SORT_TYPE::ST_IDX, TINTF_043);
        interf->option(SORT_TYPE::ST_AB, TINTF_085);
        interf->option(SORT_TYPE::ST_AB2, TINTF_08A);
#ifdef MIC_EFFECTS
        interf->option(SORT_TYPE::ST_MIC, TINTF_08D);  // эффекты с микрофоном
#endif
    interf->json_section_end();
    //interf->checkbox(TCONST_numInList, myLamp.getLampFlagsStuct().numInList , TINTF_090, false); // нумерация в списке эффектов
#ifdef MIC_EFFECTS
    //interf->checkbox(TCONST_effHasMic, myLamp.getLampFlagsStuct().effHasMic , TINTF_091, false); // значек микрофона в списке эффектов
#endif

    interf->button(button_t::submit, TCONST_set_effect, TINTF_Save, P_GRAY);            // Save btn
    interf->button_value(button_t::submit, TCONST_set_effect, TCONST_copy, TINTF_005);  // Copy button
    //if (confEff->eff_nb&0xFF00) { // пока удаление только для копий, но в теории можно удалять что угодно
        // interf->button_value(button_t::submit, TCONST_set_effect, TCONST_del_, TINTF_006, P_RED);
    //}

    interf->json_section_line();
        interf->button_value(button_t::submit, TCONST_set_effect, TCONST_delfromlist, TINTF_0B5, P_ORANGE);
        interf->button_value(button_t::submit, TCONST_set_effect, TCONST_delall, TINTF_0B4, P_RED);
    interf->json_section_end();

    interf->button_value(button_t::submit, TCONST_set_effect, TCONST_makeidx, TINTF_007, P_BLACK);

    interf->json_section_end(); // json_section_begin(TCONST_set_effect);
}

/**
 * @brief индексная страница WebUI
 * 
 */
void section_main_frame(Interface *interf, JsonObject *data){
    if (!interf) return;

    interf->json_frame_interface(); //TINTF_080);

    interf->json_section_manifest(TINTF_080, 0, LAMPFW_VERSION_STRING);       // app name/version manifest
    interf->json_section_end();

    block_menu(interf, data);
    block_effects_main(interf, data);

    interf->json_frame_flush();

    if(!(WiFi.getMode() & WIFI_MODE_STA))
    {
        // форсируем выбор вкладки настройки WiFi если контроллер не подключен к внешней AP
        interf->json_frame_interface();
            basicui::block_settings_netw(interf, data);
    }
}

/**
 * обработчик установок эффекта
 */
void set_effects_config_param(Interface *interf, JsonObject *data){
    if (!confEff || !data) return;
    EffectListElem *effect = confEff;
    
    //bool isNumInList =  (*data)[TCONST_numInList] == "1";
#ifdef MIC_EFFECTS
    bool isEffHasMic = (*data)[TCONST_effHasMic];
    myLamp.setEffHasMic(isEffHasMic);
#endif
    SORT_TYPE st = (*data)[TCONST_effSort].as<SORT_TYPE>();

    if(myLamp.getLampState().isInitCompleted){
        bool isRecreate = false;
        //isRecreate = myLamp.getLampFlagsStuct().numInList!=isNumInList;
#ifdef MIC_EFFECTS
        //isRecreate = (myLamp.getLampFlagsStuct().effHasMic!=isEffHasMic) || isRecreate;
#endif
        isRecreate = (myLamp.effects.getEffSortType()!=st) || isRecreate;

        if(isRecreate){
            myLamp.effects.setEffSortType(st);
            //myLamp.setNumInList(isNumInList);
            //myLamp.effects.removeLists();
            LOG(println, PSTR("Sort type changed, rebuilding eff list"));
            rebuild_effect_list_files(lstfile_t::all);
        }
    }
    //myLamp.setNumInList(isNumInList);

    SETPARAM(TCONST_effSort, myLamp.effects.setEffSortType(st));
    save_lamp_flags();
    
    String act = (*data)[TCONST_set_effect];
    // action is to "copy" effect
    if (act == TCONST_copy) {
        myLamp.effects.copyEffect(effect); // копируем текущий, это вызовет перестроение индекса
        LOG(println, PSTR("Effect copy, rebuild list"));
        rebuild_effect_list_files(lstfile_t::all);
        return;
    }
    
    // action is to "delete" effect
    if (act == TCONST_delfromlist || act == TCONST_delall) {
        uint16_t tmpEffnb = effect->eff_nb;
        LOG(printf_P,PSTR("delete effect->eff_nb=%d\n"), tmpEffnb);
        bool isCfgRemove = (act == TCONST_delall);

        if(tmpEffnb==myLamp.effects.getCurrent()){
            myLamp.effects.switchEffect(EFF_ENUM::EFF_NONE);
            run_action(ra::eff_next);
        }
        String tmpStr="- ";
        tmpStr+=tmpEffnb;
        myLamp.sendString(tmpStr.c_str(), CRGB::Red);
        confEff = myLamp.effects.getEffect(EFF_ENUM::EFF_NONE);
        if(isCfgRemove){
            myLamp.effects.deleteEffect(effect, true);  // удаляем эффект вместе с конфигом на ФС
            myLamp.effects.makeIndexFileFromList();     // создаем индекс по текущему списку и на выход
            //myLamp.effects.makeIndexFileFromFS();       // создаем индекс по файлам ФС и на выход
            rebuild_effect_list_files(lstfile_t::all);
        } else {
            myLamp.effects.deleteEffect(effect, false); // удаляем эффект только из активного списка
            myLamp.effects.makeIndexFileFromList();     // создаем индекс по текущему списку и на выход
            rebuild_effect_list_files(lstfile_t::selected);
        }
        return;
    }

    // action is "rebuild effects index"
    if (act == TCONST_makeidx) {
        myLamp.effects.removeLists();
        myLamp.effects.initDefault();
        LOG(println, PSTR("Force rebuild index"));
        rebuild_effect_list_files(lstfile_t::all);
        return;
    }
    
    // if selectivity changed, than need to rebuild json eff list for main page
    if ( (*data)[TCONST_eff_sel] != effect->canBeSelected() ){
        effect->canBeSelected((*data)[TCONST_eff_sel]);
        LittleFS.remove(TCONST_eff_list_json);
    }

    // could be used in demo
    effect->isFavorite((*data)[TCONST_eff_fav]);

    // set sound file, if any defined
    if ( !(*data)[TCONST_soundfile].isNull() ) myLamp.effects.setSoundfile((*data)[TCONST_soundfile], effect);

    // check if effect has been renamed
    if (!(*data)[TCONST_effname].isNull()){
        LOG(println, PSTR("Effect rename, rebuild list"));
        myLamp.effects.setEffectName((*data)[TCONST_effname], effect);
        // effect has been renamed, need to update BOTH dropdown list jsons
        myLamp.effects.makeIndexFileFromList(NULL, true);
        return show_effects_config(interf, nullptr);       // force reload setup page
    }

    resetAutoTimers();
    myLamp.effects.makeIndexFileFromList(); // обновить индексный файл после возможных изменений
    //section_main_frame(interf, nullptr);
}

/**
 * страница "Управление списком эффектов"
 * здесь выводится ПОЛНЫЙ список эффектов в выпадающем списке
 */
void show_effects_config(Interface *interf, JsonObject *data){
    if (!interf) return;

    interf->json_frame_interface();
    interf->json_section_main(TCONST_effects_config, TINTF_009);
    confEff = myLamp.effects.getSelectedListElement();

    if(LittleFS.exists(TCONST_eff_fulllist_json)){
        // формируем и отправляем кадр с запросом подгрузки внешнего ресурса
        interf->json_frame(TCONST_XLOAD);

        interf->json_section_content();
        interf->select(TCONST_effListConf, (int)confEff->eff_nb, TINTF_00A,
                        true,   // direct
                        TCONST_eff_fulllist_json
                );
        interf->json_section_end();

        // generate block with effect settings controls
        block_effect_params(interf, nullptr);
        interf->spacer();
        interf->button(button_t::generic, TCONST_effects, TINTF_00B);
        interf->json_frame_flush();
        return;
    }

    interf->constant("Rebuilding effects list, pls retry in a second...");
    interf->json_frame_flush();
    rebuild_effect_list_files(lstfile_t::full);
}

/**
 * @brief переключение эффекта в выпадающем списке на странице "управление списком эффектов"
 * т.к. страница остается таже, нужно только обновить значения нескольких полей значениями для нового эффекта
 */
void set_effects_config_list(Interface *interf, JsonObject *data){
    if (!interf || !data) return;

    // получаем номер выбраного эффекта 
    uint16_t num = (*data)[TCONST_effListConf].as<uint16_t>();

    if(confEff){ // если переключаемся, то сохраняем предыдущие признаки в эффект до переключения
        LOG(printf_P, PSTR("eff_sel: %d eff_fav : %d, new eff:%d\n"), (*data)[TCONST_eff_sel].as<bool>(),(*data)[TCONST_eff_fav].as<bool>(), num);
    }

    confEff = myLamp.effects.getEffect(num);

    //resetAutoTimers();

    // обновляем поля
    interf->json_frame_value();

#ifdef MP3PLAYER
    String tmpSoundfile;
    myLamp.effects.loadsoundfile(tmpSoundfile,confEff->eff_nb);
    interf->value(TCONST_soundfile, tmpSoundfile, false);
#endif
    interf->value(TCONST_eff_sel, confEff->canBeSelected(), false);          // доступен для выбора в выпадающем списке на главной странице
    interf->value(TCONST_eff_fav, confEff->isFavorite(), false);             // доступен в демо-режиме

    interf->json_frame_flush();
}

#ifdef EMBUI_USE_MQTT
void mqtt_publish_selected_effect_config_json(){
  if (!embui.isMQTTconected()) return;
  embui.publish(String(TCONST_embui_pub_) + TCONST_eff_config, myLamp.effects.getEffCfg().getSerializedEffConfig(), true);
}
#endif

/**
 * @brief UI block with current effect's controls
 * 
 */
void block_effect_controls(Interface *interf, JsonObject *data){
    // if no mqtt or ws clients, just quit
    if (!embui.isMQTTconected() && !embui.ws.count()) return;

    // there could be no ws clients connected
    if(interf) interf->json_section_begin(TCONST_eff_ctrls);

    // brightness control
    if(interf) interf->range(TCONST_GBR, static_cast<int>(myLamp.getBrightness()), 0, static_cast<int>(myLamp.getBrightnessScale()), 1, "Lamp Brightness", true);

    LList<std::shared_ptr<UIControl>> &controls = myLamp.effects.getControls();
    uint8_t ctrlCaseType; // тип контрола, старшие 4 бита соответствуют CONTROL_CASE, младшие 4 - CONTROL_TYPE
#ifdef MIC_EFFECTS
    bool isMicOn = myLamp.isMicOnOff();
    LOG(printf_P,PSTR("Make UI for %d controls\n"), controls.size());
    for(unsigned i=0; i<controls.size();i++)
        if(controls[i]->getId()==7 && controls[i]->getName().startsWith(TINTF_020))
            isMicOn = isMicOn && controls[i]->getVal().toInt();
#endif

    LOG(printf_P, PSTR("block_effect_controls() got %u ctrls\n"), controls.size());
    for (const auto &ctrl : controls){
        if (!ctrl->getId()) continue;       // skip old "brightness control"

        ctrlCaseType = ctrl->getType();
        switch(ctrlCaseType>>4){
            case CONTROL_CASE::HIDE :
                continue;
                break;
            case CONTROL_CASE::ISMICON :
#ifdef MIC_EFFECTS
                //if(!myLamp.isMicOnOff()) continue;
                if(!isMicOn && (!myLamp.isMicOnOff() || !(ctrl->getId()==7 && ctrl->getName().startsWith(TINTF_020)==1) )) continue;
#else
                continue;
#endif          
                break;
            case CONTROL_CASE::ISMICOFF :
#ifdef MIC_EFFECTS
                //if(myLamp.isMicOnOff()) continue;
                if(isMicOn && (myLamp.isMicOnOff() || !(ctrl->getId()==7 && ctrl->getName().startsWith(TINTF_020)==1) )) continue;
#else
                continue;
#endif   
                break;
            default: break;
        }

        bool isRandDemo = (myLamp.getLampFlagsStuct().dRand && myLamp.getMode()==LAMPMODE::MODE_DEMO);
        String ctrlId(TCONST_dynCtrl);
        ctrlId += ctrl->getId();
        String ctrlName = ctrl->getId() ? ctrl->getName() : TINTF_00D;

        switch(ctrlCaseType&0x0F){
            case CONTROL_TYPE::RANGE :
                {
                    if(isRandDemo && ctrl->getId()>0 && !(ctrl->getId()==7 && ctrl->getName().startsWith(TINTF_020)==1))
                        ctrlName=String(TINTF_0C9)+ctrlName;
                    int value = ctrl->getId() ? ctrl->getVal().toInt() : myLamp.getBrightness();
                    if(interf) interf->range( ctrlId, (long)value, ctrl->getMin().toInt(), ctrl->getMax().toInt(), ctrl->getStep().toInt(), ctrlName, true);
#ifdef EMBUI_USE_MQTT
                    embui.publish(String(TCONST_embui_pub_) + ctrlId, String(value), true);
#endif
                }
                break;
            case CONTROL_TYPE::EDIT :
                {
                    String ctrlName = ctrl->getName();
                    if(isRandDemo && ctrl->getId()>0 && !(ctrl->getId()==7 && ctrl->getName().startsWith(TINTF_020)==1))
                        ctrlName=String(TINTF_0C9)+ctrlName;
                    
                    if(interf) interf->text(String(TCONST_dynCtrl) + String(ctrl->getId())
                    , ctrl->getVal()
                    , ctrlName
                    );
#ifdef EMBUI_USE_MQTT
                    embui.publish(String(TCONST_embui_pub_) + ctrlId, ctrl->getVal(), true);
#endif
                    break;
                }
            case CONTROL_TYPE::CHECKBOX :
                {
                    String ctrlName = ctrl->getName();
                    if(isRandDemo && ctrl->getId()>0 && !(ctrl->getId()==7 && ctrl->getName().startsWith(TINTF_020)==1))
                        ctrlName=String(TINTF_0C9)+ctrlName;

                    if(interf) interf->checkbox(String(TCONST_dynCtrl) + ctrl->getId()
                    , ctrl->getVal() == "1" ? true : false
                    , ctrlName
                    , true
                    );
#ifdef EMBUI_USE_MQTT
                    embui.publish(String(TCONST_embui_pub_) + ctrlId, ctrl->getVal(), true);
#endif
                    break;
                }
            default:
#ifdef EMBUI_USE_MQTT
                embui.publish(String(TCONST_embui_pub_) + ctrlId, ctrl->getVal(), true);
#endif
                break;
        }
    }

    if(interf) interf->json_section_end();

#ifdef EMBUI_USE_MQTT
    // publish full effect config via mqtt
    mqtt_publish_selected_effect_config_json();
    if (embui.isMQTTconected()){
        embui.publish(String(TCONST_embui_pub_) + CMD_EFFECT, String(myLamp.effects.getEffnum()), true);
    }
#endif
    LOG(println, "eof block_effect_controls()");
}

void show_effect_controls(Interface *interf, JsonObject *data){
    LOG(println, "show_effect_controls()");
    if(interf) interf->json_frame_interface();
    block_effect_controls(interf, data);
    if(interf) interf->json_frame_flush();
}

/**
 * @brief Switch to specific effect
 * could be triggered via WebUI's selector list or via ra::eff_switch
 * if switched successfully, than this function calls contorls publishing via MQTT
 */
void set_switch_effect(Interface *interf, JsonObject *data){
    if (!data) return;
    LOG(println, "set_switch_effect()");

    /*
     if fader is in progress now, than we just skip switching,
     on one hand it prevents cyclic fast effect switching
     but not sure if this a good idea or bad, let's see
    */
    if (LEDFader::getInstance()->running()) return;

    uint16_t num = (*data)[TCONST_eff_run];
    EffectListElem *eff = myLamp.effects.getEffect(num);
    if (!eff) return;                                       // some unknown effect requested, quit
    uint16_t nextEff = myLamp.effects.getSelected();        // get next eff if there is fading in progress

    // сбросить флаг рандомного демо
    //myLamp.setDRand(myLamp.getLampFlagsStuct().dRand);

    LOG(printf_P, PSTR("UI EFF switch to:%d, selected:%d, isOn:%d, mode:%d\n"), eff->eff_nb, nextEff, myLamp.isLampOn(), myLamp.getMode());
    if (myLamp.isLampOn()) {
        myLamp.switcheffect(SW_SPECIFIC, myLamp.getFaderFlag(), eff->eff_nb);
    } else {
        myLamp.effects.switchEffect(eff->eff_nb); // переходим прямо на выбранный эффект если лампа "выключена"
    }

    // save curent active effect number in cfg if lamp in "normal" mode
    if(myLamp.getMode()==LAMPMODE::MODE_NORMAL)
        embui.var(TCONST_eff_run, (*data)[TCONST_eff_run]);
    resetAutoTimers();

    // publish new effect's controls to WebUI and MQTT
    show_effect_controls(interf, data);
}

// этот метод меняет контролы БЕЗ синхронизации со внешними системами
void direct_set_effects_dynCtrl(JsonObject *data){
    if (!data) return;

    String ctrlName;
    LList<std::shared_ptr<UIControl>>&controls = myLamp.effects.getControls();
    for(unsigned i=1; i<controls.size();i++){       // I skip first control here [0] as it's the old 'individual brightness'
        ctrlName = String(TCONST_dynCtrl)+String(controls[i]->getId());
        if((*data).containsKey(ctrlName)){
            if ((*data)[ctrlName].is<bool>() ){
                controls[i]->setVal((*data)[ctrlName] ? "1" : "0");     // больше стрингов во славу Богу стрингов!
            } else
                controls[i]->setVal((*data)[ctrlName]); // для всех остальных

            resetAutoTimers(true);
            if(myLamp.effects.worker) // && myLamp.effects.getCurrent()
                myLamp.effects.worker->setDynCtrl(controls[i].get());
            break;
        }
    }
}

void set_effects_dynCtrl(Interface *interf, JsonObject *data){
    if (!data) return;

    // попытка повышения стабильности, отдаем управление браузеру как можно быстрее...
    if((*data).containsKey(TCONST_force))
        direct_set_effects_dynCtrl(data);

    if(ctrlsTask){
        if(ctrlsTask->replaceIfSame(data)){
            ctrlsTask->restartDelayed();
            return;
        }
    }
   
    //LOG(println, "Delaying dynctrl");

    ctrlsTask = new CtrlsTask(data, 300, TASK_ONCE,
        [](){
            CtrlsTask *task = (CtrlsTask *)ts.getCurrentTask();
            JsonObject storage = task->getData();
            JsonObject *data = &storage; // task->getData();
            if(!data) return;

            LOG(print, "publishing & sending dynctrl: ");
            #ifdef LAMP_DEBUG
            String tmp; serializeJson(*data,tmp);LOG(println, tmp);
            #endif

            direct_set_effects_dynCtrl(data);
#ifdef EMBUI_USE_MQTT
            mqtt_publish_selected_effect_config_json();
            for (JsonPair kv : *data){
                embui.publish(String(TCONST_embui_pub_) + kv.key().c_str(), kv.value().as<String>(), true);
            }
#endif
            // отправка данных в WebUI
            {
                bool isLocalMic = false;
                Interface *interf = embui.ws.count()? new Interface(&embui, &embui.ws, 512) : nullptr;
                if (interf) {
                    interf->json_frame_value();
                    for (JsonPair kv : *data){
                        interf->value(kv.key().c_str(), kv.value(), false);
                        if( String("dynCtrl7") == kv.key().c_str() ) // будем считать что это микрофон дергается, тогда обновим все контролы
                            isLocalMic = true;
                    }
                    interf->json_frame_flush();
                    delete interf;
                }
                if(isLocalMic){
                    Interface *interf = embui.ws.count()? new Interface(&embui, &embui.ws, 1024) : nullptr;
                    show_effect_controls(interf, data);
                    delete interf;
                }
            }
            if(task==ctrlsTask)
                ctrlsTask = nullptr;

            delete task;
        },
        &ts,
        false
    );
    ctrlsTask->restartDelayed();
}

/**
 * Блок с наборами основных переключателей лампы
 * вкл/выкл, демо, кнопка и т.п.
 */
void block_main_flags(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_section_begin(TCONST_flags);
    interf->json_section_line("");
    interf->checkbox(TCONST_ONflag, myLamp.isLampOn(), TINTF_00E, true);
    interf->checkbox(TCONST_Demo, myLamp.getMode() == LAMPMODE::MODE_DEMO, TINTF_00F, true);
    interf->checkbox(TCONST_Events, myLamp.IsEventsHandled(), TINTF_011, true);
    interf->checkbox(TCONST_drawbuff, myLamp.isDrawOn(), TINTF_0CE, true);

#ifdef MIC_EFFECTS
    interf->checkbox(TCONST_Mic, myLamp.isMicOnOff(), TINTF_012, true);
#endif
    interf->checkbox_cfg(TCONST_AUX, TCONST_AUX, true);
#ifdef ESP_USE_BUTTON
    interf->checkbox(TCONST_Btn, myButtons->isButtonOn(), TINTF_013, true);
#endif
#ifdef MP3PLAYER
    interf->checkbox(TCONST_isOnMP3, myLamp.isONMP3(), TINTF_099, true);
#endif
#ifdef LAMP_DEBUG
    interf->checkbox(TCONST_debug, myLamp.isDebugOn(), TINTF_08E, true);
#endif
    // curve selector
    interf->select(TCONST_lcurve, e2int(myLamp.effects.getEffCfg().curve), "Luma curve", true);  // luma curve selector
    interf->option(0, "binary");
    interf->option(1, "linear");
    interf->option(2, "cie1931");
    interf->option(3, "exponent");
    interf->option(4, "sine");
    interf->option(5, "square");
    interf->json_section_end();     // select

    interf->json_section_end();
#ifdef MP3PLAYER
    interf->json_section_line("line124"); // спец. имя - разбирается внутри html
    if(mp3->isMP3Mode()){
        interf->button(button_t::generic, CMD_MP3_PREV, TINTF_0BD, P_GRAY);
        interf->button(button_t::generic, CMD_MP3_NEXT, TINTF_0BE, P_GRAY);
        interf->button(button_t::generic, TCONST_mp3_p5, TINTF_0BF, P_GRAY);
        interf->button(button_t::generic, TCONST_mp3_n5, TINTF_0C0, P_GRAY);
    }

    interf->json_section_end();
    // регулятор громкости mp3 плеера
    interf->range(TCONST_mp3volume, embui.paramVariant(TCONST_mp3volume).as<int>(), 1, 30, 1, TINTF_09B, true);
#endif
    interf->json_section_end();
}

/**
 * Формирование и вывод интерфейса с основными переключателями
 * вкл/выкл, демо, кнопка и т.п.
 */
void show_main_flags(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface();
    block_main_flags(interf, data);
    interf->spacer();
    interf->button(button_t::generic, TCONST_effects, TINTF_00B);
    interf->json_frame_flush();
}

/* Страница "Эффекты" (начальная страница)
    здесь выводится список эффектов который не содержит "скрытые" элементы
*/
void block_effects_main(Interface *interf, JsonObject *data){
    confEff = NULL; // т.к. не в конфигурировании, то сбросить данное значение
    if (!interf) return;

    interf->json_section_main(TCONST_effects, TINTF_000);

    interf->json_section_line(TCONST_flags);
    interf->checkbox(TCONST_ONflag, myLamp.isLampOn(), TINTF_00E, true);
    interf->button(button_t::generic, TCONST_show_flags, TINTF_014);
    interf->json_section_end();

    // 'next', 'prev' buttons << >>
    interf->json_section_line();
    interf->button(button_t::generic, TCONST_eff_prev, TINTF_015, TCONST__708090);
    interf->button(button_t::generic, TCONST_eff_next, TINTF_016, TCONST__5f9ea0);
    interf->json_section_end();


    if(LittleFS.exists(TCONST_eff_list_json)){
        // формируем и отправляем кадр с запросом подгрузки внешнего ресурса
        interf->json_frame(TCONST_XLOAD);
        interf->json_section_content();
        // side load drop-down list from /eff_list.json file
        interf->select(TCONST_eff_run, myLamp.effects.getEffnum(), TINTF_00A, true, TCONST_eff_list_json);
        interf->json_section_end();

        // build a block of controls for current effect
        block_effect_controls(interf, data);

        interf->button_value(button_t::generic, TCONST_sh_page, e2int(page::eff_config), TINTF_009);
        interf->json_section_end();
    } else {
        interf->constant("Rebuilding effects list, pls retry in a sec...");
        rebuild_effect_list_files(lstfile_t::selected);
    }

    interf->json_section_end();
}

void set_eff_prev(Interface *interf, JsonObject *data){
    // effect switch action call should be made in main loop to maintain thread safety
    Task *_t = new Task( RESCHEDULE_DELAY, TASK_ONCE, [](){ run_action(ra::eff_prev); }, &ts, false, nullptr, nullptr, true);
    _t->enableDelayed();
}

void set_eff_next(Interface *interf, JsonObject *data){
    // effect switch action call should be made in main loop to maintain thread safety
    Task *_t = new Task( RESCHEDULE_DELAY, TASK_ONCE, [](){ run_action(ra::eff_next); }, &ts, false, nullptr, nullptr, true);
    _t->enableDelayed();
}

/**
 * Обработка вкл/выкл лампы
 */
void set_onflag(Interface *interf, JsonObject *data){
    if (!data) return;
    bool newpower = (*data)[TCONST_ONflag];  // TOGLE_STATE((*data)[TCONST_ONflag], myLamp.isLampOn());
    if (newpower != myLamp.isLampOn()) {
        if (newpower) {
            // включаем через switcheffect, т.к. простого isOn недостаточно чтобы запустить фейдер и поменять яркость (при необходимости)
            myLamp.switcheffect(SW_SPECIFIC, myLamp.getFaderFlag(), myLamp.effects.getCurrent());
            myLamp.changePower(newpower);
            if (myLamp.getLampFlagsStuct().restoreState){
                save_lamp_flags();
            }
#ifdef MP3PLAYER
            if(myLamp.getLampFlagsStuct().isOnMP3)
                mp3->setIsOn(true);
#endif
#if !defined(ESP_USE_BUTTON) && !defined(ENCODER)
            if(millis()<20000){        // 10 секунд мало, как показала практика, ставим 20
                Task *_t = new Task(
                    SHOWIP_DELAY * TASK_SECOND,
                    TASK_ONCE, [](){ myLamp.sendString(WiFi.localIP().toString().c_str(), CRGB::White); },
                    &ts, false, nullptr, nullptr, true);
                _t->enableDelayed();
            }
#endif
#ifdef EMBUI_USE_MQTT
            embui.publish(String(TCONST_embui_pub_) + TCONST_on, "1", true);
            embui.publish(String(TCONST_embui_pub_) + TCONST_mode, String(myLamp.getMode()), true);
            embui.publish(String(TCONST_embui_pub_) + TCONST__demo, String(myLamp.getMode()==LAMPMODE::MODE_DEMO?"1":"0"), true);
#endif
        } else {
            resetAutoTimers();              // автосохранение конфига будет отсчитываться от этого момента
            myLamp.changePower(false);
            Task *_t = new Task(300, TASK_ONCE,
                                [](){ // при выключении бывает эксепшен, видимо это слишком длительная операция, разносим во времени и отдаем управление
                #ifdef MP3PLAYER
                                mp3->setIsOn(false);
                #endif
                if (myLamp.getLampFlagsStuct().restoreState){
                    save_lamp_flags();
                }
                #ifdef EMBUI_USE_MQTT
                                embui.publish(String(TCONST_embui_pub_) + TCONST_on, "0", true);
                                embui.publish(String(TCONST_embui_pub_) + TCONST_mode, String(myLamp.getMode()), true);
                                embui.publish(String(TCONST_embui_pub_) + TCONST__demo, String(myLamp.getMode()==LAMPMODE::MODE_DEMO?"1":"0"), true);
                #endif
                                },
                                &ts, false, nullptr, nullptr, true);
            _t->enableDelayed();
        }
    }
}

void set_demoflag(Interface *interf, JsonObject *data){
    if (!data) return;
    resetAutoTimers();
    // Специально не сохраняем, считаю что демо при старте не должно запускаться
    bool newdemo = (*data)[TCONST_Demo]; // TOGLE_STATE((*data)[TCONST_Demo], (myLamp.getMode() == LAMPMODE::MODE_DEMO));
    // сохраняем состояние демо если настроено сохранение
    if (myLamp.getLampFlagsStuct().restoreState)
        embui.var_dropnulls(TCONST_Demo, (*data)[TCONST_Demo].as<bool>());

    switch (myLamp.getMode()) {
        case LAMPMODE::MODE_OTA:
        case LAMPMODE::MODE_ALARMCLOCK:
        case LAMPMODE::MODE_NORMAL:
        case LAMPMODE::MODE_RGBLAMP:
            if(newdemo)
                myLamp.startDemoMode(embui.paramVariant(TCONST_DTimer) | DEFAULT_DEMO_TIMER);
            break;
        case LAMPMODE::MODE_DEMO:
            if(!newdemo)
                myLamp.startNormalMode();
            break;
        default:;
    }
    myLamp.setDRand(myLamp.getLampFlagsStuct().dRand);
#ifdef EMBUI_USE_MQTT
    embui.publish(String(TCONST_embui_pub_) + TCONST_mode, String(myLamp.getMode()), true);
    embui.publish(String(TCONST_embui_pub_) + TCONST__demo, String(myLamp.getMode()==LAMPMODE::MODE_DEMO? 1:0), true);
#endif
}

void set_auxflag(Interface *interf, JsonObject *data){
    if (!data) return;
    int pin = embui.paramVariant(TCONST_aux_gpio);
    if ( pin == -1) return;
    bool state = ( digitalRead(pin) == embui.paramVariant(TCONST_aux_ll) );

    if (((*data)[TCONST_AUX]) != state) {
        digitalWrite(pin, !state);
    }
}


void set_gbrflag(Interface *interf, JsonObject *data){
    // set brightness
    myLamp.setBrightness((*data)[TCONST_GBR]);

    // todo:
    // publisg
}

void set_lcurve(Interface *interf, JsonObject *data){
    myLamp.setLumaCurve(static_cast<luma::curve>((*data)[TCONST_lcurve].as<int>()));
    myLamp.effects.setLumaCurve(static_cast<luma::curve>((*data)[TCONST_lcurve].as<int>()));
}

/*
// since the following code manipulates embiu's config files, not lamp specific settings,
// this is no longer covers reqired functionality
// obsolete untill other implementation available
void block_lamp_config(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_section_hidden(TCONST_lamp_config, TINTF_018);

    interf->json_section_begin(TCONST_edit_lamp_config);
    String filename=embui.param(TCONST_fileName);
    String cfg(TINTF_018); cfg+=" ("; cfg+=filename; cfg+=")";

    // проверка на наличие конфигураций
        File tst = LittleFS.open(TCONST__backup_idx);
        if(tst.openNextFile())
        {
            interf->select(TCONST_fileName, cfg);
            File root = LittleFS.open(TCONST__backup_idx);
            File file = root.openNextFile();
            String fn;

            while (file) {
                fn=file.name();
                if(!file.isDirectory()){
                    fn.replace(TCONST__backup_idx_,"");
                    //LOG(println, fn);
                    interf->option(fn, fn);
                    file = root.openNextFile();
                }
            }
            interf->json_section_end(); // select

            interf->json_section_line();
                interf->button_value(button_t::submit, TCONST_edit_lamp_config, TCONST_load, TINTF_019, P_GREEN);
                interf->button_value(button_t::submit, TCONST_edit_lamp_config, TCONST_save, TINTF_Save);
                interf->button_value(button_t::submit, TCONST_edit_lamp_config, TCONST_delCfg, TINTF_006, P_RED);
            interf->json_section_end(); // json_section_line
            filename.clear();
            interf->spacer();
        }

    interf->json_section_begin(TCONST_add_lamp_config);
        interf->text(TCONST_fileName2, filename.c_str(), TINTF_01A);
        interf->button(button_t::submit, TCONST_add_lamp_config, TINTF_01B);
    interf->json_section_end();

    interf->json_section_end(); // json_section_begin
    interf->json_section_end(); // json_section_hidden
}

void show_lamp_config(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface();
    block_lamp_config(interf, data);
    interf->json_frame_flush();
}

void edit_lamp_config(Interface *interf, JsonObject *data){
    // Рбоата с конфигурациями в ФС
    if (!data) return;
    String name = (data->containsKey(TCONST_fileName) ? (*data)[TCONST_fileName] : (*data)[TCONST_fileName2]);
    String act = (*data)[TCONST_edit_lamp_config];

    if(name.isEmpty() || act.isEmpty())
        name = (*data)[TCONST_fileName2].as<String>();
    LOG(printf_P, PSTR("name=%s, act=%s\n"), name.c_str(), act.c_str());

    if(name.isEmpty()) return;

    if (act == TCONST_delCfg) { // удаление
        String filename(TCONST__backup_glb_);
        filename += name;
        if (LittleFS.begin()) LittleFS.remove(filename);

        filename = TCONST__backup_idx_;
        filename += name;
        if (LittleFS.begin()) LittleFS.remove(filename);

        filename = TCONST__backup_evn_;
        filename += name;
        if (LittleFS.begin()) LittleFS.remove(filename);
#ifdef ESP_USE_BUTTON
        filename = TCONST__backup_btn_;
        filename += name;
        if (LittleFS.begin()) LittleFS.remove(filename);
#endif
    } else if (act == TCONST_load) { // загрузка
        //myLamp.changePower(false);
        resetAutoTimers();

        String filename(TCONST__backup_glb_);
        filename += name;
        embui.load(filename.c_str());

        filename = TCONST__backup_idx_;
        filename += name;
        myLamp.effects.initDefault(filename.c_str());

        filename = TCONST__backup_evn_;
        filename += name;
        myLamp.events.loadConfig(filename.c_str());
#ifdef ESP_USE_BUTTON
        filename = TCONST__backup_btn_;
        filename += name;
        myButtons->clear();
        if (!myButtons->loadConfig()) {
            default_buttons();
        }
#endif
        //embui.var(TCONST_fileName, name);

        String str("CFG:");
        str += name;
        myLamp.sendString(str.c_str(), CRGB::Red);

        Task *_t = new Task(3*TASK_SECOND, TASK_ONCE, [](){ myLamp.effects.makeIndexFileFromList(); sync_parameters(); }, &ts, false, nullptr, nullptr, true);
        _t->enableDelayed();

    } else { // создание
        if(!name.endsWith(".json")){
            name.concat(".json");
        }

        String filename(TCONST__backup_glb_);
        filename += name;
        embui.save(filename.c_str(), true);

        filename = TCONST__backup_idx_;
        filename += name;
        myLamp.effects.makeIndexFileFromList(filename.c_str(), false);

        filename = TCONST__backup_evn_;
        filename += name;
        myLamp.events.saveConfig(filename.c_str());
#ifdef ESP_USE_BUTTON
        filename = TCONST__backup_btn_;
        filename += name;
        myButtons->saveConfig(filename.c_str());
#endif
    }

    show_lamp_config(interf, data);
}
*/

void block_lamp_textsend(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_section_begin(TCONST_textsend);

    interf->spacer(TINTF_01C);
    interf->text(TCONST_msg, P_EMPTY, TINTF_01D);
    interf->color(TCONST_txtColor, embui.paramVariant(TCONST_txtColor), TINTF_01E);
    interf->button(button_t::submit, TCONST_textsend, TINTF_01F, P_GRAY);

    interf->json_section_hidden(TCONST_text_config, TINTF_002);
        interf->json_section_begin(TCONST_edit_text_config);
            interf->spacer(TINTF_001);
                interf->range(TCONST_txtSpeed, 110-embui.paramVariant(TCONST_txtSpeed).as<int>(), 10, 100, 5, TINTF_044, false);
                interf->range(TCONST_txtOf, embui.paramVariant(TCONST_txtOf).as<int>(), -1, (int)(mx->cfg.h()>6?mx->cfg.h():6)-6, 1, TINTF_045);
                interf->range(TCONST_txtBfade, embui.paramVariant(TCONST_txtBfade).as<int>(), 0, 255, 1, TINTF_0CA);
                
            interf->spacer(TINTF_04E);
                interf->number(TCONST_ny_period, embui.paramVariant(TCONST_ny_period).as<int>(), TINTF_04F);
                //interf->number(TCONST_ny_unix, TINTF_050);
                String datetime;
                TimeProcessor::getDateTimeString(datetime, embui.paramVariant(TCONST_ny_unix));
                interf->text(TCONST_ny_unix, datetime.c_str(), TINTF_050);
                interf->button(button_t::submit, TCONST_edit_text_config, TINTF_Save, P_GRAY);
            interf->spacer();
                //interf->button(button_t::generic, TCONST_effects, TINTF_00B);
                interf->button(button_t::generic, TCONST_lamptext, TINTF_00B);
        interf->json_section_end();
    interf->json_section_end();

    interf->json_section_end();
}

void set_lamp_textsend(Interface *interf, JsonObject *data){
    if (!data) return;
    resetAutoTimers(); // откладываем автосохранения
    String tmpStr = (*data)[TCONST_txtColor];
    embui.var(TCONST_txtColor, tmpStr);
    embui.var(TCONST_msg, (*data)[TCONST_msg]);

    tmpStr.replace("#", "0x");
    myLamp.sendString((*data)[TCONST_msg], (CRGB::HTMLColorCode)strtol(tmpStr.c_str(), NULL, 0));
}

/**
 * @brief UI Draw on screen function
 * 
 */
void set_drawing(Interface *interf, JsonObject *data){
    // draw pixel
    if ((*data)[P_color]){
        CRGB c = strtol((*data)[P_color].as<const char*>(), NULL, 0);
        myLamp.writeDrawBuf(c, (*data)["col"], (*data)["row"]);
        return;
    }
    // screen solid fill
    if ((*data)[TCONST_fill]){
        CRGB val = strtol((*data)[TCONST_fill].as<const char*>(), NULL, 0);
        myLamp.fillDrawBuf(val);
    }
}

// clear draw buffer to solid balck
void set_clear(Interface *interf, JsonObject *data){
        CRGB color=CRGB::Black;
        myLamp.fillDrawBuf(color);
}

void block_lamptext(Interface *interf, JsonObject *data){
    //Страница "Вывод текста"
    if (!interf) return;
    interf->json_section_main(TCONST_lamptext, TINTF_001);

    block_lamp_textsend(interf, data);

    interf->json_section_end();
}

void set_text_config(Interface *interf, JsonObject *data){
    if (!data) return;
    (*data)[TCONST_txtSpeed] = 110 -(*data)[TCONST_txtSpeed].as<int>();
    SETPARAM(TCONST_txtSpeed, myLamp.setTextMovingSpeed((*data)[TCONST_txtSpeed].as<int>()));
    SETPARAM(TCONST_txtOf, myLamp.setTextOffset((*data)[TCONST_txtOf]));
    SETPARAM(TCONST_ny_period, myLamp.setNYMessageTimer((*data)[TCONST_ny_period]));
    SETPARAM(TCONST_txtBfade, myLamp.setBFade((*data)[TCONST_txtBfade]));

    String newYearTime = (*data)[TCONST_ny_unix]; // Дата/время наструпления нового года с интерфейса
    struct tm t;
    tm *tm=&t;
    localtime_r(TimeProcessor::now(), tm);  // reset struct to local now()

    // set desired date
    tm->tm_year = newYearTime.substring(0,4).toInt()-TM_BASE_YEAR;
    tm->tm_mon  = newYearTime.substring(5,7).toInt()-1;
    tm->tm_mday = newYearTime.substring(8,10).toInt();
    tm->tm_hour = newYearTime.substring(11,13).toInt();
    tm->tm_min  = newYearTime.substring(14,16).toInt();
    tm->tm_sec  = 0;

    time_t ny_unixtime = mktime(tm);
    LOG(printf_P, PSTR("Set New Year at %d %d %d %d %d (%ld)\n"), tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, ny_unixtime);

    embui.var(TCONST_ny_unix, ny_unixtime); myLamp.setNYUnixTime(ny_unixtime);

    if(!interf){
        interf = embui.ws.count()? new Interface(&embui, &embui.ws, 1024) : nullptr;
        //section_text_frame(interf, data);
        section_main_frame(interf, nullptr); // вернемся на главный экран (то же самое при начальном запуске)
        delete interf;
    } else
        section_text_frame(interf, data);
}

#ifdef MIC_EFFECTS
void block_settings_mic(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_section_main(TCONST_settings_mic, TINTF_020);

    interf->checkbox(TCONST_Mic, myLamp.isMicOnOff(), TINTF_012, true);

    interf->json_section_begin(TCONST_set_mic);
    if (!myLamp.isMicCalibration()) {
        interf->number_constrained(TCONST_micScale, round(myLamp.getLampState().getMicScale() * 100) / 100, TINTF_022, 0.01f, 0.0f, 2.0f);
        interf->number_constrained(TCONST_micNoise, round(myLamp.getLampState().getMicNoise() * 100) / 100, TINTF_023, 0.01f, 0.0f, 32.0f);
        interf->range (TCONST_micnRdcLvl, (int)myLamp.getLampState().getMicNoiseRdcLevel(), 0, 4, 1, TINTF_024, false);

        interf->button(button_t::submit, TCONST_set_mic, TINTF_Save, P_GRAY);
        interf->json_section_end();

        interf->spacer();
        interf->button(button_t::generic, TCONST_mic_cal, TINTF_025, P_RED);
    } else {
        interf->button(button_t::generic, TCONST_mic_cal, TINTF_027, P_RED );
    }

    interf->spacer();
    interf->button(button_t::generic, TCONST_settings, TINTF_00B);

    interf->json_section_end();
}

void show_settings_mic(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface();
    block_settings_mic(interf, data);
    interf->json_frame_flush();
}

void set_settings_mic(Interface *interf, JsonObject *data){
    if (!data) return;
    float scale = (*data)[TCONST_micScale]; //atof((*data)[TCONST_micScale].as<String>().c_str());
    float noise = (*data)[TCONST_micNoise]; //atof((*data)[TCONST_micNoise].as<String>().c_str());
    mic_noise_reduce_level_t rdl = static_cast<mic_noise_reduce_level_t>((*data)[TCONST_micnRdcLvl].as<unsigned>());

    // LOG(printf_P, PSTR("scale=%2.3f noise=%2.3f rdl=%d\n"),scale,noise,rdl);
    // String tmpStr;
    // serializeJson(*data, tmpStr);
    // LOG(printf_P, PSTR("*data=%s\n"),tmpStr.c_str());

    SETPARAM(TCONST_micScale, myLamp.getLampState().setMicScale(scale));
    SETPARAM(TCONST_micNoise, myLamp.getLampState().setMicNoise(noise));
    SETPARAM(TCONST_micnRdcLvl, myLamp.getLampState().setMicNoiseRdcLevel(rdl));

    basicui::section_settings_frame(interf, data);
    //section_settings_frame(interf, data);
}

void set_micflag(Interface *interf, JsonObject *data){
    if (!data) return;
    myLamp.setMicOnOff((*data)[TCONST_Mic]);
    save_lamp_flags();
    show_effect_controls(interf,data);
}

void set_settings_mic_calib(Interface *interf, JsonObject *data){
    //if (!data) return;
    if (!myLamp.isMicOnOff()) {
        myLamp.sendString(String(TINTF_026).c_str(), CRGB::Red);
    } else if(!myLamp.isMicCalibration()) {
        myLamp.sendString(String(TINTF_025).c_str(), CRGB::Red);
        myLamp.setMicCalibration();
    } else {
        myLamp.sendString(String(TINTF_027).c_str(), CRGB::Red);
    }

    show_settings_mic(interf, data);
}
#endif

#ifdef EMBUI_USE_MQTT
void set_settings_mqtt(Interface *interf, JsonObject *data){
    if (!data) return;
    basicui::set_settings_mqtt(interf,data);
    embui.mqttReconnect();
    int interval = (*data)[P_m_tupd];
    LOG(print, "New MQTT interval: "); LOG(println, interval);
    myLamp.setmqtt_int(interval);
    basicui::section_settings_frame(interf, data);
}
#endif

#ifdef EMBUI_USE_FTP
// настройка ftp
void set_ftp(Interface *interf, JsonObject *data){
    if (!data) return;

    basicui::set_ftp(interf, data);
    basicui::section_settings_frame(interf, data);
}
#endif

/**
 * @brief WebUI страница "Настройки" - "другие"
 * 
 */
void page_settings_other(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface();
    interf->json_section_main(TCONST_set_other, TINTF_002);
    
    interf->spacer(TINTF_030);

    interf->checkbox(TCONST_f_restore_state, myLamp.getLampFlagsStuct().restoreState, TINTF_f_restore_state, false);
    interf->checkbox(TCONST_isFaderON, myLamp.getLampFlagsStuct().isFaderON , TINTF_03D, false);
    interf->checkbox(TCONST_isClearing, myLamp.getLampFlagsStuct().isEffClearing , TINTF_083, false);
    interf->checkbox(TCONST_DRand, myLamp.getLampFlagsStuct().dRand , TINTF_03E, false);
    interf->checkbox(TCONST_showName, myLamp.getLampFlagsStuct().showName , TINTF_09A, false);

    interf->number_constrained(TCONST_brtScl, static_cast<int>(myLamp.getBrightnessScale()), "Brightness Scale", 1, 5, static_cast<int>(MAX_BRIGHTNESS));

    interf->range(TCONST_DTimer, embui.paramVariant(TCONST_DTimer).as<int>(), 30, 600, 15, TINTF_03F);
    float sf = embui.paramVariant(TCONST_spdcf);
    interf->range(TCONST_spdcf, sf, 0.25f, 4.0f, 0.25f, TINTF_0D3, false);

#ifdef TM1637_CLOCK
    interf->spacer(TINTF_0D4);
    interf->json_section_line();
        interf->checkbox(TCONST_tm24, myLamp.getLampFlagsStuct().tm24, TINTF_0D7, false);
        interf->checkbox(TCONST_tmZero, myLamp.getLampFlagsStuct().tmZero, TINTF_0D8, false);
    interf->json_section_end(); // line

    interf->json_section_line();
        interf->range(TCONST_tmBrightOn,  (int)myLamp.getBrightOn(),  0, 7, 1, TINTF_0D5, false);
        interf->range(TCONST_tmBrightOff, (int)myLamp.getBrightOff(), 0, 7, 1, TINTF_0D6, false);
    interf->json_section_end(); // line

    #ifdef DS18B20
    interf->checkbox(TCONST_ds18b20, myLamp.getLampFlagsStuct().isTempOn, TINTF_0E0, false);
    #endif
#endif
    interf->spacer(TINTF_0BA);

    interf->json_section_line();
        interf->range(TCONST_alarmP, (int)myLamp.getAlarmP(), 1, 15, 1, TINTF_0BB, false);     // рассвет длительность
        interf->range(TCONST_alarmT, (int)myLamp.getAlarmT(), 1, 15, 1, TINTF_0BC, false);     // рассвет светить после
    interf->json_section_end(); // line

    interf->button(button_t::submit, TCONST_set_other, TINTF_Save, P_GRAY);

    interf->spacer();
    interf->button(button_t::generic, TCONST_settings, TINTF_00B);

    interf->json_frame_flush();
}

void set_settings_other(Interface *interf, JsonObject *data){
    if (!data) return;
    resetAutoTimers();
        // LOG(printf_P,PSTR("Settings: %s\n"),tmpData.c_str());
        myLamp.setFaderFlag((*data)[TCONST_isFaderON]);
        myLamp.setClearingFlag((*data)[TCONST_isClearing]);
        myLamp.setDRand((*data)[TCONST_DRand]);
        myLamp.setShowName((*data)[TCONST_showName]);
    myLamp.setRestoreState((*data)[TCONST_f_restore_state]);

        SETPARAM(TCONST_DTimer, ({if (myLamp.getMode() == LAMPMODE::MODE_DEMO){ myLamp.demoTimer(T_ENABLE, embui.paramVariant(TCONST_DTimer)); } }) );

        float sf = (*data)[TCONST_spdcf];
        SETPARAM(TCONST_spdcf, myLamp.setSpeedFactor(sf));

        // save non-default brightness scale
        if ( (*data)[TCONST_brtScl] != DEF_BRT_SCALE ){
            embui.var(TCONST_brtScl, (*data)[TCONST_brtScl], true);
            myLamp.setBrightnessScale((*data)[TCONST_brtScl]);
        }

    #ifdef TM1637_CLOCK
        uint8_t tmBri = ((*data)[TCONST_tmBrightOn]).as<uint8_t>()<<4; // старшие 4 бита
        tmBri = tmBri | ((*data)[TCONST_tmBrightOff]).as<uint8_t>(); // младшие 4 бита
        embui.var(TCONST_tmBright, tmBri); myLamp.setTmBright(tmBri);
        myLamp.settm24((*data)[TCONST_tm24]);
        myLamp.settmZero((*data)[TCONST_tmZero]);
        #ifdef DS18B20
        myLamp.setTempDisp((*data)[TCONST_ds18b20]);
        #endif
    #endif

        uint8_t alatmPT = ((*data)[TCONST_alarmP]).as<uint8_t>()<<4; // старшие 4 бита
        alatmPT = alatmPT | ((*data)[TCONST_alarmT]).as<uint8_t>(); // младшие 4 бита
        embui.var(TCONST_alarmPT, alatmPT); myLamp.setAlarmPT(alatmPT);
        //SETPARAM(TCONST_alarmPT, myLamp.setAlarmPT(alatmPT));
        //LOG(printf_P, PSTR("alatmPT=%d, alatmP=%d, alatmT=%d\n"), alatmPT, myLamp.getAlarmP(), myLamp.getAlarmT());

        save_lamp_flags();

    if(interf)
        basicui::section_settings_frame(interf, data);
}
/*
// страницу-форму настроек времени строим методом фреймворка (ломает переводы, возвращено обратно)
void show_settings_time(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface();

    // Headline
    interf->json_section_main(TCONST_SET_TIME, TINTF_051);

    interf->comment(TINTF_052);     // комментарий-описание секции

    // сперва рисуем простое поле с текущим значением правил временной зоны из конфига
    interf->text(P_TZSET, TINTF_053);

    // user-defined NTP server
    interf->text(P_userntp, TINTF_054);
    // manual date and time setup
    interf->comment(TINTF_055);
    interf->datetime(P_DTIME, "", true);
    interf->hidden(P_DEVICEDATETIME,""); // скрытое поле для получения времени с устройства
    interf->button(button_t::submit, TCONST_SET_TIME, TINTF_Save, P_GRAY);

    interf->spacer();

    // exit button
    interf->button(button_t::generic, TCONST_SETTINGS, TINTF_00B);

    // close and send frame
    interf->json_section_end();
    interf->json_frame_flush();

    // формируем и отправляем кадр с запросом подгрузки внешнего ресурса со списком правил временных зон
    // полученные данные заместят предыдущее поле выпадающим списком с данными о всех временных зонах
    interf->json_frame_custom(TCONST_XLOAD);
    interf->json_section_content();
                    //id            val                         label   direct  skipl URL for external data
    interf->select(P_TZSET, embui.param(P_TZSET), "",     false,  true, "/js/tz.json");
    interf->json_section_end();
    interf->json_frame_flush();
}

void set_settings_time(Interface *interf, JsonObject *data){
    basicui::set_settings_time(interf, data);
    myLamp.sendString(String("%TM").c_str(), CRGB::Green);
#ifdef RTC
    rtc.updateRtcTime();
#endif
    basicui::section_settings_frame(interf, data);
}

void block_settings_update(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_section_hidden(T_DO_OTAUPD, TINTF_056);
    interf->spacer(TINTF_059);
    interf->file(T_DO_OTAUPD, T_DO_OTAUPD, TINTF_05A);
    interf->button(button_t::generic, TCONST_REBOOT, TINTF_096, !data ? P_RED : "");       // кнопка перехода в настройки времени
    interf->json_section_end();
}
*/

void block_settings_event(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_section_main(TCONST_show_event, TINTF_011);

    interf->checkbox(TCONST_Events, myLamp.IsEventsHandled(), TINTF_086, true);

    interf->json_section_begin(TCONST_event_conf);
    interf->select(TCONST_eventList, 0, TINTF_05B);

    int num = 0;
    LList<DEV_EVENT *> *events= myLamp.events.getEvents();
    for(unsigned i=0; i<events->size(); i++){
        interf->option(num, (*events)[i]->getName());
        ++num;
    }
    interf->json_section_end();

    interf->json_section_line();
    interf->button_value(button_t::submit, TCONST_event_conf, TCONST_edit, TINTF_05C, P_GREEN);
    interf->button_value(button_t::submit, TCONST_event_conf, TCONST_delete, TINTF_006, P_RED);
    interf->json_section_end();

    interf->json_section_end();

    interf->button(button_t::generic, TCONST_event_conf, TINTF_05D);

    interf->spacer();
    interf->button(button_t::generic, TCONST_effects, TINTF_00B);

    interf->json_section_end();
}

void show_settings_event(Interface *interf, JsonObject *data){
    if (!interf) return;

    if(cur_edit_event && !myLamp.events.isEnumerated(*cur_edit_event)){
        LOG(println, "Удалено временное событие!");
        delete cur_edit_event;
        cur_edit_event = NULL;
    } else {
        cur_edit_event = NULL;
    }

    interf->json_frame_interface();
    block_settings_event(interf, data);
    interf->json_frame_flush();
}

void set_eventflag(Interface *interf, JsonObject *data){
    if (!data) return;
    myLamp.setIsEventsHandled((*data)[TCONST_Events]);
    save_lamp_flags();
}

/**
 * @brief Save the event configuration data
 * save/update event data from WebUI
 */
void set_event_conf(Interface *interf, JsonObject *data){
    if (!data) return;
    DEV_EVENT event;
    String act;

    //String output;
    //serializeJson((*data), output);
    //LOG(println, output.c_str());

    if(cur_edit_event){
        myLamp.events.delEvent(*cur_edit_event);
    } else if (data->containsKey(TCONST_eventList)) {
        unsigned num = (*data)[TCONST_eventList];
        LList<DEV_EVENT *> *events = myLamp.events.getEvents();
        if(events->size()>num)
            events->remove(num);
    }

    if (data->containsKey(TCONST_enabled)) {
        event.isEnabled = ((*data)[TCONST_enabled]);
    } else {
        event.isEnabled = true;
    }

    event.d1 = ((*data)[TCONST_d1]);
    event.d2 = ((*data)[TCONST_d2]);
    event.d3 = ((*data)[TCONST_d3]);
    event.d4 = ((*data)[TCONST_d4]);
    event.d5 = ((*data)[TCONST_d5]);
    event.d6 = ((*data)[TCONST_d6]);
    event.d7 = ((*data)[TCONST_d7]);
    event.setEvent((EVENT_TYPE)(*data)[TCONST_evList].as<long>());
    event.setRepeat((*data)[TCONST_repeat]);
    event.setStopat((*data)[TCONST_stopat]);
    String tmEvent = (*data)[TCONST_tmEvent];

    struct tm t;
    tm *tm=&t;
    localtime_r(TimeProcessor::now(), tm);  // reset struct to local now()

    // set desired date
    tm->tm_year=tmEvent.substring(0,4).toInt()-TM_BASE_YEAR;
    tm->tm_mon = tmEvent.substring(5,7).toInt()-1;
    tm->tm_mday=tmEvent.substring(8,10).toInt();
    tm->tm_hour=tmEvent.substring(11,13).toInt();
    tm->tm_min=tmEvent.substring(14,16).toInt();
    tm->tm_sec=0;

    time_t ut = mktime(tm);
    event.setUnixtime(ut);
    LOG(printf_P, PSTR("Set Event at %4d-%2d-%2d %2d:%2d:00 -> %llu\n"), tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, (unsigned long long)ut);

    String buf; // внешний буффер, т.к. добавление эвента ниже
    switch(event.getEvent()){
        case EVENT_TYPE::ALARM: {
                DynamicJsonDocument doc(1024);
                doc[TCONST_alarmP] = (*data)[TCONST_alarmP];
                doc[TCONST_alarmT] = (*data)[TCONST_alarmT];
                doc[TCONST_msg] = (*data)[TCONST_msg];

#ifdef MP3PLAYER
                doc[TCONST_afS] = (*data)[TCONST_afS];
                doc[TCONST_lV] = (*data)[TCONST_lV];
                doc[TCONST_sT] = (*data)[TCONST_sT];
#endif
                serializeJson(doc,buf);
                buf.replace("\"","'");
                event.setMessage(buf);
                myLamp.events.addEvent(event);
            }
            break;
        case EVENT_TYPE::SEND_TIME: {
                DynamicJsonDocument doc(1024);
                doc[TCONST_isShowOff] = (*data)[TCONST_isShowOff];
#ifdef MP3PLAYER
                doc[TCONST_isPlayTime] = (*data)[TCONST_isPlayTime];
#endif
                serializeJson(doc,buf);
                buf.replace("\"","'");
                event.setMessage(buf);
                myLamp.events.addEvent(event);
            }
            break;
        default:
            event.setMessage((*data)[TCONST_msg] ? (*data)[TCONST_msg] : String());
            myLamp.events.addEvent(event);
            break;
    }
    myLamp.events.saveConfig();
    cur_edit_event = NULL;
    show_settings_event(interf, data);
}

void show_event_conf(Interface *interf, JsonObject *data){
    String act;
    bool edit = false;
    unsigned num = 0;
    if (!interf || !data) return;

    LOG(print,"event_conf="); LOG(println, (*data)[TCONST_event_conf].as<String>()); //  && data->containsKey(TCONST_event_conf)

    if (data->containsKey(TCONST_eventList)) {
        DEV_EVENT *curr = NULL;
        num = (*data)[TCONST_eventList];

        LList<DEV_EVENT *> *events = myLamp.events.getEvents();
        if(events->size()>num)
            curr = events->get(num);

        if (!curr) return;
        act = (*data)[TCONST_event_conf].as<String>();
        cur_edit_event = curr;
        edit = true;
    } else if(cur_edit_event != NULL){
        if(data->containsKey(TCONST_evList))
            cur_edit_event->setEvent((*data)[TCONST_evList].as<EVENT_TYPE>()); // меняем тип налету
        if(myLamp.events.isEnumerated(*cur_edit_event))
            edit = true;
    } else {
        LOG(println, "Созданан пустой эвент!");
        cur_edit_event = new DEV_EVENT();
    }

    if (act == TCONST_delete) {
        myLamp.events.delEvent(*cur_edit_event);
        cur_edit_event = NULL;
        myLamp.events.saveConfig();
        show_settings_event(interf, data);
        return;
    } else if (data->containsKey(TCONST_save)) {
        set_event_conf(interf, data);
        return;
    }

    interf->json_frame_interface();

    if (edit) {
        interf->json_section_main(TCONST_set_event, TINTF_05C);
        interf->constant(TCONST_eventList, cur_edit_event->getName(), num);
        interf->checkbox(TCONST_enabled, (cur_edit_event->isEnabled), TINTF_05E, false);
    } else {
        interf->json_section_main(TCONST_set_event, TINTF_05D);
    }

    interf->json_section_line();
        interf->select(TCONST_evList, cur_edit_event->getEvent(), TINTF_05F, true);
            interf->option(EVENT_TYPE::ON, TINTF_060);
            interf->option(EVENT_TYPE::OFF, TINTF_061);
            interf->option(EVENT_TYPE::DEMO, TINTF_062);
            interf->option(EVENT_TYPE::ALARM, TINTF_063);
            interf->option(EVENT_TYPE::SEND_TEXT, TINTF_067);
            interf->option(EVENT_TYPE::SEND_TIME, TINTF_068);
            interf->option(EVENT_TYPE::SET_EFFECT, TINTF_00A);
            interf->option(EVENT_TYPE::SET_WARNING, TINTF_0CB);
            interf->option(EVENT_TYPE::SET_GLOBAL_BRIGHT, TINTF_00C);
            interf->option(EVENT_TYPE::SET_WHITE_LO, TINTF_0EA);
            interf->option(EVENT_TYPE::SET_WHITE_HI, TINTF_0EB);
            interf->option(EVENT_TYPE::AUX_ON, TINTF_06A);
            interf->option(EVENT_TYPE::AUX_OFF, TINTF_06B);
            interf->option(EVENT_TYPE::AUX_TOGGLE, TINTF_06C);
            interf->option(EVENT_TYPE::LAMP_CONFIG_LOAD, TINTF_064);
            interf->option(EVENT_TYPE::EFF_CONFIG_LOAD, TINTF_065);
#ifdef ESP_USE_BUTTON
            interf->option(EVENT_TYPE::BUTTONS_CONFIG_LOAD, TINTF_0E9);
#endif
            interf->option(EVENT_TYPE::EVENTS_CONFIG_LOAD, TINTF_066);
            interf->option(EVENT_TYPE::PIN_STATE, TINTF_069);

        interf->json_section_end();
        interf->datetime(TCONST_tmEvent, cur_edit_event->getDateTime(), TINTF_06D);
    interf->json_section_end();
    interf->json_section_line();
        interf->number(TCONST_repeat, cur_edit_event->getRepeat(), TINTF_06E);
        interf->number(TCONST_stopat, cur_edit_event->getStopat(), TINTF_06F);
    interf->json_section_end();

    switch(cur_edit_event->getEvent()){
        case EVENT_TYPE::ALARM: {
                DynamicJsonDocument doc(1024);
                String buf = cur_edit_event->getMessage();
                buf.replace("'","\"");
                DeserializationError err = deserializeJson(doc,buf);
                int alarmP = !err && doc.containsKey(TCONST_alarmP) ? doc[TCONST_alarmP].as<uint8_t>() : myLamp.getAlarmP();
                int alarmT = !err && doc.containsKey(TCONST_alarmT) ? doc[TCONST_alarmT].as<uint8_t>() : myLamp.getAlarmT();
                String msg = !err && doc.containsKey(TCONST_msg) ? doc[TCONST_msg] : cur_edit_event->getMessage();

                interf->spacer(TINTF_0BA);
                interf->text(TCONST_msg, msg.c_str(), TINTF_070);
                interf->json_section_line();
                    interf->range(TCONST_alarmP, alarmP, 1, 15, 1, TINTF_0BB, false);
                    interf->range(TCONST_alarmT, alarmT, 1, 15, 1, TINTF_0BC, false);
                interf->json_section_end();
#ifdef MP3PLAYER
                bool limitAlarmVolume = !err && doc.containsKey(TCONST_lV) ? doc[TCONST_lV] : myLamp.getLampFlagsStuct().limitAlarmVolume;
                bool alarmFromStart = !err && doc.containsKey(TCONST_afS) ? doc[TCONST_afS] : true;
                int st = !err && doc[TCONST_sT] ? doc[TCONST_sT] : myLamp.getLampFlagsStuct().alarmSound;
                interf->json_section_line();
                    interf->checkbox(TCONST_afS, alarmFromStart, TINTF_0D1, false);
                    interf->checkbox(TCONST_lV, limitAlarmVolume, TINTF_0D2, false);
                interf->json_section_end();
                interf->select(TCONST_sT, st, TINTF_0A3, false);
                    interf->option(ALARM_SOUND_TYPE::AT_NONE, TINTF_09F);
                    interf->option(ALARM_SOUND_TYPE::AT_FIRST, TINTF_0A0);
                    interf->option(ALARM_SOUND_TYPE::AT_SECOND, TINTF_0A4);
                    interf->option(ALARM_SOUND_TYPE::AT_THIRD, TINTF_0A5);
                    interf->option(ALARM_SOUND_TYPE::AT_FOURTH, TINTF_0A6);
                    interf->option(ALARM_SOUND_TYPE::AT_FIFTH, TINTF_0A7);
                    interf->option(ALARM_SOUND_TYPE::AT_RANDOM, TINTF_0A1);
                    interf->option(ALARM_SOUND_TYPE::AT_RANDOMMP3, TINTF_0A2);
                interf->json_section_end();
#endif
            }
            break;
        case EVENT_TYPE::SEND_TIME: {
                DynamicJsonDocument doc(1024);
                String buf = cur_edit_event->getMessage();
                buf.replace("'","\"");
                DeserializationError err = deserializeJson(doc,buf);
                bool isShowOff  = !err && doc[TCONST_isShowOff];
                bool isPlayTime = !err && doc[TCONST_isPlayTime];

                interf->spacer("");

                interf->json_section_line();
                    interf->checkbox(TCONST_isShowOff, isShowOff, TINTF_0EC, false);
#ifdef MP3PLAYER
                    interf->checkbox(TCONST_isPlayTime, isPlayTime, TINTF_0ED, false);
#endif
                interf->json_section_end();
            }
            break;
        default:
            interf->text(TCONST_msg, cur_edit_event->getMessage().c_str(), TINTF_070);
            break;
    }
    interf->json_section_hidden(TCONST_repeat, TINTF_071);
        interf->json_section_line();
            interf->checkbox(TCONST_d1, (cur_edit_event->d1), TINTF_072, false);
            interf->checkbox(TCONST_d2, (cur_edit_event->d2), TINTF_073, false);
            interf->checkbox(TCONST_d3, (cur_edit_event->d3), TINTF_074, false);
            interf->checkbox(TCONST_d4, (cur_edit_event->d4), TINTF_075, false);
            interf->checkbox(TCONST_d5, (cur_edit_event->d5), TINTF_076, false);
            interf->checkbox(TCONST_d6, (cur_edit_event->d6), TINTF_077, false);
            interf->checkbox(TCONST_d7, (cur_edit_event->d7), TINTF_078, false);
        interf->json_section_end();
    interf->json_section_end();

    if (edit) {
        interf->hidden(TCONST_save, true); // режим редактирования
        interf->button(button_t::submit, TCONST_set_event, TINTF_079);
    } else {
        interf->hidden(TCONST_save, false); // режим добавления
        interf->button(button_t::submit, TCONST_set_event, TINTF_05D, P_GREEN);
    }

    interf->spacer();
    interf->button(button_t::generic, TCONST_show_event, TINTF_00B);

    interf->json_section_end();
    interf->json_frame_flush();
}

void set_eventlist(Interface *interf, JsonObject *data){
    if (!data) return;
    
    if(cur_edit_event && cur_edit_event->getEvent()!=(*data)[TCONST_evList].as<EVENT_TYPE>()){ // только если реально поменялось, то обновляем интерфейс
        show_event_conf(interf,data);
    } else if((*data).containsKey(TCONST_save)){ // эта часть срабатывает даже если нажата кнопка "обновить, следовательно ловим эту ситуацию"
        set_event_conf(interf, data); //через какую-то хитрую жопу отработает :)
    }
}

#ifdef ESP_USE_BUTTON
void set_gaugetype(Interface *interf, JsonObject *data){
        if (!data) return;
        myLamp.setGaugeType((*data)[TCONST_EncVG].as<GAUGETYPE>());
        save_lamp_flags();
    }

void block_settings_butt(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_section_main(TCONST_show_button, TINTF_013);

    interf->checkbox(TCONST_Btn, myButtons->isButtonOn(), TINTF_07B, true);
    interf->select(TCONST_EncVG, myLamp.getLampFlagsStuct().GaugeType, TINTF_0DD, true);
        interf->option(GAUGETYPE::GT_NONE, TINTF_0EE);
        interf->option(GAUGETYPE::GT_VERT, TINTF_0EF);
        interf->option(GAUGETYPE::GT_HORIZ, TINTF_0F0);
    interf->json_section_end();
    interf->spacer();

    interf->json_section_begin(TCONST_butt_conf);
    interf->select(TCONST_buttList, 0, TINTF_07A);
    for (int i = 0; i < myButtons->size(); i++) {
        interf->option(i, (*myButtons)[i]->getName());
    }
    interf->json_section_end();

    interf->json_section_line();
    interf->button_value(button_t::submit, TCONST_butt_conf, TCONST_edit, TINTF_05C, P_GREEN);
    interf->button_value(button_t::submit, TCONST_butt_conf, TCONST_delete, TINTF_006, P_RED);
    interf->json_section_end();

    interf->json_section_end();

    interf->button(button_t::generic, TCONST_butt_conf, TINTF_05D);

    interf->spacer();
    interf->button(button_t::generic, TCONST_settings, TINTF_00B);

    interf->json_section_end();
}

void show_settings_butt(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface();
    block_settings_butt(interf, data);
    interf->json_frame_flush();
}

void set_butt_conf(Interface *interf, JsonObject *data){
    if (!data) return;
    Button *btn = nullptr;
    bool on = ((*data)[TCONST_on]);
    bool hold = ((*data)[TCONST_hold]);
    bool onetime = ((*data)[TCONST_onetime]);
    uint8_t clicks = (*data)[TCONST_clicks];
    String param = (*data)[TCONST_bparam].as<String>();
    BA action = (BA)(*data)[TCONST_bactList].as<long>();

    if (data->containsKey(TCONST_buttList)) {
        int num = (*data)[TCONST_buttList];
        if (num < myButtons->size()) {
            btn = (*myButtons)[num];
        }
    }
    if (btn) {
        btn->action = action;
        btn->flags.on = on;
        btn->flags.hold = hold;
        btn->flags.click = clicks;
        btn->flags.onetime = onetime;
        btn->setParam(param);
    } else {
        myButtons->add(new Button(on, hold, clicks, onetime, action, param));
    }

    myButtons->saveConfig();
    show_settings_butt(interf, data);
}

void show_butt_conf(Interface *interf, JsonObject *data){
    if (!interf || !data) return;

    Button *btn = nullptr;
    String act;
    int num = 0;

    if (data->containsKey(TCONST_buttList)) {
        num = (*data)[TCONST_buttList];
        if (num < myButtons->size()) {
            act = (*data)[TCONST_butt_conf].as<String>();
            btn = (*myButtons)[num];
        }
    }

    if (act == TCONST_delete) {
        myButtons->remove(num);
        myButtons->saveConfig();
        show_settings_butt(interf, data);
        return;
    } else
    if (data->containsKey(TCONST_save)) {
        set_butt_conf(interf, data);
        return;
    }


    interf->json_frame_interface();

    if (btn) {
        interf->json_section_main(TCONST_set_butt, TINTF_05C);
        interf->constant(TCONST_buttList, num, btn->getName());
    } else {
        interf->json_section_main(TCONST_set_butt, TINTF_05D);
    }

    interf->select(TCONST_bactList, btn? btn->action : 0, TINTF_07A, false);
    for (int i = 1; i < BA::BA_END; i++) {
        interf->option(i, btn_get_desc((BA)i));
    }
    interf->json_section_end();

    interf->text(TCONST_bparam, btn? btn->getParam() : String(), TINTF_0B9);

    interf->checkbox(TCONST_on, btn? btn->flags.on : 0, TINTF_07C, false);
    interf->checkbox(TCONST_hold, btn? btn->flags.hold : 0, TINTF_07D, false);
    interf->number_constrained(TCONST_clicks, btn? btn->flags.click : 0, TINTF_07E, 1, 0, 7);
    interf->checkbox(TCONST_onetime, btn? btn->flags.onetime&1 : 0, TINTF_07F, false);

    if (btn) {
        interf->hidden(TCONST_save, true);
        interf->button(button_t::submit, TCONST_set_butt, TINTF_079);
    } else {
        interf->button(button_t::submit, TCONST_set_butt, TINTF_05D, P_GREEN);
    }

    interf->spacer();
    interf->button_value(button_t::generic, TCONST_sh_page, e2int(page::setup_bttn), TINTF_00B);

    interf->json_section_end();
    interf->json_frame_flush();
}

void set_btnflag(Interface *interf, JsonObject *data){
    if (!data) return;
    //SETPARAM(TCONST_Btn, myButtons->setButtonOn((*data)[TCONST_Btn] == "1"));
    bool isSet = (*data)[TCONST_Btn];
    myButtons->setButtonOn(isSet);
    myLamp.setButton(isSet);
    save_lamp_flags();
}
#endif  // BUTTON

#ifdef ENCODER
void block_settings_enc(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_section_main(TCONST_set_enc, TINTF_0DC);

    interf->select(TCONST_EncVG, myLamp.getLampFlagsStuct().GaugeType, TINTF_0DD, true);
        interf->option(GAUGETYPE::GT_NONE, TINTF_0EE);
        interf->option(GAUGETYPE::GT_VERT, TINTF_0EF);
        interf->option(GAUGETYPE::GT_HORIZ, TINTF_0F0);
    interf->json_section_end();
    interf->color(TCONST_EncVGCol, embui.paramVariant(TCONST_EncVGCol), TINTF_0DE);
    interf->spacer();

    interf->color(TCONST_encTxtCol, embui.paramVariant(TCONST_encTxtCol), TINTF_0DF);
    interf->range(TCONST_encTxtDel, 110-getEncTxtDelay(), 10, 100, 5, TINTF_044, false);
    interf->button(button_t::submit, TCONST_set_enc, TINTF_Save, P_GRAY);
    interf->spacer();
    interf->button(button_t::generic, TCONST_settings, TINTF_00B);
    interf->json_section_end();
}
void show_settings_enc(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface();
    block_settings_enc(interf, data);
    interf->json_frame_flush();
}
void set_settings_enc(Interface *interf, JsonObject *data){
    if (!data) return;

    myLamp.setGaugeType((*data)[TCONST_EncVG].as<GAUGETYPE>());
    save_lamp_flags();
    SETPARAM(TCONST_EncVGCol);
    String tmpStr = (*data)[TCONST_EncVGCol];
    tmpStr.replace("#", "0x");
    GAUGE::GetGaugeInstance()->setGaugeTypeColor((CRGB)strtol(tmpStr.c_str(), NULL, 0));

    SETPARAM(TCONST_encTxtCol);
    String tmpStr2 = (*data)[TCONST_encTxtCol];
    tmpStr2.replace("#", "0x");
    setEncTxtColor((CRGB)strtol(tmpStr2.c_str(), NULL, 0));
    (*data)[TCONST_encTxtDel]=JsonUInt(110U-(*data)[TCONST_encTxtDel].as<int>());
    SETPARAM(TCONST_encTxtDel, setEncTxtDelay((*data)[TCONST_encTxtDel]))
    basicui::section_settings_frame(interf, data);
}
#endif  // ENCODER

void set_debugflag(Interface *interf, JsonObject *data){
    if (!data) return;
    myLamp.setDebug((*data)[TCONST_debug]);
    save_lamp_flags();
}

void set_drawflag(Interface *interf, JsonObject *data){
    if (!data) return;
    myLamp.setDraw((*data)[TCONST_drawbuff]);
    save_lamp_flags();
}

#ifdef MP3PLAYER
// show page with MP3 Player setup
void show_settings_mp3(Interface *interf, JsonObject *data){
    if (!interf) return;

    interf->json_frame_interface();
    interf->json_section_main(TCONST_settings_mp3, TINTF_099);

    // volume
    interf->range(TCONST_mp3volume, embui.paramVariant(TCONST_mp3volume).as<int>(), 1, 30, 1, TINTF_09B, true);

    // выключатель и статус плеера
    interf->json_section_line(); // расположить в одной линии
        interf->checkbox(TCONST_isOnMP3, myLamp.isONMP3(), TINTF_099, true);
        // show message if DFPlayer is not available
        if (!mp3->isReady())
            interf->constant("DFPlayer is not connected, not ready or not responding :(");
        else
            interf->constant("DFPlayer player: Connected");
    interf->json_section_end();

    interf->json_section_begin(TCONST_set_mp3);
    interf->spacer(TINTF_0B1);
    interf->json_section_line(); // расположить в одной линии
        interf->checkbox(TCONST_playName, myLamp.getLampFlagsStuct().playName , TINTF_09D, false);
        interf->checkbox(TCONST_playEffect, myLamp.getLampFlagsStuct().playEffect , TINTF_09E, false);
        interf->checkbox(TCONST_playMP3, myLamp.getLampFlagsStuct().playMP3 , TINTF_0AF, false);
    interf->json_section_end();

    interf->json_section_line(); // время/будильник
    interf->select(TCONST_playTime, myLamp.getLampFlagsStuct().playTime, TINTF_09C, false);
    interf->option(TIME_SOUND_TYPE::TS_NONE, TINTF_0B6);
    interf->option(TIME_SOUND_TYPE::TS_VER1, TINTF_0B7);
    interf->option(TIME_SOUND_TYPE::TS_VER2, TINTF_0B8);
    interf->json_section_end();

    interf->select(TCONST_alarmSound, myLamp.getLampFlagsStuct().alarmSound, TINTF_0A3, false);
    interf->option(ALARM_SOUND_TYPE::AT_NONE, TINTF_09F);
    interf->option(ALARM_SOUND_TYPE::AT_FIRST, TINTF_0A0);
    interf->option(ALARM_SOUND_TYPE::AT_SECOND, TINTF_0A4);
    interf->option(ALARM_SOUND_TYPE::AT_THIRD, TINTF_0A5);
    interf->option(ALARM_SOUND_TYPE::AT_FOURTH, TINTF_0A6);
    interf->option(ALARM_SOUND_TYPE::AT_FIFTH, TINTF_0A7);
    interf->option(ALARM_SOUND_TYPE::AT_RANDOM, TINTF_0A1);
    interf->option(ALARM_SOUND_TYPE::AT_RANDOMMP3, TINTF_0A2);
    interf->json_section_end();
    interf->json_section_end(); // время/будильник

    interf->checkbox(TCONST_limitAlarmVolume, myLamp.getLampFlagsStuct().limitAlarmVolume , TINTF_0B3, false);

    interf->json_section_line();
        interf->select(TCONST_eqSetings, myLamp.getLampFlagsStuct().MP3eq, TINTF_0A8, false);
        interf->option(DFPLAYER_EQ_NORMAL, TINTF_0A9);
        interf->option(DFPLAYER_EQ_POP, TINTF_0AA);
        interf->option(DFPLAYER_EQ_ROCK, TINTF_0AB);
        interf->option(DFPLAYER_EQ_JAZZ, TINTF_0AC);
        interf->option(DFPLAYER_EQ_CLASSIC, TINTF_0AD);
        interf->option(DFPLAYER_EQ_BASS, TINTF_0AE);
        interf->json_section_end();
        
        interf->number(TCONST_mp3count, mp3->getMP3count(), TINTF_0B0);
    interf->json_section_end();

    interf->button(button_t::submit, TCONST_set_mp3, TINTF_Save, P_GRAY);
    interf->json_section_end();

    interf->spacer();
    interf->button(button_t::generic, TCONST_settings, TINTF_00B);

    interf->json_frame_flush();
}

void set_settings_mp3(Interface *interf, JsonObject *data){
    if (!data) return;

    resetAutoTimers(); // сдвинем таймеры автосейва, т.к. длительная операция
    uint8_t val = (*data)[TCONST_eqSetings].as<uint8_t>();
    myLamp.setEqType(val);
    mp3->setEqType(val); // пишет в плеер!

    myLamp.setPlayTime((*data)[TCONST_playTime].as<int>());
    myLamp.setPlayName((*data)[TCONST_playName]);
    myLamp.setPlayEffect((*data)[TCONST_playEffect]); mp3->setPlayEffect(myLamp.getLampFlagsStuct().playEffect);
    myLamp.setAlatmSound((ALARM_SOUND_TYPE)(*data)[TCONST_alarmSound].as<int>());
    myLamp.setPlayMP3((*data)[TCONST_playMP3]); mp3->setPlayMP3(myLamp.getLampFlagsStuct().playMP3);
    myLamp.setLimitAlarmVolume((*data)[TCONST_limitAlarmVolume]);

    SETPARAM(TCONST_mp3count, mp3->setMP3count((*data)[TCONST_mp3count].as<int>())); // кол-во файлов в папке мп3

    save_lamp_flags();
    basicui::section_settings_frame(interf, data);
    //section_settings_frame(interf, data);
}

void set_mp3flag(Interface *interf, JsonObject *data){
    if (!data) return;
    myLamp.setONMP3((*data)[TCONST_isOnMP3]);
    if(myLamp.isLampOn())
        mp3->setIsOn(myLamp.isONMP3(), true); // при включенной лампе - форсировать воспроизведение
    else {
        mp3->setIsOn(myLamp.isONMP3(), false); // при выключенной - не форсировать, но произнести время, но не ранее чем через 10с после перезагрузки
        if(myLamp.isONMP3() && millis()>10000)
            if( !data->containsKey(TCONST_force) || (*data)[TCONST_force] ) // при наличие force="1" или без этого ключа
                mp3->playTime(TimeProcessor::getInstance().getHours(), TimeProcessor::getInstance().getMinutes(), (TIME_SOUND_TYPE)myLamp.getLampFlagsStuct().playTime);
    }
    save_lamp_flags();
}

void set_mp3volume(Interface *interf, JsonObject *data){
    if (!data) return;
    int volume = (*data)[TCONST_mp3volume];
    embui.var(TCONST_mp3volume, volume, true);
    mp3->setVolume(volume);
}

void set_mp3_player(Interface *interf, JsonObject *data){
    if (!data) return;

    if(!myLamp.isONMP3()) return;
    uint16_t cur_palyingnb = mp3->getCurPlayingNb();
    if(data->containsKey(CMD_MP3_PREV)){
        mp3->playEffect(cur_palyingnb-1,"");
    } else if(data->containsKey(CMD_MP3_NEXT)){
        mp3->playEffect(cur_palyingnb+1,"");
    } else if(data->containsKey(TCONST_mp3_p5)){
        mp3->playEffect(cur_palyingnb-5,"");
    } else if(data->containsKey(TCONST_mp3_n5)){
        mp3->playEffect(cur_palyingnb+5,"");
    }
}
#endif

/*
    сохраняет настройки GPIO и перегружает контроллер
 */
void set_gpios(Interface *interf, JsonObject *data){
    if (!data) return;

    DynamicJsonDocument doc(512);
    if (!embuifs::deserializeFile(doc, TCONST_fcfg_gpio)) doc.clear();     // reset if cfg is broken or missing
    //LOG(printf, "Set GPIO configuration %d\n", (*data)[TCONST_set_gpio].as<int>());

    gpio_device dev = static_cast<gpio_device>((*data)[TCONST_set_gpio].as<int>());
    switch(dev){
#ifdef MP3PLAYER
        // DFPlayer gpios
        case gpio_device::dfplayer : {
            // save pin numbers into config file if present/valid
            if ( (*data)[TCONST_mp3rx] == static_cast<int>(GPIO_NUM_NC) ) doc.remove(TCONST_mp3rx);
            else doc[TCONST_mp3rx] = (*data)[TCONST_mp3rx];

            if ( (*data)[TCONST_mp3tx] == static_cast<int>(GPIO_NUM_NC) ) doc.remove(TCONST_mp3tx);
            else doc[TCONST_mp3tx] = (*data)[TCONST_mp3tx];
            break;
        }
#endif
        // MOSFET gpios
        case gpio_device::mosfet : {
            if ( (*data)[TCONST_mosfet_gpio] == static_cast<int>(GPIO_NUM_NC) ) doc.remove(TCONST_mosfet_gpio);
            else doc[TCONST_mosfet_gpio] = (*data)[TCONST_mosfet_gpio];

            doc[TCONST_mosfet_ll] = (*data)[TCONST_mosfet_ll];
            break;
        }
        // AUX gpios
        case gpio_device::aux : {
            if ( (*data)[TCONST_aux_gpio] == static_cast<int>(GPIO_NUM_NC) ) doc.remove(TCONST_aux_gpio);
            else doc[TCONST_aux_gpio] = (*data)[TCONST_aux_gpio];

            doc[TCONST_aux_ll] = (*data)[TCONST_aux_ll];
            break;
        }
#ifdef TM1637_CLOCK
        // TM1637 gpios
        case gpio_device::tmdisplay : {
            // save pin numbers into config file if present/valid
            if ( (*data)[TCONST_tm_clk] == static_cast<int>(GPIO_NUM_NC) ) doc.remove(TCONST_tm_clk);
            else doc[TCONST_tm_clk] = (*data)[TCONST_tm_clk];

            if ( (*data)[TCONST_tm_dio] == static_cast<int>(GPIO_NUM_NC) ) doc.remove(TCONST_tm_dio);
            else doc[TCONST_tm_dio] = (*data)[TCONST_tm_dio];
            break;
        }
#endif
        // WS LED strip gpios
        case gpio_device::ledstrip : {
            if ( (*data)[TCONST_mx_gpio] == static_cast<int>(GPIO_NUM_NC) ) doc.remove(TCONST_mx_gpio);
            else doc[TCONST_mx_gpio] = (*data)[TCONST_mx_gpio];
            break;
        }
        default :
            return;     // for any uknown action - just quit
    }

    // save resulting config
    embuifs::serialize2file(doc, TCONST_fcfg_gpio);

    run_action(ra::reboot);         // reboot in 5 sec
    basicui::section_settings_frame(interf, nullptr);
}


void section_effects_frame(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface(); //TINTF_080);
    block_effects_main(interf, data);
    interf->json_frame_flush();
}

void section_text_frame(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface(); //TINTF_080);
    block_lamptext(interf, data);
    interf->json_frame_flush();
}

//Страница "Рисование"
void page_drawing(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface();  //TINTF_080);
    interf->json_section_main(TCONST_drawing, TINTF_0CE);

    StaticJsonDocument<256>doc;
    JsonObject param = doc.to<JsonObject>();

    param[TCONST_width] = mx->cfg.w();
    param[TCONST_height] = mx->cfg.h();
    param[TCONST_blabel] = TINTF_0CF;
    param[TCONST_drawClear] = TINTF_0D9;

    interf->checkbox(TCONST_drawbuff, myLamp.isDrawOn(), TINTF_0CE, true);
    interf->div(TCONST_drawing, TCONST_drawing, embui.param(TCONST_txtColor), TINTF_0D0, P_EMPTY, param);

    interf->json_frame_flush();
}

#ifdef USE_STREAMING
void block_streaming(Interface *interf, JsonObject *data){
    //Страница "Трансляция"
    interf->json_section_main(TCONST_streaming, TINTF_0E2);
        interf->json_section_line();
            interf->checkbox(TCONST_ONflag, myLamp.isLampOn(), TINTF_00E, true);
            interf->checkbox(TCONST_isStreamOn, myLamp.isStreamOn(), TINTF_0E2, true);
            interf->checkbox(TCONST_direct, myLamp.isDirect(), TINTF_0E6, true);
            interf->checkbox(TCONST_mapping, myLamp.isMapping(), TINTF_0E7, true);
        interf->json_section_end();
        interf->select(TCONST_stream_type, embui.paramVariant(TCONST_stream_type), TINTF_0E3, true);
            interf->option(E131, TINTF_0E4);
            interf->option(SOUL_MATE, TINTF_0E5);
        interf->json_section_end();
        interf->range(TCONST_bright, (String)myLamp.getBrightness(), 0, 255, 1, (String)TINTF_00D, true);
        if (embui.paramVariant(TCONST_stream_type).toInt() == E131){
            interf->range(TCONST_Universe, embui.paramVariant(TCONST_Universe), 1, 255, 1, TINTF_0E8, true);
            int uni = mx->cfg.h() / (512U / (mx->cfg.w() * 3)) + !!mx->cfg.h()%(512U / (mx->cfg.w() * 3));
            interf->comment( String("Universes:") + uni + ";    X:" + mx->cfg.w() + ";    Y:" + (512U / (mx->cfg.w() * 3)) );
            interf->comment(String("Как настроить разметку матрицы в Jinx! можно посмотреть <a href=\"https://community.alexgyver.ru/threads/wifi-lampa-budilnik-proshivka-firelamp_jeeui-gpl.2739/page-454#post-103219\">на форуме</a>"));
        }
    interf->json_section_end();
}
void section_streaming_frame(Interface *interf, JsonObject *data){
    // Трансляция
    if (!interf) return;
    interf->json_frame_interface(TINTF_080);
    block_streaming(interf, data);
    interf->json_frame_flush();
}

void set_streaming(Interface *interf, JsonObject *data){
    if (!data) return;
    bool flag = (*data)[TCONST_isStreamOn];
    myLamp.setStream(flag);
    LOG(printf_P, PSTR("Stream set %d \n"), flag);
    if (flag) {
        STREAM_TYPE type = (STREAM_TYPE)embui.paramVariant(TCONST_stream_type).as<int>();
        if (ledStream) {
            if (ledStream->getStreamType() != type){
                Led_Stream::clearStreamObj();
            }
        }
        Led_Stream::newStreamObj(type);
    }
    else {
        Led_Stream::clearStreamObj();
    }
    save_lamp_flags();
}

void set_streaming_drirect(Interface *interf, JsonObject *data){
    if (!data) return;
    bool flag = (*data)[TCONST_direct];
    myLamp.setDirect(flag);
    if (ledStream){
        if (flag) {
#ifdef EXT_STREAM_BUFFER
            myLamp.setStreamBuff(false);
#else
            myLamp.clearDrawBuf();
#endif
            myLamp.effectsTimer(TCONST_DISABLE);
            FastLED.clear();
            FastLED.show();
        }
        else {
            myLamp.effectsTimer(TCONST_ENABLE);
#ifdef EXT_STREAM_BUFFER
            myLamp.setStreamBuff(true);
#else
            if (!myLamp.isDrawOn())             // TODO: переделать с запоминанием старого стейта
                myLamp.setDrawBuff(true);
#endif
        }
    }
    save_lamp_flags();
}
void set_streaming_mapping(Interface *interf, JsonObject *data){
    if (!data) return;
    myLamp.setMapping((*data)[TCONST_mapping]);
    save_lamp_flags();
}
void set_streaming_bright(Interface *interf, JsonObject *data){
    if (!data) return;
    remote_action(RA_CONTROL, (String(TCONST_dynCtrl)+"0").c_str(), (*data)[TCONST_bright].as<const char*>(), NULL);
}

void set_streaming_type(Interface *interf, JsonObject *data){
    if (!data) return;
    SETPARAM(TCONST_stream_type);
    STREAM_TYPE type = (STREAM_TYPE)(*data)[TCONST_stream_type].as<int>();
    LOG(printf_P, PSTR("Stream Type %d \n"), type);
    if (myLamp.isStreamOn()) {
        if (ledStream) {
            if (ledStream->getStreamType() == type)
                return;
            Led_Stream::clearStreamObj();
        }
        Led_Stream::newStreamObj(type);
    }
    section_streaming_frame(interf, data);
}

void set_streaming_universe(Interface *interf, JsonObject *data){
    if (!data) return;
    SETPARAM(TCONST_Universe);
    if (ledStream) {
        if (ledStream->getStreamType() == E131) {
            Led_Stream::newStreamObj(E131);
        }
    }
}
#endif

// Additional buttons on "Settings" page
void user_settings_frame(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->button_value(button_t::generic, TCONST_sh_page, e2int(page::setup_ledstrip), TINTF_ledstrip);
#ifdef MIC_EFFECTS
    interf->button_value(button_t::generic, TCONST_sh_page, e2int(page::mike), TINTF_020);
#endif
#ifdef MP3PLAYER
    interf->button_value(button_t::generic, TCONST_sh_page, e2int(page::setup_dfplayer), TINTF_099);
#endif
#ifdef ESP_USE_BUTTON
    interf->button_value(button_t::generic, TCONST_sh_page, e2int(page::setup_bttn), TINTF_013);
#endif
#ifdef ENCODER
    interf->button_value(button_t::generic, TCONST_sh_page, e2int(page::setup_encdr), TINTF_0DC);
#endif
    interf->button_value(button_t::generic, TCONST_sh_page, e2int(page::setup_other), TINTF_082);

    interf->button_value(button_t::generic, TCONST_sh_page, e2int(page::setup_esp), TINTF_08F);

    // show gpio setup page button
    interf->button_value(button_t::generic, TCONST_sh_page, e2int(page::setup_gpio), TINTF_gpiocfg);

    // obsolete
    //block_lamp_config(interf, data);
}

// Страница "Настройки ESP"
void section_sys_settings_frame(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface(); // TINTF_08F);

    interf->json_section_main(TCONST_sysSettings, TINTF_08F);
        interf->spacer(TINTF_092); // заголовок
        interf->json_section_line(TINTF_092); // расположить в одной линии
#ifdef ESP_USE_BUTTON
            interf->number_constrained(TCONST_PINB, embui.paramVariant(TCONST_PINB).as<int>(), TINTF_094, 1, 0, NUM_OUPUT_PINS);
#endif
        interf->json_section_end(); // конец контейнера
        interf->spacer();
        interf->number_constrained(TCONST_CLmt, embui.paramVariant(TCONST_CLmt).as<int>(), TINTF_095, /* step */ 100, /* min */ 1000, /* max*/ 16000);    // current limit

        // Editor frame
        //interf->json_section_main(TCONST_edit, "");
        //interf->iframe(TCONST_edit, TCONST_edit);
        //interf->json_section_end();

        interf->button(button_t::submit, TCONST_sysSettings, TINTF_Save, P_GRAY);

        interf->spacer();
        interf->button(button_t::generic, TCONST_settings, TINTF_00B);
    interf->json_frame_flush();
}

void set_sys_settings(Interface *interf, JsonObject *data){
    if(!data) return;

#ifdef ESP_USE_BUTTON
    {String tmpChk = (*data)[TCONST_PINB]; if(tmpChk.toInt()>16) return;}
#endif
    {String tmpChk = (*data)[TCONST_CLmt]; if(tmpChk.toInt()>16000) return;}

#ifdef ESP_USE_BUTTON
    SETPARAM(TCONST_PINB);
#endif
#ifdef MP3PLAYER
    SETPARAM(TCONST_mp3rx);
    SETPARAM(TCONST_mp3tx);
#endif
    SETPARAM(TCONST_CLmt);
/*
    if(!embui.sysData.isWSConnect){ // если последние 5 секунд не было коннекта, защита от зацикливания ребута
        myLamp.sendString(String(TINTF_096).c_str(), CRGB::Red, true);
        new Task(TASK_SECOND, TASK_ONCE, nullptr, &ts, true, nullptr, [](){ embui.autosave(true); LOG(println, "Rebooting..."); remote_action(RA::RA_REBOOT, NULL, NULL); });
    }
*/
    section_effects_frame(interf,data);
}

void set_lamp_flags(Interface *interf, JsonObject *data){
    if(!data) return;
    SETPARAM(TCONST_syslampFlags);
}

void save_lamp_flags(){
    DynamicJsonDocument doc(160);
    JsonObject obj = doc.to<JsonObject>();
    obj[TCONST_syslampFlags] = ulltos(myLamp.getLampFlags());
    set_lamp_flags(nullptr, &obj);
}

/**
 * @brief page with GPIO mapping setup
 * 
 */
void page_gpiocfg(Interface *interf, JsonObject *data){
    if (!interf) return;

    interf->json_frame_interface();
    interf->json_section_main(TCONST_pin, "GPIO Configuration");

    interf->comment((char*)0, "<ul><li>Check <a href=\"https://github.com/vortigont/FireLamp_JeeUI/wiki/\" target=\"_blank\">WiKi page</a> for GPIO reference</li><li>Set '-1' to disable GPIO</li><li>MCU will <b>reboot</b> on any gpio change! Wait 5-10 sec after each save</li>");

    DynamicJsonDocument doc(512);
    embuifs::deserializeFile(doc, TCONST_fcfg_gpio);

    interf->json_section_begin(TCONST_set_gpio, "");
    // gpio для подключения LED матрицы
    interf->json_section_hidden(TCONST_mx_gpio, "Matrix");
        interf->json_section_line(); // расположить в одной линии
            interf->number_constrained(TCONST_mx_gpio, doc[TCONST_mx_gpio] | static_cast<int>(GPIO_NUM_NC), "LED Matrix gpio", /*step*/ 1, /*min*/ -1, /*max*/ NUM_OUPUT_PINS);
        interf->json_section_end();
        interf->button_value(button_t::submit, TCONST_set_gpio, static_cast<int>(gpio_device::ledstrip), TINTF_Save);
    interf->json_section_end();

#ifdef MP3PLAYER
    // gpio для подключения DP-плеера
    interf->json_section_hidden(TCONST_playMP3, "DFPlayer");
        interf->json_section_line(); // расположить в одной линии
            interf->number_constrained(TCONST_mp3rx, doc[TCONST_mp3rx] | static_cast<int>(GPIO_NUM_NC), TINTF_097, /*step*/ 1, /*min*/ -1, /*max*/ NUM_OUPUT_PINS);
            interf->number_constrained(TCONST_mp3tx, doc[TCONST_mp3tx] | static_cast<int>(GPIO_NUM_NC), TINTF_098, 1, -1, NUM_OUPUT_PINS);
        interf->json_section_end();
        interf->button_value(button_t::submit, TCONST_set_gpio, static_cast<int>(gpio_device::dfplayer), TINTF_Save);
    interf->json_section_end();
#endif

#ifdef TM1637_CLOCK
    // gpio для подключения 7 сегментного индикатора
    interf->json_section_hidden(TCONST_tm24, "TM1637 Display");
        interf->json_section_line(); // расположить в одной линии
            interf->number_constrained(TCONST_tm_clk, doc[TCONST_tm_clk] | static_cast<int>(GPIO_NUM_NC), "TM Clk gpio", /*step*/ 1, /*min*/ -1, /*max*/ NUM_OUPUT_PINS);
            interf->number_constrained(TCONST_tm_dio, doc[TCONST_tm_dio] | static_cast<int>(GPIO_NUM_NC), "TM DIO gpio", 1, -1, NUM_OUPUT_PINS);
        interf->json_section_end();
        interf->button_value(button_t::submit, TCONST_set_gpio, static_cast<int>(gpio_device::tmdisplay), TINTF_Save);
    interf->json_section_end();
#endif

    // gpio для подключения КМОП транзистора
    interf->json_section_hidden(TCONST_mosfet_gpio, "MOSFET");
        interf->json_section_line(); // расположить в одной линии
            interf->number_constrained(TCONST_mosfet_gpio, doc[TCONST_mosfet_gpio] | static_cast<int>(GPIO_NUM_NC), "MOSFET gpio", /*step*/ 1, /*min*/ -1, /*max*/ NUM_OUPUT_PINS);
            interf->number_constrained(TCONST_mosfet_ll,   doc[TCONST_mosfet_ll]   | 1, "MOSFET logic level", 1, 0, 1);
        interf->json_section_end();
        interf->button_value(button_t::submit, TCONST_set_gpio, static_cast<int>(gpio_device::mosfet), TINTF_Save);
    interf->json_section_end();

    // gpio AUX
    interf->json_section_hidden(TCONST_aux_gpio, TCONST_AUX);
        interf->json_section_line(); // расположить в одной линии
            interf->number_constrained(TCONST_aux_gpio, doc[TCONST_aux_gpio] | static_cast<int>(GPIO_NUM_NC), "AUX gpio", /*step*/ 1, /*min*/ -1, /*max*/ NUM_OUPUT_PINS);
            interf->number_constrained(TCONST_aux_ll,   doc[TCONST_aux_ll]   | 1, "AUX logic level", 1, 0, 1);
        interf->json_section_end();
        interf->button_value(button_t::submit, TCONST_set_gpio, static_cast<int>(gpio_device::aux), TINTF_Save);
    interf->json_section_end();

    interf->json_section_end(); // json_section_begin ""

    interf->json_frame_flush();
}



// кастомный обработчик, для реализации особой обработки событий сокетов
void ws_action_handle(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    bool res = false; // false == EmbUI default action
    switch(type){
        case AwsEventType::WS_EVT_ERROR :
            {
                resetAutoTimers();
                uint16_t effNum = myLamp.effects.getSelected();
                myLamp.effects.switchEffect(EFF_NONE);
                myLamp.effects.removeConfig(effNum);
                myLamp.effects.switchEffect(effNum);
                String tmpStr="- ";
                tmpStr+=effNum;
                myLamp.sendString(tmpStr.c_str(), CRGB::Red);

                res = true;
                break;
            }
        default :
            res = false; 
            break;
    }
}

// обработчик, для поддержки приложения WLED APP
void wled_handle(AsyncWebServerRequest *request){
    if(request->hasParam("T")){
        int pwr = request->getParam("T")->value().toInt();
        if (pwr == 2) run_action( myLamp.isLampOn() ? ra::off : ra::on);            // toggle is '2'
        else run_action( pwr ? ra::on : ra::off);
    }
    uint8_t bright = myLamp.isLampOn() ? myLamp.getBrightness() : 0;

    if (request->hasParam("A")){
        bright = request->getParam("A")->value().toInt();
        run_action(ra::brt_nofade, bright);
    }

    String result = "<?xml version=\"1.0\" ?><vs><ac>";
    result.concat(myLamp.isLampOn()?bright:0);
    result.concat("</ac><ds>");
    result.concat(embui.hostname());
    result.concat("</ds></vs>");

    request->send(200, PGmimexml, result);
}

/**
 * Набор конфигурационных переменных и обработчиков интерфейса
 */
void create_parameters(){
    LOG(println, "Создание дефолтных параметров");
    // создаем дефолтные параметры для нашего проекта
    embui.var_create(TCONST_syslampFlags, ulltos(myLamp.getLampFlags())); // Дефолтный набор флагов
    embui.var_create(TCONST_eff_run, 1);
    embui.var_create(P_m_tupd, DEFAULT_MQTTPUB_INTERVAL); // "m_tupd" интервал отправки данных по MQTT в секундах (параметр в энергонезависимой памяти)

    //embui.var_create(TCONST_fileName, "cfg1.json"); // "fileName"

    embui.var_create(TCONST_AUX, false);
    embui.var_create(TCONST_msg, "");
    embui.var_create(TCONST_txtColor, TCONST__ffffff);
    embui.var_create(TCONST_txtBfade, FADETOBLACKVALUE);
    embui.var_create(TCONST_txtSpeed, 100);
    embui.var_create(TCONST_txtOf, 0);
    embui.var_create(TCONST_effSort, 1);
    embui.var_create(TCONST_GlobBRI, 127);

    embui.var_create(TCONST_ny_period, 0);
    embui.var_create(TCONST_ny_unix, TCONST_1609459200);

#ifdef MIC_EFFECTS
    embui.var_create(TCONST_micScale, 1.28);
    embui.var_create(TCONST_micNoise, 0.0);
    embui.var_create(TCONST_micnRdcLvl, 0);
#endif

    embui.var_create(TCONST_DTimer, DEFAULT_DEMO_TIMER); // Дефолтное значение, настраивается из UI
    embui.var_create(TCONST_alarmPT, 85); // 5<<4+5, старшие и младшие 4 байта содержат 5

    embui.var_create(TCONST_spdcf, 1.0);

    // пины и системные настройки
#ifdef ESP_USE_BUTTON
    embui.var_create(TCONST_PINB, BTN_PIN); // Пин кнопки
    embui.var_create(TCONST_EncVG, static_cast<int>(GAUGETYPE::GT_VERT) );         // Тип шкалы
#endif
#ifdef ENCODER
    embui.var_create(TCONST_encTxtCol, "#FFA500");  // Дефолтный цвет текста (Orange)
    embui.var_create(TCONST_encTxtDel, 40);        // Задержка прокрутки текста
    embui.var_create(TCONST_EncVG, (int)GAUGETYPE::GT_VERT);  // Тип шкалы
    embui.var_create(TCONST_EncVGCol, "#FF2A00");  // Дефолтный цвет шкалы
#endif

#ifdef MP3PLAYER
    embui.var_create(TCONST_mp3rx, MP3_RX_PIN); // Пин RX плеера
    embui.var_create(TCONST_mp3tx, MP3_TX_PIN); // Пин TX плеера
    embui.var_create(TCONST_mp3volume, 25); // громкость
    embui.var_create(TCONST_mp3count, 255); // кол-во файлов в папке mp3
#endif
#ifdef TM1637_CLOCK
    embui.var_create(TCONST_tmBright, 82); // 5<<4+5, старшие и младшие 4 байта содержат 5
#endif
    embui.var_create(TCONST_CLmt, CURRENT_LIMIT); // Лимит по току
#ifdef USE_STREAMING
    embui.var_create(TCONST_stream_type, SOUL_MATE); // Тип трансляции
    embui.var_create(TCONST_Universe, 1); // Universe для E1.31
#endif
    // далее идут обработчики параметров

   /**
    * регистрируем статические секции для web-интерфейса с системными настройками,
    * сюда входит:
    *  - WiFi-manager
    *  - установка часового пояса/правил перехода сезонного времени
    *  - установка текущей даты/времени вручную
    *  - базовые настройки MQTT
    *  - OTA обновление прошивки и образа файловой системы
    */
    basicui::add_sections();

    embui.section_handle_add(TCONST_sh_page, show_page_selector);
    embui.section_handle_add(TCONST_sysSettings, set_sys_settings);

    embui.section_handle_add(TCONST_syslampFlags, set_lamp_flags);

    embui.section_handle_add(TCONST_main, section_main_frame);                   // заглавная страница веб-интерфейса
    embui.section_handle_add(TCONST_show_flags, show_main_flags);                // нажатие кнопки "еще..." на странице "Эффекты"

    embui.section_handle_add(TCONST_effects, section_effects_frame);             // меню: переход на страницу "Эффекты"
    embui.section_handle_add(TCONST_eff_ctrls, show_effect_controls);            // блок контролов текущего эффекта
    embui.section_handle_add(TCONST_eff_run, set_switch_effect);
    embui.section_handle_add(TCONST_dynCtrl_, set_effects_dynCtrl);

    embui.section_handle_add(TCONST_eff_prev, set_eff_prev);
    embui.section_handle_add(TCONST_eff_next, set_eff_next);

    embui.section_handle_add(TCONST_effListConf, set_effects_config_list);
    embui.section_handle_add(TCONST_set_effect, set_effects_config_param);

    embui.section_handle_add(TCONST_ONflag, set_onflag);
    embui.section_handle_add(TCONST_Demo, set_demoflag);
    embui.section_handle_add(TCONST_GBR, set_gbrflag);                           // Lamp brightness
    embui.section_handle_add(TCONST_lcurve, set_lcurve);                                // luma curve control
    embui.section_handle_add(TCONST_AUX, set_auxflag);
    embui.section_handle_add(TCONST_drawing, page_drawing);
    embui.section_handle_add(TCONST_draw_dat, set_drawing);
    //embui.section_handle_add(TCONST_drawClear, set_clear);
#ifdef USE_STREAMING    
    embui.section_handle_add(TCONST_streaming, section_streaming_frame);
    embui.section_handle_add(TCONST_isStreamOn, set_streaming);
    embui.section_handle_add(TCONST_stream_type, set_streaming_type);
    embui.section_handle_add(TCONST_direct, set_streaming_drirect);
    embui.section_handle_add(TCONST_mapping, set_streaming_mapping);
    embui.section_handle_add(TCONST_Universe, set_streaming_universe);
    embui.section_handle_add(TCONST_bright, set_streaming_bright);
#endif

    embui.section_handle_add(TCONST_lamptext, section_text_frame);
    embui.section_handle_add(TCONST_textsend, set_lamp_textsend);
    //embui.section_handle_add(TCONST_add_lamp_config, edit_lamp_config);
    //embui.section_handle_add(TCONST_edit_lamp_config, edit_lamp_config);

    embui.section_handle_add(TCONST_edit_text_config, set_text_config);
    embui.section_handle_add(TCONST_drawbuff, set_drawflag);

    embui.section_handle_add(TCONST_settings_ledstrip, set_ledstrip);           // Set LED strip layout setup
    embui.section_handle_add(TCONST_set_other, set_settings_other);
    embui.section_handle_add(TCONST_set_gpio, set_gpios);        // Set gpios

#ifdef MIC_EFFECTS
    embui.section_handle_add(TCONST_set_mic, set_settings_mic);
    embui.section_handle_add(TCONST_Mic, set_micflag);
    embui.section_handle_add(TCONST_mic_cal, set_settings_mic_calib);
#endif
    embui.section_handle_add(TCONST_show_event, show_settings_event);
    embui.section_handle_add(TCONST_event_conf, show_event_conf);
    embui.section_handle_add(TCONST_set_event, set_event_conf);
    embui.section_handle_add(TCONST_Events, set_eventflag);
    embui.section_handle_add(TCONST_evList, set_eventlist);
#ifdef ESP_USE_BUTTON
    embui.section_handle_add(TCONST_butt_conf, show_butt_conf);
    embui.section_handle_add(TCONST_set_butt, set_butt_conf);
    embui.section_handle_add(TCONST_Btn, set_btnflag);
    embui.section_handle_add(TCONST_EncVG, set_gaugetype);
#endif

#ifdef LAMP_DEBUG
    embui.section_handle_add(TCONST_debug, set_debugflag);
#endif

#ifdef MP3PLAYER
    embui.section_handle_add(TCONST_isOnMP3, set_mp3flag);
    embui.section_handle_add(TCONST_mp3volume, set_mp3volume);
    embui.section_handle_add(TCONST_show_mp3, show_settings_mp3);
    embui.section_handle_add(TCONST_set_mp3, set_settings_mp3);

    embui.section_handle_add(CMD_MP3_PREV, set_mp3_player);
    embui.section_handle_add(CMD_MP3_NEXT, set_mp3_player);
    embui.section_handle_add(TCONST_mp3_p5, set_mp3_player);
    embui.section_handle_add(TCONST_mp3_n5, set_mp3_player);
#endif
#ifdef ENCODER
    embui.section_handle_add(TCONST_set_enc, set_settings_enc);
#endif
}

void sync_parameters(){
    DynamicJsonDocument doc(1024);
    JsonObject obj = doc.to<JsonObject>();

#ifdef EMBUI_USE_MQTT
    myLamp.setmqtt_int(embui.paramVariant(P_m_tupd));
#endif

    String syslampFlags(embui.param(TCONST_syslampFlags));
    LAMPFLAGS tmp;
    tmp.lampflags = stoull(syslampFlags); //atol(embui.param(TCONST_syslampFlags).c_str());
    LOG(printf_P, PSTR("tmp.lampflags=%llu\n"), tmp.lampflags);

    obj[TCONST_drawbuff] = tmp.isDraw;
    set_drawflag(nullptr, &obj);
    doc.clear();

#ifdef LAMP_DEBUG
    obj[TCONST_debug] = tmp.isDebug ;
    set_debugflag(nullptr, &obj);
    doc.clear();
#endif

    obj[TCONST_Events] = tmp.isEventsHandled;
    CALL_INTF_OBJ(set_eventflag);
    doc.clear();
    TimeProcessor::getInstance().attach_callback(std::bind(&LAMP::setIsEventsHandled, &myLamp, myLamp.IsEventsHandled())); // только после синка будет понятно включены ли события

    // check "restore state" flag
    if (tmp.restoreState){
        if(tmp.ONflag){ // если лампа включена, то устанавливаем эффект ДО включения
            run_action(ra::eff_switch, embui.paramVariant(TCONST_eff_run));
            //CALL_SETTER(TCONST_eff_run, embui.paramVariant(TCONST_eff_run), set_switch_effect);
        }
        run_action(ra::on);     // set_onflag(nullptr, &obj);
        if(!tmp.ONflag){ // иначе - после
            run_action(ra::eff_switch, embui.paramVariant(TCONST_eff_run));
            //CALL_SETTER(TCONST_eff_run, embui.paramVariant(TCONST_eff_run), set_switch_effect);
        }
        doc.clear();
        if(myLamp.isLampOn())
            run_action(ra::demo, embui.paramVariant(TCONST_Demo));
            //CALL_SETTER(TCONST_Demo, embui.paramVariant(TCONST_Demo), set_demoflag); // Демо через режимы, для него нужнен отдельный флаг :(
    } else {
        run_action(ra::eff_switch, embui.paramVariant(TCONST_eff_run));
    }

#ifdef MP3PLAYER
    // т.к. sync_parameters запускается при перезапуске лампы, установку мп3 нужно отложить до момента инициализации плеера
    Task *t = new Task(DFPLAYER_START_DELAY+250, TASK_ONCE, nullptr, &ts, false, nullptr, [tmp](){
    if(!mp3->isReady()){
        LOG(println, "DFPlayer not ready yet...");
        if(millis()<10000){
            ts.getCurrentTask()->restartDelayed(TASK_SECOND*2);
            return;
        }
    }
    
    DynamicJsonDocument doc(1024);
    JsonObject obj = doc.to<JsonObject>();

    obj[TCONST_playTime] = tmp.playTime;
    obj[TCONST_playName] = tmp.playName ;
    obj[TCONST_playEffect] = tmp.playEffect ;
    obj[TCONST_alarmSound] = tmp.alarmSound;
    obj[TCONST_eqSetings] = tmp.MP3eq; // пишет в плеер!
    obj[TCONST_playMP3] = tmp.playMP3 ;
    obj[TCONST_mp3count] = embui.paramVariant(TCONST_mp3count);
    obj[TCONST_mp3volume] = embui.paramVariant(TCONST_mp3volume);
    obj[TCONST_limitAlarmVolume] = tmp.limitAlarmVolume;

    set_settings_mp3(nullptr, &obj);
    doc.clear();

    mp3->setupplayer(myLamp.effects.getCurrent(), myLamp.effects.getSoundfile()); // установить начальные значения звука
    obj[TCONST_isOnMP3] = tmp.isOnMP3 ;
    set_mp3flag(nullptr, &obj);
    }, true);
    t->enableDelayed();
#endif

    // not sure if reapply for AUX is required here
    //CALL_SETTER(TCONST_AUX, embui.paramVariant(TCONST_AUX), set_auxflag);

    myLamp.setClearingFlag(tmp.isEffClearing);

#ifdef MIC_EFFECTS
    myLamp.setEffHasMic(tmp.effHasMic);
#endif
    SORT_TYPE type = (SORT_TYPE)embui.paramVariant(TCONST_effSort);
    obj[TCONST_effSort] = type;
    set_effects_config_param(nullptr, &obj);
    doc.clear();

#ifdef ESP_USE_BUTTON
    obj[TCONST_Btn] = tmp.isBtn ;
    CALL_INTF_OBJ(set_btnflag);
    obj[TCONST_EncVG] = tmp.GaugeType;
    CALL_INTF_OBJ(set_gaugetype);
    doc.clear();
#endif
#ifdef ENCODER
    obj[TCONST_encTxtCol] = embui.param(TCONST_encTxtCol);
    obj[TCONST_encTxtDel] = 110 - embui.paramVariant(TCONST_encTxtDel).as<int>();
    obj[TCONST_EncVG] = tmp.GaugeType ;;
    obj[TCONST_EncVGCol] = embui.param(TCONST_EncVGCol);
    set_settings_enc(nullptr, &obj);
    doc.clear();
#endif

    obj[TCONST_txtSpeed] = 110 - embui.paramVariant(TCONST_txtSpeed).as<int>();
    obj[TCONST_txtOf] = embui.param(TCONST_txtOf);
    obj[TCONST_ny_period] = embui.param(TCONST_ny_period);
    obj[TCONST_txtBfade] = embui.param(TCONST_txtBfade);

    String datetime;
    TimeProcessor::getDateTimeString(datetime, embui.paramVariant(TCONST_ny_unix));
    obj[TCONST_ny_unix] = datetime;
    
    set_text_config(nullptr, &obj);
    doc.clear();

#ifdef USE_STREAMING
    obj[TCONST_isStreamOn] = tmp.isStream ;
    set_streaming(nullptr, &obj);
    doc.clear();

    obj[TCONST_direct] = tmp.isDirect ;
    set_streaming_drirect(nullptr, &obj);
    doc.clear();

    obj[TCONST_mapping] = tmp.isMapping ;
    set_streaming_mapping(nullptr, &obj);
    doc.clear();

    obj[TCONST_stream_type] = embui.param(TCONST_stream_type);
    set_streaming_type(nullptr, &obj);
    doc.clear();

    obj[TCONST_Universe] = embui.param(TCONST_Universe);
    set_streaming_universe(nullptr, &obj);
    doc.clear();
#endif

    // собираем конфигурацию для объекта лампы из сохраненного конфига и текущего же состояния лампы (масло масляное)
    // имеет смысл при первом запуске. todo: часть можно выкинуть ибо переписывание в самих себя
    obj[TCONST_MIRR_H] = mx->cfg.hmirror() ;
    obj[TCONST_MIRR_V] = mx->cfg.vmirror() ;
    obj[TCONST_isFaderON] = tmp.isFaderON ;
    obj[TCONST_isClearing] = tmp.isEffClearing ;
    obj[TCONST_DRand] = tmp.dRand ;
    obj[TCONST_showName] = tmp.showName ;
    obj[TCONST_DTimer] = embui.paramVariant(TCONST_DTimer);
    obj[TCONST_spdcf] = embui.paramVariant(TCONST_spdcf);
    obj[TCONST_f_restore_state] = tmp.restoreState;                             // "restore state" flag

#ifdef TM1637_CLOCK
    uint8_t tmBright = embui.paramVariant(TCONST_tmBright);
    obj[TCONST_tmBrightOn] = tmBright>>4;
    obj[TCONST_tmBrightOff] = tmBright&0x0F;
    obj[TCONST_tm24] = tmp.tm24 ;
    obj[TCONST_tmZero] = tmp.tmZero ;
    #ifdef DS18B20
    obj[TCONST_ds18b20] = tmp.isTempOn ;
    #endif
#endif

    uint8_t alarmPT = embui.paramVariant(TCONST_alarmPT);
    obj[TCONST_alarmP] = alarmPT>>4;
    obj[TCONST_alarmT] = alarmPT&0x0F;

    // выполняется метод, который обрабатывает форму вебморды "настройки" - "другие"
    set_settings_other(nullptr, &obj);
    doc.clear();

#ifdef MIC_EFFECTS
    obj[TCONST_Mic] = tmp.isMicOn ;
    myLamp.getLampState().setMicAnalyseDivider(0);
    set_micflag(nullptr, &obj);
    doc.clear();

    // float scale = atof(embui.param(TCONST_micScale).c_str());
    // float noise = atof(embui.param(TCONST_micNoise).c_str());
    // mic_noise_reduce_level_t lvl=(mic_noise_reduce_level_t)embui.param(TCONST_micnRdcLvl).toInt();

    obj[TCONST_micScale] = embui.param(TCONST_micScale); //scale;
    obj[TCONST_micNoise] = embui.param(TCONST_micNoise); //noise;
    obj[TCONST_micnRdcLvl] = embui.param(TCONST_micnRdcLvl); //lvl;
    set_settings_mic(nullptr, &obj);
    doc.clear();
#endif

    //save_lamp_flags(); // обновить состояние флагов (закомментированно, окончательно состояние установится через 0.3 секунды, после set_settings_other)

    //--------------- начальная инициализация состояния
    myLamp.getLampState().freeHeap = ESP.getFreeHeap();
#ifdef ESP8266
    FSInfo fs_info;
    LittleFS.info(fs_info);
    myLamp.getLampState().fsfreespace = fs_info.totalBytes-fs_info.usedBytes;
    myLamp.getLampState().HeapFragmentation = ESP.getHeapFragmentation();
#endif
#ifdef ESP32
    myLamp.getLampState().fsfreespace = LittleFS.totalBytes() - LittleFS.usedBytes();
    myLamp.getLampState().HeapFragmentation = 0;
#endif
    //--------------- начальная инициализация состояния

    Task *_t = new Task(TASK_SECOND, TASK_ONCE, [](){ // откладыаем задачу на 1 секунду, т.к. выше есть тоже отложенные инициализации, см. set_settings_other()
        myLamp.getLampState().isInitCompleted = true; // ставим признак того, что инициализация уже завершилась, больше его не менять и должен быть в самом конце sync_parameters() !!!
    }, &ts, false, nullptr, nullptr, true);
    _t->enableDelayed();
    LOG(println, "sync_parameters() done");
}

void show_progress(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface();
    interf->json_section_hidden(T_DO_OTAUPD, String(TINTF_056) + " : " + (*data)[TINTF_05A].as<String>()+ "%");
    interf->json_section_end();
    interf->json_frame_flush();
}

uint8_t uploadProgress(size_t len, size_t total){
    DynamicJsonDocument doc(256);
    JsonObject obj = doc.to<JsonObject>();
    static int prev = 0; // используется чтобы не выводить повторно предыдущее значение, хрен с ней, пусть живет
    float part = total / 50.0;
    int curr = len / part;
    uint8_t progress = 100*len/total;
    if (curr != prev) {
        prev = curr;
        for (int i = 0; i < curr; i++) Serial.print("=");
        Serial.print("\n");
        obj[TINTF_05A] = progress;
        CALL_INTF_OBJ(show_progress);
    }
    if (myLamp.getGaugeType()!=GAUGETYPE::GT_NONE){
        GAUGE::GaugeShow(len, total, 100);
    }
    return progress;
}

// Функции обработчики и другие служебные
#ifdef ESP_USE_BUTTON
void default_buttons(){
    myButtons->clear();
    // Выключена
    myButtons->add(new Button(false, false, 1, true, BA::BA_ON)); // 1 клик - ON
    myButtons->add(new Button(false, false, 2, true, BA::BA_DEMO)); // 2 клика - Демо
    myButtons->add(new Button(false, true, 0, true, BA::BA_WHITE_LO)); // удержание Включаем белую лампу в мин яркость
    myButtons->add(new Button(false, true, 1, true, BA::BA_WHITE_HI)); // удержание + 1 клик Включаем белую лампу в полную яркость
    myButtons->add(new Button(false, true, 0, false, BA::BA_BRIGHT)); // удержание из выключенного - яркость
    myButtons->add(new Button(false, true, 1, false, BA::BA_BRIGHT)); // удержание из выключенного - яркость

    // Включена
    myButtons->add(new Button(true, false, 1, true, BA::BA_OFF)); // 1 клик - OFF
    myButtons->add(new Button(true, false, 2, true, BA::BA_EFF_NEXT)); // 2 клика - след эффект
    myButtons->add(new Button(true, false, 3, true, BA::BA_EFF_PREV)); // 3 клика - пред эффект
    myButtons->add(new Button(true, false, 5, true, BA::BA_SEND_IP)); // 5 клика - показ IP
    myButtons->add(new Button(true, false, 6, true, BA::BA_SEND_TIME)); // 6 клика - показ времени
    myButtons->add(new Button(true, false, 7, true, BA::BA_EFFECT, String("253"))); // 7 кликов - эффект часы
    myButtons->add(new Button(true, true, 0, false, BA::BA_BRIGHT)); // удержание яркость
    myButtons->add(new Button(true, true, 1, false, BA::BA_SPEED)); // удержание + 1 клик скорость
    myButtons->add(new Button(true, true, 2, false, BA::BA_SCALE)); // удержание + 2 клика масштаб
}
#endif

// набор акшенов, которые дергаются из всех мест со всех сторон
void remote_action(RA action, ...){
    LOG(printf_P, PSTR("Remote Action: %d: "), action);
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();

    char *key = NULL, *val = NULL, *value = NULL;
    va_list prm;
    va_start(prm, action);
    while ((key = (char *)va_arg(prm, char *)) && (val = (char *)va_arg(prm, char *))) {
        LOG(printf_P, PSTR("RA param: '%s = %s' "), key, val);
        obj[key] = val;
    }
    va_end(prm);

    if (key && !val) {
        value = key;
        LOG(printf_P, PSTR("%s"), value);
    }
    LOG(println);

    switch (action) {
        case RA::RA_SEND_IP:
            myLamp.sendString(WiFi.localIP().toString().c_str(), CRGB::White);
#ifdef TM1637_CLOCK
            if(tm1637) tm1637->showip();
#endif
            break;
        default:;
    }
}

String httpCallback(const String &param, const String &value, bool isset){
    String result = "Ok";
    String upperParam(param);
    upperParam.toUpperCase();
    RA action = RA_UNKNOWN;
    LOG(printf_P, PSTR("HTTP: %s - %s\n"), upperParam.c_str(), value.c_str());

    if(!isset) {
        LOG(println, "GET");
        if (upperParam == CMD_ON)
            { result = myLamp.isLampOn() ; }
        else if (upperParam == CMD_OFF)
            { result = !myLamp.isLampOn() ; }
        else if (upperParam == CMD_G_BRIGHT) { result = myLamp.getBrightness(); }
        else if (upperParam == CMD_DEMO)
            { result = myLamp.getMode() == LAMPMODE::MODE_DEMO ; }
#ifdef MP3PLAYER
        else if (upperParam == CMD_PLAYER) 
            { result = myLamp.isONMP3() ; }
        else if (upperParam == CMD_MP3_SOUND) 
            { result = mp3->getCurPlayingNb(); }
        //else if (upperParam == CMD_MP3_PREV) { run_action(ra::mp3_prev, 1); return result; }
        //else if (upperParam == CMD_MP3_NEXT) { run_action(ra::mp3_next, 1); return result; }
#endif
#ifdef MIC_EFFECTS
        else if (upperParam == CMD_MIC) 
            { result = myLamp.isMicOnOff() ; }
#endif
        else if (upperParam == CMD_EFFECT)
            { result = myLamp.effects.getCurrent();  }
        else if (upperParam == CMD_WARNING)
            { myLamp.showWarning(CRGB::Orange,5000,500); }
        else if (upperParam == CMD_EFF_CONFIG) {
                String result(myLamp.effects.getEffCfg().getSerializedEffConfig());
#ifdef EMBUI_USE_MQTT
                embui.publish(String(TCONST_embui_pub_) + TCONST_eff_config, result, true);
#endif
                return result;
            }
        else if (upperParam == CMD_CONTROL) {
            LList<std::shared_ptr<UIControl>>&controls = myLamp.effects.getControls();
            for(unsigned i=0; i<controls.size();i++){
                if(value == String(controls[i]->getId())){
                    result = String("[") + controls[i]->getId() + ",\"" + (controls[i]->getId()==0 ? String(myLamp.getBrightness()) : controls[i]->getVal()) + "\"]";
#ifdef EMBUI_USE_MQTT
                    embui.publish(String(TCONST_embui_pub_) + TCONST_control, result, true);
#endif
                    return result;
                }
            }
        }
        else if (upperParam == CMD_LIST)  {
            result = "[";
            bool first=true;
            EffectListElem *eff = nullptr;
            String effname((char *)0);
            while ((eff = myLamp.effects.getNextEffect(eff)) != nullptr) {
                if (!first) result += ",";
                result += eff->eff_nb;
                first=false;
            }
            result += "]";
        }
        else if (upperParam == CMD_SHOWLIST)  {
            result = "[";
            bool first=true;
            EffectListElem *eff = nullptr;
            String effname((char *)0);
            while ((eff = myLamp.effects.getNextEffect(eff)) != nullptr) {
                if (eff->canBeSelected()) {
                    if (!first) result += ",";
                    result += eff->eff_nb;
                    first=false;
                }
            }
            result = result + "]";
        }
        else if (upperParam == CMD_DEMOLIST)  {
            result = "[";
            bool first=true;
            EffectListElem *eff = nullptr;
            String effname((char *)0);
            while ((eff = myLamp.effects.getNextEffect(eff)) != nullptr) {
                if (eff->isFavorite()) {
                    if (!first) result += ",";
                    result += eff->eff_nb;
                    first=false;
                }
            }
            result += "]";
        }
        else if (upperParam == CMD_EFF_NAME)  {
            String effname((char *)0);
            uint16_t effnum = String(value).toInt();
            effnum = effnum ? effnum : myLamp.effects.getCurrent();
            myLamp.effects.loadeffname(effname, effnum);
            result = String("[")+effnum+String(",\"")+effname+String("\"]");
        }
        else if (upperParam == CMD_EFF_ONAME)  {
            String effname((char *)0);
            uint16_t effnum = String(value).toInt();
            effnum = effnum ? effnum : myLamp.effects.getCurrent();
            effname = T_EFFNAMEID[(uint8_t)effnum];
            result = String("[")+effnum+String(",\"")+effname+String("\"]");
        }
        if (upperParam == CMD_MOVE_NEXT) { run_action(ra::eff_next); return result; }
        if (upperParam == CMD_MOVE_PREV) { run_action(ra::eff_prev); return result; }
        if (upperParam == CMD_MOVE_RND)  { run_action(ra::eff_rnd);  return result; }
        if (upperParam == CMD_REBOOT) { run_action(ra::reboot); return result; }
        else if (upperParam == CMD_ALARM) { result = myLamp.isAlarm() ; }
        else if (upperParam == CMD_MATRIX) { char buf[32]; sprintf_P(buf, PSTR("[%d,%d]"), mx->cfg.w(), mx->cfg.h());  result = buf; }
#ifdef EMBUI_USE_MQTT        
        embui.publish(String(TCONST_embui_pub_) + upperParam, result, true);
#endif
        return result;
    } else {
        LOG(println, "SET");
        if ( upperParam == CMD_ON || upperParam == CMD_OFF ){ run_action(value.toInt() ? ra::on : ra::off ); return result; }
        else if (upperParam == CMD_DEMO) { run_action(ra::demo, value.toInt() ? true : false ); return result; }
        // scroll text
        else if (upperParam == CMD_MSG)  { myLamp.sendString(value.c_str()); return result; }
        if (upperParam == CMD_EFFECT)    { run_action(ra::eff_next, value.toInt()); return result; }
        if (upperParam == CMD_MOVE_NEXT) { run_action(ra::eff_next); return result; }
        if (upperParam == CMD_MOVE_PREV) { run_action(ra::eff_prev); return result; }
        if (upperParam == CMD_MOVE_RND)  { run_action(ra::eff_rnd);  return result; }
        if (upperParam == CMD_REBOOT)    { run_action(ra::reboot); return result; }
        if (upperParam == CMD_G_BRIGHT)  { run_action(ra::brt, value.toInt()); return result; }
        if (upperParam == CMD_ALARM) { ALARMTASK::startAlarm(&myLamp, value.c_str()); }
        else if (upperParam == CMD_WARNING) {
            StaticJsonDocument<256> obj;
            deserializeJson(obj, value);
            JsonObject o = obj.as<JsonObject>();
            run_action(ra::warn, &o);
            return result;
        }
//        else if (upperParam == CMD_DRAW) action = RA_DRAW;             // у меня не идей зачем нужно попиксельное рисование через единичные http запросы /это делается через акшен set_drawing()/
//        else if (upperParam == CMD_FILL_MATRIX) action = RA_FILLMATRIX; // у меня не идей зачем нужно заливать поле рисование через единичные http запросы /это делается через акшен set_drawing()/
//        else if (upperParam == CMD_RGB) action = RA_RGB;               // what's the diff with FILL_MATRIX?
#ifdef MP3PLAYER
        if (upperParam == CMD_MP3_PREV) { run_action(ra::mp3_prev, 1); return result; }
        if (upperParam == CMD_MP3_NEXT) { run_action(ra::mp3_next, 1); return result; }
        if (upperParam == CMD_MP3_SOUND){ mp3->playEffect(value.toInt(), ""); return result; }
        if (upperParam == CMD_PLAYER){    run_action(ra::mp3_enable, value.toInt()); return result; }
        if (upperParam == CMD_MP3_VOLUME){ run_action(ra::mp3_vol, value.toInt()); return result; }
#endif
#ifdef MIC_EFFECTS
        else if (upperParam == CMD_MIC) { run_action(ra::miconoff, value.toInt() ? true : false ); return result; }
#endif
        //else if (upperParam.startsWith(TCONST_dynCtrl)) { action = RA_CONTROL; remote_action(action, upperParam.c_str(), value.c_str(), NULL); return result; }
        else if (upperParam == CMD_EFF_CONFIG) {
            return httpCallback(upperParam, "", false); // set пока не реализована
        }
        else if (upperParam == CMD_CONTROL || upperParam == CMD_INC_CONTROL) {
            String str=value;
            DynamicJsonDocument doc(256);
            deserializeJson(doc,str);
            JsonArray arr = doc.as<JsonArray>();
            uint16_t id=0;
            String val;

            if(arr.size()<2){ // мало параметров, т.е. это GET команда, возвращаем состояние контрола
                return httpCallback(CMD_CONTROL, value, false);
            }

            if(upperParam == CMD_INC_CONTROL){ // это команда увеличения контрола на значение, соотвественно получаем текущее
                val = arr[1].as<String>().toInt();
                str = httpCallback(CMD_CONTROL, arr[0], false);
                deserializeJson(doc,str);
                arr = doc.as<JsonArray>();
                arr[1] = arr[1].as<String>().toInt()+val.toInt();
            }

            for (size_t i = 0; i < arr.size(); i++) {
                switch(i){
                    case 0: {
                        id = arr[i].as<uint16_t>();
                        break;
                    }
                    case 1: val = arr[i].as<String>(); break;
                    default : break;
                }
			}
            run_action(String(TCONST_dynCtrl)+id, val.toInt());
            return httpCallback(CMD_CONTROL, String(id), false); // т.к. отложенный вызов, то иначе обрабатыаем
        }
        else if (upperParam == CMD_EFF_NAME)  {
            String effname((char *)0);
            uint16_t effnum=String(value).toInt();
            myLamp.effects.loadeffname(effname, effnum);
            result = String("[")+effnum+String(",\"")+effname+String("\"]");
#ifdef EMBUI_USE_MQTT
            embui.publish(String(TCONST_embui_pub_) + upperParam, result, true);
#endif
            return result;
        }
        else if (upperParam == CMD_EFF_ONAME)  {
            String effname((char *)0);
            uint16_t effnum=String(value).toInt();
            effname = T_EFFNAMEID[(uint8_t)effnum];
            result = String("[")+effnum+String(",\"")+effname+String("\"]");
#ifdef EMBUI_USE_MQTT
            embui.publish(String(TCONST_embui_pub_) + upperParam, result, true);
#endif
            return result;
        }
        else if (upperParam == CMD_AUX_ON) { run_action(ra::aux, true); return result; }
        else if (upperParam == CMD_AUX_OFF) { run_action(ra::aux, false); return result; }
        else if (upperParam == CMD_AUX_TOGGLE) { run_action(ra::aux_flip); return result; }

        // execute action
        remote_action(action, value.c_str(), NULL);
    }
    return result;
}

#ifdef ESP_USE_BUTTON
void load_button_config(const char* path){
    if (path){
        String filename(TCONST__backup_btn_);
        filename.concat(path);
        myButtons->clear();
        if (!myButtons->loadConfig(filename.c_str())) {
            default_buttons();
        }
    } else {
        if (!myButtons->loadConfig()) {
            default_buttons();
        }
    }
}
#endif

void load_events_config(const char* path){
    if (!path) return myLamp.events.loadConfig();

    String filename(TCONST__backup_evn_);
    filename.concat(path);
    myLamp.events.loadConfig(filename.c_str());
}

/**
 * @brief build a page with LED strip setup
 * 
 */
void page_ledstrip_setup(Interface *interf, JsonObject *data){
    if (!interf) return;

    DynamicJsonDocument doc(256);
    embuifs::deserializeFile(doc, TCONST_fcfg_ledstrip);

    interf->json_frame_interface();
    interf->json_section_main(TCONST_settings_ledstrip, TINTF_ledstrip);

    interf->json_section_line(); // расположить в одной линии
        interf->number_constrained(TCONST_width,  doc[TCONST_width].as<int>()  ? doc[TCONST_width].as<int>()  : 16, "ширина", 1, 1, 256);
        interf->number_constrained(TCONST_height, doc[TCONST_height].as<int>() ? doc[TCONST_height].as<int>() : 16, "высота", 1, 1, 256);
    interf->json_section_end();

    interf->json_section_line(); // расположить в одной линии
        interf->checkbox(TCONST_snake, doc[TCONST_snake], "змейка", false);
        interf->checkbox(TCONST_vertical, doc[TCONST_vertical], "вертикальная", false);
        interf->checkbox(TCONST_vflip, doc[TCONST_vflip], "V-отражение", false);
        interf->checkbox(TCONST_hflip, doc[TCONST_hflip], "H-отражение", false);
    interf->json_section_end();

    interf->button(button_t::submit, TCONST_settings_ledstrip, TINTF_Save);  // Save
    interf->button(button_t::generic, TCONST_settings, TINTF_00B);           // Exit
    interf->json_frame_flush();
}


/*
    сохраняет настройки LED ленты
*/
void set_ledstrip(Interface *interf, JsonObject *data){
    if (!data) return;
    // save new led strip config
    embuifs::serialize2file(*data, TCONST_fcfg_ledstrip);

    // apply options that could be adjusted easily
    mx->cfg.snake((*data)[TCONST_snake]);
    mx->cfg.vertical((*data)[TCONST_vertical]);
    mx->cfg.vmirror((*data)[TCONST_vflip]);
    mx->cfg.hmirror((*data)[TCONST_hflip]);

    // check if any dimension parameters has changed, then I need to resize led buffer
    if (((*data)[TCONST_width] != mx->cfg.w()) || ((*data)[TCONST_height] != mx->cfg.h())){
        mx->resize((*data)[TCONST_width], (*data)[TCONST_height]);
        LOG(printf, "resize LED buffer to w,h:(%d,%d)\n", mx->cfg.w(), mx->cfg.h());
    }
    myLamp.reset_led_buffs();
}
