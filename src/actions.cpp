/*
    This file is a part of FireLamp_JeeUI project
    https://github.com/vortigont/FireLamp_JeeUI

    Copyright © 2023 Emil Muratov (vortigont)
    Copyright © 2020 Dmytro Korniienko (kDn)

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

#include "actions.hpp"
#include "lamp.h"

void run_action(ra act){
  LOG(printf_P, PSTR("run_action: %d\n"), static_cast<int>(act));
  switch (act){
    // AUX PIN flip
    case ra::aux_flip : {
      if ( embui.paramVariant(TCONST_aux_gpio) == -1) return;    // if AUX pin is not set, than quit
      run_action(ra::aux, !digitalRead(embui.paramVariant(TCONST_aux_gpio)) );  // we simply flip the state here
      break;
    }

    // demo next effect
    case ra::demo_next : {
      if (myLamp.getLampFlagsStuct().dRand)
        myLamp.switcheffect(SW_RND, myLamp.getFaderFlag());
      else
        myLamp.switcheffect(SW_NEXT_DEMO, myLamp.getFaderFlag());

      run_action(ra::eff_switch, myLamp.effects.getEffnum() );     // call switch effect as in GUI/main page
      break;
    }

    // switch to next effect
    case ra::eff_next : {
      // pick next available effect (considering it is enabled)
      run_action(ra::eff_switch, myLamp.effects.getNext());
      break;
    }

    // switch to previous effect
    case ra::eff_prev : {
      // pick previous available effect (considering it is enabled)
      run_action(ra::eff_switch, myLamp.effects.getPrev());
      break;
    }

    // switch to previous effect
    case ra::eff_rnd : {
      // pick previous available effect (considering it is enabled)
      run_action(ra::eff_switch, myLamp.effects.getByCnt(random(0, myLamp.effects.getEffectsListSize())) );
      break;
    }

    // turn lamp ON
    case ra::on : {
      run_action(TCONST_ONflag, true);
      break;
    }

    // turn lamp OFF
    case ra::off : {
      myLamp.stopRGB(); // выключение RGB-режима
      run_action(TCONST_ONflag, false);
      break;
    }

    case ra::reboot : {
      // make warning signaling
      StaticJsonDocument<128> warn;
      deserializeJson(warn, "{\"event\":[\"#ec21ee\",3000,500,true,\"Reboot...\"]}");
      JsonObject j = warn.as<JsonObject>();
      run_action(ra::warn, &j);
      Task *t = new Task(5 * TASK_SECOND, TASK_ONCE, nullptr, &ts, false, nullptr, [](){ ESP.restart(); });
      t->enableDelayed();
      break;
    }
    default:;
  }
//  StaticJsonDocument<ACTION_PARAM_SIZE> jdoc;
//  JsonObject obj = jdoc.to<JsonObject>();
//  run_action(act, &obj);
}

void run_action(ra act, JsonObject *data){
  LOG(printf_P, PSTR("run_action_o: %d\n"), static_cast<int>(act));
  switch (act){

    // apply effect control value
    case ra::brt :
    case ra::brt_nofade :
    case ra::eff_ctrl : {
      // usually this action is called with key:value pair for a specific control
      (*data)[P_data][TCONST_force] = true;        // какой-то костыль с задержкой обновления WebUI
      break;
    }

    // show warning on a lamp
    case ra::warn : {
      if ( !(*data).containsKey(TCONST_event) ) return;   // invalid object
      // here we receive JsonArray with alert params
      JsonArray arr = (*data)[TCONST_event];
      if (arr.size() < 3 ) return;    // some malformed warning config

      // color
      String tmpStr(arr[0].as<const char*>());
      tmpStr.replace("#", "0x");
      uint32_t col = strtol(tmpStr.c_str(), NULL, 0);
      myLamp.showWarning(col, // color
             arr[1],          // duration
             arr[2],          // period
             arr[3],          // type
             arr[4],          // overwrite
             arr[5]);         // text message
      return;   // nothig to inject
    }

    default:
      return;
  }

  embui.post(*data, true);                    // inject packet back into EmbUI action selector
}

/*
        case RA::RA_SEND_TEXT: {
            if (value && *value) {
                String tmpStr = embui.param(TCONST_txtColor);
                tmpStr.replace("#","0x");
                CRGB::HTMLColorCode color = (CRGB::HTMLColorCode)strtol(tmpStr.c_str(), NULL, 0);

                myLamp.sendString(value, color);
            }
            break;
        }
*/
