#!/system/bin/sh
# HoloDeck - Daemon Lector Serial USB para Android (Magisk Root)
# Lee la ESP32 conectada por cable USB OTG y reenvía comandos a Fully Kiosk
sleep 3

while true; do
  PORT=""
  if [ -c /dev/ttyACM0 ]; then
    PORT="/dev/ttyACM0"
  elif [ -c /dev/ttyUSB0 ]; then
    PORT="/dev/ttyUSB0"
  fi

  if [ -n "$PORT" ]; then
    chmod 666 "$PORT" 2>/dev/null
    stty -F "$PORT" 115200 raw -echo 2>/dev/null

    while IFS= read -r line || [ -n "$line" ]; do
      cmd=$(echo "$line" | tr -d '\r\n')
      [ -z "$cmd" ] && continue

      # 1. Enviar broadcast a Fully Kiosk (llega a onHoloBroadcast en index.html)
      am broadcast -a com.holodeck.COMMAND --es cmd "$cmd" >/dev/null 2>&1

      # 2. Inyección de tecla como respaldo nativo de Android
      case "$cmd" in
        "ENC:UP")
          input keyevent 19
          ;;
        "ENC:DOWN")
          input keyevent 20
          ;;
        "BTN:CLICK")
          input keyevent 62
          ;;
        "BTN:HOLD")
          input keyevent 43
          ;;
        "TAG:01")
          input keyevent 8
          ;;
        "TAG:02")
          input keyevent 9
          ;;
        "TAG:03")
          input keyevent 10
          ;;
        "TAG:EJECT")
          input keyevent 7
          ;;
      esac
    done < "$PORT"
  fi
  sleep 2
done
