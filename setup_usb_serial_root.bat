@echo off
chcp 65001 >nul
echo ========================================================
echo   HOLODECK - INSTALADOR DAEMON SERIAL USB (MAGISK ROOT)
echo ========================================================
echo Conecta el celular de la rocola a la PC por USB temporalmente
echo o asegurate de que este conectado por ADB Wi-Fi...
echo.

set ADB=.\platform-tools\adb.exe
if not exist "%ADB%" set ADB=adb

%ADB% wait-for-device

echo [1/3] Creando carpeta de servicios en Magisk...
%ADB% shell "su -c 'mkdir -p /data/adb/service.d'"

echo [2/3] Copiando script holodeck_serial.sh al celular...
%ADB% push holodeck_serial.sh /data/local/tmp/holodeck_serial.sh
%ADB% shell "su -c 'cp /data/local/tmp/holodeck_serial.sh /data/adb/service.d/holodeck_serial.sh && chmod 755 /data/adb/service.d/holodeck_serial.sh'"

echo [3/3] Iniciando el servicio en segundo plano ahora mismo...
%ADB% shell "su -c 'pkill -f holodeck_serial.sh 2>/dev/null; nohup /data/adb/service.d/holodeck_serial.sh >/dev/null 2>&1 &'"

echo.
echo ========================================================
echo  ¡LISTO! Servicio Serial USB instalado en Magisk.
echo  Se iniciara solo en cada encendido del celular.
echo  Ya puedes conectar el cable USB entre la ESP32 y el celular.
echo ========================================================
pause
