__[CHANGELOG](/CHANGELOG.md)__ | [![PlatformIO CI](https://github.com/vortigont/FireLamp_JeeUI/actions/workflows/pio_build.yml/badge.svg)](https://github.com/vortigont/FireLamp_JeeUI/actions/workflows/pio_build.yml)

## Огненная лампа FireLamp_EmbUI
Это деполитизированный форк проекта "огненной" лампы [FireLamp_EmbUI](https://github.com/DmytroKorniienko/FireLamp_EmbUI).

<details>
  <summary>Project Manifest</summary>

Проект был пересобран из старых форков и архивов репозитория пользователей и участников разработки на момент примерно 2021 года. В [исходном](https://github.com/DmytroKorniienko/FireLamp_EmbUI) проекте был полностью вырезан русский язык, задним числом переписана история разработки в Git и удалена соотвествующая тема [форума](https://community.alexgyver.ru/threads/wifi-lampa-budilnik-obsuzhdenie-proekta.1411/). Данный форк это не срез исходного репозитория, хотя сохранил часть общей истории до определенного момента.

</details>

Обсуждение и поддержка данной прошивки идет [на форуме](https://community.alexgyver.ru/threads/wifi-lampa-budilnik-obsuzhdenie-proshivki-firelamp_embui.7257/)

![GitHub Logo](/Схема51.jpg)

### ESP8266 vs ESP32
Основная разработка ведется под контроллеры семейства esp32. ESP8266 морально устарел и поддерживается по остаточному принципу, _категорически не рекоммендую_ использовать платы на 8266 для изготовления новых ламп!

### Распиновка
Референсная таблица назначения выводов по-умолчанию

ниже указаны номера gpio, НЕ нумерация выводов на плате! Соответствие нумерации пинов платы и gpio чипа ищите в описании конкретной платы произодителя. (на wemos нумерация не совпадает с gpio)

<details>
 <summary>ESP32</summary>

|gpio | назначение |
|-|-|
|0     | подключение матрицы|
|34    | вход микрофона|
|4     | TM1637 CLK|
|5     | TM1637 DIO|
|17    | tx (DFPlayer rx)|
|16    | rx (DFPlayer tx)|
|2     | TM1637 Clk|
|13    | TM1637 Data|
|15    | N канальный МОП (N-MOSFET)|
|14    | ttp223 или обычная кнопка|

</details>

<details>
 <summary>ESP8266</summary> 

|gpio | назначение |
|-|-|
|0     | подключение матрицы|
|acd0  | вход микрофона|
|4     | TM1637 CLK  (i2c SDA)|
|5     | TM1637 DIO  (i2c SCL)|
|12    | tx (DFPlayer rx)|
|14    | rx (DFPlayer tx)|
|2     | TM1637 Clk|
|13    | TM1637 Data|
|15    | N канальный МОП (N-MOSFET)  /P-канальный транзистор подключать нельзя! вывод имеет внутреннюю подтяжку к земле|
|16    | ttp223 или обычная кнопка|

</details>


## Как собрать проект

### Правильный способ:
Проект собирается с помощью [Platformio](https://platformio.org/)

Для сборки проекта понадобится следующее:

Установить [IDE Visual Studio Code](https://code.visualstudio.com/), и в качестве плагина к ней установить [Platformio](https://platformio.org/). О том как это сделать можно найти массу роликов на youtube, например [этот](https://www.youtube.com/watch?v=NSljt17mg74).

Желательно еще установить [Git](https://gitforwindows.org/), обновлять проект будет значительно проще

### Легкий способ:
для пользователей Windows OS можно использовать builder скрипт. Билдер был изначально написан @kostyamat (за что ему спасибо), адаптирован под форк текущей комадой (@andy040670).
Запускаете билдер и последовательно проходите шаги установки питона, гит, платформио, клонирование репозитория и сборку требуемого варианта прошивки.

### Готовые сборки
Свежие сборки от тов. @andy040670 вместе с инструкцией по заливке с помощью esp flashdownload tool можно найти в [этом](https://community.alexgyver.ru/threads/wifi-lampa-budilnik-obsuzhdenie-proshivki-firelamp_embui.7257/post-140904) сообщении.

## Как скачать/обновлять проект

Актуальный срез проекта всегда можно скачать в виде zip-архива [по ссылке](https://github.com/vortigont/FireLamp_JeeUI/archive/master.zip), либо перейдя по ссылке последнего релиза.
Либо можно поддерживать клон репозитория и подтягивать обновления перед каждой новой сборкой
Открываем Git-bash, клонируем репозиторий в тукущую папку командой 'git clone --depth 1 --no-single-branch https://github.com/vortigont/FireLamp_JeeUI.git'
В последующем для того чтобы обновить репозиторий достаточно перейти в папку проекта и выполнить команду 'git pull'.

После того как вы скопировали проект (в виде zip архива или через git clone), необходимо в папке include
скопировать файл *user_config.h.default* под новым именем *user_config.h* и в нем настроить сборку под свои параметры матрицы, номеров выводов и требуемых функций.

## Сборка

 * Открываем папку проекта в VSCode
 * Что бы собрать прошивку, можно воспользоваться кнопками в нижней статусной строке:
   - PlatformIO:Build - собрать прошивку
   - PlatformIO:Upload - загрузить прошивку через USB в плату.

Platformio сам скачает необходимые библиотеки для сборки проекта.

Также можно открыть терминал с помощью клавиш Ctrl+Shit+` и воспользоваться расширенными командами для сборки проекта.

 - **pio run -t upload** - собрать и записать в контроллер проект по умолчанию под платформу esp32
 - **pio run -e esp8266 -t upload** - собрать и записать версию esp8266, будет собрана облегченная прошивка без вывода отладочных сообщений через serial интерфейс
			    Рекомендуется для повседневного использования, если нет нужды отлаживать работу лампы
 - **pio run -e esp32debug -t upload** - собрать и прошить версию esp32 с отладкой, будет собрана прошивка под esp32 c выводом отладочных сообщений через serial интерфейс
 - **pio run -e esp8266dev -t upload** - собрать и прошить отладочную версию под esp8266
 - **pio deviсe monitor** - запустить serial-монитор для просмотра сообщений, выдаваемых контроллером
 
 Для работы лапмы нужно сформировать и залить в контроллер образ файловой системы. Выполняется это командой аналогично записи прошивки, но с параметром `-t uploadfs`, к примеру
 - **pio run -e esp32 -t uploadfs** - записать в контроллер образ ФС под платформу esp32
 - **pio run -e esp8266 -t uploadfs** - записать в контроллер образ ФС под платформу esp8266

 После первой прошивки дальнейшие обновления можно заливать в контроллер по воздуху. Для этого нужно зайти браузером на контроллер по URL вида http://embui-xxxx/update, где xxxx это ID контроллера, нажать на кномку 'Firmware', выбрать файл с прошивкой и загрузить его в контроллер.
 Файл с прошивкой Platformio кладет в подпапки проекта:
  - **.pio/build/esp8266/firmware.bin** - версия под esp8266
  - **.pio/build/esp32/firmware.bin** - версия под esp32

## <a name="ota">Обновление и прошивка по воздуху (OTA-update)</a>
Последующее обновление ПО можно осуществлять по воздуху, без подключения лампы кабелем. Можно обновить проект, собрать прошивку и залить полученный файл через форму обновления открыв лампу в браузере (напр. http://firelamp/upload), где _firelamp_ это имя хоста или ip-адресс контроллера в сети.
Так же можно воспользоваться скриптом для платформио, который автоматически зальет прошивку в лампу. Для этого нужно создать свой профиль сборки проекта и указать в нем адрес доступа к лампе.
 - Создайте в каталоге проекта файл `user_profile.ini`
 - скопируйте в файл пример секции `; OTA Upload example` из файла `platformio.ini`
 - раскомментируйте параметры, убрав `;` в начале строк
 - замените в параметре `upload_port = http://firelamp/update` имя устройства лампы на свое или на ip адрес
 - если нужно собирать проект под `esp8266` замените параметр `extends = esp32` на `extends = esp8266` 
 - соберите и загрузите прошивку в лампу командой `pio run -e ota -t upload`


# Управление

## По кнопке:
* Из выключенного состояния
   - 1 касание - включить на последнем эффекте
   - 2 касания - включить в режиме ДЕМО
   - долгое удержание - включить в режиме "белая лампа" на минимальную яркость (ночник)
   - касание, удержание - включить в режиме "белая лампа" на максимальную яркость
* Во включенном состоянии
   - 1 касание - выключить лампу
   - 2 касания - следующий эффект
   - 3 касания - предыдущий эффект
   - 5 касаний - вывод IP на лампу
   - 6 касаний - вывод текущего времени бегущей строкой
   
   - удержание - регулировка яркости
   - 1 касание, удержание - регулировка "скорости" эффекта
   - 2 касания, удержание - регулировка "масштаба" эффекта
   
Это дефолтное состояние для кнопки, но абсолютно любую настройку можно поменять

## По HTTP
команды можно посылать на лампу через браузер или curl по URL вида http://esp-xxxxxx/cmd?arg=param
   - /cmd?on /cmd?off /cmd?on=true /cmd?on=false - вкл/выкл
   - /cmd?demo - режим ДЕМО
   - /cmd?g_bright /cmd?g_bright=true - проверка/установка для глобальной яркости
   - /cmd?msg=Hello - вывод сообщения на лампу
   - /cmd?g_brtpct /cmd?g_brtpct=0-100 - получить/установить яркость в процентах
   - /cmd?bright=0-255 - яркость
   - /cmd?speed=0-255 - скорость
   - /cmd?scale=0-255 - шкала
   - /cmd?effect=N - эффект номер N
   - /cmd?move_next - следующий эффект
   - /cmd?move_prev - предыдущий эффект
   - /cmd?move_rnd - случайный эффект
   - /cmd?effect - номер текущего эффекта
   - /cmd?warning /cmd?warning=[16777215,5000,500] - неблокирующий вывод мигалки поверх эффекта (выдача предупреждений)
   - /cmd?alarm=true - форсировать включение будильника
   - /cmd?dynCtrlX , к примеру cmd?dynCtrl5=123 - получить/установить значение дин. контрола
   - /cmd?reboot - перезагрузить лампу
   - /cmd?mp3_vol - установить громкость мп3-плеера

так же команды можно объединять в цепочки в одной посылке, т.е. допустим включить лампу, установить яркость, перейти к эффекту
аналогичные команды работают и для MQTT, если чего-то не хватает или что-то не получается - спрашивайте в теме

Дополнительные служебные комманды:

   - /heap - показать свободное место на куче
   - /echo - показать эхо-ответ (json для формирования интерфейса)
   - /config - показать текущий конфиг (json основных настроек)
   - /scan - показать доступные WiFi-сети
   - /config.json - скачать активный конфиг лампы
   - /events_config.json - скачать активный конфиг событий
   - /update - форма http-обновления прошивки
   - /edit - вызов редактора конфигов (esp8266/esp8266 - логин/пассворд)


## Настройки WebUI

### DFPlayer

параметры работы мп3-плеера:

 - эффект вкл., плеер выкл. - воспроизводится звук эффектов (отдельная фоновая музыка под каждый эффект)
 - эффект выкл., плеер выкл. - звука не будет, кроме звука времени и озвучивания эффектов (если задано)
 - эффект выкл., плеер вкл. - звук будет для мп3 плеера, управление через кнопку "Еще" на основной странице, при смене эффекта звуковая дорожка не меняется
 - эффект вкл., плеер вкл. - звук будет для мп3 плеера, управление через кнопку "Еще" на основной странице, плюс при смене эффекта вручную. Смена эффектов в режиме "Демо" не переключает дорожки
