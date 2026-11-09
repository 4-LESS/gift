@echo off
chcp 65001 >nul
echo ========================================================
echo   HOLODECK - BLOQUEO DE ANUNCIOS + WIFI HELPER (ROOT)
echo ========================================================
echo Conectando por ADB al celular de la rocola...

set ADB=.\platform-tools\adb.exe
if not exist "%ADB%" set ADB=adb

%ADB% wait-for-device

echo [1/3] Activando DNS Privado AdGuard a nivel global de Android...
%ADB% shell "su -c 'settings put global private_dns_mode hostname && settings put global private_dns_specifier dns.adguard-dns.com'"

echo [2/3] Activando modulo Systemless Hosts en Magisk y aplicando lista AdBlock...
%ADB% shell "su -c 'mkdir -p /data/adb/modules/hosts/system/etc && cat /system/etc/hosts > /data/adb/modules/hosts/system/etc/hosts'"
%ADB% shell "su -c 'echo \"0.0.0.0 googleads.g.doubleclick.net\" >> /data/adb/modules/hosts/system/etc/hosts'"
%ADB% shell "su -c 'echo \"0.0.0.0 pagead2.googlesyndication.com\" >> /data/adb/modules/hosts/system/etc/hosts'"
%ADB% shell "su -c 'echo \"0.0.0.0 ad.doubleclick.net\" >> /data/adb/modules/hosts/system/etc/hosts'"
%ADB% shell "su -c 'echo \"0.0.0.0 static.doubleclick.net\" >> /data/adb/modules/hosts/system/etc/hosts'"
%ADB% shell "su -c 'echo \"0.0.0.0 www.googleadservices.com\" >> /data/adb/modules/hosts/system/etc/hosts'"
%ADB% shell "su -c 'echo \"0.0.0.0 adservice.google.com\" >> /data/adb/modules/hosts/system/etc/hosts'"
%ADB% shell "su -c 'echo \"0.0.0.0 s.youtube.com\" >> /data/adb/modules/hosts/system/etc/hosts'"
%ADB% shell "su -c 'mount -o bind /data/adb/modules/hosts/system/etc/hosts /system/etc/hosts'"

echo [3/3] Otorgando permisos de control Wi-Fi y modificacion del sistema a Fully Kiosk...
%ADB% shell "su -c 'pm grant de.ozerov.fully android.permission.ACCESS_FINE_LOCATION 2>/dev/null; pm grant de.ozerov.fully android.permission.CHANGE_WIFI_STATE 2>/dev/null; appops set de.ozerov.fully CHANGE_WIFI_STATE allow 2>/dev/null'"

echo.
echo ¡Listo! Bloqueo Root + DNS + Permisos Wi-Fi aplicados en el celular.
pause
