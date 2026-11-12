@echo off
chcp 65001 >nul
echo ========================================================
echo   HOLODECK - INSTALADOR DAEMON SERIAL USB (MAGISK ROOT)
echo ========================================================
echo Conectando al celular de la rocola...
echo.

set ADB=.\platform-tools\adb.exe
if not exist "%ADB%" set ADB=adb

%ADB% wait-for-device

echo [1/3] Creando carpeta de servicios en Magisk...
%ADB% shell "su -c 'mkdir -p /data/adb/service.d'"

echo [2/3] Copiando script holodeck_serial.sh al celular (formato LF)...
%ADB% push holodeck_serial.sh /data/local/tmp/holodeck_serial.sh
%ADB% shell "su -c 'tr -d \"\r\" < /data/local/tmp/holodeck_serial.sh > /data/adb/service.d/holodeck_serial.sh && chmod 755 /data/adb/service.d/holodeck_serial.sh'"

echo [3/3] Iniciando el servicio en segundo plano ahora mismo...
%ADB% shell "su -c 'pkill -f holodeck_serial.sh 2>/dev/null; nohup /data/adb/service.d/holodeck_serial.sh >/dev/null 2>&1 &'"

echo.
echo ========================================================
echo  ¡LISTO! Servicio Serial USB instalado y corriendo.
echo ========================================================
